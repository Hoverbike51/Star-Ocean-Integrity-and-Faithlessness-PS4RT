[CmdletBinding()]
param(
    [switch]$Test,
    [string]$Msys2Root = $(if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { 'C:\msys64' })
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$bash = Join-Path $Msys2Root 'usr\bin\bash.exe'
$cygpath = Join-Path $Msys2Root 'usr\bin\cygpath.exe'
if (-not (Test-Path -LiteralPath $bash) -or -not (Test-Path -LiteralPath $cygpath)) {
    throw "MSYS2 introuvable dans '$Msys2Root'. Voir packaging\windows\README.md."
}
$unixRoot = (& $cygpath -u $root).Trim()
$unixMsys2 = (& $cygpath -u $Msys2Root).Trim()
$buildArgument = if ($Test) { ' --test' } else { '' }
$command = 'export MSYSTEM=CLANG64 MSYS2_ROOT="$2"; cd "$1"; bash build.sh' + $buildArgument
& $bash -lc $command -- $unixRoot $unixMsys2
exit $LASTEXITCODE
