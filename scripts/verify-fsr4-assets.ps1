# Verify external FSR 4 v07 assets without compiling or starting the game.
[CmdletBinding()]
param([string]$Root = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Root) { $Root = $projectRoot }
. (Join-Path $PSScriptRoot 'file-hash.ps1')
$rootPath = (Resolve-Path -LiteralPath $Root).Path
$manifestPath = Join-Path $projectRoot 'packaging\fsr4-v07-assets.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schema_version -ne 1 -or ($manifest.output_tiers -join ',') -ne '1080,2160,4320') {
    throw 'Unsupported FSR 4 asset inventory.'
}
$entries = @{}
foreach ($entry in $manifest.files.PSObject.Properties) { $entries[$entry.Name] = $entry.Value }
foreach ($model in @('native','quality','balanced','performance','ultraperf','drs')) {
    foreach ($tier in $manifest.output_tiers) {
        foreach ($pass in @('pre','post') + @(1..12 | ForEach-Object { "pass$_" })) {
            $name = "fsr4_shaders/fsr4_model_v07_i8_${model}_${tier}_${pass}.spv"
            if (-not $entries.ContainsKey($name)) { throw "FSR 4 inventory is missing $name" }
        }
    }
    foreach ($part in @('initializers','pre_weights')) {
        $name = "fsr4_shaders/fsr4_model_v07_i8_${model}_${part}.bin"
        $requiredSize = if ($part -eq 'initializers') { 89216 } else { 1024 }
        if (-not $entries.ContainsKey($name) -or $entries[$name].size -ne $requiredSize) {
            throw "Invalid FSR 4 model weight inventory: $name"
        }
    }
}
foreach ($entry in $manifest.files.PSObject.Properties) {
    if ($entry.Name -notmatch '^fsr4_shaders/[A-Za-z0-9_.-]+$') {
        throw "Invalid FSR 4 asset path: $($entry.Name)"
    }
    $path = Join-Path $rootPath $entry.Name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing FSR 4 asset: $($entry.Name)" }
    if ((Get-Item -LiteralPath $path).Length -ne $entry.Value.size -or
        (Get-So5FileSha256 $path) -ne $entry.Value.sha256) {
        throw "FSR 4 asset verification failed: $($entry.Name)"
    }
}
Write-Host "OK: $($entries.Count) FSR 4 v07 assets verified (1080 / 2160 / 4320 tiers)."
