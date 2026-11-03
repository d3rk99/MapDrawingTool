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

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TOOLS = os.path.join(REPO, "Tools")
sys.path.insert(0, TOOLS)
sys.path.insert(0, HERE)     # python -I leaves the script's own folder off the path
import access  # noqa: E402
import compose_layers  # noqa: E402  (shares the submission rules with the builder)
import live  # noqa: E402
import notify  # noqa: E402

CONFIG_PATH = os.path.join(HERE, "config.json")
BUILD_OUT = os.path.join(REPO, "Build", "@MapDrawingTool")   # signed, ready to copy to the server / Workshop
PAPER_DIR = os.path.join(REPO, "Build", "paper")              # <map id>/paper_<hash>.paa, see live.build_paper
MAX_LAYER_BYTES = 8 * 1024 * 1024
MAX_TEXT = 64
ICON_COUNT = 20          # eMapMarkerTypes in vanilla scripts\5_mission\gui\mapmarkersinfo.c
STATUSES = ("pending", "approved", "rejected")


def load_config():
    if not os.path.exists(CONFIG_PATH):
        cfg = {"adminToken": secrets.token_urlsafe(24), "submissionsDir": os.path.join(REPO, "Submissions"),
               "serverProfileDir": "", "host": "127.0.0.1", "port": 8640}
        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=1)
        print(f"Wrote {CONFIG_PATH} with a new admin token (open the file to read it).")
    with open(CONFIG_PATH, encoding="utf-8") as f:
        cfg = json.load(f)
    if not cfg.get("sessionSecret"):               # signs player sign-in cookies (access.py)
        cfg["sessionSecret"] = secrets.token_urlsafe(32)
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


