# Map Drawing Tool (working name)

Players draw on a server's map in a web app; the server owner approves; approved work
shows up in game on the paper map in hand and the full-screen map, with no mod update.

## Maps and servers

One web app serves any number of **maps**. A map is one set of drawings on one **world**
(Chernarus `chernarusplus`, Livonia `enoch`, Sakhal `sakhal`, Nasdara `nasdara`). Each game
server shows one map, picked in its `profiles\MapDrawingTool\config.json`:

```json
{ "liveUrl": "http://map.example.com:8640/", "mapId": "pvp", "pollSeconds": 60 }
```

`mapId` empty means the map named like the server's world. So servers on the same world can
share one map (same `mapId`) or each have their own. Maps are listed in `Web\config.json`:

```json
"maps": [
  { "id": "pvp", "name": "PvP server", "world": "chernarusplus" },
  { "id": "pve", "name": "PvE server", "world": "chernarusplus" },
  { "id": "enoch", "name": "Livonia", "world": "enoch" }
]
```

Without a `maps` list there is one map per world, its id the world name. A server whose map is
for another world logs it and shows nothing (the drawings would land in the wrong places).
Players pick the map in the editor; `http://.../?map=<id>` opens one directly.

## How content reaches the game

| Where | What shows | How it gets there | Workshop update |
|---|---|---|---|
| Full-screen map (M) | markers, all approved drawings | live: the game server polls the web app's `/api/<map id>/live` and relays it to players. Brush-only drawings are lines; others are small PAAs each client downloads once into `$profile:MapDrawingTool/cache` | No |
| Paper map in hand | the whole map texture with every approved layer | live: the web app paints `Build\paper\<map id>\paper_<hash>.paa` when a layer is approved or revoked (2.4 MB for Chernarus, about 10 MB for the 4096² maps); clients download it in the background and the mod puts it on the map in the player's hands (`ItemMap.SetObjectTexture`) | No |
| Wooden map boards | vanilla map; approved drawings only with `build-mod.ps1 -BakeLayers` (the admin page's Build button, for the map open there) baked into the world's texture | Yes |

So one shared mod works for every server: each server's players get that server's drawings from
its own `liveUrl` and `mapId`, and nothing has to be rebuilt or published when players draw. The
mod ships the vanilla map (decided 2026-11-03), which the paper shows until the server's texture
has downloaded.

The full-screen map is drawn by the engine, not from the paper texture (tested, see
`Test\README.md`). Players download images straight from `liveUrl`, so it must be reachable by
them, not `127.0.0.1`. Without a `liveUrl` the server reads `markers.json` (markers only).

With `cfggameplay.json` `UIData.use3DMap: true` (and `enableCfgGameplayFile = 1`), unfolding a
map shows only the paper in hand; M (the physical key, not the keybind) toggles the full-screen
map over it, and clicks there don't fold the paper.

With `-BakeLayers`, painted layers use the MMmap method: `MapDrawingTool_<world>.pbo` has the
world texture's prefix (Chernarus `DZ\structures\signs\Tourist\data`) and contains only that
texture, so it replaces the vanilla one; the opened paper map and the tourist boards show it.
Baking is tested on Chernarus only. Sakhal's and Nasdara's textures sit in the same folder as
the rest of their world data (`DZ\worlds\<world>\data`), so overriding them that way is untested.

## Map texture calibration (worlds.json)

texture pixel = `originPx` + (X, `size` − Z) × `pxPerMetre` (one number, or `[x, y]`).

| World | Texture (in the game files) | Size | originPx | pxPerMetre |
|---|---|---|---|---|
| chernarusplus | `Addons\structures_signs.pbo` `tourist\data\karta_co.paa`, 2048² | 15360 | 22, 22 | 0.125 |
| enoch | `Addons\structures_signs_bliss.pbo` `tourist\data\karta_enoch_co.paa`, 4096² | 12800 | 56, 53.5 | 0.3106, 0.2811 |
| sakhal | `sakhal\Addons\worlds_sakhal_data.pbo` `map_unfolded_sakhal_co.paa`, 4096² | 15360 | 170, 0 | 0.2423 |
| nasdara | `nasdara\addons\worlds_nasdara_data.pbo` `map_unfolded_nasdara_co.paa`, 4096² | 16384 | 44, 40.5 | 0.24583, 0.24339 |

Chernarus was checked against the satellite layer. The others (2026-11-03) by drawing each
mission's `mapgrouppos.xml` buildings on the texture and fitting offset and scale until they
land on the map's buildings; the same fit gives Chernarus's values to within a pixel. Livonia's
map is not square on its texture. Nasdara's buildings sit along roads rather than as drawn
blocks, so its fit is the least certain (a few pixels, roughly 20 m). Painting outside the map
frame (the paper margin) is allowed; markers must be inside the world. All four checked in game
(2026-11-03): rings drawn through this calibration sit centred on game-placed markers on the
full-screen map and on the printed buildings on the paper map.

## Layout

- `Addons\MapDrawingTool\` — scripts (static addon, rarely changes)
- `Tools\worlds.json` — worlds: texture, calibration, size.
- `Tools\make_basemap.py <world> [source.paa]` — turns the world's paper texture (read straight
  out of the game's PBO) into `Web\static\maps\<world>.png` (what gets painted on) and `.jpg`
  (what the editor shows). Pass a PAA to start from a customised map, e.g. MMmap's.
- `Tools\pbo.py` — minimal PBO reader used by `make_basemap.py`.
- `Tools\compose_layers.py` — paints approved layers onto the base PNG in "created" order,
  writes the texture addon's `config.cpp` and per-map `markers.json`.
- `Tools\build-mod.ps1` — AddonBuilder → `sign-derk.ps1` → `E:\DayZMods\Mods\@MapDrawingTool`.
  With `-BakeLayers [-Maps id=world]` also compose → ImageToPAA → texture addon. Stops at a
  signed, verified folder.
- `Submissions\<map id>\<id>\submission.json` — the contract between web app and builder
  (format in the docstring of `compose_layers.py`). Layers are placed by texture pixels (`px`).

## Web app (`Web\`)

```
python -m pip install -r Web\requirements.txt
python -I Web\app.py
```

Server owners get the web app as a zip (`python -I Tools\package_webapp.py` builds
`Build\release\MapDrawingTool-WebApp-<version>.zip`) with `setup.bat`, `start.bat` and
`Package\SERVER_SETUP.md`, their setup guide. The Workshop description is `Workshop\description.txt`.
DayZ and DayZ Tools are found through Steam (`Tools\paths.py`); `dayzPath` / `imageToPaa` in
`Web\config.json` override.

- `http://127.0.0.1:8640/` — player editor on the real map texture: brush, eraser, stamps
  (9 built in, or upload a PNG), painted text, undo/redo, live markers with the 20 in-game icons.
  Sizes are in metres. A drawing is cropped to what was painted and sent as one layer submission.
- `/admin` — approval queue per map with previews, approve / reject / revoke, and a Build button
  that runs `Tools\build-mod.ps1 -BakeLayers` for the open map (only needed for the wooden boards).
- First run writes `Web\config.json` with a random admin token (gitignored). A map's
  `serverProfileDir` (or the top-level one, without a `maps` list) gets its approved markers at
  once, for servers without a `liveUrl`.

- Rejected submissions can be deleted for good from `/admin` (approved ones must be revoked first).

### Who may submit (`Web\access.py`)

Players can sign in with Steam and/or Discord. The owner sets all of this, and the webhooks below,
on `/admin` > **Settings** (`Web\settings.py`), which writes `Web\config.json` and applies it at once.
Secrets (Steam API key, Discord client secret and bot token, webhook addresses) are never sent back
to the browser; a field left empty keeps the saved value. An optional Discord bot token lets the page
list the server's roles by name (the bot needs no permissions); sign-in and role checks work without
it. Per-map rules from the page go to `"mapSubmitters"`. In `Web\config.json`:

```json
"publicUrl": "https://map.example.com",
"login": {
  "steam": true,
  "steamApiKey": "",
  "discord": { "clientId": "", "clientSecret": "", "guildId": "" }
},
"submitters": { "who": "anyone", "steamIds": [], "discordRoles": [], "perHour": 10, "maxPending": 20 }
```

- `who`: `anyone` (a typed name, limited per address), `signed-in` (Steam or Discord), or
  `listed` (only `steamIds`, or Discord members with any of `discordRoles` in `guildId`).
- A map in `maps` can carry its own `"submitters": {...}`; its keys override the default.
- `perHour` / `maxPending`: per player (per address for anonymous) across all maps.
- Steam sign-in needs nothing else. For player names set `steamApiKey` (free, steamcommunity.com/dev/apikey);
  without it names come from public profiles, which Steam often throttles, so they can show as SteamIDs.
- Discord: create an application at discord.com/developers, add the redirect
  `<publicUrl>/login/discord/return` under OAuth2, copy its client ID and secret. With `guildId`
  the sign-in also reads the player's roles in that server (scope `guilds.members.read`), so no bot
  is needed. Role IDs: Discord Developer Mode, right-click the role, Copy Role ID. Roles are read
  at sign-in.
- The admin page shows who sent each submission (Steam profile link, Discord user ID, or address).

### Notifications (`Web\notify.py`)

```json
"webhooks": [
  { "url": "https://discord.com/api/webhooks/...", "format": "discord", "events": ["submitted"] },
  { "url": "https://example.com/hook", "format": "json", "secret": "...", "events": ["submitted", "approved", "rejected", "deleted"], "maps": ["pvp"] }
]
```

`discord` posts a message linking to `/admin`; `json` posts `{event, map, mapName, submission,
adminUrl}` signed with `X-MDT-Signature: sha256=<HMAC>` when `secret` is set. `events` defaults to
`submitted`, `maps` to all.

Before putting it on the internet: it listens on 127.0.0.1 only (`host` in `config.json`). Put it
behind HTTPS (a reverse proxy such as Caddy) and set `publicUrl`: sign-in returns there and the
session cookie is only marked secure on https.

## Not done yet

- Memento Mori: the base is the vanilla `karta_co` (decided 2026-10-27). MMmap also ships
  `karta_co.paa` under the same prefix; with `-BakeLayers` on a server running both, one wins
  unpredictably. Set `afterAddons: ["MM_Map"]` or take `karta_co.paa` out of MMmap.
- Steam and Discord sign-in tested with simulated sessions only; a real sign-in round trip is untested.
- Workshop publishing is manual (workspace rule). The shared release ships the vanilla map.
- Old `paper_*.paa` files stay in players' `$profile:MapDrawingTool/cache`.
