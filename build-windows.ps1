[CmdletBinding()]
param(
    [switch]$Test,
    [switch]$PlayerFolder = $true,
    [switch]$Dlss,
    [switch]$NoDlss,
    [string]$DlssSdk = $env:DLSS_SDK_ROOT,
    [string]$DlssGenerator = '',
    [string]$Msys2Root = $(if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { 'C:\msys64' })
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($Dlss -and $NoDlss) { throw 'Choose -Dlss or -NoDlss, not both.' }
if (-not $DlssSdk) {
    $candidate = Join-Path $root 'out\dependencies\nvidia-dlss'
    if (Test-Path -LiteralPath (Join-Path $candidate 'include\nvsdk_ngx_vk.h')) { $DlssSdk = $candidate }
}
if ($Dlss -and -not $DlssSdk) { throw 'Set -DlssSdk or DLSS_SDK_ROOT to the official NVIDIA DLSS SDK.' }
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

# DLSS is optional. The native application builds and runs without the SDK.
# No SDK download, Python installation or NVIDIA driver replacement is performed.
if ($DlssSdk -and -not $NoDlss) {
    $cmake = Join-Path $Msys2Root 'clang64\bin\cmake.exe'
    $bridge = Join-Path $root 'out\dlss-bridge'
    if (-not $DlssGenerator) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere)) { throw 'The optional DLSS bridge requires Visual Studio 2022 or newer C++ tools.' }
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
        if (-not $installation) { throw 'No Visual Studio x64 C++ toolchain was found for the optional DLSS bridge.' }
        $major = [int]($installation.Split('.')[0])
        if ($major -ge 18) { $DlssGenerator = 'Visual Studio 18 2026' }
        elseif ($major -eq 17) { $DlssGenerator = 'Visual Studio 17 2022' }
        else { throw 'The optional DLSS bridge requires Visual Studio 2022 or newer.' }
    }
    & $cmake --fresh -S (Join-Path $root 'gpu\dlss_bridge') -B $bridge -G $DlssGenerator -A x64 `
        "-DDLSS_SDK_ROOT=$([IO.Path]::GetFullPath($DlssSdk))" `
        "-DVULKAN_INCLUDE=$(Join-Path $Msys2Root 'clang64\include')"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $cmake --build $bridge --config Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    foreach ($destination in @('out','out\gpu')) {
        Copy-Item -LiteralPath (Join-Path $bridge 'Release\ps4rt_dlss.dll') -Destination (Join-Path $root $destination)
        Copy-Item -LiteralPath (Join-Path $DlssSdk 'lib\Windows_x86_64\rel\nvngx_dlss.dll') -Destination (Join-Path $root $destination)
    }
    $sdkNotices = Join-Path $root 'out\dlss-notices'
    New-Item -ItemType Directory -Path $sdkNotices -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $DlssSdk 'LICENSE.txt') -Destination $sdkNotices
}

if ($NoDlss) {
    foreach ($directory in @('out','out\gpu')) {
        foreach ($name in @('ps4rt_dlss.dll','nvngx_dlss.dll')) {
            $component=Join-Path (Join-Path $root $directory) $name
            if(Test-Path -LiteralPath $component){Remove-Item -LiteralPath $component}
        }
    }
}

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
