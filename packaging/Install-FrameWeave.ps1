<#
  FrameWeave installer.

  FrameWeave runs on Veyra's official runtime package (Qt, FFmpeg and the NVIDIA / AMD / Intel components that
  Veyra's author distributes). This script downloads that package from Veyra's own GitHub release, checks its
  SHA-256 against the value pinned when this FrameWeave release was built, unpacks it, and adds FrameWeave on top.
  No administrator rights are needed: everything goes into your user folder.

  Usage (normally just double-click Install-FrameWeave.cmd):
    Install-FrameWeave.ps1 [-Gpu Auto|NVIDIA|AMD] [-InstallDir <folder>] [-NoShortcuts] [-NoLaunch]
                        [-ExperimentalExtras Ask|Yes|No]
    Install-FrameWeave.ps1 -Uninstall [-InstallDir <folder>]

  A test build (a version that is not a release's X.Y.Z) never installs over the FrameWeave that is there:
  unless -InstallDir names a folder, it goes to %LOCALAPPDATA%\Programs\FrameWeave-test, its shortcuts are
  called "FrameWeave test", and it starts with a copy of the installed FrameWeave's settings.

  Works with Windows PowerShell 5.1 and PowerShell 7.
#>
[CmdletBinding()]
param(
    [ValidateSet('Auto', 'NVIDIA', 'AMD')]
    [string]$Gpu = 'Auto',
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'Programs\FrameWeave'),
    [switch]$NoShortcuts,
    [switch]$NoLaunch,
    # Veyra's experimental extras whose NVIDIA files fall outside NVIDIA's licence (see below).
    # Ask (default): a question when run in a console window; No when there is no one to ask.
    [ValidateSet('Ask', 'Yes', 'No')]
    [string]$ExperimentalExtras = 'Ask',
    [switch]$Uninstall,
    # Signed in-app updates (src/ui/UpdateChecker.cpp): the running FrameWeave that started this
    # installer; it quits right after, and the copy waits until it has (at most a minute).
    [int]$WaitForPid = 0
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# Windows PowerShell that was started from PowerShell 7 (FrameWeave launched from a PowerShell 7 window, or
# by a tool running under it, then updating itself) inherits PowerShell 7's module path and cannot load
# its own modules: "Get-FileHash is not recognized", and the update stopped there with its window
# closing at once (found by the update test in CI, November 2026). Use Windows PowerShell's own paths.
if ($PSVersionTable.PSVersion.Major -le 5 -and $env:PSModulePath -match '\\PowerShell\\') {
    $env:PSModulePath = @(
        (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'WindowsPowerShell\Modules'),
        (Join-Path $env:ProgramFiles 'WindowsPowerShell\Modules'),
        (Join-Path $PSHOME 'Modules')) -join ';'
}

# Filled in by packaging/make-release.ps1 when the release is built.
$PushFpsVersion = '@PUSHFPS_VERSION@'
$UpstreamRepo = '@UPSTREAM_REPO@'
$UpstreamTag = '@UPSTREAM_TAG@'
$UpstreamSha256 = @{
    'NVIDIA' = '@SHA256_NVIDIA@'
    'AMD'    = '@SHA256_AMD@'
}

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$filesDir = Join-Path $here 'FrameWeave-files'
# A test build (its version is not a release's X.Y.Z) never installs over the FrameWeave that is there. Unless a
# folder is named it goes into a folder of its own; its shortcuts have their own name whatever the folder; and
# in its own folder it starts with a copy of the installed FrameWeave's settings, which is only read.
# (23 November 2026: a private test build, double-clicked, replaced the owner's own FrameWeave.)
$TestBuild = $PushFpsVersion -notmatch '^\d+\.\d+\.\d+$'
$MainDir = Join-Path $env:LOCALAPPDATA 'Programs\FrameWeave'
$OwnFolder = $TestBuild -and -not $PSBoundParameters.ContainsKey('InstallDir')
if ($OwnFolder) { $InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\FrameWeave-test' }
$linkName = 'FrameWeave.lnk'
if ($TestBuild) { $linkName = 'FrameWeave test.lnk' }
$startMenu = Join-Path ([Environment]::GetFolderPath('Programs')) $linkName
$desktop = Join-Path ([Environment]::GetFolderPath('Desktop')) $linkName

function Step([string]$text) { Write-Host ''; Write-Host "  $text" -ForegroundColor Green }
function Info([string]$text) { Write-Host "    $text" }
function Fail([string]$text) {
    Write-Host ''; Write-Host "  $text" -ForegroundColor Red
    if ($WaitForPid -gt 0 -and -not $script:installUncertain) {
        # An update started inside FrameWeave: the program has closed by now. Say what happened, start the
        # copy that is still installed and keep this window open so the reason can be read.
        Write-Host '  The update did not finish. You can run it again from FrameWeave, or download the release by hand.' -ForegroundColor Yellow
        try {
            $existing = Join-Path $InstallDir 'FrameWeave.exe'
            if ((Test-Path $existing) -and -not (Get-Process -Name 'FrameWeave' -ErrorAction SilentlyContinue)) { Start-Process -FilePath $existing -WorkingDirectory $InstallDir }
        } catch { }
        if (-not [Console]::IsInputRedirected) { Read-Host '  Press Enter to close this window' | Out-Null }
    }
    exit 1
}

# Anything unexpected ends through Fail too: in an update the window stays open with the reason and the
# installed copy starts again, where it used to close at once with FrameWeave already gone.
trap { Fail ("Unexpected error: " + $_.Exception.Message) }

Write-Host ''
Write-Host "  FrameWeave $PushFpsVersion" -ForegroundColor Green
Write-Host '  Based on Veyra by Likely7 (GPL-3.0)'
if ($OwnFolder) {
    Write-Host ''
    Write-Host '  This is a test build. It gets its own folder and its own shortcut ("FrameWeave test").' -ForegroundColor Yellow
    Write-Host '  The FrameWeave you have is not changed.' -ForegroundColor Yellow
}

# SHA-256 through .NET: no PowerShell module is needed for it (see the module path note at the top).
function Get-Sha256([string]$path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($path)
    try { return ([System.BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') }
    finally { $stream.Dispose(); $sha.Dispose() }
}

# Record relative names from the delivered trees. Uninstallation only enumerates
# real children, so a manifest entry cannot introduce an absolute or parent path.
$installRootInfo = [IO.DirectoryInfo]::new($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($InstallDir))
if (-not $installRootInfo.Parent) { Fail 'The installation folder cannot be a drive or share root.' }
$InstallDir = $installRootInfo.FullName
for ($ancestor = $installRootInfo; $ancestor; $ancestor = $ancestor.Parent) {
    if ($ancestor.Exists -and ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        Fail "The installation folder goes through a link: $($ancestor.FullName)"
    }
}

# A journal beside the installation survives an interrupted process. Only package
# files are replaced. The backup index is independent of the destination names.
function Get-InstallFile([string]$Name) {
    if ([IO.Path]::IsPathRooted($Name) -or $Name -match '(^|[\\/])\.\.?([\\/]|$)|:') { throw "Invalid package path: $Name" }
    $file = [IO.FileInfo]::new([IO.Path]::Combine($InstallDir, $Name))
    if (-not $file.FullName.StartsWith($InstallDir.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Outside installation: $Name" }
    for ($entry = $file; $entry; $entry = $(if ($entry -is [IO.FileInfo]) { $entry.Directory } else { $entry.Parent })) {
        if ((Test-Path -LiteralPath $entry.FullName) -and ([IO.File]::GetAttributes($entry.FullName) -band [IO.FileAttributes]::ReparsePoint)) { throw "Linked installation path: $Name" }
    }
    return $file
}

function Restore-Install {
    if (-not [IO.Directory]::Exists($transaction)) { return }
    if ([IO.File]::GetAttributes($transaction) -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked transaction directory.' }
    if (-not [IO.File]::Exists($journal)) { throw "Unrecognized transaction directory: $transaction" }
    if (-not [IO.File]::Exists((Join-Path $transaction 'committed'))) {
        $entries = [IO.File]::ReadAllLines($journal)
        for ($i = $entries.Count - 1; $i -ge 0; --$i) {
            $kind, $name = $entries[$i] -split '\|', 2
            $dest = (Get-InstallFile $name).FullName
            $backup = Join-Path $transaction ("old-$i")
            if ($kind -eq 'dir') {
                if ([IO.Directory]::Exists($dest) -and [IO.Directory]::GetFileSystemEntries($dest).Length -eq 0) { [IO.Directory]::Delete($dest) }
            }
            elseif ($kind -eq 'new') { [IO.File]::Delete($dest) }
            elseif ($kind -eq 'old') {
                if ([IO.File]::Exists($backup)) {
                    if ([IO.File]::Exists($dest)) { [IO.File]::Replace($backup, $dest, [System.Management.Automation.Language.NullString]::Value) }
                    else { [IO.File]::Move($backup, $dest) }
                }
            } else { throw 'Invalid installation journal.' }
        }
    }
    # Detach a finished journal atomically. An interrupted cleanup may leave
    # stale backups, but can never replay an already completed rollback.
    $finished = $transaction + '-finished-' + [guid]::NewGuid().ToString('N')
    try { [IO.Directory]::Move($transaction, $finished) }
    catch [UnauthorizedAccessException], [IO.IOException] {
        # A reader sharing delete can let Windows rename its file but prevent a
        # rename of the parent directory. Only retire backups after commit.
        if (-not [IO.File]::Exists((Join-Path $transaction 'committed'))) { throw }
        foreach ($backup in [IO.Directory]::GetFiles($transaction, 'old-*')) {
            $name = [IO.Path]::GetFileName($backup)
            if ($name -notmatch '^old-\d+$') { throw "Unexpected transaction file: $backup" }
            $detached = "$finished-$name"
            [IO.File]::Move($backup, $detached)
            try { [IO.File]::Delete($detached) } catch { Info "In-use backup retained: $detached" }
        }
        [IO.Directory]::Move($transaction, $finished)
    }
    foreach ($item in [IO.Directory]::EnumerateFiles($finished)) {
        if ([IO.Path]::GetFileName($item) -notmatch '^(journal|pending|committed|old-\d+)$') { throw "Unexpected transaction file: $item" }
        try { [IO.File]::Delete($item) } catch { Info "In-use backup retained: $item" }
    }
    if ([IO.Directory]::GetFileSystemEntries($finished).Length -eq 0) { [IO.Directory]::Delete($finished) }
}

function Set-InstallFile([string]$Name, [string]$Source) {
    $dest = Get-InstallFile $Name
    if ([IO.Directory]::Exists($dest.FullName)) { throw "A directory occupies the package file path: $Name" }
    $exists = [IO.File]::Exists($dest.FullName)
    if (-not $exists -and -not $Source) { return }
    $pending = Join-Path $transaction 'pending'
    if ($Source) {
        $missing = [Collections.Generic.Stack[IO.DirectoryInfo]]::new()
        for ($dir = $dest.Directory; -not $dir.Exists; $dir = $dir.Parent) { $missing.Push($dir) }
        while ($missing.Count) {
            $dir = $missing.Pop()
            $relative = $dir.FullName.Substring($InstallDir.TrimEnd('\').Length + 1)
            [IO.File]::AppendAllText($journal, "dir|$relative" + [Environment]::NewLine)
            ++$script:transactionIndex
            $dir.Create()
        }
        [IO.File]::Copy($Source, $pending, $true)
    }
    $kind = if ($exists) { 'old' } else { 'new' }
    [IO.File]::AppendAllText($journal, "$kind|$Name" + [Environment]::NewLine)
    $backup = Join-Path $transaction ("old-" + $script:transactionIndex++)
    if (-not $Source) { [IO.File]::Move($dest.FullName, $backup) }
    elseif ($exists) { [IO.File]::Replace($pending, $dest.FullName, $backup) }
    else { [IO.File]::Move($pending, $dest.FullName) }
}

$hash = [Security.Cryptography.SHA256]::Create()
try { $key = [BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($InstallDir.ToUpperInvariant()))).Replace('-', '') } finally { $hash.Dispose() }
$installLock = [Threading.Mutex]::new($false, "Local\FrameWeave-Install-$key")
try { $locked = $installLock.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
if (-not $locked) { $script:installUncertain = $true; Fail 'Another installation is in progress.' }
try {
$transaction = Join-Path $installRootInfo.Parent.FullName ('.FrameWeave-update-' + $key)
$journal = Join-Path $transaction 'journal'
$script:installUncertain = $true
Restore-Install
$script:installUncertain = $false
$manifest = Join-Path $InstallDir 'FrameWeave-installed-files.txt'
$manifestHeader = 'FrameWeave installed files v1'
$owned = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$hasManifest = $false
if ([IO.File]::Exists($manifest)) {
    if ([IO.File]::GetAttributes($manifest) -band [IO.FileAttributes]::ReparsePoint) { Fail 'The installed-files manifest is a link.' }
    $lines = [IO.File]::ReadAllLines($manifest)
    if ($lines.Length -eq 0 -or $lines[0] -ne $manifestHeader) { Fail "Unrecognized installed-files manifest in $InstallDir." }
    foreach ($line in $lines) { if ($line -ne $manifestHeader) { [void]$owned.Add($line) } }
    $hasManifest = $true
}

function Add-Payload([IO.DirectoryInfo]$Folder, [string]$Prefix, [string]$SkipTop = '') {
    foreach ($entry in $Folder.EnumerateFileSystemInfos()) {
        if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -or ($entry.Name -eq $SkipTop)) { continue }
        $name = $Prefix + $entry.Name
        if ($name -like 'runtime_local/user-data-*' -or $name -eq 'FrameWeave-installed-files.txt') { continue }
        if ($entry -is [IO.DirectoryInfo]) {
            [void]$owned.Add($name + '/')
            Add-Payload $entry ($name + '/')
        } else {
            [void]$owned.Add($name)
        }
    }
}

function Remove-Payload([IO.DirectoryInfo]$Folder, [string]$Prefix) {
    foreach ($entry in $Folder.GetFileSystemInfos()) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
        $name = $Prefix + $entry.Name
        if ($name -like 'runtime_local/user-data-*' -or $name -eq 'FrameWeave-installed-files.txt') { continue }
        if ($entry -is [IO.DirectoryInfo]) {
            if ($owned.Contains($name + '/')) {
                Remove-Payload $entry ($name + '/')
                if ($entry.GetFileSystemInfos().Length -eq 0) { $entry.Delete() }
            }
        } elseif ($owned.Contains($name)) {
            # Older installers left no inventory. Only delete an identifiable
            # file from the supplied package when its contents still match.
            if (-not $hasManifest) {
                $source = Join-Path $filesDir $name
                if ((Get-Sha256 $source) -ne (Get-Sha256 $entry.FullName)) { continue }
            }
            Remove-Item -LiteralPath $entry.FullName -Force
        }
    }
}

if ($Uninstall) {
    Step 'Removing FrameWeave'
    if (-not $installRootInfo.Exists) { Info "Nothing installed in $InstallDir"; exit 0 }
    if (-not $hasManifest) {
        $versionFile = Join-Path $InstallDir 'FrameWeave-VERSION.txt'
        if (-not [IO.File]::Exists($versionFile) -or
            ([IO.File]::GetAttributes($versionFile) -band [IO.FileAttributes]::ReparsePoint) -or
            (Get-Content -LiteralPath $versionFile -TotalCount 1) -notmatch '^FrameWeave \d+\.\d+\.\d+([-.+][\w.-]+)?$') {
            Info "No recognizable installation record in $InstallDir. Its files were kept."
            exit 0
        }
        if ([IO.Directory]::Exists($filesDir)) { Add-Payload ([IO.DirectoryInfo]::new($filesDir)) '' }
        Info 'Legacy installation: unmatched files and runtime dependencies are kept. Reinstall before uninstalling to record the complete payload.'
    }
    Remove-Payload $installRootInfo ''
    if ($hasManifest) { [IO.File]::Delete($manifest) } else { [IO.File]::Delete($versionFile) }
    if ($installRootInfo.GetFileSystemInfos().Length -eq 0) { $installRootInfo.Delete() }
    else { Info "Other files remain in $InstallDir." }
    foreach ($link in @($startMenu, $desktop)) {
        if (Test-Path -LiteralPath $link -PathType Leaf) {
            $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($link)
            if ($shortcut.TargetPath -eq (Join-Path $InstallDir 'FrameWeave.exe')) { Remove-Item -LiteralPath $link -Force }
        }
    }
    exit 0
}

if (-not (Test-Path (Join-Path $filesDir 'FrameWeave.exe'))) {
    Fail 'FrameWeave-files\FrameWeave.exe is missing. Extract the whole zip first, then run the installer from that folder.'
}

if ([Environment]::OSVersion.Version.Major -lt 10 -or -not [Environment]::Is64BitOperatingSystem) {
    Fail 'FrameWeave needs 64-bit Windows 10 or 11 (Windows 11 recommended).'
}

# Pick the runtime package that matches the graphics card.
if ($Gpu -eq 'Auto') {
    $names = @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
    if ($names -match 'NVIDIA') { $Gpu = 'NVIDIA' } else { $Gpu = 'AMD' }
    Step "Graphics card: $(($names | Where-Object { $_ }) -join ', ')"
    if ($Gpu -eq 'AMD') { Info 'No NVIDIA card found: using the AMD / Intel package (FSR and XeSS frame generation).' }
}

$version = $UpstreamTag.TrimStart('v')
$asset = "Veyra-$version-$Gpu-win64-portable.zip"
$expected = $UpstreamSha256[$Gpu]
$url = "https://github.com/$UpstreamRepo/releases/download/$UpstreamTag/$asset"
$cacheDir = Join-Path $env:TEMP 'FrameWeave-download'
$zip = Join-Path $cacheDir $asset
New-Item -ItemType Directory -Force $cacheDir | Out-Null

function Test-Download { (Test-Path $zip) -and ((Get-Sha256 $zip) -ieq $expected) }

if (Test-Download) {
    Step "Using the runtime package downloaded earlier"
} else {
    Step "Downloading the runtime package ($Gpu, about 750 MB) from Veyra's GitHub release"
    Info $url
    $curl = Join-Path $env:SystemRoot 'System32\curl.exe'
    if (Test-Path $curl) {
        & $curl -L --fail --retry 3 --retry-delay 5 -C - -o $zip $url
        if ($LASTEXITCODE -ne 0) { Fail "Download failed (curl exit $LASTEXITCODE). Check your internet connection and run the installer again; it continues where it stopped." }
    } else {
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    }
    if (-not (Test-Download)) {
        Remove-Item $zip -Force -ErrorAction SilentlyContinue
        Fail 'The downloaded file does not match the expected SHA-256, so it was deleted. Run the installer again.'
    }
}
Info "SHA-256 verified: $expected"

Step 'Unpacking'
$stage = Join-Path $env:TEMP ("FrameWeave-stage-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $stage | Out-Null
try {
    $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    # Some Windows tar builds lose Unicode characters in absolute paths.
    if ((Test-Path $tar) -and ($zip + $stage) -notmatch '[^\x00-\x7f]') {
        & $tar -xf $zip -C $stage
        if ($LASTEXITCODE -ne 0) { Fail "Unpacking failed (tar exit $LASTEXITCODE)." }
    } else {
        Expand-Archive -LiteralPath $zip -DestinationPath $stage -Force
    }
    $exe = Get-ChildItem $stage -Recurse -Filter 'veyra_qml_ui.exe' | Select-Object -First 1
    if (-not $exe) { Fail 'The runtime package has an unexpected layout (veyra_qml_ui.exe not found).' }
    $runtimeRoot = $exe.DirectoryName

    # Licensing. By default FrameWeave installs only what NVIDIA's licence allows to be passed on:
    #  - NVIDIA's runtime files only as NVIDIA ships them. A file that says it is from NVIDIA but is not
    #    an original with a valid NVIDIA signature (a community-modified DLSS build: Veyra's RTX 30 AI
    #    enhancement slot) is an "experimental extra".
    #  - NVIDIA's DLSS runtimes are licensed for systems with NVIDIA graphics only. The AMD package's AI
    #    enhancement folders run NVIDIA's model on Radeon cards: also experimental extras.
    # Veyra ships the extras in its own package. Nothing is lost: they are offered here, off by default,
    # and installed only if the user says yes (or passes -ExperimentalExtras Yes). Left out, they are also
    # removed if an earlier install placed them, and Veyra reports their feature as unavailable.
    $extras = @()
    if ($Gpu -eq 'AMD') {
        foreach ($rel in 'runtime\amd-nr-rdna3', 'runtime\amd-nr') {
            if (Test-Path (Join-Path $runtimeRoot $rel)) { $extras += [pscustomobject]@{ Rel = $rel; Dir = $true; Why = 'AI enhancement on Radeon (runs NVIDIA''s model on non-NVIDIA graphics)' } }
        }
    }
    foreach ($f in Get-ChildItem $runtimeRoot -Recurse -File -Include *.dll, *.exe) {
        if ($f.VersionInfo.CompanyName -notmatch 'NVIDIA') { continue }
        $rel = $f.FullName.Substring($runtimeRoot.Length).TrimStart('\')
        if ($extras | Where-Object { $_.Dir -and $rel.StartsWith($_.Rel + '\', [StringComparison]::OrdinalIgnoreCase) }) { continue }
        $sig = Get-AuthenticodeSignature -FilePath $f.FullName
        $signer = if ($sig.SignerCertificate) { $sig.SignerCertificate.Subject } else { '' }
        if ($sig.Status -eq 'Valid' -and $signer -match 'NVIDIA') { continue }
        $why = if ($rel -match 'nr-ampere') { 'AI enhancement on RTX 30 (community-modified NVIDIA file)' } else { 'community-modified NVIDIA file' }
        $extras += [pscustomobject]@{ Rel = $rel; Dir = $false; Why = $why }
    }
    $keepExtras = $false
    if ($extras.Count) {
        if ($ExperimentalExtras -eq 'Yes') { $keepExtras = $true }
        elseif ($ExperimentalExtras -eq 'Ask' -and -not [Console]::IsInputRedirected) {
            Step 'Optional: Veyra''s experimental extras'
            foreach ($x in $extras) { Info "- $($x.Why)" }
            Info 'They use NVIDIA files outside the terms NVIDIA licenses them under, so FrameWeave leaves them out'
            Info 'unless you choose them. Everything else works without them.'
            $answer = Read-Host '    Install the experimental extras too? [y/N]'
            $keepExtras = $answer -match '^(y|yes|j|ja)$'
        }
    }
    $skipped = @()
    foreach ($x in $extras) {
        if ($keepExtras) { Info "Installing experimental extra by your choice: $($x.Rel)"; continue }
        $src = Join-Path $runtimeRoot $x.Rel
        if ($x.Dir) {
            $x | Add-Member -NotePropertyName Files -NotePropertyValue @([IO.Directory]::EnumerateFiles($src, '*', [IO.SearchOption]::AllDirectories) | ForEach-Object { $_.Substring($runtimeRoot.Length + 1) })
            Remove-Item -LiteralPath $src -Recurse -Force
        } else { Remove-Item $src -Force }

        $skipped += "$($x.Rel) ($($x.Why); install with -ExperimentalExtras Yes)"
    }
    foreach ($item in $skipped) { Info "Not installed: $item" }

    Step "Installing to $InstallDir"
    if ($WaitForPid -gt 0) {
        $old = Get-Process -Id $WaitForPid -ErrorAction SilentlyContinue
        if ($old) { Info 'Waiting for FrameWeave to close'; $old.WaitForExit(60000) | Out-Null }
    }
    # Only a FrameWeave running from this very folder: "...\FrameWeave" is also how "...\FrameWeave-test" begins.
    $installRoot = $InstallDir.TrimEnd('\') + '\'
    $running = Get-Process -Name 'FrameWeave' -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase) }
    if ($running) { Fail 'FrameWeave is running. Close it, then run the installer again.' }
    # A test build in its own folder takes a copy of the installed FrameWeave's settings, once: never again when
    # the test folder has settings of its own. Decided before anything is copied into the folder.
    $mainData = Join-Path $MainDir 'runtime_local\user-data-2.0.0'
    $ownData = Join-Path $InstallDir 'runtime_local\user-data-2.0.0'
    $copySettings = $OwnFolder -and (Test-Path $mainData) -and -not (Test-Path $ownData)
    New-Item -ItemType Directory -Force $InstallDir | Out-Null
    $script:transactionIndex = 0
    $preparing = $transaction + '-preparing-' + [guid]::NewGuid().ToString('N')
    [void][IO.Directory]::CreateDirectory($preparing)
    [IO.File]::WriteAllText((Join-Path $preparing 'journal'), '')
    [IO.Directory]::Move($preparing, $transaction)
    $script:installUncertain = $true
    try {
        foreach ($source in @($runtimeRoot, $filesDir)) {
            foreach ($file in [IO.Directory]::EnumerateFiles($source, '*', [IO.SearchOption]::AllDirectories)) {
                $rel = $file.Substring($source.Length + 1)
                if ($source -eq $runtimeRoot -and $rel.StartsWith('virtualcam\', [StringComparison]::OrdinalIgnoreCase)) { continue }
                if ($rel -in 'veyra_qml_ui.exe','veyra_update_worker.exe') { continue }
                if ($rel -like 'runtime_local\user-data-*') { continue }
                try { Set-InstallFile $rel $file }
                catch { throw "Could not install '${rel}': $($_.Exception.Message)" }
            }
        }
        foreach ($own in 'veyra_qml_ui.exe','veyra_update_worker.exe') { Set-InstallFile $own '' }
        if (-not $keepExtras) {
            foreach ($x in $extras) {
                if ($x.Dir) {
                    # Remove only files belonging to the excluded runtime package.
                    foreach ($name in $x.Files) { Set-InstallFile $name '' }
                } else { Set-InstallFile $x.Rel '' }
            }
        }
        $versionLines = @(
            "FrameWeave $PushFpsVersion",
            "Runtime: $asset from https://github.com/$UpstreamRepo/releases/tag/$UpstreamTag",
            "Runtime SHA-256: $expected"
        ) + @($skipped | ForEach-Object { "Not installed: $_" })
        Set-Content -LiteralPath (Join-Path $stage 'version.tmp') -Value $versionLines -Encoding UTF8
        Set-InstallFile 'FrameWeave-VERSION.txt' (Join-Path $stage 'version.tmp')
        # Merge earlier inventories for upgrades. Never inventory the destination:
        # it can also contain files belonging to its user or another program.
        Add-Payload ([IO.DirectoryInfo]::new($runtimeRoot)) '' 'virtualcam'
        [void]$owned.Remove('veyra_qml_ui.exe')
        Add-Payload ([IO.DirectoryInfo]::new($filesDir)) ''
        [void]$owned.Add('FrameWeave-VERSION.txt')
        $stagedManifest = Join-Path $stage 'manifest.tmp'
        [IO.File]::WriteAllLines($stagedManifest, [string[]](@($manifestHeader) + @($owned)))
        Set-InstallFile 'FrameWeave-installed-files.txt' $stagedManifest
        [IO.File]::WriteAllText((Join-Path $transaction 'committed'), '')
    } catch {
        $cause = $_
        Restore-Install
        $script:installUncertain = $false
        throw "Installation did not complete. Changes were rolled back. $($cause.Exception.Message)"
    }
    Restore-Install
    $script:installUncertain = $false
    if ($copySettings) {
        & robocopy $mainData $ownData /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { Info 'Your settings could not be copied: the test build starts with settings of its own.' } else { Info 'Copied the settings of your FrameWeave for the test build.' }
    }
} finally {
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}

$target = Join-Path $InstallDir 'FrameWeave.exe'
if (-not $NoShortcuts) {
    Step 'Adding Start menu and desktop shortcuts'
    $shell = New-Object -ComObject WScript.Shell
    foreach ($link in @($startMenu, $desktop)) {
        $s = $shell.CreateShortcut($link)
        $s.TargetPath = $target
        $s.WorkingDirectory = $InstallDir
        $s.IconLocation = "$target,0"
        $s.Description = 'FrameWeave: console frame generation'
        if ($TestBuild) { $s.Description = 'FrameWeave test build' }
        $s.Save()
    }
}

# Windows keeps icons in a cache keyed by file path, so after an update the shortcuts and taskbar can keep
# showing the previous version's icon. Ask Explorer to refresh it (per user, no admin rights, best effort).
try {
    $ie4 = Join-Path $env:WINDIR 'System32\ie4uinit.exe'
    if (Test-Path $ie4) {
        $p = Start-Process -FilePath $ie4 -ArgumentList '-show' -WindowStyle Hidden -PassThru -ErrorAction Stop
        if (-not $p.WaitForExit(10000)) { $p.Kill() }
    }
} catch { }

Step 'FrameWeave is installed.'
Info "Program: $target"
Info "Remove it any time with: Install-FrameWeave.cmd -Uninstall"
if ($OwnFolder) { Info "Your own FrameWeave in $MainDir was not changed. Start this test build with the shortcut ""FrameWeave test""." }
Info "The downloaded package stays in $cacheDir for quick reinstalls; you can delete it."

if (-not $NoLaunch) { Start-Process -FilePath $target -WorkingDirectory $InstallDir }
} finally {
    $installLock.ReleaseMutex()
    $installLock.Dispose()
}
exit 0
