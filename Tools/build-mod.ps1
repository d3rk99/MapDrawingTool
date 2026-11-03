# Builds @MapDrawingTool: the script addon. The mod ships the vanilla map; each server's drawings
# reach the paper map in hand and the full-screen map live (README.md), so one build serves every server.
# -BakeLayers also builds a map texture addon per world painted from approved submissions
# (MapDrawingTool_<world>, same file path as the vanilla texture), for the wooden boards. Without
# it, texture addons left by earlier builds are removed.
# -Maps "<map id>=<world>", ...: the web app maps to bake (one per world); default: folders named like a world.
# Usage: powershell -NoProfile -File Tools\build-mod.ps1 [-Submissions <dir>] [-Destination <mod dir>] [-BakeLayers] [-Maps ...]
# Stops at a Derk-signed, verified folder. Publishing to the Workshop is a separate, manual step.
param([string]$Submissions = "", [string]$Destination = "E:\DayZMods\Mods\@MapDrawingTool", [switch]$BakeLayers,
  [string[]]$Maps = @())
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
if (!$Submissions) { $Submissions = Join-Path $repo "Submissions" }
$tools = "E:\Steam\steamapps\common\DayZ Tools\Bin"
$builder = Join-Path $tools "AddonBuilder\AddonBuilder.exe"
$toPaa = Join-Path $tools "ImageToPAA\ImageToPAA.exe"
foreach ($exe in @($builder, $toPaa)) { if (!(Test-Path -LiteralPath $exe)) { throw "Missing $exe" } }

$stage = Join-Path $repo "Build\stage"
if (Test-Path -LiteralPath $stage) {
  if ([IO.Path]::GetFullPath($stage) -ne [IO.Path]::GetFullPath((Join-Path $repo "Build\stage"))) { throw "Unexpected stage path" }
  Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null

& python -I (Join-Path $PSScriptRoot "compose_layers.py") $Submissions $stage @Maps
if ($LASTEXITCODE -ne 0) { throw "compose_layers.py failed" }

$addons = @(@{Name = 'MapDrawingTool'; Source = (Join-Path $repo 'Addons\MapDrawingTool'); Prefix = 'MapDrawingTool'})
$prefixFiles = @()
if ($BakeLayers) { $prefixFiles = Get-ChildItem -LiteralPath $stage -Filter '*.prefix' }
foreach ($prefixFile in $prefixFiles) {
  $dir = Join-Path $stage $prefixFile.BaseName
  foreach ($png in Get-ChildItem -LiteralPath $dir -Filter *.png) {
    $paa = [IO.Path]::ChangeExtension($png.FullName, ".paa")
    $ErrorActionPreference = "Continue"
    & $toPaa $png.FullName $paa 2>&1 | Out-Null
    $ErrorActionPreference = "Stop"
    if (!(Test-Path -LiteralPath $paa)) { throw "ImageToPAA failed for $($png.Name)" }
    Remove-Item -LiteralPath $png.FullName
  }
  $addons += @{Name = $prefixFile.BaseName; Source = $dir; Prefix = (Get-Content -LiteralPath $prefixFile.FullName -Raw).Trim()}
}

$out = Join-Path $Destination "Addons"
New-Item -ItemType Directory -Force -Path $out | Out-Null
# PBOs from the old tile-overlay design would load an empty CfgMapDrawingTool; drop them
$old = Join-Path $out 'MapDrawingTool_Layers.pbo'
foreach ($f in @($old, ($old + '.Derk.bisign'))) { if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f } }
# texture addons this build does not make (all of them without -BakeLayers)
$names = $addons | ForEach-Object { $_.Name }
foreach ($f in Get-ChildItem -LiteralPath $out -Filter 'MapDrawingTool_*.pbo') {
  if ($names -notcontains $f.BaseName) {
    Remove-Item -LiteralPath $f.FullName
    $sig = $f.FullName + '.Derk.bisign'
    if (Test-Path -LiteralPath $sig) { Remove-Item -LiteralPath $sig }
    Write-Host "Removed $($f.Name) (vanilla map; drawings go live)"
  }
}
foreach ($addon in $addons) {
  $pbo = Join-Path $out ($addon.Name + '.pbo')
  $started = Get-Date
  # AddonBuilder exits 0 on "[ERROR]: Build failed" and prints a banner on stderr; check the log and the PBO time.
  $ErrorActionPreference = "Continue"
  $log = & $builder $addon.Source $out ("-prefix=" + $addon.Prefix) "-packonly" "-clear" 2>&1 | Out-String
  $ErrorActionPreference = "Stop"
  if ($LASTEXITCODE -ne 0 -or $log -match "\[ERROR\]") { Write-Host $log; throw "AddonBuilder failed for $($addon.Name) (close DayZ/servers holding the PBO open)" }
  if (!(Test-Path $pbo) -or (Get-Item $pbo).LastWriteTime -lt $started.AddSeconds(-2)) { throw "No new PBO at $pbo" }
  Write-Host "Built $pbo ($((Get-Item $pbo).Length) bytes, prefix $($addon.Prefix))"
}

& (Join-Path $PSScriptRoot 'sign-derk.ps1') -ModPath $Destination
Copy-Item -LiteralPath (Join-Path $repo "Mod\mod.cpp") -Destination $Destination -Force
Copy-Item -LiteralPath (Join-Path $stage "manifest.json") -Destination $Destination -Force
Write-Host "Live markers per world (copy to the server's profiles\MapDrawingTool\markers.json): $(Join-Path $stage 'profile')"
