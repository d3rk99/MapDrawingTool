# Map Drawing Tool: server setup

Players draw on your server's map in a web page, you approve, and it shows in game (paper map in
hand and the M map) within a minute or two. Two parts: the **mod** on your DayZ server, and this
**web app**, which you host yourself.

## What you need

- A Windows PC or VPS that stays on while players use the map: your own PC, the PC running your
  DayZ server, or a Windows VPS. Rented game-server panels (Nitrado, GTX...) can't run it; use a
  separate machine and point the server at it.
- **Python 3.10+** from python.org (tick "Add python.exe to PATH" in the installer).
- **DayZ Tools** (free, Steam > Library > Tools).
- **DayZ** or **DayZ Server** installed on the same PC (Steam). The web app reads the map textures
  from it. For Livonia, Sakhal and Nasdara the DLC files must be installed.

## 1. Install the mod

1. Add `@MapDrawingTool` (Workshop) to your server's `-mod=`.
2. Copy `keys\Derk.bikey` from the mod into your server's `keys` folder.
3. Start the server once. It creates `profiles\MapDrawingTool\config.json`.

## 2. Set up the web app

1. Unzip this package somewhere permanent, e.g. `C:\MapDrawingTool`.
2. Run **setup.bat**. It installs the Python packages, finds DayZ and DayZ Tools, makes a map image
   for each installed map, and writes `Web\config.json` with your **admin token**.
3. Run **start.bat** and leave the window open. The web app is now at `http://127.0.0.1:8640`.
4. Open `http://127.0.0.1:8640/admin`, paste the `adminToken` from `Web\config.json`, click Open.
   Everything else is on the **Settings** tab.

## 3. Let players reach it

Players' games download the map images from the web app, so it has to be reachable from the
internet, not only from your PC.

- **Simple:** in `Web\config.json` set `"host": "0.0.0.0"`, forward TCP port 8640 on your router
  to this PC, and allow it in Windows Firewall. Your address is `http://<your public IP>:8640`.
- **Better (HTTPS, needed for safe sign-in):** run a reverse proxy such as Caddy with your domain:
  `map.yourdomain.com { reverse_proxy 127.0.0.1:8640 }`. Caddy gets the certificate itself.

Then on **Settings > Web address** enter that public address and save.

## 4. Point your DayZ server at it

Edit `profiles\MapDrawingTool\config.json` on the DayZ server and restart it:

```json
{ "liveUrl": "https://map.yourdomain.com/", "mapId": "chernarusplus", "pollSeconds": 60 }
```

- `liveUrl`: the public address from step 3.
- `mapId`: which map's drawings this server shows. Empty = the server's own world
  (`chernarusplus`, `enoch`, `sakhal`, `nasdara`). Two servers with the same `mapId` share drawings.

**Several servers on the same terrain with different drawings:** add maps to `Web\config.json`,
restart start.bat, and give each server its map's id:

```json
"maps": [
  { "id": "pvp", "name": "PvP server", "world": "chernarusplus" },
  { "id": "pve", "name": "PvE server", "world": "chernarusplus" }
]
```

## 5. Give players the link

Players open your public address (add `?map=pvp` to open one map directly), draw, and submit. You
approve on `/admin`. Approved work appears in game within a minute or two, no restart.

## Optional

- **Paper map in hand, M for the full-screen map:** in the mission's `cfggameplay.json` set
  `"use3DMap": true`, and in `serverDZ.cfg` set `enableCfgGameplayFile = 1;`.
- **Sign-in and who may submit:** Settings tab. Steam sign-in needs nothing (add a free Steam Web API
  key for names). Discord: create an application at discord.com/developers, add the redirect shown
  in Settings, paste its ID and secret and your Discord server ID. Then choose anyone / signed in /
  only listed Steam accounts or Discord roles, per map if you like.
- **Notifications:** Settings > Notifications, paste a Discord webhook, tick "New submission".

## Updating

Download the new package, unzip it over the old folder (your `Web\config.json` and `Submissions`
folder stay), and run setup.bat again.

## Troubleshooting

- **Players see nothing in game:** the server's `liveUrl` must be the public address, reachable
  from outside your network. Check the server's script log for `[MapDrawingTool]` lines.
- **setup.bat: ImageToPAA not found:** install DayZ Tools, or set `"imageToPaa"` in `Web\config.json`
  to the full path of `ImageToPAA.exe`.
- **A map is missing in the editor:** its files are not installed (DLC); install them and run setup.bat.
- **DayZ installed outside Steam's usual places:** set `"dayzPath"` in `Web\config.json`.
