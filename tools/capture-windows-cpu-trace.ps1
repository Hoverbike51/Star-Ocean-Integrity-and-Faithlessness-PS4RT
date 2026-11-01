# Native Windows CPU trace for short PS4RT performance investigations.
# Windows Performance Recorder/Analyzer is part of the Windows ADK, not a player dependency.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Administrator)) {
    $arguments = @(
        '-NoProfile'
        '-ExecutionPolicy', 'Bypass'
        '-File', ('"{0}"' -f $PSCommandPath)
    )
    Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $arguments
    exit 0
}

$repository = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$outputDirectory = Join-Path $repository 'out\diagnostics'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$tracePath = Join-Path $outputDirectory "ps4rt-cpu-$timestamp.etl"
$profilePath = Join-Path $outputDirectory "ps4rt-cpu-$timestamp-profile.txt"
$activityPath = Join-Path $outputDirectory "ps4rt-cpu-$timestamp-threads.txt"
$stackPath = Join-Path $outputDirectory "ps4rt-cpu-$timestamp-stacks.txt"

$wpr = Join-Path $env:SystemRoot 'System32\wpr.exe'
$toolkit = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Windows Performance Toolkit'
$xperf = Join-Path $toolkit 'xperf.exe'
$exporter = Join-Path $toolkit 'wpaexporter.exe'
$exportProfile = Join-Path $repository 'tools\ps4rt-cpu-export.wpaProfile'

if (-not (Test-Path -LiteralPath $wpr)) {
    throw 'Windows Performance Recorder was not found.'
}

Write-Host ''
Write-Host 'Starting a sampled CPU trace. It records CPU scheduling and call stacks, not game or save contents.'
Write-Host 'For the shortest useful capture, run this tool while the game is already in a scene that stutters.'
Write-Host 'Recording will stop automatically after 30 seconds of gameplay.'
Write-Host ''

& $wpr -start CPU -filemode
if ($LASTEXITCODE -ne 0) {
    throw 'Windows Performance Recorder could not start. Another recording may already be active.'
}

$stopped = $false
try {
    $deadline = (Get-Date).AddMinutes(5)
    while (-not (Get-Process -Name 'Star Ocean 5' -ErrorAction SilentlyContinue)) {
        if ((Get-Date) -ge $deadline) {
            throw 'PS4RT did not start within five minutes.'
        }
        Write-Host 'Waiting for Star Ocean 5.exe...'
        Start-Sleep -Seconds 2
    }
    Write-Host 'PS4RT detected. Capturing the next 30 seconds...'
    for ($remaining = 30; $remaining -gt 0; --$remaining) {
        Write-Progress -Activity 'Capturing PS4RT CPU activity' -Status "$remaining seconds remaining" -PercentComplete ((30 - $remaining) * 100 / 30)
        Start-Sleep -Seconds 1
    }
    Write-Progress -Activity 'Capturing PS4RT CPU activity' -Completed
    & $wpr -stop $tracePath
    if ($LASTEXITCODE -ne 0) {
        throw 'Windows Performance Recorder could not save the trace.'
    }
    $stopped = $true
} finally {
    if (-not $stopped) {
        & $wpr -cancel | Out-Null
    }
}

if (Test-Path -LiteralPath $xperf) {
    & $xperf -i $tracePath -tle -o $profilePath -a profile -detail
    & $xperf -i $tracePath -tle -o $activityPath -a activityintervals -thread -interval 100000 -mintime 1000
    & $xperf -i $tracePath -tle -o $stackPath -a stack -process 'Star Ocean 5.*' -butterfly 5
}

if ((Test-Path -LiteralPath $exporter) -and (Test-Path -LiteralPath $exportProfile)) {
    $exportDirectory = Join-Path $outputDirectory "ps4rt-cpu-$timestamp-wpa"
    New-Item -ItemType Directory -Path $exportDirectory -Force | Out-Null
    & $exporter -i $tracePath -profile $exportProfile -outputfolder $exportDirectory -prefix 'ps4rt-' -outputformat CSV
}

Write-Host ''
Write-Host 'CPU trace saved successfully:'
Write-Host $tracePath
Read-Host 'Press Enter to close this window'

