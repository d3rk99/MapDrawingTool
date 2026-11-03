# Live map feasibility test (never ships)

## Results (2026-10-27, DayZ 1.30, local server + client)

| Question | Answer |
|---|---|
| HTTP from script (`RestApi` GET) | Works on client and server |
| `RestContext.FILE` download to disk | Broken: reports success, writes 0 bytes, for every path tried |
| Binary file from script | Works: GET the file as hex text, decode, `FileSerializer.Write(int)` writes exactly 4 raw little-endian bytes, no header. Output byte-identical (last word zero-padded) |
| Decode speed (hex, `IndexOf` per char) | ~0.75–0.9 s per 31 KB. Too slow for a 2 MB texture; needs a faster decoder, chunking over frames, or small images |
| Decode speed, fixed (2026-11-03) | Hex with `Substring` on a part held in a field still dropped the game to a few FPS for 38 s (2.4 MB): script strings are values, so each word copied the whole part. Parts as JSON `{"d":[int,...]}` parsed by `JsonSerializer.ReadFromString`, written within 2 ms per frame (`TickCount` is ~10,000 ticks/ms here): 2.4 MB in 7.7 s, FPS fine, file byte-identical |
| `ImageWidget.LoadImageFile("$profile:...paa")` | Works on the client (512×512) |
| Full-screen map (M / `MapMenu`) uses `karta_co.paa`? | No. It is drawn by the engine's `MapWidget` |
| Paper map in hand / boards use `karta_co.paa`? | In hand: yes (`mapTextureOpened`). Boards: same file, not checked in this run |
| Overlay on the full-screen map | Works when the widgets are children of the `Map` widget (like vanilla's GPS marker), positioned with `MapToScreen` minus the map's screen pos. As siblings of the map window they are hidden under it |
| Overlay after DayZ 1.30.164289 (2026-11-03 update) | Children of the `Map` widget are no longer drawn at all (a vanilla texture there stayed invisible). Fixed by creating the overlay on the map screen's `layoutRoot` with `SetSort(1000)`, positioned with `MapToScreen` minus the overlay's own screen pos |
| Live lines (`CanvasWidget.DrawLine`) | Work, pinned to world positions |
| Keep paper unfolded, M toggles the screen | `cfggameplay.json` `UIData.use3DMap: true` (needs `enableCfgGameplayFile = 1`) plus a raw `KeyState(KC_M)` toggle. `PlayerBase.IsMapCallbackEndInput/CancelInput` must return false while the screen is open, or a click folds the map |

Stroke transparency (real mod, same day): `CanvasWidget.DrawLine` only honours the alpha in
the line colour, so a see-through stroke darkens wherever two pieces overlap. `SetAlpha` on the
canvas, and on a parent frame with `inheritalpha 1`, both had no effect (lines stayed solid). An
`RTTextureWidget` (from a layout) with the canvas inside, shown via `ImageWidget.SetImageTexture`,
rendered blank white over the whole map. Image layers (`ImageWidget` + PAA) do blend correctly.
Solid strokes look clean with plain segments plus an octagon "dot" (four 45° bars) on every joint;
segments stretched half a width past their ends left square bumps at corners.

Paper map texture from `$profile` (2026-11-03, `build-test.ps1 -Texture`, MapDrawingToolTexTest):
`ItemMap.SetObjectTexture(1, "$profile:MapDrawingTool/textest_karta.paa")` shows that PAA on the
unfolded paper in hand, and switching it while held works. So one shared mod can show a
per-server map texture downloaded at runtime. An absolute path (`E:/...`) fails: `FileExist` is
false and the paper renders blank. `ItemMap` is an engine class and cannot be `modded`; set the
texture from `PlayerBase.OnItemInHandsChanged` (also fires on unfold). The texture set when the map
first came into hands did not show on the first unfold; later sets did.

Note: `map` is a reserved type name in Enforce; a variable called `map` fails to compile.

Answers, in game:

1. Does the HTTP client (`RestApi`) work on a client and on the server?
2. Can an image downloaded into `$profile` be shown by an `ImageWidget`?
3. Does the map screen use `karta_co.paa`? The test karta has a big red X and "MDT TEST karta_co".

It also draws the downloaded overlay (green "DOWNLOADED" box) pinned over Chernogorsk and a
magenta canvas triangle Chernogorsk–Elektro–Stary Sobor on the map screen.

Run: web app on 8640 (serves `Web/static/test/overlay.paa`), then

```
Test\build-test.ps1
Test\start-test.ps1 -ProfileRoot <folder>
Test\start-test.ps1 -ProfileRoot <folder> -Client
```

Results: `[MDT-TEST]` lines in `<folder>\Server\script_*.log` and `<folder>\Client\script_*.log`,
and as pink text on the map screen. The character spawns with a paper map.
