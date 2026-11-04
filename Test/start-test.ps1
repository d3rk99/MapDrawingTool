# Starts a local DayZ server (and with -Client, the game) running only @MapDrawingToolTest.
# The web app must be running on 127.0.0.1:8640 (it serves the overlay the test downloads).
# Usage: Test\start-test.ps1 -ProfileRoot <folder>          start the server
#        Test\start-test.ps1 -ProfileRoot <folder> -Client  start the game and join it
#        -Mod E:\DayZMods\Mods\@MapDrawingTool   runs the real mod instead of the feasibility test
#        -Map <web app map id> writes <ProfileRoot>\Server\MapDrawingTool\config.json for the real mod
#        A second server at once needs its own -ProfileRoot, -Port and -InstanceId.
#        -Client -PlayerProfile: the game uses your normal profile (%LOCALAPPDATA%\DayZ, also its logs), as a player's would; stamps go in %LOCALAPPDATA%\DayZ\MapDrawingTool\stamps
param([Parameter(Mandatory = $true)][string]$ProfileRoot, [switch]$Client, [int]$Port = 2402,
  [string]$Mod = 'E:\DayZMods\Mods\@MapDrawingToolTest', [int]$InstanceId = 42, [string]$Map = '',
  [string]$Mission = 'mdttest.chernarusplus', [switch]$PlayerProfile)
$ErrorActionPreference = 'Stop'
$mod = $Mod
$serverRoot = 'E:\Steam\steamapps\common\DayZServer'
$clientRoot = 'E:\Steam\steamapps\common\DayZ'
New-Item -ItemType Directory -Force -Path $ProfileRoot | Out-Null

if ($Client) {
  if (Get-Process -Name DayZ_x64, DayZ_BE -ErrorAction SilentlyContinue) { throw 'Exit the running DayZ client first.' }
  $args = @('0', '1', '1', '-exe', 'DayZ_x64.exe', '-noPause', '-noSplash', '-window', "`"-mod=$mod`"",
    '-name=MDTTest', '-connect=127.0.0.1', "-port=$Port")
  if (!$PlayerProfile) { $args += "`"-profiles=$ProfileRoot\Client`"" }
  Start-Process -FilePath (Join-Path $clientRoot 'DayZ_BE.exe') -ArgumentList $args -WorkingDirectory $clientRoot
  Write-Output "Client starting, joining 127.0.0.1:$Port"
  return
}

$cfg = Join-Path $ProfileRoot 'serverDZ.cfg'
$text = Get-Content -LiteralPath (Join-Path $serverRoot 'serverDZ.cfg') -Raw
$text = $text -replace 'hostname\s*=\s*"[^"]*"', "hostname = `"MDT live map test $Port`""
$text = $text -replace 'instanceId\s*=\s*\d+', "instanceId = $InstanceId"
$text += "`r`nsteamQueryPort = $($Port + 25000);`r`n"
if ($Map) {
  $mdt = Join-Path $ProfileRoot 'Server\MapDrawingTool'
  New-Item -ItemType Directory -Force -Path $mdt | Out-Null
  # the web app's serverKey (Settings > Game servers) so in-game drawings reach it
  $key = (Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\Web\config.json') -Raw | ConvertFrom-Json).serverKey
  "{`"liveUrl`":`"http://127.0.0.1:8640/`",`"mapId`":`"$Map`",`"serverKey`":`"$key`",`"pollSeconds`":30}" | Set-Content -LiteralPath (Join-Path $mdt 'config.json') -Encoding ASCII
}
# mdttest.<world> = dayzOffline.<world> with cfggameplay.json "use3DMap": true and a paper map at spawn
# (the unfolded map stays in hand; M toggles the full-screen map). The file only loads with enableCfgGameplayFile.
$text = $text -replace 'template\s*=\s*"[^"]*"', "template = `"$Mission`""
$text += "`r`nenableCfgGameplayFile = 1;`r`n"
Set-Content -LiteralPath $cfg -Value $text -Encoding ASCII
$args = @("`"-config=$cfg`"", "`"-profiles=$ProfileRoot\Server`"", "-port=$Port", '-dologs', '-adminlog', '-freezecheck', "`"-mod=$mod`"")
$p = Start-Process -FilePath (Join-Path $serverRoot 'DayZServer_x64.exe') -ArgumentList $args -WorkingDirectory $serverRoot -WindowStyle Minimized -PassThru
Write-Output "Server PID $($p.Id) on port $Port"
