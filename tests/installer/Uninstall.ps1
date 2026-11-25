# Exercise the inventory written by the Windows packaging smoke test in a disposable directory.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Installer,
    [Parameter(Mandatory)][string]$InstalledDir
)
$ErrorActionPreference = 'Stop'
$outside = Join-Path ([IO.Path]::GetTempPath()) ([IO.Path]::GetRandomFileName())
[IO.Directory]::CreateDirectory($outside) | Out-Null

function Uninstall([string]$Directory) {
    & $Installer -Uninstall -InstallDir $Directory
    if ($LASTEXITCODE -ne 0) { throw "Uninstall failed for $Directory (exit $LASTEXITCODE)" }
}

# Replace a delivered subdirectory with a junction to an unrelated directory
# holding a file with a delivered name. A file inventory alone cannot protect it.
$qml = Join-Path $InstalledDir 'qml'
$original = Join-Path $outside 'original-qml'
$probe = Join-Path $outside 'custom'
[IO.Directory]::CreateDirectory($probe) | Out-Null
Copy-Item -LiteralPath (Join-Path $InstalledDir 'FrameWeave-installed-files.txt'), (Join-Path $InstalledDir 'FrameWeave.exe') -Destination $probe
Copy-Item -LiteralPath $qml -Destination $original -Recurse
$externalFile = @(Get-ChildItem -LiteralPath $original -File -Recurse)[0]
$linkType = if ([Environment]::OSVersion.Platform -eq 'Win32NT') { 'Junction' } else { 'SymbolicLink' }
$qml = Join-Path $probe 'qml'
New-Item -ItemType $linkType -Path $qml -Target $original | Out-Null
$foreign = Join-Path $probe 'a file [1].txt'
[IO.File]::WriteAllText($foreign, 'private')
Uninstall $probe
if ((Test-Path -LiteralPath (Join-Path $probe 'FrameWeave.exe')) -or
    (Test-Path -LiteralPath (Join-Path $probe 'FrameWeave-installed-files.txt'))) { throw 'Owned files left in custom install' }
if ([IO.File]::ReadAllText($foreign) -ne 'private' -or -not [IO.File]::Exists($externalFile.FullName)) {
    throw 'Uninstall removed a foreign file or followed a directory link'
}

# File sharing on Windows prevents deletion while another process is using an
# installed file. Keep the inventory until all managed files can be removed.
if ([Environment]::OSVersion.Platform -eq 'Win32NT') {
    $locked = Join-Path $outside 'locked'
    [IO.Directory]::CreateDirectory($locked) | Out-Null
    Copy-Item -LiteralPath (Join-Path $InstalledDir 'FrameWeave-installed-files.txt'), (Join-Path $InstalledDir 'FrameWeave.exe') -Destination $locked
    $held = [IO.File]::Open((Join-Path $locked 'FrameWeave.exe'), 'Open', 'Read', 'Read')
    try {
        & $Installer -Uninstall -InstallDir $locked
        if ($LASTEXITCODE -eq 0 -or -not [IO.File]::Exists((Join-Path $locked 'FrameWeave-installed-files.txt'))) {
            throw 'Uninstall lost the inventory or reported success while an installed file was locked'
        }
    } finally { $held.Dispose() }
    Uninstall $locked
    if ([IO.Directory]::Exists($locked)) { throw 'Retry did not finish uninstalling the unlocked files' }
}

# An old installer recorded only the version. Matching payload files can be
# removed, but files of unknown origin and a modified payload file must survive.
$legacy = Join-Path $outside 'legacy'
[IO.Directory]::CreateDirectory($legacy) | Out-Null
$package = Join-Path (Split-Path -Parent $Installer) 'FrameWeave-files'
Copy-Item -LiteralPath (Join-Path $package 'FrameWeave.exe') -Destination $legacy
$other = Join-Path $legacy 'NOTICE.md'
[IO.File]::WriteAllText($other, 'local changes')
[IO.File]::WriteAllText((Join-Path $legacy 'FrameWeave-VERSION.txt'), 'FrameWeave 4.4.5')
Uninstall $legacy
if ([IO.File]::Exists((Join-Path $legacy 'FrameWeave.exe')) -or [IO.File]::ReadAllText($other) -ne 'local changes') {
    throw 'Legacy uninstall did not preserve an unmatched file or remove the matching executable'
}
Uninstall $legacy
# Remove only these test fixtures; unlink the junction itself before cleanup.
[IO.Directory]::Delete($qml)
Remove-Item -LiteralPath $outside -Force -Recurse
