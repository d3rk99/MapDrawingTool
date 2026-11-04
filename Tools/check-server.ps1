# Map Drawing Tool: checks the DayZ servers running on this machine. Changes nothing.
# Run it on the server machine while the servers are running:
#   powershell -ExecutionPolicy Bypass -File check-server.ps1
# For each DayZServer process: its -mod list, its profile folder, whether the newest script log
# shows the mod loading, and where profiles\MapDrawingTool\config.json is.

$procs = Get-CimInstance Win32_Process -Filter "Name like 'DayZServer%'"
if (!$procs) { Write-Host "No DayZ server is running on this machine. Start them, then run this again."; return }

foreach ($p in $procs) {
  $cmd = $p.CommandLine
  $root = Split-Path $p.ExecutablePath
  Write-Host ""
  Write-Host "=== Server PID $($p.ProcessId): $root" -ForegroundColor Cyan

  $mods = if ($cmd -match '-(?:server)?mod=("[^"]*"|\S+)') { ([regex]::Matches($cmd, '-(?:server)?mod=("[^"]*"|\S+)') | ForEach-Object { $_.Groups[1].Value.Trim('"') }) -join ';' } else { "" }
  $hasMod = $mods -match 'MapDrawingTool|3833411645'
  Write-Host ("Mods:        " + ($(if ($mods) { $mods } else { "(none)" })))
  if ($hasMod) { Write-Host "             MapDrawingTool is in the mod list" -ForegroundColor Green }
  else { Write-Host "             MapDrawingTool is NOT in -mod= / -serverMod= (add @MapDrawingTool)" -ForegroundColor Yellow }

  $prof = if ($cmd -match '-profiles=("[^"]*"|\S+)') { $Matches[1].Trim('"') } else { "" }
  if ($prof -and -not [System.IO.Path]::IsPathRooted($prof)) { $prof = Join-Path $root $prof }
  if (!$prof) { $prof = Join-Path $env:LOCALAPPDATA "DayZ"; Write-Host "Profile:     $prof (no -profiles= on the start line, so the Windows user's folder)" }
  else { Write-Host "Profile:     $prof" }

  $log = Get-ChildItem -Path $prof -Filter "script_*.log" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (!$log) { Write-Host "Script log:  none found in the profile folder" -ForegroundColor Yellow }
  else {
    Write-Host "Script log:  $($log.FullName) (last written $($log.LastWriteTime))"
    $scripts = Select-String -Path $log.FullName -Pattern 'Module: (Game|World|Mission);.*MapDrawingTool' | Select-Object -First 1
    $mission = Select-String -Path $log.FullName -Pattern 'Module: Mission;' | Select-Object -First 1
    $lines = Select-String -Path $log.FullName -Pattern '\[MapDrawingTool\]' | Select-Object -First 3
    $errors = Select-String -Path $log.FullName -Pattern '\(E\).*(mapdrawingtool|mdt_)' | Select-Object -First 3
    if ($scripts) { Write-Host "             the mod's scripts are compiled in" -ForegroundColor Green }
    else { Write-Host "             the mod's scripts are NOT in this log's script modules: the server did not load @3833411645" -ForegroundColor Yellow }
    if (!$mission) { Write-Host "             the mission has not loaded yet: wait a minute after start and run this again" -ForegroundColor Yellow }
    if ($lines) { Write-Host "             mod started:" -ForegroundColor Green; $lines | ForEach-Object { Write-Host "             $($_.Line.Trim())" } }
    elseif ($scripts -and $mission) { Write-Host "             no [MapDrawingTool] lines although the mission loaded: another mod's MissionServer.OnInit may not call super.OnInit()" -ForegroundColor Yellow }
    $errors | ForEach-Object { Write-Host "             error: $($_.Line.Trim())" -ForegroundColor Red }
  }

  $cfg = Join-Path $prof "MapDrawingTool\config.json"
  if (Test-Path $cfg) {
    Write-Host "Config:      $cfg" -ForegroundColor Green
    $c = Get-Content $cfg -Raw | ConvertFrom-Json
    Write-Host ("             liveUrl '" + $c.liveUrl + "', mapId '" + $c.mapId + "', serverKey " + $(if ($c.serverKey) { "set" } else { "empty" }))
  } else { Write-Host "Config:      not at $cfg" -ForegroundColor Yellow }

  $key = Get-ChildItem -Path (Join-Path $root "keys") -Filter "Derk.bikey" -ErrorAction SilentlyContinue
  if ($key) { Write-Host "Key:         keys\Derk.bikey present" -ForegroundColor Green }
  else { Write-Host "Key:         keys\Derk.bikey missing in $root\keys (players get kicked)" -ForegroundColor Yellow }
}
Write-Host ""
Read-Host "Press Enter to close"
