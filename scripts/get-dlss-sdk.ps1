[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'file-hash.ps1')
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dlss-sdk.json') -Raw | ConvertFrom-Json
# Build-time dependency only. Player machines do not download an SDK or require Visual Studio.
# NVIDIA's license is retained alongside the bundled runtime DLL.
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
foreach ($entry in $manifest.files) {
    $file = Join-Path $Destination $entry.path
    if ((Test-Path -LiteralPath $file) -and (Get-So5FileSha256 $file) -eq $entry.sha256) { continue }
    New-Item -ItemType Directory -Path (Split-Path -Parent $file) -Force | Out-Null
    $temporary = $file + '.partial'
    try {
        Write-Host ('Downloading verified NVIDIA DLSS build dependency: ' + $entry.path)
        $url = 'https://raw.githubusercontent.com/NVIDIA/DLSS/' + $manifest.commit + '/' + $entry.path
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $temporary
        if ((Get-So5FileSha256 $temporary) -ne $entry.sha256) {
            throw ('NVIDIA SDK checksum mismatch: ' + $entry.path)
        }
        Move-Item -LiteralPath $temporary -Destination $file -Force
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
    }
}
