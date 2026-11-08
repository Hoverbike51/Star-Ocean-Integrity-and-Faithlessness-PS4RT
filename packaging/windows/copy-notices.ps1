# Collect notices for the exact DLLs in a staged player folder and linked source dependencies.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Destination,
      [string]$Msys2Root=$(if($env:MSYS2_ROOT){$env:MSYS2_ROOT}else{'C:\msys64'}))
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$stage=(Resolve-Path -LiteralPath $Destination).Path
$notice=Join-Path $stage 'docs\third-party'
if(Test-Path -LiteralPath $notice){throw 'Third-party notice output must be new.'}
New-Item -ItemType Directory -Path $notice | Out-Null
$inventory=[Collections.Generic.List[string]]::new()
$inventory.Add('Third-party notices for this Windows build')
$inventory.Add('=========================================')
$inventory.Add('Package versions, licenses and upstream URLs follow. No machine paths are recorded.')
function Copy-Notice([string]$Source,[string]$Relative) {
 $target=Join-Path $notice $Relative
 if([IO.Path]::GetExtension($target) -notin '.txt','.md'){$target+='.txt'}
 New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force | Out-Null
 Copy-Item -LiteralPath $Source -Destination $target
}
foreach($base in @('gpu\third_party','third_party')){
 Get-ChildItem -LiteralPath (Join-Path $root $base) -Recurse -File | Where-Object {
  $_.Name -match '^(LICENSE|LICENCE|COPYING|COPYRIGHT|NOTICE)([.\-_]|$)'
 } | ForEach-Object {
  $relative=$_.FullName.Substring($root.Length+1)
  Copy-Notice $_.FullName ('source\'+$relative)
 }
}
foreach($name in @('THIRD_PARTY_GPL3.txt','THIRD_PARTY_LGPL21.txt','THIRD_PARTY_LGPL3.txt')){
 Copy-Notice (Join-Path $root ('docs\'+$name)) ('common\'+$name)
}
$pacman=Join-Path $Msys2Root 'usr\bin\pacman.exe'
$dllDirectories=@($stage)
$bin=Join-Path $stage 'bin'
if(Test-Path -LiteralPath $bin -PathType Container){$dllDirectories+=$bin}
$dlls=@(Get-ChildItem -LiteralPath $dllDirectories -Filter '*.dll' -File |
 Where-Object {$_.Name -notin 'ps4rt_dlss.dll','nvngx_dlss.dll'} | ForEach-Object {'/clang64/bin/'+$_.Name} | Sort-Object -Unique)
if(-not $dlls.Count){throw 'No staged DLLs to audit.'}
if(Test-Path -LiteralPath (Join-Path $bin 'ps4rt_dlss.dll')) {
 Copy-Notice (Join-Path $root 'gpu\dlss_bridge\LICENSE.txt') 'dlss-bridge\LICENSE.txt'
 $inventory.Add('Optional DLSS bridge: MIT; IFreemz, bbport and PS4RT contributors. Host/bridge ABI 2.')
}
if(Test-Path -LiteralPath (Join-Path $bin 'nvngx_dlss.dll')) {
 $sdkLicense=Join-Path $root 'out\dlss-notices\LICENSE.txt'
 if(-not (Test-Path -LiteralPath $sdkLicense)){throw 'Optional NVIDIA runtime requires its matching SDK license. Build with -DlssSdk or omit DLSS.'}
 Copy-Notice $sdkLicense 'nvidia-dlss\LICENSE.txt'
 $inventory.Add('NVIDIA DLSS runtime: separate NVIDIA RTX SDK terms; https://github.com/NVIDIA/DLSS')
 foreach($file in @('ps4rt_dlss.dll','nvngx_dlss.dll')) {
  $dll=Join-Path $bin $file
  $inventory.Add($file+' | version '+[Diagnostics.FileVersionInfo]::GetVersionInfo($dll).FileVersion+' | SHA256 '+(Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash)
 }
}
$owners=@(& $pacman -Qoq @dlls)
if($LASTEXITCODE -ne 0){throw 'Could not identify all staged DLL packages.'}
$owners+=@('mingw-w64-clang-x86_64-boost','mingw-w64-clang-x86_64-fmt',
           'mingw-w64-clang-x86_64-robin-map','mingw-w64-clang-x86_64-vulkan-memory-allocator')
foreach($owner in @($owners | Sort-Object -Unique)){
 $details=@(& $pacman -Qi $owner)
 if($LASTEXITCODE -ne 0){throw "Could not read dependency metadata: $owner"}
 $inventory.Add('')
 foreach($line in $details){if($line -match '^(Name|Version|URL|Licenses)\s*:'){$inventory.Add($line)}}
 $short=$owner -replace '^mingw-w64-clang-x86_64-',''
 if($short -eq 'vulkan-memory-allocator'){$short='VulkanMemoryAllocator'}
 $directory=Join-Path $Msys2Root ('clang64\share\licenses\'+$short)
 if(Test-Path -LiteralPath $directory){
  Get-ChildItem -LiteralPath $directory -Recurse -File | ForEach-Object {
   Copy-Notice $_.FullName ('msys2\'+$short+'\'+$_.FullName.Substring($directory.Length+1))
  }
 }else{$inventory.Add('No separate notice file is installed for this package; retain the declared license and upstream URL.')}
}
$inventory.Add('')
$inventory.Add('Source builds require the corresponding MSYS2 package sources and the exact project source revision.')
$inventory.Add('Bundled source dependencies retain the notices copied under source/. GNU license texts are under common/.')
$inventory | Set-Content -LiteralPath (Join-Path $notice 'DEPENDENCIES.txt') -Encoding utf8
Write-Output ('Third-party notices collected: '+@(Get-ChildItem -LiteralPath $notice -Recurse -File).Count+' files')
