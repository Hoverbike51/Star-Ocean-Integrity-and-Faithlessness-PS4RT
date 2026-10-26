param([Parameter(Mandatory = $true)][string]$Package)
$ErrorActionPreference = 'Stop'
$work = Join-Path ([IO.Path]::GetTempPath()) ("ps4rt-smoke-" + [guid]::NewGuid())
try {
    New-Item -ItemType Directory -Path $work | Out-Null
    $archive = (Resolve-Path -LiteralPath $Package).Path
    & "$env:SystemRoot\System32\tar.exe" -xf $archive -C $work
    if ($LASTEXITCODE -ne 0) { throw "L'extraction du paquet a échoué ($LASTEXITCODE)." }
    $tree = Get-ChildItem -LiteralPath $work -Directory | Select-Object -First 1 -ExpandProperty FullName
    if (-not $tree) { throw 'Le paquet ne contient aucun dossier racine.' }
    $runtime = Join-Path $tree 'ps4rt.exe'
    $launcher = Join-Path $tree 'PS4RT-Launcher.exe'
    $extractor = Join-Path $tree 'PS4RT-PkgExtractor.exe'
    $extractorTool = Join-Path $tree 'tools\pkg-extractor\pkg_extractor.exe'
    $extractorLibc = Join-Path $tree 'tools\pkg-extractor\libc++.dll'
    $extractorZlib = Join-Path $tree 'tools\pkg-extractor\zlib1.dll'
    foreach ($required in @($runtime, $launcher, $extractor, $extractorTool, $extractorLibc, $extractorZlib)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Fichier requis absent : $required" }
    }
    $cleanPath = "$env:SystemRoot\System32;$env:SystemRoot"
    $oldPath = $env:PATH
    $env:PATH = $cleanPath
    try {
        $output = & $runtime --cpu-features 2>&1
        if ($LASTEXITCODE -ne 0) { throw "ps4rt.exe --cpu-features a échoué ($LASTEXITCODE): $output" }
        $output | Write-Host
        $launcherProcess = Start-Process -FilePath $launcher -ArgumentList '--self-test' -PassThru -WindowStyle Hidden
        if (-not $launcherProcess.WaitForExit(10000)) {
            $launcherProcess.Kill()
            throw 'PS4RT-Launcher.exe --self-test ne répond pas après 10 secondes.'
        }
        if ($launcherProcess.ExitCode -ne 0) { throw "PS4RT-Launcher.exe --self-test a échoué ($($launcherProcess.ExitCode))." }
    } finally {
        $env:PATH = $oldPath
    }
    $pythonFiles = @(Get-ChildItem -LiteralPath $tree -Recurse -File -Filter '*.py')
    if ($pythonFiles.Count) { throw 'Le paquet public contient encore des scripts Python.' }
    Write-Host 'OK: runtime, launcher et extracteur natifs sont complets sans MSYS2 ni Python.'
} finally {
    if (Test-Path -LiteralPath $work) {
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
            Write-Warning "Le test a réussi, mais Windows conserve encore un verrou temporaire sur $work"
        }
    }
}
