# Map Drawing Tool: server setup

Players draw on your server's map in a web page, you approve, and it shows in game (paper map in
hand and the M map) within a minute or two. Two parts: the **mod** on your DayZ server, and this
**web app**, which you host yourself.

## What you need

- A Windows PC or VPS that stays on while players use the map: your own PC, the PC running your
  DayZ server, or a Windows VPS. Rented game-server panels (Nitrado, GTX...) can't run it; use a
  separate machine and point the server at it.
- Nothing else: no Python, no DayZ Tools, no DayZ install. The package has the four official maps
  (Chernarus, Livonia, Sakhal, Nasdara) and makes the game's textures itself.

## 1. Install the mod

1. Add `@MapDrawingTool` (Workshop) to your server's `-mod=`.
2. Copy `keys\Derk.bikey` from the mod into your server's `keys` folder.
3. Start the server once. It creates `profiles\MapDrawingTool\config.json`.

## 2. Set up the web app

1. Unzip this package somewhere permanent, e.g. `C:\MapDrawingTool`.
2. Run **MapDrawingTool.exe** and leave the window open. The first start writes `Web\config.json`
   with your **admin token**. The web app is now at `http://127.0.0.1:8640`.
   Windows may say "Windows protected your PC" (the exe is not code-signed): click More info, then
   Run anyway.
3. Open `http://127.0.0.1:8640/admin`, paste the `adminToken` from `Web\config.json`, click Open.
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
{ "liveUrl": "https://map.yourdomain.com/", "lanUrl": "http://192.168.1.50:8640/", "mapId": "", "serverKey": "...", "pollSeconds": 60 }
```

Settings > Game servers shows these lines ready to copy, with your server key and this PC's LAN address.

- `liveUrl`: the public address from step 3.
- `lanUrl` (optional): the web app's address on your own network. The DayZ server uses it, and so
  does every player who joined the server over a private address (192.168.x, 10.x, 127.0.0.1...).
  Needed when you or the servers sit on the same network as the web app and your router can't reach
  its own public IP from inside (no NAT loopback). Players from the internet keep using `liveUrl`.
  Leave it empty when the web app is elsewhere.
- `serverKey`: copy it from the web app's Settings > Game servers. Without it, players can't send
  drawings made in game (everything else still works).
- `mapId`: which map's drawings this server shows. Empty = the server's own world
  (`chernarusplus`, `enoch`, `sakhal`, `nasdara`). Two servers with the same `mapId` share drawings.

**Several servers on the same terrain with different drawings:** add maps to `Web\config.json`,
restart MapDrawingTool.exe, and give each server its map's id:

```json
"maps": [
  { "id": "pvp", "name": "PvP server", "world": "chernarusplus" },
  { "id": "pve", "name": "PvE server", "world": "chernarusplus" }
]
```

## 5. Give players the link

Players open your public address (add `?map=pvp` to open one map directly), draw, and submit. You
approve on `/admin`. Approved work appears in game within a minute or two, no restart.

## 6. Let pens spawn

Drawing in game needs a pen, and vanilla DayZ never spawns them (`nominal` 0 in types.xml). The
`types` folder in this package has `mdt_types.xml`: Pen_Black, Pen_Red, Pen_Green and Pen_Blue on
shelves in towns, villages and offices, about 6 of each. `types\README.txt` shows how to add it to
your mission's economy (its own file through cfgeconomycore.xml, or pasted into db	ypes.xml).

## Optional

- **Paper map in hand, M for the full-screen map:** in the mission's `cfggameplay.json` set
  `"use3DMap": true`, and in `serverDZ.cfg` set `enableCfgGameplayFile = 1;`.
- **Sign-in and who may submit:** Settings tab. Steam sign-in needs nothing (add a free Steam Web API
  key for names). Discord: create an application at discord.com/developers, add the redirect shown
  in Settings, paste its ID and secret and your Discord server ID. Then choose anyone / signed in /
  only listed Steam accounts or Discord roles, per map if you like.
- **Notifications:** Settings > Notifications, paste a Discord webhook, tick "New submission".
- **Personal drawings:** players can keep drawings to themselves (only they see them, on their own
  map). Needs Steam sign-in on the web, or drawing in game. Settings decides whether personal work
  needs your approval (off by default) or is allowed at all, per map.
- **Lifespan:** players choose how long a drawing stays (1 day up to permanent); you set the longest
  and the default in Settings.
- **Drawing in game:** players with a map and a pen press M, then D, and draw with the mouse;
  Enter sends it to you for approval (or straight to their own map if personal). Needs `serverKey`.

## Updating

Close MapDrawingTool.exe, unzip the new package over the old folder (your `Web\config.json`,
`Submissions` and `Build` folders stay), and start it again.

Coming from 0.1.0 or 0.2.0 (the Python version): unzip into the same folder the same way. The
old `setup.bat`, `start.bat` and `.py` files are no longer used and can be deleted.

## Troubleshooting

- **Players see nothing in game:** the server's `liveUrl` must be the public address, reachable
  from outside your network. Check the server's script log for `[MapDrawingTool]` lines.
- **The window closes or shows an error at start:** the message says what is wrong, most often a typo
  in `Web\config.json` or port 8640 already in use (another copy running; change `"port"`).
- **Antivirus removes the exe:** some flag unsigned programs; allow the MapDrawingTool folder.
