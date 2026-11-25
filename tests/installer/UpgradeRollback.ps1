# Runs the real installer with a small local runtime archive. No device, download or shell integration.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Installer, [Parameter(Mandatory)][string]$NewScratchDirectory)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $NewScratchDirectory) { throw 'Use a new scratch directory.' }
$root = [IO.Directory]::CreateDirectory($NewScratchDirectory).FullName
$cache = [IO.Directory]::CreateDirectory((Join-Path $root 'PushFPS-download')).FullName
$payload = [IO.Directory]::CreateDirectory((Join-Path $root 'PushFPS-files')).FullName
$runtime = [IO.Directory]::CreateDirectory((Join-Path $root 'runtime')).FullName
$dest = [IO.Directory]::CreateDirectory((Join-Path $root 'installed')).FullName
function Put($dir,$name,$value) { [IO.File]::WriteAllText((Join-Path $dir $name), $value) }
function Expect($dir,$name,$text) { if ([IO.File]::ReadAllText((Join-Path $dir $name)) -cne $text) { throw "Wrong contents: $name" } }
Put $runtime 'veyra_qml_ui.exe' 'fixture'
Put $runtime 'engine.dll' 'new-engine'
Put $runtime 'extra.dll' 'temporary'
$qml = [IO.Directory]::CreateDirectory((Join-Path $runtime 'qml')).FullName
Put $qml 'example.qml' 'package QML'
Put $payload 'PushFPS.exe' 'new-app'
Put $dest 'PushFPS.exe' 'old-app'
Put $dest 'engine.dll' 'old-engine'
Put $dest 'personal.txt' 'personal'
$zip = Join-Path $cache 'Veyra-1.0-AMD-win64-portable.zip'
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($runtime,$zip)
$script = [IO.File]::ReadAllText($Installer).Replace('@PUSHFPS_VERSION@','1.0.0').Replace('@UPSTREAM_TAG@','v1.0').Replace('@SHA256_AMD@',(Get-FileHash $zip).Hash)
$script = $script.Replace('if (Test-Path $ie4)', 'if ($false)')
$localInstaller = Join-Path $root 'installer.ps1'
function Run([string]$body, [int]$expected) {
    [IO.File]::WriteAllText($localInstaller,$body,[Text.UTF8Encoding]::new($true))
    $shell = (Get-Process -Id $PID).Path
    & $shell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $localInstaller -InstallDir $dest -Gpu AMD -ExperimentalExtras No -NoShortcuts -NoLaunch
    if ($LASTEXITCODE -ne $expected) { throw "Installer returned $LASTEXITCODE, expected $expected" }
}
$previousTemp = $env:TEMP
try {
    $env:TEMP = $root
    $held = [IO.File]::Open((Join-Path $dest 'PushFPS.exe'),'Open','Read','Read')
    try { Run $script 1 } finally { $held.Dispose() }
    Expect $dest 'engine.dll' 'old-engine'
    Expect $dest 'PushFPS.exe' 'old-app'
    if ((Test-Path (Join-Path $dest 'extra.dll')) -or (Test-Path (Join-Path $dest 'qml'))) { throw 'New file or directory survived rollback' }
    $afterRuntime = 'Set-InstallFile $rel $file'
    $interrupted = $script.Replace($afterRuntime, $afterRuntime + [Environment]::NewLine + '                if ($rel -eq "engine.dll") { [Environment]::Exit(42) }')
    Run $interrupted 42
    Expect $dest 'engine.dll' 'new-engine'
    $marker = '$manifest = Join-Path'
    Run ($script.Replace($marker, '[Environment]::Exit(43)' + [Environment]::NewLine + $marker)) 43
    Expect $dest 'engine.dll' 'old-engine'
    Run $script 0
    Expect $dest 'engine.dll' 'new-engine'
    Expect $dest 'PushFPS.exe' 'new-app'
    Expect $dest 'personal.txt' 'personal'
    if (-not (Test-Path (Join-Path $dest 'PushFPS-installed-files.txt'))) { throw 'Missing committed manifest' }
    Write-Output 'PASS locked upgrade rolls back, interrupted process recovers, retry commits and preserves personal files'
} finally { $env:TEMP = $previousTemp }
