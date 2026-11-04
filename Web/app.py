"""Map Drawing Tool web app: players draw and submit, the server owner approves and builds.

Run:  python -I Web/app.py            (http://127.0.0.1:8640, editor at /, approval queue at /admin)
First run writes Web/config.json with a random admin token. Keep that file private.

One web app serves any number of maps. A map is one community's set of drawings on one world
(terrain); a game server picks its map with "mapId" in profiles\MapDrawingTool\config.json, so
several servers on the same world can share a map or each have their own. Maps are listed in
config.json "maps": [{"id": "pvp", "name": "PvP server", "world": "chernarusplus"}, ...]; with
no list there is one map per world, its id the world name.

Submissions are written to Submissions/<map id>/<id>/ in the format Tools/compose_layers.py reads.
Approved work goes live in game within a minute or two (see live.py). Approving or revoking a
marker also rewrites markers.json (and copies it to the map's or config's "serverProfileDir"),
the fallback for servers without a liveUrl.
"""
import base64
import datetime
import hashlib
import io
import json
import os
import re
import secrets
import shutil
import subprocess
import sys
import threading

from flask import Flask, abort, jsonify, request, send_file, send_from_directory
from PIL import Image

# MDT_ROOT: the packaged MapDrawingTool.exe sets it to its own folder (same layout as the repo)
HERE = (os.path.join(os.environ["MDT_ROOT"], "Web") if os.environ.get("MDT_ROOT")
        else os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(HERE)
TOOLS = os.path.join(REPO, "Tools")
sys.path.insert(0, TOOLS)
sys.path.insert(0, HERE)     # python -I leaves the script's own folder off the path
import access  # noqa: E402
import compose_layers  # noqa: E402  (shares the submission rules with the builder)
import live  # noqa: E402
import notify  # noqa: E402
import settings  # noqa: E402

CONFIG_PATH = os.path.join(HERE, "config.json")
BUILD_OUT = os.path.join(REPO, "Build", "@MapDrawingTool")   # signed, ready to copy to the server / Workshop
PAPER_DIR = os.path.join(REPO, "Build", "paper")              # <map id>/paper_<hash>.paa, see live.build_paper
MAX_LAYER_BYTES = 8 * 1024 * 1024
MAX_TEXT = 64
ICON_COUNT = 20          # eMapMarkerTypes in vanilla scripts\5_mission\gui\mapmarkersinfo.c
STATUSES = ("pending", "approved", "rejected")    # set by the owner; "withdrawn" is set by the author


def load_config():
    if not os.path.exists(CONFIG_PATH):
        cfg = {"adminToken": secrets.token_urlsafe(24), "submissionsDir": os.path.join(REPO, "Submissions"),
               "serverProfileDir": "", "host": "127.0.0.1", "port": 8640}
        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=1)
        print(f"Wrote {CONFIG_PATH} with a new admin token (open the file to read it).")
    with open(CONFIG_PATH, encoding="utf-8-sig") as f:
        cfg = json.load(f)
    changed = False
    if not cfg.get("sessionSecret"):               # signs player sign-in cookies (access.py)
        cfg["sessionSecret"] = secrets.token_urlsafe(32)
        changed = True
    if not cfg.get("serverKey"):                   # game servers send in-game drawings with it (api_ingame)
        cfg["serverKey"] = secrets.token_urlsafe(18)
        changed = True
    for hook in cfg.get("webhooks") or []:         # the admin page's Settings edits webhooks by id
        if not hook.get("id"):
            hook["id"] = secrets.token_hex(4)
            changed = True
    if changed:
        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=1)
    return cfg


CFG = load_config()
SUBS = CFG["submissionsDir"]
WORLDS = compose_layers.load_worlds()


def load_maps():
    """map id -> {"id", "name", "world", "serverProfileDir"}, from config "maps", else one per
    world that has a base texture (id = world name, as before maps existed)."""
    maps = {}
    for m in CFG.get("maps") or []:
        mid, world = str(m.get("id", "")).lower(), str(m.get("world", "")).lower()
        if not re.fullmatch(r"[a-z0-9_-]{1,32}", mid) or world not in WORLDS:
            raise SystemExit(f"config.json maps: bad entry {m} (id: a-z 0-9 _ -, world: one of {sorted(WORLDS)})")
        maps[mid] = {"id": mid, "name": " ".join(str(m.get("name") or mid).split())[:48], "world": world,
                     "serverProfileDir": m.get("serverProfileDir", ""), "submitters": m.get("submitters") or {}}
    if not maps:
        for world, info in sorted(WORLDS.items()):
            if os.path.isfile(compose_layers.base_path(world)):
                maps[world] = {"id": world, "name": info.get("name", world), "world": world,
                               "serverProfileDir": CFG.get("serverProfileDir", "")}
    return maps


