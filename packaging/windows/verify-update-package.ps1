# Validate the final payload with the native updater, including the frozen v1.0.1 contract.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Package,
    [string]$Validator,
    [string]$ExpectedVersion
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if(-not $Validator){$Validator=Join-Path $root 'out\SO5-Updater.exe'}
$Validator=(Resolve-Path -LiteralPath $Validator).Path
$inputPath=(Resolve-Path -LiteralPath $Package).Path
if(-not $ExpectedVersion){$ExpectedVersion=(Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim()}
if($ExpectedVersion -notmatch '^\d+\.\d+\.\d+$'){throw 'Invalid expected release version.'}
$tempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$work=Join-Path $tempRoot ('so5-update-verify-'+[guid]::NewGuid())
New-Item -ItemType Directory -Path $work | Out-Null
try {
    if(Test-Path -LiteralPath $inputPath -PathType Container) {
        $payloadRoot=$inputPath
        $arguments=@('--validate-package',('"{0}"' -f $inputPath),$ExpectedVersion)
    } else {
        if([IO.Path]::GetExtension($inputPath) -ine '.zip'){throw 'Select the player ZIP or its extracted root folder.'}
        $payloadRoot=Join-Path $work 'package'
        # Native extraction checks traversal, collisions, links and resource limits before writing.
        $arguments=@('--validate-archive',('"{0}"' -f $inputPath),$ExpectedVersion,('"{0}"' -f (Join-Path $work 'package')))
    }
    $stdout=Join-Path $work 'stdout.txt';$stderr=Join-Path $work 'stderr.txt'
    $process=Start-Process -FilePath $Validator -ArgumentList $arguments -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $null=$process.Handle # Retain the process handle: Windows PowerShell 5.1 can otherwise lose ExitCode.
    if(-not $process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw 'Native package validation timed out.'}
    if(Test-Path -LiteralPath $stdout){Get-Content -LiteralPath $stdout | Write-Host}
    $detail=if(Test-Path -LiteralPath $stderr){(Get-Content -LiteralPath $stderr -Raw)}else{''}
    if($process.ExitCode -ne 0){throw "Release package rejected ($($process.ExitCode)): $detail Rebuild SO5-Updater.exe if it does not support validation."}
    & (Join-Path $root 'scripts\verify-fsr4-assets.ps1') -Root $payloadRoot
    $amdInventory=Get-Content -LiteralPath (Join-Path $root 'gpu\third_party\amd-fsr-sdk24\binaries.json') -Raw | ConvertFrom-Json
    $amdFolder=Join-Path $payloadRoot 'gpu\assets\amd-fsr-sdk24'
    foreach($binary in $amdInventory.binaries.PSObject.Properties) {
        if($binary.Name -notmatch '^[a-z0-9_]+\.dll$'){throw 'Invalid AMD provider inventory entry.'}
        $dll=Join-Path $amdFolder $binary.Name
        if(-not (Test-Path -LiteralPath $dll -PathType Leaf) -or
           (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant() -ne $binary.Value) {
            throw "Packaged AMD provider/runtime does not match the verified source: $($binary.Name)"
        }
    }
    Write-Host "PASS: final player payload, FSR model/provider hashes and v1.0.1 update compatibility ($ExpectedVersion)."
} finally {
    if(Test-Path -LiteralPath $work) {
        $resolved=(Resolve-Path -LiteralPath $work).Path
        if([IO.Path]::GetDirectoryName($resolved).TrimEnd('\') -ine $tempRoot.TrimEnd('\') -or
           [IO.Path]::GetFileName($resolved) -notmatch '^so5-update-verify-[0-9a-f-]{36}$') {
            throw 'Refusing cleanup outside the isolated validation directory.'
        }
        $items=@(Get-Item -LiteralPath $resolved)+@(Get-ChildItem -LiteralPath $resolved -Force -Recurse)
        if(@($items | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count) {
            throw 'Refusing recursive cleanup of a validation directory containing links.'
        }
        $removed=$false
        for($attempt=0;$attempt -lt 5 -and -not $removed;++$attempt) {
            try {Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction Stop;$removed=$true}
            catch {Start-Sleep -Milliseconds 250}
        }
        if(-not $removed){Write-Warning "Validation finished, but Windows still holds temporary files: $resolved"}
    }
}
