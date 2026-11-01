# Validate the actual build in a disposable player folder, without creating a release ZIP.
[CmdletBinding()]
param([switch]$KeepStage)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$build = Join-Path $root 'out'
$stage = Join-Path $build ('update-player-qa-' + [guid]::NewGuid())
$validator = Join-Path $build 'gpu\launcher-updater-test.exe'
if (-not (Test-Path -LiteralPath $validator)) { throw 'Build developer tests first: .\build-windows.cmd -Test' }
if (Test-Path -LiteralPath $stage) { throw 'The isolated test directory already exists.' }
$oldPath = $env:PATH
try {
    foreach ($directory in @('', 'out', 'launcher', 'gpu', 'bin', 'docs')) {
        New-Item -ItemType Directory -Path (Join-Path $stage $directory) -Force | Out-Null
    }
    foreach ($name in @('Star Ocean 5.exe', 'Star Ocean 5 Launcher.exe', 'PkgExtractor.exe')) {
        Copy-Item -LiteralPath (Join-Path $build $name) -Destination (Join-Path $stage 'bin')
    }
    Copy-Item -LiteralPath (Join-Path $build 'SO5-Updater.exe') -Destination (Join-Path $stage 'bin\SO5-Updater.exe')
    Copy-Item -LiteralPath (Join-Path $build 'bootstrap\Star Ocean 5 Launcher.exe') -Destination $stage
    Get-ChildItem -LiteralPath $build -File -Filter '*.dll' | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $stage 'bin')
    }
    foreach ($name in @('LICENSE', 'README.md', 'CHANGELOG.md', 'VERSION')) {
        Copy-Item -LiteralPath (Join-Path $root $name) -Destination $stage
    }
    Copy-Item -LiteralPath (Join-Path $root 'packaging\windows\README-Windows.txt') -Destination $stage
    foreach ($name in @('UPSTREAM_CREDITS.md', 'MINIZ_LICENSE.txt', 'LIBCXX_LICENSE.txt', 'LAUNCHER_GUIDE.md', 'IN_GAME_MENU_GUIDE.md')) {
        Copy-Item -LiteralPath (Join-Path $root ('docs\' + $name)) -Destination (Join-Path $stage 'docs')
    }
    New-Item -ItemType Directory -Path (Join-Path $stage 'docs\images\readme') -Force | Out-Null
    foreach ($guideImages in @('launcher','custom_menu')) {
        Copy-Item -LiteralPath (Join-Path $root ('docs\images\readme\' + $guideImages)) -Destination (Join-Path $stage 'docs\images\readme') -Recurse
    }
    Copy-Item -LiteralPath (Join-Path $root 'launcher\assets') -Destination (Join-Path $stage 'launcher') -Recurse
    Copy-Item -LiteralPath (Join-Path $root 'gpu\assets') -Destination (Join-Path $stage 'gpu') -Recurse
    Copy-Item -LiteralPath (Join-Path $root 'fsr4_shaders') -Destination $stage -Recurse
    & (Join-Path $PSScriptRoot 'copy-notices.ps1') -Destination $stage
    $privateFiles = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object {
        $_.Name -match '(?i)\.(etl|wpaprofile|csv|ps1|cmd|py|log)$|^performance.*\.json$|settings.*\.ini$'
    })
    if ($privateFiles.Count) { throw 'Player payload contains personal diagnostics, settings or developer tracing tools.' }
    $env:PATH = "$build;$env:SystemRoot\System32;$env:SystemRoot"
    & $validator --validate-package $stage
    if ($LASTEXITCODE -ne 0) { throw "Native updater validation failed ($LASTEXITCODE)." }
    & (Join-Path $PSScriptRoot 'smoke-test.ps1') -Package $stage
    Write-Host 'PASS: real player payload and updater validated without a release ZIP.'
    if ($KeepStage) { Write-Host "Test player folder: $stage" }
} finally {
    $env:PATH = $oldPath
    if (-not $KeepStage -and (Test-Path -LiteralPath $stage)) {
        $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
        if ([IO.Path]::GetDirectoryName($resolvedStage) -ne $build -or
            -not [IO.Path]::GetFileName($resolvedStage).StartsWith('update-player-qa-')) {
            throw 'Refusing to remove an unexpected test path.'
        }
        $items = @((Get-Item -LiteralPath $resolvedStage)) + @(Get-ChildItem -LiteralPath $resolvedStage -Recurse -Force)
        $links = @($items | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
        if ($links.Count) { throw 'Refusing recursive cleanup of a test directory containing links.' }
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}