def write_sub(path, sub):
    tmp = os.path.join(path, "submission.json.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(sub, f, indent=1)
    os.replace(tmp, os.path.join(path, "submission.json"))


def all_subs(map_id):
    root = os.path.join(SUBS, map_id)
    if not os.path.isdir(root):
        return []
    out = []
    for name in sorted(os.listdir(root)):
        p = os.path.join(root, name)
        if os.path.isfile(os.path.join(p, "submission.json")):
            out.append(read_sub(p))
    out.sort(key=lambda s: (s.get("created", ""), s.get("id", "")))
    return out


def public(sub):
    keep = ("id", "kind", "status", "created", "author", "group", "title", "px", "x", "z", "text", "color", "icon",
            "reviewed", "note")
    return {k: sub[k] for k in keep if k in sub}


def require_admin():
    token = request.headers.get("X-Admin-Token", "")
    if not secrets.compare_digest(token, CFG["adminToken"]):
        abort(403)


def clean_text(value, limit=MAX_TEXT):
    return " ".join(str(value or "").split())[:limit]


def export_markers(map_id):
    """Writes the approved markers for a map, the same file the build makes, and copies it to the
    map's serverProfileDir (the fallback for a game server without a liveUrl)."""
    root = os.path.join(SUBS, map_id)
    subs = compose_layers.load_submissions(root) if os.path.isdir(root) else []
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
    map_id, world = map_or_404(map_id)
    return jsonify([public(s) for s in all_subs(map_id) if s.get("status") == "approved"])


@app.get("/api/<map_id>/image/<sub_id>")
def api_image(map_id, sub_id):
    map_id, world = map_or_404(map_id)
    path = sub_dir(map_id, sub_id)
    sub = read_sub(path)
    if sub.get("kind") != "layer":
        abort(404)
    if sub.get("status") != "approved":
        require_admin()
    return send_file(os.path.join(path, sub["image"]), mimetype="image/png", max_age=0)


@app.post("/api/<map_id>/submissions")
def api_submit(map_id):
    map_id, world = map_or_404(map_id)
    size = float(WORLDS[world]["size"])
    allowed, why, ident = access.check(map_id)
    if not allowed:
        return jsonify(error=why), 403 if ident is None else 429
    body = request.get_json(silent=True) or {}
    kind = body.get("kind")
    user = access.user()
    author = user["name"] if user else clean_text(body.get("author"), 32)
    if not author:
        return jsonify(error="Enter your name"), 400
    sub = {"kind": kind, "status": "pending", "created": now(), "author": author, "by": ident,
           "group": clean_text(body.get("group"), 32), "title": clean_text(body.get("title"))}

    png = None
    if kind == "layer":
        try:
            x0, y0, x1, y1 = (int(v) for v in body["px"])
            data = body["image"].split(",", 1)[1] if body["image"].startswith("data:") else body["image"]
            png = base64.b64decode(data, validate=True)
        except (KeyError, ValueError, TypeError, AttributeError):
            return jsonify(error="Bad layer data"), 400
        if len(png) > MAX_LAYER_BYTES:
            return jsonify(error="Drawing too large"), 400
        tw, th = texture_size(world)
        if not (0 <= x0 < x1 <= tw and 0 <= y0 < y1 <= th):
            return jsonify(error="Drawing is outside the map"), 400
        try:
            img = Image.open(io.BytesIO(png))
            img.verify()
            img = Image.open(io.BytesIO(png)).convert("RGBA")
        except Exception:
            return jsonify(error="Not a PNG image"), 400
        if img.size != (x1 - x0, y1 - y0) or img.getchannel("A").getbbox() is None:
            return jsonify(error="Drawing is empty or the wrong size"), 400
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
                return jsonify(error="Bad stroke data"), 400
    elif kind == "marker":
        try:
            x, z = float(body["x"]), float(body["z"])
            icon = int(body.get("icon", 0))
            color = str(body.get("color", "#FFC00000"))
            compose_layers.argb(color)
        except (KeyError, ValueError, TypeError):
            return jsonify(error="Bad marker data"), 400
        text = clean_text(body.get("text"))
        if not (0 <= x <= size and 0 <= z <= size) or not 0 <= icon < ICON_COUNT or not text:
            return jsonify(error="Marker needs a label and a spot on the map"), 400
        if len(color.lstrip("#")) not in (6, 8):
            return jsonify(error="Bad colour"), 400
        sub.update(x=round(x, 1), z=round(z, 1), text=text, color=color, icon=icon)
    else:
        return jsonify(error="Unknown kind"), 400

    with write_lock:
        sub_id = datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + secrets.token_hex(3)
        path = os.path.join(SUBS, map_id, sub_id)
        os.makedirs(path)
        sub["id"] = sub_id
        if png:
            with open(os.path.join(path, "layer.png"), "wb") as f:
                f.write(png)
        write_sub(path, sub)
    notify.send(CFG, "submitted", MAPS[map_id], public(sub), access.public_url() + "/admin")
    return jsonify(id=sub_id, status="pending")


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
        if sub.get("status") != "rejected":
            return jsonify(error="Only rejected submissions can be deleted"), 400
        shutil.rmtree(path)
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
        if sub.get("kind") == "layer":
            approved = [s for s in all_subs(map_id) if s.get("status") == "approved"]
            live.build_paper(world, approved, lambda sid: sub_dir(map_id, sid), os.path.join(PAPER_DIR, map_id))
    if status != before and status in ("approved", "rejected"):
        notify.send(CFG, status, MAPS[map_id], public(sub), access.public_url() + "/admin")
    return jsonify(status=status, liveMarkers=markers)


# ---------------------------------------------------------------- game server API (live map)

@app.get("/api/<map_id>/live")
def api_live(map_id):
    """Everything approved on this map, for game servers to relay to players (see live.py)."""
    map_id, world = map_or_404(map_id)
    approved = [s for s in all_subs(map_id) if s.get("status") == "approved"]
    markers = [compose_layers.marker_entry(s) for s in approved if s.get("kind") == "marker"]
    with write_lock:
        body = live.manifest(map_id, world, WORLDS[world], approved, lambda sid: os.path.join(SUBS, map_id, sid),
                             markers, os.path.join(PAPER_DIR, map_id))
    return app.response_class(json.dumps(body, separators=(",", ":")), mimetype="application/json")


@app.get("/api/<map_id>/live/paper/<int:part>")
def api_live_paper(map_id, part):
    map_id, world = map_or_404(map_id)
    paa = live.paper_path(os.path.join(PAPER_DIR, map_id))
    text = live.json_part(paa, part) if paa else None
    if text is None:
        abort(404)
    return app.response_class(text, mimetype="application/json")


@app.get("/api/<map_id>/live/<sub_id>/<int:part>")
def api_live_part(map_id, sub_id, part):
    map_id, world = map_or_404(map_id)
    path = sub_dir(map_id, sub_id)
    if read_sub(path).get("status") != "approved" or not os.path.isfile(os.path.join(path, "live.paa")):
        abort(404)
    text = live.json_part(os.path.join(path, "live.paa"), part)
    if text is None:
        abort(404)
    return app.response_class(text, mimetype="application/json")


@app.get("/api/admin/build")
def api_build_status():
    require_admin()
    return jsonify(build_state)


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


if __name__ == "__main__":
    app.run(host=CFG.get("host", "127.0.0.1"), port=int(CFG.get("port", 8640)), threaded=True)