MAPS = load_maps()
app = Flask(__name__, static_folder=os.path.join(HERE, "static"), static_url_path="/static")
app.config["MAX_CONTENT_LENGTH"] = MAX_LAYER_BYTES * 2
write_lock = threading.Lock()
access.init(app, CFG, MAPS, lambda m: all_subs(m))
build_state = {"running": False, "log": "", "finished": None, "ok": None}


def now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def map_or_404(map_id):
    """(map id, world) for a URL's map id."""
    m = MAPS.get(map_id.lower())
    if not m:
        abort(404)
    return m["id"], m["world"]


def sub_dir(map_id, sub_id):
    if not sub_id.replace("-", "").isalnum():
        abort(404)
    path = os.path.join(SUBS, map_id, sub_id)
    if not os.path.isfile(os.path.join(path, "submission.json")):
        abort(404)
    return path


def read_sub(path):
    with open(os.path.join(path, "submission.json"), encoding="utf-8") as f:
        return json.load(f)


_changes = {}            # map folder -> writes since start; with the folder's time it keys the index
_index = {}              # map folder -> (key, submissions)


def _changed(path):
    folder = os.path.dirname(os.path.normpath(path))
    _changes[folder] = _changes.get(folder, 0) + 1


def write_sub(path, sub):
    tmp = os.path.join(path, "submission.json.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(sub, f, indent=1)
    os.replace(tmp, os.path.join(path, "submission.json"))
    _changed(path)


def all_subs(map_id):
    """Every submission on a map, oldest first. Read from disk only when something changed: game
    servers ask every 30-60 s, and a busy server can have thousands of submissions."""
    root = os.path.normpath(os.path.join(SUBS, map_id))
    if not os.path.isdir(root):
        return []
    key = (_changes.get(root, 0), os.stat(root).st_mtime_ns)
    cached = _index.get(root)
    if cached and cached[0] == key:
        return cached[1]
    out = []
    for name in sorted(os.listdir(root)):
        p = os.path.join(root, name)
        if os.path.isfile(os.path.join(p, "submission.json")):
            out.append(read_sub(p))
    out.sort(key=lambda s: (s.get("created", ""), s.get("id", "")))
    _index[root] = (key, out)
    return out


def public(sub):
    keep = ("id", "kind", "status", "created", "author", "group", "title", "px", "x", "z", "text", "color", "icon",
            "reviewed", "note", "scope", "expires", "source")
    return {k: sub[k] for k in keep if k in sub}


def is_admin():
    return secrets.compare_digest(request.headers.get("X-Admin-Token", ""), CFG["adminToken"])


def require_admin():
    if not is_admin():
        abort(403)


def is_live(sub):
    """Approved and not past its lifespan."""
    return sub.get("status") == "approved" and (not sub.get("expires") or sub["expires"] > now())


def live_subs(map_id):
    return [s for s in all_subs(map_id) if is_live(s)]


def is_personal(sub):
    return sub.get("scope") == "personal"


def clean_text(value, limit=MAX_TEXT):
    return " ".join(str(value or "").split())[:limit]


def export_markers(map_id):
    """Writes the approved markers for a map, the same file the build makes, and copies it to the
    map's serverProfileDir (the fallback for a game server without a liveUrl)."""
    root = os.path.join(SUBS, map_id)
    subs = [s for s in all_subs(map_id) if is_live(s) and not is_personal(s)]
    markers = [compose_layers.marker_entry(s) for s in subs if s.get("kind") == "marker"]
    text = json.dumps({"markers": markers}, indent=1)
    targets = [os.path.join(root, "markers.json")]
    if MAPS[map_id].get("serverProfileDir"):
        targets.append(os.path.join(MAPS[map_id]["serverProfileDir"], "markers.json"))
    for target in targets:
        os.makedirs(os.path.dirname(target), exist_ok=True)
        tmp = target + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(text)
        os.replace(tmp, target)
    return len(markers)


# ---------------------------------------------------------------- pages

@app.get("/")
def editor_page():
    return send_from_directory(app.static_folder, "editor.html")


@app.get("/admin")
def admin_page():
    return send_from_directory(app.static_folder, "admin.html")


# ---------------------------------------------------------------- player API

_tex_size = {}


def texture_size(world):
    """(width, height) of the world's base texture; the PNG is what the build paints on."""
    if world not in _tex_size:
        with Image.open(compose_layers.base_path(world)) as img:
            _tex_size[world] = img.size
    return _tex_size[world]


@app.get("/api/maps")
def api_maps():
    out = []
    for m in MAPS.values():
        world = m["world"]
        if not os.path.isfile(compose_layers.base_path(world)):
            continue
        info = WORLDS[world]
        w, h = texture_size(world)
        out.append({"id": m["id"], "name": m["name"], "world": world, "size": info["size"],
                    "basemap": f"/static/maps/{world}.jpg", "width": w, "height": h,
                    "originPx": info["texture"]["originPx"], "pxPerMetre": list(compose_layers.px_per_metre(info))})
    return jsonify(out)


@app.get("/api/<map_id>/approved")
def api_approved(map_id):
    """What the editor shows: everyone's live global work, plus the signed-in Steam player's own personal work."""
    map_id, world = map_or_404(map_id)
    u = access.user()
    me = u["id"] if u and u["provider"] == "steam" else None
    return jsonify([public(s) for s in live_subs(map_id) if not is_personal(s) or (me and s.get("owner") == me)])


@app.get("/api/<map_id>/image/<sub_id>")
def api_image(map_id, sub_id):
    map_id, world = map_or_404(map_id)
    path = sub_dir(map_id, sub_id)
    sub = read_sub(path)
    if sub.get("kind") != "layer":
        abort(404)
    u = access.user()
    owner = u and sub.get("by") == f"{u['provider']}:{u['id']}"
    if (sub.get("status") != "approved" or is_personal(sub)) and not owner:
        require_admin()
    return send_file(os.path.join(path, sub["image"]), mimetype="image/png", max_age=0)


@app.post("/api/<map_id>/submissions")
def api_submit(map_id):
    map_id, world = map_or_404(map_id)
    result, code = create_submission(map_id, world, request.get_json(silent=True) or {}, "session", "web")
    return jsonify(result), code


def create_submission(map_id, world, body, user, source):
    """Checks and stores one submission. user: "session" (the web editor's signed-in player) or a
    user dict (a drawing made in game). Returns (response dict, HTTP status)."""
    size = float(WORLDS[world]["size"])
    allowed, why, ident = access.check(map_id, user)
    if not allowed:
        return {"error": why}, 403 if ident is None else 429
    if user == "session":
        user = access.user()
    rules = access.rules(map_id)
    kind = body.get("kind")
    author = user["name"] if user else clean_text(body.get("author"), 32)
    if not author:
        return {"error": "Enter your name"}, 400

    # global (everyone) or personal (only its author; needs a SteamID, which the game knows)
    scope = body.get("scope") or ("global" if rules["allowGlobal"] else "personal")
    if scope not in ("global", "personal") or not rules["allowGlobal" if scope == "global" else "allowPersonal"]:
        return {"error": f"{scope.capitalize()} drawings are not allowed on this map"}, 400
    if scope == "personal" and not (user and user["provider"] == "steam"):
        return {"error": "Personal drawings need Steam sign-in (the game knows players by their Steam account)"}, 400
    try:
        days = int(body.get("days", rules.get("defaultDays") or 0))
    except (TypeError, ValueError):
        return {"error": "Bad lifespan"}, 400
    if days not in access.lifespans(rules):
        return {"error": "That lifespan is not allowed on this map"}, 400
    approve_now = scope == "personal" and not rules["approvePersonal"]
    sub = {"kind": kind, "status": "approved" if approve_now else "pending", "created": now(), "author": author,
           "by": ident, "group": clean_text(body.get("group"), 32), "title": clean_text(body.get("title")),
           "scope": scope, "source": source}
    if scope == "personal":
        sub["owner"] = user["id"]
    if approve_now:
        sub["reviewed"] = now()
    if days:
        sub["expires"] = (datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(days=days)).strftime("%Y-%m-%dT%H:%M:%SZ")

    png = None
    if kind == "layer":
        try:
            x0, y0, x1, y1 = (int(v) for v in body["px"])
            data = body["image"].split(",", 1)[1] if body["image"].startswith("data:") else body["image"]
            png = base64.b64decode(data, validate=True)
        except (KeyError, ValueError, TypeError, AttributeError):
            return {"error": "Bad layer data"}, 400
        if len(png) > MAX_LAYER_BYTES:
            return {"error": "Drawing too large"}, 400
        tw, th = texture_size(world)
        if not (0 <= x0 < x1 <= tw and 0 <= y0 < y1 <= th):
            return {"error": "Drawing is outside the map"}, 400
        try:
            img = Image.open(io.BytesIO(png))
            img.verify()
            img = Image.open(io.BytesIO(png)).convert("RGBA")
        except Exception:
            return {"error": "Not a PNG image"}, 400
        if img.size != (x1 - x0, y1 - y0) or img.getchannel("A").getbbox() is None:
            return {"error": "Drawing is empty or the wrong size"}, 400
        buf = io.BytesIO()
        img.save(buf, "PNG", optimize=True)      # re-encode: never store what the browser sent as-is
        png = buf.getvalue()
        sub.update(image="layer.png", px=[x0, y0, x1, y1])
        # A drawing made only of brush strokes also comes as lines, which go live sharper than the image.
        strokes = body.get("strokes")
        if strokes:
            try:
                clean = []
                for s in strokes[:200]:
                    color = str(s["color"])
                    int(color[1:7], 16)
                    pts = [[round(float(x), 1), round(float(y), 1)] for x, y in s["points"]]
                    if len(color) != 7 or not pts:
                        raise ValueError
                    clean.append({"color": color, "alpha": min(1.0, max(0.0, float(s["alpha"]))),
                                  "width": min(200.0, max(0.5, float(s["width"]))), "points": pts})
                live.strokes_to_world(WORLDS[world], clean)        # checks the point limit
                sub["strokes"] = clean
            except (KeyError, ValueError, TypeError):
                return {"error": "Bad stroke data"}, 400
    elif kind == "marker":
        try:
            x, z = float(body["x"]), float(body["z"])
            icon = int(body.get("icon", 0))
            color = str(body.get("color", "#FFC00000"))
            compose_layers.argb(color)
        except (KeyError, ValueError, TypeError):
            return {"error": "Bad marker data"}, 400
        text = clean_text(body.get("text"))
        if not (0 <= x <= size and 0 <= z <= size) or not 0 <= icon < ICON_COUNT or not text:
            return {"error": "Marker needs a label and a spot on the map"}, 400
        if len(color.lstrip("#")) not in (6, 8):
            return {"error": "Bad colour"}, 400
        sub.update(x=round(x, 1), z=round(z, 1), text=text, color=color, icon=icon)
    else:
        return {"error": "Unknown kind"}, 400

    with write_lock:
        sub_id = datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + secrets.token_hex(3)
        path = os.path.join(SUBS, map_id, sub_id)
        os.makedirs(path)
        sub["id"] = sub_id
        if png:
            with open(os.path.join(path, "layer.png"), "wb") as f:
                f.write(png)
        write_sub(path, sub)
        if approve_now and png:
            live.ensure_live(WORLDS[world], path, sub)
    if not approve_now:              # personal work that skips approval is nothing for the owner to review
        notify.send(CFG, "submitted", MAPS[map_id], public(sub), access.public_url() + "/admin")
    return {"id": sub_id, "status": sub["status"], "scope": scope, "expires": sub.get("expires")}, 200


# ---------------------------------------------------------------- admin API

@app.get("/api/admin/submissions")
def api_admin_list_all():
    """Every map's submissions in one list, each with its "map" id (the admin page shows them together)."""
    require_admin()
    # "by" (steam:<id>, discord:<id> or ip:<address>) only for the owner
    return jsonify([{**public(s), "map": m, "by": s.get("by", "")} for m in MAPS for s in all_subs(m)])


@app.get("/api/admin/<map_id>/submissions")
def api_admin_list(map_id):
    require_admin()
    map_id, world = map_or_404(map_id)
    return jsonify([public(s) for s in all_subs(map_id)])


@app.delete("/api/admin/<map_id>/submissions/<sub_id>")
def api_admin_delete(map_id, sub_id):
    """Deletes a rejected submission for good (its folder: image, live copy, json). Approved work has
    to be revoked first, so nothing that is live in game disappears from here."""
    require_admin()
    map_id, world = map_or_404(map_id)
    with write_lock:
        path = sub_dir(map_id, sub_id)
        sub = read_sub(path)
        if sub.get("status") not in ("rejected", "withdrawn"):
            return jsonify(error="Only rejected or withdrawn submissions can be deleted"), 400
        shutil.rmtree(path)
        _changed(path)
    notify.send(CFG, "deleted", MAPS[map_id], public(sub), access.public_url() + "/admin")
    return jsonify(deleted=sub_id)


@app.post("/api/admin/<map_id>/review/<sub_id>")
def api_admin_review(map_id, sub_id):
    require_admin()
    map_id, world = map_or_404(map_id)
    body = request.get_json(silent=True) or {}
    status = body.get("status")
    if status not in STATUSES:
        return jsonify(error="Bad status"), 400
    with write_lock:
        path = sub_dir(map_id, sub_id)
        sub = read_sub(path)
        before = sub.get("status")
        sub["status"] = status
        sub["reviewed"] = now()
        sub["note"] = clean_text(body.get("note"), 200)
        write_sub(path, sub)
        markers = export_markers(map_id) if sub.get("kind") == "marker" else None
        if sub.get("kind") == "layer" and status == "approved":
            live.ensure_live(WORLDS[world], path, sub)     # build the in-game copy now, not on a game server's poll
        if sub.get("kind") == "layer" and not is_personal(sub):
            glob = [s for s in live_subs(map_id) if not is_personal(s)]
            live.build_paper(world, glob, lambda sid: sub_dir(map_id, sid), os.path.join(PAPER_DIR, map_id))
    if status != before and status in ("approved", "rejected"):
        notify.send(CFG, status, MAPS[map_id], public(sub), access.public_url() + "/admin")
    return jsonify(status=status, liveMarkers=markers)


# ---------------------------------------------------------------- game server API (live map)

_live_cache = {}         # map id -> (key, minute, json)


@app.get("/api/<map_id>/live")
def api_live(map_id):
    """Everything approved for everyone on this map, for game servers to relay to players
    (see live.py). Built once per change (and once a minute, for lifespans that run out) and
    then handed to every game server as is. Personal work is not in it: /live/players."""
    map_id, world = map_or_404(map_id)
    all_subs(map_id)
    key = (_index.get(os.path.normpath(os.path.join(SUBS, map_id)), (None,))[0], json.dumps(access.options(map_id)))
    minute = now()[:16]
    cached = _live_cache.get(map_id)
    if cached and cached[0] == key and cached[1] == minute:
        text = cached[2]
    else:
        glob = [s for s in live_subs(map_id) if not is_personal(s)]
        with write_lock:
            body = live.manifest(map_id, world, WORLDS[world], glob, lambda sid: os.path.join(SUBS, map_id, sid),
                                 os.path.join(PAPER_DIR, map_id), access.options(map_id), None)
        text = json.dumps(body, separators=(",", ":"))
        _live_cache[map_id] = (key, minute, text)
    return app.response_class(text, mimetype="application/json")


def personal_of(map_id):
    """SteamID -> that player's live personal submissions."""
    out = {}
    for s in live_subs(map_id):
        if is_personal(s) and s.get("owner"):
            out.setdefault(s["owner"], []).append(s)
    return out


@app.post("/api/<map_id>/live/players")
def api_live_players(map_id):
    """{"serverKey", "players": [SteamID, ...]} (who is online) -> {"versions": {SteamID: version}}
    for those with personal work. The game server fetches a player's work (/live/player) only when
    their version changed, so a poll stays small however many players ever drew something."""
    map_id, world = map_or_404(map_id)
    body = request.get_json(silent=True) or {}
    if not server_ok(body):
        return jsonify(ok=False, error="The game server's serverKey does not match the web app's"), 200
    online = {str(p) for p in (body.get("players") or [])[:1000]}
    mine = personal_of(map_id)
    versions = {sid: live.personal_version(subs) for sid, subs in mine.items() if sid in online}
    return jsonify(ok=True, versions=versions), 200


@app.post("/api/<map_id>/live/player")
def api_live_player(map_id):
    """{"serverKey", "steamId"} -> that player's personal work (markers, layers, their own paper
    texture: global + personal). Their paper is built here, so only for players who are online."""
    map_id, world = map_or_404(map_id)
    body = request.get_json(silent=True) or {}
    if not server_ok(body):
        return jsonify(ok=False, error="The game server's serverKey does not match the web app's"), 200
    sid = str(body.get("steamId", ""))
    mine = personal_of(map_id).get(sid, [])
    glob = [s for s in live_subs(map_id) if not is_personal(s)]
    with write_lock:
        entry = live.personal_entry(world, WORLDS[world], sid, glob, mine, lambda i: os.path.join(SUBS, map_id, i),
                                    os.path.join(PAPER_DIR, map_id), access.personal_key(map_id, sid))
        live.clean_personal_papers(os.path.join(PAPER_DIR, map_id))
    return jsonify(ok=True, version=live.personal_version(mine), **entry), 200


def server_ok(body):
    key = CFG.get("serverKey") or ""
    return bool(key) and secrets.compare_digest(str(body.get("serverKey", "")), key)


@app.get("/api/<map_id>/live/paper/<int:part>")
def api_live_paper(map_id, part):
    """The map's paper texture, or with ?o=<steam id>&k=<key> that player's own (global + personal)."""
    map_id, world = map_or_404(map_id)
    owner = request.args.get("o", "")
    folder = os.path.join(PAPER_DIR, map_id)
    if owner:
        if not owner.isdigit() or not secrets.compare_digest(request.args.get("k", ""), access.personal_key(map_id, owner)):
            abort(404)
        folder = os.path.join(folder, "personal", owner)
    paa = live.paper_path(folder)
    text = live.json_part(paa, part) if paa else None
    if text is None:
        abort(404)
    return app.response_class(text, mimetype="application/json")


@app.get("/api/<map_id>/live/<sub_id>/<int:part>")
def api_live_part(map_id, sub_id, part):
    map_id, world = map_or_404(map_id)
    m = re.fullmatch(r"stamp-(\d{17})-([0-9a-f]{16})", sub_id)
    if m:                                     # a player's stamp PNG as a PAA preview (ingame/upload)
        paa = os.path.join(upload_dir(m.group(1)), m.group(2) + ".paa")
        text = live.json_part(paa, part) if os.path.isfile(paa) else None
        if text is None:
            abort(404)
        return app.response_class(text, mimetype="application/json")
    path = sub_dir(map_id, sub_id)
    sub = read_sub(path)
    if not is_live(sub) or not os.path.isfile(os.path.join(path, "live.paa")):
        abort(404)
    if is_personal(sub) and not secrets.compare_digest(request.args.get("k", ""), access.personal_key(map_id, sub.get("owner", ""))):
        abort(404)
    text = live.json_part(os.path.join(path, "live.paa"), part)
    if text is None:
        abort(404)
    return app.response_class(text, mimetype="application/json")


# ---------------------------------------------------------------- players' own submissions

def mine_item(world, sub):
    """One of a player's submissions for their list: status, scope, lifespan, where it is
    ("rect" x0, z0, x1, z1 in metres) and, for line drawings, the lines (an in-game preview)."""
    item = {k: sub[k] for k in ("id", "kind", "title", "status", "scope", "expires", "created", "text") if k in sub}
    item.setdefault("scope", "global")
    info = WORLDS[world]
    if sub.get("kind") == "marker":
        item["rect"] = [sub["x"] - 60, sub["z"] - 60, sub["x"] + 60, sub["z"] + 60]
    elif sub.get("px"):
        x0, z1 = live.tex_to_world(info, sub["px"][0], sub["px"][1])
        x1, z0 = live.tex_to_world(info, sub["px"][2], sub["px"][3])
        item["rect"] = [round(x0, 1), round(z0, 1), round(x1, 1), round(z1, 1)]
        if sub.get("strokes"):
            try:
                item["strokes"] = live.strokes_to_world(info, sub["strokes"])
            except ValueError:
                pass
    return item


def my_subs(map_id, ident):
    """The submissions this player made on this map, newest first (withdrawn ones left out)."""
    mine = [s for s in all_subs(map_id) if s.get("by") == ident and s.get("status") != "withdrawn"]
    return sorted(mine, key=lambda s: s.get("created", ""), reverse=True)


def withdraw(map_id, world, sub_id, ident):
    """The author takes a submission back: it leaves the game at once; the owner still sees it
    (Rejected tab, "withdrawn") and can delete it for good."""
    with write_lock:
        path = sub_dir(map_id, sub_id)
        sub = read_sub(path)
        if sub.get("by") != ident:
            return {"error": "That is not your submission"}, 403
        if sub.get("status") == "withdrawn":
            return {"error": "Already removed"}, 400
        was_live = is_live(sub)
        sub["status"] = "withdrawn"
        sub["reviewed"] = now()
        write_sub(path, sub)
        if was_live and not is_personal(sub):
            if sub.get("kind") == "marker":
                export_markers(map_id)
            else:
                glob = [s for s in live_subs(map_id) if not is_personal(s)]
                live.build_paper(world, glob, lambda sid: sub_dir(map_id, sid), os.path.join(PAPER_DIR, map_id))
    notify.send(CFG, "withdrawn", MAPS[map_id], public(sub), access.public_url() + "/admin")
    return {"ok": True, "id": sub_id}, 200


def session_ident():
    u = access.user()
    return f"{u['provider']}:{u['id']}" if u else None


@app.get("/api/<map_id>/mine")
def api_mine(map_id):
    """The signed-in player's own submissions on this map (the editor's My submissions)."""
    map_id, world = map_or_404(map_id)
    ident = session_ident()
    if not ident:
        return jsonify(error="Sign in to see your submissions"), 403
    return jsonify([mine_item(world, s) for s in my_subs(map_id, ident)])


@app.post("/api/<map_id>/mine/<sub_id>/withdraw")
def api_mine_withdraw(map_id, sub_id):
    map_id, world = map_or_404(map_id)
    ident = session_ident()
    if not ident:
        return jsonify(error="Sign in first"), 403
    result, code = withdraw(map_id, world, sub_id, ident)
    return jsonify(result), code


def server_player(body):
    """The SteamID a game server vouches for, or None when its serverKey is wrong."""
    key = CFG.get("serverKey") or ""
    sid = str(body.get("steamId", ""))
    if not key or not secrets.compare_digest(str(body.get("serverKey", "")), key) or not re.fullmatch(r"\d{17}", sid):
        return None
    return sid


@app.post("/api/<map_id>/ingame/mine")
def api_ingame_mine(map_id):
    """A player's own submissions, asked for by the game server (the in-game list, X on the map)."""
    map_id, world = map_or_404(map_id)
    sid = server_player(request.get_json(silent=True) or {})
    if not sid:
        return jsonify(ok=False, error="The game server's serverKey does not match the web app's"), 200
    return jsonify(ok=True, items=[mine_item(world, s) for s in my_subs(map_id, "steam:" + sid)][:50]), 200


@app.post("/api/<map_id>/ingame/withdraw")
def api_ingame_withdraw(map_id):
    map_id, world = map_or_404(map_id)
    body = request.get_json(silent=True) or {}
    sid = server_player(body)
    if not sid:
        return jsonify(ok=False, error="The game server's serverKey does not match the web app's"), 200
    sub_id = str(body.get("id", ""))
    if not sub_id.replace("-", "").isalnum() or not os.path.isfile(os.path.join(SUBS, map_id, sub_id, "submission.json")):
        return jsonify(ok=False, error="That drawing no longer exists"), 200
    result, code = withdraw(map_id, world, sub_id, "steam:" + sid)
    return jsonify({**result, "ok": code == 200}), 200


UPLOAD_MAX = 96 * 1024          # bytes; the game sends a PNG in small pieces through the game server
UPLOAD_PX = 1024
STAMPS_DIR = os.path.join(HERE, "static", "stamps")


def upload_dir(sid):
    return os.path.join(SUBS, "_uploads", sid)


def stamp_image(name, sid):
    """A stamp by name: built in (Web/static/stamps/<name>.png) or "upload:<id>", the player's own."""
    if name.startswith("upload:"):
        uid = name[7:]
        path = os.path.join(upload_dir(sid), uid + ".png") if re.fullmatch(r"[0-9a-f]{16}", uid) else ""
    else:
        path = os.path.join(STAMPS_DIR, name + ".png") if re.fullmatch(r"[a-z]{2,12}", name) else ""
    if not path or not os.path.isfile(path):
        return None
    return Image.open(path)


@app.post("/api/<map_id>/ingame")
def api_ingame(map_id):
    """A drawing made in game (map and pen), relayed by the game server.

    {"serverKey", "steamId", "name", "title", "scope", "days", "strokes": [...], "texts": [...],
    "stamps": [...], "markers": [{"x", "z", "text", "icon", "color"}]} (see live.ingame_layer).
    Lines, text and stamps become one layer submission, each marker a marker submission, all with
    the same scope and lifespan. serverKey (Settings > Game servers) proves it comes from the
    owner's game server, which vouches for the player's SteamID. Always answers 200 with
    {"ok": ...}: the game only sees the body, not the HTTP status."""
    map_id, world = map_or_404(map_id)
    body = request.get_json(silent=True) or {}
    if not server_ok(body):
        return jsonify(ok=False, error="The game server's serverKey does not match the web app's"), 200
    if not access.rules(map_id)["ingame"]:
        return jsonify(ok=False, error="Drawing in game is turned off on this map"), 200
    sid = str(body.get("steamId", ""))
    if not re.fullmatch(r"\d{17}", sid):
        return jsonify(ok=False, error="Bad player id"), 200
    user = {"provider": "steam", "id": sid, "name": clean_text(body.get("name"), 32) or "Steam " + sid}
    common = {"scope": body.get("scope"), "days": body.get("days"), "title": clean_text(body.get("title")) or "Drawn in game"}
    results = []
    made = live.ingame_layer(WORLDS[world], body.get("strokes") or [], body.get("texts") or [],
                             body.get("stamps") or [], lambda n: stamp_image(n, sid), CFG.get("fonts"))
    if made:
        png, px, strokes = made
        tw, th = texture_size(world)
        clip = [max(0, px[0]), max(0, px[1]), min(tw, px[2]), min(th, px[3])]
        if clip[0] >= clip[2] or clip[1] >= clip[3]:
            return jsonify(ok=False, error="The drawing is outside the map"), 200
        if clip != px:
            img = Image.open(io.BytesIO(png)).crop((clip[0] - px[0], clip[1] - px[1], clip[2] - px[0], clip[3] - px[1]))
            buf = io.BytesIO()
            img.save(buf, "PNG")
            png, strokes = buf.getvalue(), None
        layer = {"kind": "layer", "px": clip, "image": base64.b64encode(png).decode(), **common}
        if strokes:
            layer["strokes"] = strokes
        results.append(create_submission(map_id, world, layer, user, "game"))
    for m in (body.get("markers") or [])[:20]:
        argb = int(m.get("color", 0xFFC00000)) & 0xFFFFFFFF
        results.append(create_submission(map_id, world, {
            "kind": "marker", "x": m.get("x"), "z": m.get("z"), "text": m.get("text"), "icon": m.get("icon", 0),
            "color": "#%08X" % argb, **common}, user, "game"))
    if not results:
        return jsonify(ok=False, error="Nothing drawn"), 200
    bad = [r for r, code in results if code != 200]
    if bad and len(bad) == len(results):
        return jsonify(ok=False, error=bad[0].get("error", "Refused")), 200
    first = next(r for r, code in results if code == 200)
    return jsonify(ok=True, id=first["id"], status=first["status"], scope=first["scope"], count=len(results) - len(bad),
                   error=bad[0].get("error", "") if bad else ""), 200


@app.post("/api/<map_id>/ingame/upload")
def api_ingame_upload(map_id):
    """A PNG from the player's Documents\\DayZ\\MapDrawingTool\\stamps folder, sent by the game as
    little-endian ints (the game can read a file only 4 bytes at a time, so up to 3 bytes at the end
    may be missing: a PNG ends with a fixed IEND chunk, which is put back). Kept per player; the
    drawing then names it "upload:<id>"."""
    map_id, world = map_or_404(map_id)
    body = request.get_json(silent=True) or {}
    sid = server_player(body)
    if not sid:
        return jsonify(ok=False, error="The game server's serverKey does not match the web app's"), 200
    words = body.get("words") or []
    if not words or len(words) * 4 > UPLOAD_MAX + 4:
        return jsonify(ok=False, error=f"Stamp images can be up to {UPLOAD_MAX // 1024} KB"), 200
    try:
        data = b"".join(int(w).to_bytes(4, "little", signed=True) for w in words)
    except (TypeError, ValueError, OverflowError):
        return jsonify(ok=False, error="The image could not be read"), 200
    if 0 < int(body.get("bytes") or 0) <= len(data):
        data = data[:int(body["bytes"])]                      # the last word's padding
    iend = b"\x00\x00\x00\x00IEND\xaeB`\x82"
    cut = data.rfind(b"IEND")
    if cut >= 4:
        data = data[:cut - 4] + iend
    try:
        img = Image.open(io.BytesIO(data))
        img.load()
        if img.format != "PNG":
            raise ValueError
        img = img.convert("RGBA")
    except Exception:
        return jsonify(ok=False, error="Only PNG images can be stamps"), 200
    if max(img.size) > UPLOAD_PX:
        img.thumbnail((UPLOAD_PX, UPLOAD_PX))
    buf = io.BytesIO()
    img.save(buf, "PNG")                                     # re-encoded: never keep what was sent as is
    uid = hashlib.sha1(buf.getvalue()).hexdigest()[:16]
    os.makedirs(upload_dir(sid), exist_ok=True)
    png = os.path.join(upload_dir(sid), uid + ".png")
    with open(png, "wb") as f:
        f.write(buf.getvalue())
    try:                                      # the game shows this while the player places the stamp
        parts = live.stamp_preview(png, os.path.join(upload_dir(sid), uid + ".paa"))
    except Exception as e:
        print(f"stamp preview for {sid}/{uid} failed: {e}", flush=True)
        parts = 0
    return jsonify(ok=True, id=uid, name=clean_text(body.get("name"), 64), w=img.width, h=img.height,
                   preview=f"stamp-{sid}-{uid}" if parts else "", parts=parts), 200


@app.get("/api/admin/build")
def api_build_status():
    require_admin()
    # the release package has no build script (baking needs DayZ Tools' AddonBuilder and the mod's key)
    return jsonify({**build_state, "available": os.path.isfile(os.path.join(TOOLS, "build-mod.ps1"))})


@app.post("/api/admin/build")
def api_build():
    """Bakes one map's drawings into its world's texture (?map=<id>), for the wooden boards."""
    require_admin()
    map_id, world = map_or_404(request.args.get("map", ""))
    if build_state["running"]:
        return jsonify(error="A build is already running"), 409

    def run():
        script = os.path.join(TOOLS, "build-mod.ps1")
        try:
            # Into Build\, never the live mod folder: a running game or server keeps those PBOs open.
            proc = subprocess.run(["powershell", "-NoProfile", "-File", script, "-Submissions", SUBS,
                                   "-Destination", BUILD_OUT, "-BakeLayers", "-Maps", f"{map_id}={world}"],
                                  capture_output=True, text=True, timeout=1800)
            build_state.update(log=(proc.stdout + proc.stderr)[-20000:], ok=proc.returncode == 0)
        except Exception as e:     # timeouts, missing powershell
            build_state.update(log=str(e), ok=False)
        build_state.update(running=False, finished=now())

    build_state.update(running=True, log="Building...", ok=None, finished=None)
    threading.Thread(target=run, daemon=True).start()
    return jsonify(started=True)


settings.init(app, CFG, MAPS, CONFIG_PATH, require_admin, access.public_url)


if __name__ == "__main__":
    app.run(host=CFG.get("host", "127.0.0.1"), port=int(CFG.get("port", 8640)), threaded=True)
