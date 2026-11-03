param([Parameter(Mandatory=$true)][string]$ModPath)
$ErrorActionPreference='Stop'
$private='E:\DayZMods\Keys\Derk.biprivatekey'
$public='E:\DayZMods\Keys\Derk.bikey'
foreach($file in @($private,$public)){if(!(Test-Path -LiteralPath $file)){throw ('Required Derk signing file is missing: '+$file)}}
$mod=[IO.Path]::GetFullPath($ModPath).TrimEnd('\')
$addons=Join-Path $mod 'Addons'
$pbos=@(Get-ChildItem -LiteralPath $addons -Filter '*.pbo' -File)
if(!$pbos.Count){throw 'No PBO files found to sign'}
$sign='E:\Steam\steamapps\common\DayZ Tools\Bin\DsUtils\DSSignFile.exe'
$check='E:\Steam\steamapps\common\DayZ Tools\Bin\DsUtils\DSCheckSignatures.exe'
foreach($pbo in $pbos){
 & $sign $private $pbo.FullName
 if($LASTEXITCODE-ne 0){throw ('Derk signing failed: '+$pbo.Name)}
 if(!(Test-Path -LiteralPath ($pbo.FullName+'.Derk.bisign'))){throw ('Derk signature was not created: '+$pbo.Name)}
}
New-Item -ItemType Directory -Force -Path (Join-Path $mod 'keys')|Out-Null
Copy-Item -LiteralPath $public -Destination (Join-Path $mod 'keys\Derk.bikey') -Force
# Remove obsolete signatures only inside this explicitly supplied mod folder.
foreach($signature in Get-ChildItem -LiteralPath $addons -Filter '*.bisign' -File){
 if($signature.Name-notlike '*.pbo.Derk.bisign'){
  if(![IO.Path]::GetFullPath($signature.FullName).StartsWith($mod+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Signature path escapes mod directory'}
  Remove-Item -LiteralPath $signature.FullName
 }
}
$result=@(& $check $addons (Join-Path $mod 'keys') 2>&1)
$result|Write-Output
if($LASTEXITCODE-ne 0 -or ($result-match 'is wrong|is invalid|failed') -or @($result-match 'is OK').Count-ne $pbos.Count){throw 'Signature verification failed; do not publish this folder'}
Write-Output ('Verified '+$pbos.Count+' PBOs signed with Derk only. No private key copied.')
