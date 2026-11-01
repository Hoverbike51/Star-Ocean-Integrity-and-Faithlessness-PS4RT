[CmdletBinding()]
param(
    [switch]$Test,
    [switch]$PlayerFolder = $true,
    [string]$Msys2Root = $(if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { 'C:\msys64' })
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$bash = Join-Path $Msys2Root 'usr\bin\bash.exe'
$cygpath = Join-Path $Msys2Root 'usr\bin\cygpath.exe'
if (-not (Test-Path -LiteralPath $bash) -or -not (Test-Path -LiteralPath $cygpath)) {
    throw "MSYS2 was not found at '$Msys2Root'. See packaging\windows\README.md."
}
$unixRoot = (& $cygpath -u $root).Trim()
$unixMsys2 = (& $cygpath -u $Msys2Root).Trim()
$buildArgument = if ($Test) { ' --test' } else { '' }
$command = 'export MSYSTEM=CLANG64 MSYS2_ROOT="$2"; cd "$1"; bash build.sh' + $buildArgument
& $bash -lc $command -- $unixRoot $unixMsys2
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($PlayerFolder) {
    $packageCommand = 'export MSYSTEM=CLANG64 MSYS2_ROOT="$2"; cd "$1"; bash packaging/windows/package.sh --folder-only'
    & $bash -lc $packageCommand -- $unixRoot $unixMsys2
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $version = (Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim()
    $playerFolderPath = Join-Path $root "dist\ps4rt-star-ocean-5-windows-x64-v$version-latest"
    & (Join-Path $root 'packaging\windows\smoke-test.ps1') -Package $playerFolderPath
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

exit 0
