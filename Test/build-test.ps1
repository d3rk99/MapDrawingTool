# Builds the in-game feasibility test mod (never ships). See Test\README.md.
# -Texture builds the paper map texture test (MapDrawingToolTexTest) instead.
param([string]$Destination = "E:\DayZMods\Mods\@MapDrawingToolTest", [switch]$Texture)
$ErrorActionPreference = "Stop"
$builder = "E:\Steam\steamapps\common\DayZ Tools\Bin\AddonBuilder\AddonBuilder.exe"
$out = Join-Path $Destination "Addons"
New-Item -ItemType Directory -Force -Path $out | Out-Null
$addons = @(@{Name = 'MapDrawingToolTest'; Prefix = 'MapDrawingToolTest'},
            @{Name = 'MapDrawingToolTest_karta'; Prefix = 'DZ\structures\signs\Tourist\data'})
if ($Texture) { $addons = @(@{Name = 'MapDrawingToolTexTest'; Prefix = 'MapDrawingToolTexTest'}) }
foreach ($addon in $addons) {
  $pbo = Join-Path $out ($addon.Name + '.pbo')
  $started = Get-Date
  $ErrorActionPreference = "Continue"
  $log = & $builder (Join-Path $PSScriptRoot $addon.Name) $out ("-prefix=" + $addon.Prefix) "-packonly" "-clear" 2>&1 | Out-String
  $ErrorActionPreference = "Stop"
  if ($LASTEXITCODE -ne 0 -or $log -match "\[ERROR\]") { Write-Host $log; throw "AddonBuilder failed for $($addon.Name)" }
  if (!(Test-Path $pbo) -or (Get-Item $pbo).LastWriteTime -lt $started.AddSeconds(-2)) { throw "No new PBO at $pbo" }
  Write-Host "Built $pbo"
}
& (Join-Path $PSScriptRoot '..\Tools\sign-derk.ps1') -ModPath $Destination
