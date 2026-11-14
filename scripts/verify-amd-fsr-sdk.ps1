# Verify the exact signed AMD/Microsoft binaries distributed with this source revision.
param([string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference='Stop'
$sdk=Join-Path $Root 'gpu\third_party\amd-fsr-sdk24'
$manifest=Get-Content -LiteralPath (Join-Path $sdk 'binaries.json') -Raw | ConvertFrom-Json
foreach($entry in $manifest.binaries.PSObject.Properties) {
 if($entry.Name -notmatch '^[a-z0-9_]+\.dll$'){throw 'Invalid AMD SDK binary name.'}
 $file=Join-Path (Join-Path $sdk 'bin') $entry.Name
 if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value) {
  throw "AMD FSR SDK 2.4 verification failed: $($entry.Name)"
 }
}
Write-Host 'AMD FSR SDK 2.4: exact signed provider/runtime binaries verified.'
