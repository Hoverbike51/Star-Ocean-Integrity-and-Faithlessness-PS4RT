param([Parameter(Mandatory = $true)][string]$Package, [switch]$RequireDlss)
$ErrorActionPreference = 'Stop'
$work = $null
try {
    $resolvedPackage = (Resolve-Path -LiteralPath $Package).Path
    if (Test-Path -LiteralPath $resolvedPackage -PathType Container) {
        $tree = $resolvedPackage
    } else {
        $work = Join-Path ([IO.Path]::GetTempPath()) ("ps4rt-smoke-" + [guid]::NewGuid())
        New-Item -ItemType Directory -Path $work | Out-Null
        & "$env:SystemRoot\System32\tar.exe" -xf $resolvedPackage -C $work
        if ($LASTEXITCODE -ne 0) { throw "Package extraction failed ($LASTEXITCODE)." }
        $tree = Get-ChildItem -LiteralPath $work -Directory | Select-Object -First 1 -ExpandProperty FullName
        if (-not $tree) { throw 'The package does not contain a root directory.' }
    }
    & (Join-Path $PSScriptRoot '..\..\scripts\verify-fsr4-assets.ps1') -Root $tree
    $entry = Join-Path $tree 'Star Ocean 5 Launcher.exe'
    $runtime = Join-Path $tree 'bin\Star Ocean 5.exe'
    $launcher = Join-Path $tree 'bin\Star Ocean 5 Launcher.exe'
    $extractor = Join-Path $tree 'bin\PkgExtractor.exe'
    $updater = Join-Path $tree 'bin\SO5-Updater.exe'
    $versionFile = Join-Path $tree 'VERSION'
    foreach($obsolete in @('ps4rt.exe','PS4RT-Launcher.exe','PS4RT-PkgExtractor.exe','SO5-PkgExtractor.exe')) {
        if(Test-Path -LiteralPath (Join-Path $tree $obsolete)){throw "Obsolete executable in player package: $obsolete"}
    }
    foreach ($required in @($entry, $runtime, $launcher, $extractor, $updater, $versionFile)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required file is missing: $required" }
    }
    $expectedVersion = (Get-Content -LiteralPath $versionFile -Raw).Trim()
    & (Join-Path $PSScriptRoot 'verify-update-package.ps1') -Package $tree -ExpectedVersion $expectedVersion
    if($LASTEXITCODE -ne 0){throw 'Native update compatibility validation failed.'}
    $bridge = Test-Path -LiteralPath (Join-Path $tree 'bin\ps4rt_dlss.dll')
    $nvidia = Test-Path -LiteralPath (Join-Path $tree 'bin\nvngx_dlss.dll')
    if ($bridge -ne $nvidia -or ($RequireDlss -and -not $bridge)) { throw 'Incomplete DLSS runtime pair in player package.' }
    foreach ($executable in @($entry, $runtime, $launcher, $extractor, $updater)) {
        $metadata = (Get-Item -LiteralPath $executable).VersionInfo
        if ($metadata.FileVersion -ne $expectedVersion -or
            ('{0}.{1}.{2}.{3}' -f $metadata.FileMajorPart, $metadata.FileMinorPart, $metadata.FileBuildPart, $metadata.FilePrivatePart) -ne "$expectedVersion.0") {
            throw "Incorrect file version: $executable"
        }
        if ($metadata.CompanyName -ne 'HoverMods Vault') { throw "Incorrect publisher: $executable" }
        if ($metadata.OriginalFilename -cne [IO.Path]::GetFileName($executable)) { throw "Incorrect original filename resource: $executable" }
    }
    foreach ($expected in @(
        @{Path=$runtime; Description='STAR OCEAN: Integrity and Faithlessness'},
        @{Path=$launcher; Description='Star Ocean 5 Launcher'},
        @{Path=$extractor; Description='PKG Extractor'}
    )) {
        if ((Get-Item -LiteralPath $expected.Path).VersionInfo.FileDescription -cne $expected.Description) {
            throw "Incorrect executable description: $($expected.Path)"
        }
    }
    $unexpectedRoot = @(Get-ChildItem -LiteralPath $tree -File | Where-Object {
        $_.Extension -ieq '.dll' -or ($_.Extension -ieq '.exe' -and $_.Name -ine 'Star Ocean 5 Launcher.exe')
    })
    if ($unexpectedRoot.Count) { throw 'Only the native launcher entry may be an executable at the player root; DLLs belong in bin.' }
    $cleanPath = "$env:SystemRoot\System32;$env:SystemRoot"
    $oldPath = $env:PATH
    $env:PATH = $cleanPath
    try {
        $output = & $runtime --cpu-features 2>&1
        if ($LASTEXITCODE -ne 0) { throw "Star Ocean 5.exe --cpu-features failed ($LASTEXITCODE): $output" }
        $output | Write-Host
        $launcherProcess = Start-Process -FilePath $entry -ArgumentList '--self-test' -PassThru -WindowStyle Hidden
        if (-not $launcherProcess.WaitForExit(10000)) {
            $launcherProcess.Kill()
            throw 'Star Ocean 5 Launcher.exe --self-test did not respond within 10 seconds.'
        }
        if ($launcherProcess.ExitCode -ne 0) { throw "Star Ocean 5 Launcher.exe --self-test failed ($($launcherProcess.ExitCode))." }
        $actualLauncher = Start-Process -FilePath $launcher -ArgumentList '--self-test' -PassThru -WindowStyle Hidden
        if (-not $actualLauncher.WaitForExit(10000)) { $actualLauncher.Kill(); throw 'The bin launcher self-test timed out.' }
        if ($actualLauncher.ExitCode -ne 0) { throw 'The bin launcher self-test failed.' }
        $extractorOutput = & $extractor --self-test 2>&1
        if ($LASTEXITCODE -ne 0) { throw "Native PKG extractor self-test failed: $extractorOutput" }
        $updaterProcess = Start-Process -FilePath $updater -ArgumentList '--self-test' -PassThru -WindowStyle Hidden
        if (-not $updaterProcess.WaitForExit(10000)) { $updaterProcess.Kill(); throw 'Native updater self-test timed out.' }
        if ($updaterProcess.ExitCode -ne 0) { throw "Native updater self-test failed ($($updaterProcess.ExitCode))." }
    } finally {
        $env:PATH = $oldPath
    }
    $pythonFiles = @(Get-ChildItem -LiteralPath $tree -Recurse -File -Filter '*.py')
    if ($pythonFiles.Count) { throw 'The public package still contains Python scripts.' }
    Write-Host 'OK: the native runtime, launcher, extractor and updater are complete without MSYS2 or Python.'
} finally {
    if ($work -and (Test-Path -LiteralPath $work)) {
        $removed = $false
        for ($attempt = 0; $attempt -lt 5 -and -not $removed; $attempt++) {
            try {
                Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction Stop
                $removed = $true
            } catch {
                Start-Sleep -Milliseconds 250
            }
        }
        if (-not $removed) {
            Write-Warning "The test succeeded, but Windows still holds a temporary lock on $work"
        }
    }
}
