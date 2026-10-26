[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Game,
    [ValidateSet('off', 'fsr3', 'fsr4')]
    [string]$Upscaler,
    [ValidateSet('1080', '1440', '4k')]
    [string]$OutputResolution,
    [switch]$PrepareOnly
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$arguments = @((Join-Path $root 'scripts\ps4rt_launch.py'), '--game', $Game)
if ($Upscaler) { $arguments += @('--upscaler', $Upscaler) }
if ($OutputResolution) { $arguments += @('--output-resolution', $OutputResolution) }
if ($PrepareOnly) { $arguments += '--prepare-only' }

& python @arguments
exit $LASTEXITCODE
