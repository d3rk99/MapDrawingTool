"""Live map data: what game servers poll so approved work shows on the in-game (M) map at once.

The full-screen map in game is drawn by the engine, not from karta_co.paa, so the mod draws
approved work on top of it (tested 2026-10-27, Test/README.md). Per approved layer, either:
  - "strokes": the drawing was fully opaque brush strokes only, sent as lines (sharp at every zoom), or
  - "image": a small PAA of the layer clipped to the map frame, which clients download in parts.
Plus "paper": the world's whole map texture with every approved layer painted on (paper_<hash>.paa).
Clients put it on the paper map in hand, which takes a texture from $profile (tested 2026-11-03),
so one shared mod shows each server its own map with no Workshop update.

DayZ script cannot download a file straight to disk (RestContext.FILE writes nothing), so parts
are JSON {"d": [int, ...]}: every 4 bytes are the signed int they form little-endian. The client
parses a part natively (JsonSerializer) and writes each int with FileSerializer (exactly 4 bytes).
Parts used to be hex text decoded in script; that copied the whole part string per word and
dropped the game to a few FPS for the length of a 2.4 MB download.
"""
import hashlib
import json
import os

from PIL import Image

import compose_layers
import paa as paa_writer         # PAA without DayZ Tools
from compose_layers import px_per_metre

PART_BYTES = 32 * 1024      # bytes per part (about 90 KB of JSON; one native parse per part)
MAX_LIVE_PX = 512           # longest side of a live image; DayZ needs power-of-two textures
MAX_STROKE_POINTS = 4000    # per layer, keeps the manifest small enough to relay to every player


def _pot(n):
    p = 16
    while p < n and p < MAX_LIVE_PX:
        p *= 2
    return p


def _calib(world_info):
    tex = world_info["texture"]
    sx, sy = px_per_metre(world_info)
    return float(world_info["size"]), tex["originPx"][0], tex["originPx"][1], sx, sy


def tex_to_world(world_info, tx, ty):
    size, ox, oy, sx, sy = _calib(world_info)
    return (tx - ox) / sx, size - (ty - oy) / sy


def _simplify(points, tolerance):
    """Ramer-Douglas-Peucker: drops points within `tolerance` of the line through their neighbours."""
    if len(points) < 3:
        return points
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        a, b = stack.pop()
        (ax, az), (bx, bz) = points[a], points[b]
        dx, dz = bx - ax, bz - az
        length = (dx * dx + dz * dz) ** 0.5
        best, best_i = 0.0, -1
        for i in range(a + 1, b):
            px, pz = points[i]
            d = abs(dz * (px - ax) - dx * (pz - az)) / length if length else ((px - ax) ** 2 + (pz - az) ** 2) ** 0.5
            if d > best:
                best, best_i = d, i
        if best > tolerance:
            keep[best_i] = True
            stack += [(a, best_i), (best_i, b)]
    return [p for p, k in zip(points, keep) if k]


def strokes_to_world(world_info, strokes):
    """Editor strokes (texture px) -> manifest strokes (world metres, ARGB int colour).
    The editor records a point every few metres; drawn in game as one DrawLine each, a wide
    see-through stroke turns hatched (overlapping ends darken, corners gap). Simplifying to
    a quarter of the stroke width leaves a few long segments that look the same."""
    size, ox, oy, sx, sy = _calib(world_info)
    out, total = [], 0
    for s in strokes:
        width = float(s["width"]) * 2 / (sx + sy)
        world = [tex_to_world(world_info, tx, ty) for tx, ty in s["points"]]
        pts = []
        for x, z in _simplify(world, max(2.0, min(15.0, width / 4))):
            pts += [round(x, 1), round(z, 1)]
        total += len(pts) // 2
        if total > MAX_STROKE_POINTS:
            raise ValueError("too many stroke points")
        r, g, b = (int(s["color"][i:i + 2], 16) for i in (1, 3, 5))
        a = max(0, min(255, round(float(s["alpha"]) * 255)))
        argb = (a << 24) | (r << 16) | (g << 8) | b
        out.append({"color": argb - (1 << 32) if argb >= 1 << 31 else argb,
                    "width": round(width, 1), "points": pts})
    return out


def build_image(world_info, sub_path, sub):
    """Clips the layer to the map frame, scales it to a power-of-two PAA and records where it goes.
    Returns the live entry, or None when the drawing is only on the paper margin."""
    size, ox, oy, sx, sy = _calib(world_info)
    px0, py0, px1, py1 = sub["px"]
    fx0, fy0, fx1, fy1 = ox, oy, ox + size * sx, oy + size * sy         # map frame on the texture
    cx0, cy0, cx1, cy1 = max(px0, fx0), max(py0, fy0), min(px1, fx1), min(py1, fy1)
    if cx0 >= cx1 or cy0 >= cy1:
        return None
    img = Image.open(os.path.join(sub_path, sub["image"])).convert("RGBA")
    img = img.crop((round(cx0 - px0), round(cy0 - py0), round(cx1 - px0), round(cy1 - py0)))
    if img.getchannel("A").getbbox() is None:
        return None
    # The layer was painted at texture resolution; upscaling 2x keeps edges smooth when zoomed in.
    img = img.resize((_pot(img.width * 2), _pot(img.height * 2)), Image.LANCZOS)
    paa = os.path.join(sub_path, "live.paa")
    paa_writer.save(img, paa, alpha=True)
    data = open(paa, "rb").read()
    x0, z1 = tex_to_world(world_info, cx0, cy0)
    x1, z0 = tex_to_world(world_info, cx1, cy1)
    return {"kind": "image", "rect": [round(x0, 1), round(z0, 1), round(x1, 1), round(z1, 1)],
            "bytes": len(data), "parts": -(-len(data) // PART_BYTES), "hash": hashlib.sha1(data).hexdigest()[:16],
            "w": img.width, "h": img.height}


def ensure_live(world_info, sub_path, sub):
    """The live entry for an approved layer, built once and kept in live.json next to it."""
    meta_path = os.path.join(sub_path, "live.json")
    if os.path.isfile(meta_path):
        with open(meta_path, encoding="utf-8") as f:
            entry = json.load(f)
        missing = entry.get("kind") == "image" and not os.path.isfile(os.path.join(sub_path, "live.paa"))
        if not missing and (entry.get("kind") != "strokes" or entry.get("rev") == 2):
            return entry                   # older stroke entries and lost PAAs are rebuilt below
    # In game a line can only be see-through by its colour's alpha, which darkens wherever pieces
    # overlap (Test/README.md). A drawing with any see-through stroke goes live as its image, which
    # the editor painted one whole stroke at a time; fully opaque drawings stay sharp lines.
    strokes = sub.get("strokes")
    if strokes and all(float(s["alpha"]) >= 1 for s in strokes):
        entry = {"kind": "strokes", "rev": 2, "strokes": strokes_to_world(world_info, strokes)}
    else:
        entry = build_image(world_info, sub_path, sub) or {"kind": "none"}
    with open(meta_path, "w", encoding="utf-8") as f:
        json.dump(entry, f)
    return entry


def build_paper(world, approved, sub_path_of, paper_dir):
    """The paper map texture: base texture plus every approved layer, as paper_<hash>.paa in
    paper_dir (older ones are removed). Returns the manifest entry, or None with no layers."""
    layers = []
    for sub in approved:
        if sub.get("kind") == "layer":
            layers.append((os.path.join(sub_path_of(sub["id"]), sub["image"]), sub["px"]))
    if not layers:
        return None
    # Named after the files' size and time, not their contents: this runs on every game server poll,
    # for every player with personal drawings, and the 4096 px base images are 15-20 MB.
    hasher = hashlib.sha1()
    for path in [compose_layers.base_path(world)] + [img for img, _ in layers]:
        st = os.stat(path)
        hasher.update(f"{os.path.basename(path)}:{st.st_size}:{st.st_mtime_ns};".encode())
    hasher.update(json.dumps([px for _, px in layers]).encode())
    name = "paper_" + hasher.hexdigest()[:16] + ".paa"
    paa = os.path.join(paper_dir, name)
    if not os.path.isfile(paa):
        os.makedirs(paper_dir, exist_ok=True)
        img, _ = compose_layers.paint_layers(world, layers)
        paa_writer.save(img.convert("RGB"), paa + ".tmp", alpha=False)
        os.replace(paa + ".tmp", paa)
        for old in os.listdir(paper_dir):
            if old != name and old.startswith("paper_"):
                os.remove(os.path.join(paper_dir, old))
    size = os.path.getsize(paa)
    return {"id": "paper", "kind": "paper", "hash": name[6:-4], "bytes": size, "parts": -(-size // PART_BYTES)}


def paper_path(paper_dir):
    names = [n for n in os.listdir(paper_dir) if n.startswith("paper_") and n.endswith(".paa")] if os.path.isdir(paper_dir) else []
    return os.path.join(paper_dir, names[0]) if names else None


def stamp_preview(png, paa, side=256):
    """A player's stamp PNG as a small square PAA the game can show (it cannot show PNGs): the image
    centred on a transparent square, so the in-game preview keeps its shape. Returns the part count."""
    if not os.path.isfile(paa):
        img = Image.open(png).convert("RGBA")
        img.thumbnail((side, side), Image.LANCZOS)
        square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
        square.paste(img, ((side - img.width) // 2, (side - img.height) // 2))
        paa_writer.save(square, paa + ".tmp", alpha=True)
        os.replace(paa + ".tmp", paa)
    return -(-os.path.getsize(paa) // PART_BYTES)


def json_part(paa, part):
    """One part of a PAA as {"d": [signed little-endian int32, ...]}, zero-padded to whole words."""
    data = open(paa, "rb").read()
    chunk = data[part * PART_BYTES:(part + 1) * PART_BYTES]
    if not chunk:
        return None
    chunk += b"\0" * (-len(chunk) % 4)
    words = [int.from_bytes(chunk[i:i + 4], "little", signed=True) for i in range(0, len(chunk), 4)]
    return json.dumps({"d": words}, separators=(",", ":"))


def _layer_entries(world_info, subs, sub_path_of, query=""):
    layers = []
    for sub in subs:
        if sub.get("kind") != "layer":
            continue
        entry = ensure_live(world_info, sub_path_of(sub["id"]), sub)
        if entry["kind"] == "none":
            continue
        if "bytes" in entry:
            entry = {**entry, "parts": -(-entry["bytes"] // PART_BYTES)}    # live.json may predate PART_BYTES
        if query:
            entry = {**entry, "query": query}
        layers.append({"id": sub["id"], **entry})
    return layers


def manifest(map_id, world, world_info, approved, sub_path_of, paper_dir, options, key_of):
    """What a game server relays to its players. "mapId" is the URL path clients download parts from.

    approved: approved, unexpired submissions. Global ones go to everyone. With key_of, personal ones
    are grouped under "personal" by their author's SteamID (tests, small maps); the web app's live
    route leaves them out (key_of None) and game servers ask per online player (personal_entry)."""
    glob = [s for s in approved if s.get("scope", "global") != "personal"]
    body = {"mapId": map_id, "world": world, "options": options,
            "markers": [compose_layers.marker_entry(s) for s in glob if s.get("kind") == "marker"],
            "layers": _layer_entries(world_info, glob, sub_path_of)}
    paper = build_paper(world, glob, sub_path_of, paper_dir)
    if paper:
        body["paper"] = paper
    owners = {}
    for s in approved if key_of else []:
        if s.get("scope") == "personal" and s.get("owner"):
            owners.setdefault(s["owner"], []).append(s)
    personal = []
    for owner, mine in sorted(owners.items()):
        key = key_of(owner)
        entry = {"owner": owner,
                 "markers": [compose_layers.marker_entry(s) for s in mine if s.get("kind") == "marker"],
                 "layers": _layer_entries(world_info, mine, sub_path_of, "k=" + key)}
        if any(s.get("kind") == "layer" for s in mine):
            own_paper = build_paper(world, glob + mine, sub_path_of, os.path.join(paper_dir, "personal", owner))
            if own_paper:
                entry["paper"] = {**own_paper, "query": f"o={owner}&k={key}"}
        personal.append(entry)
    if key_of:
        body["personal"] = personal
    body["version"] = hashlib.sha1(json.dumps(body, sort_keys=True).encode()).hexdigest()[:16]
    return body


def personal_version(subs):
    """Changes whenever a player's personal work changes (added, removed, run out)."""
    sig = sorted((s["id"], s.get("status", ""), s.get("expires") or "") for s in subs)
    return hashlib.sha1(json.dumps(sig).encode()).hexdigest()[:16]


def personal_entry(world, world_info, owner, glob, mine, sub_path_of, paper_dir, key):
    """One player's personal work for their game: markers, layers (download links carry k=key) and
    their own paper texture (global + personal layers)."""
    entry = {"owner": owner,
             "markers": [compose_layers.marker_entry(s) for s in mine if s.get("kind") == "marker"],
             "layers": _layer_entries(world_info, mine, sub_path_of, "k=" + key)}
    if any(s.get("kind") == "layer" for s in mine):
        folder = os.path.join(paper_dir, "personal", owner)
        own_paper = build_paper(world, glob + mine, sub_path_of, folder)
        if own_paper:
            entry["paper"] = {**own_paper, "query": f"o={owner}&k={key}"}
            os.utime(folder)                         # in use: clean_personal_papers keeps it
    return entry


_last_clean = {}


def clean_personal_papers(paper_dir, days=30):
    """Removes personal paper textures nobody asked for in `days` (players who stopped playing).
    Runs at most once an hour."""
    import shutil
    import time
    now = time.time()
    if now - _last_clean.get(paper_dir, 0) < 3600:
        return
    _last_clean[paper_dir] = now
    root = os.path.join(paper_dir, "personal")
    if not os.path.isdir(root):
        return
    for owner in os.listdir(root):
        folder = os.path.join(root, owner)
        if os.path.isdir(folder) and now - os.path.getmtime(folder) > days * 86400:
            shutil.rmtree(folder, ignore_errors=True)


# Fonts for text written in game. The web app runs on Windows, which has these; another font file
# can be named in Web/config.json "fonts": {"name": "C:/path/font.ttf"}.
FONTS = {"plain": "arial.ttf", "bold": "arialbd.ttf", "hand": "Inkfree.ttf", "marker": "impact.ttf",
         "comic": "comic.ttf"}


def font_file(name, extra=None):
    path = (extra or {}).get(name) or FONTS.get(name) or FONTS["plain"]
    if not os.path.isabs(path):
        path = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts", path)
    return path


def ingame_layer(world_info, strokes, texts=(), stamps=(), stamp_image=None, fonts=None):
    """A drawing made in game as the editor would have sent it: (png bytes, px rect, editor
    strokes in texture px, or None when it also has text or stamps). None when it is empty.

    strokes: [{"color": ARGB int, "width": metres, "points": [x, z, ...]}]
    texts:   [{"text", "x", "z", "size": letter height in metres, "color", "font", "rot": degrees}]
    stamps:  [{"stamp": name, "x", "z", "size": width in metres, "rot"}]; stamp_image(name) gives
             the stamp as a PIL image (built-in or the player's upload), or None."""
    import io
    from PIL import ImageDraw, ImageFont
    size, ox, oy, sx, sy = _calib(world_info)
    k = (sx + sy) / 2
    to_px = lambda x, z: (ox + float(x) * sx, oy + (size - float(z)) * sy)

    def rgba(argb):
        argb = int(argb) & 0xFFFFFFFF
        return (argb >> 16 & 255, argb >> 8 & 255, argb & 255, (argb >> 24 & 255) or 255)

    # every item as (image or None, centre px, draw function) to find the bounding box first
    pieces = []
    lines = []
    for st in list(strokes)[:100]:
        pts = st.get("points") or []
        xy = [to_px(pts[i], pts[i + 1]) for i in range(0, len(pts) - 1, 2)]
        if xy:
            width = max(1.0, min(200.0, float(st.get("width", 20)) * k))
            lines.append((xy, rgba(st.get("color", 0)), width))
    for t in list(texts)[:50]:
        text = " ".join(str(t.get("text", "")).split())[:60]
        if not text:
            continue
        height = max(6, min(600, float(t.get("size", 150)) * k))
        try:
            font = ImageFont.truetype(font_file(str(t.get("font", "plain")), fonts), int(height))
        except OSError:
            font = ImageFont.load_default()
        box = font.getbbox(text)
        img = Image.new("RGBA", (box[2] - box[0] + 8, box[3] - box[1] + 8), (0, 0, 0, 0))
        ImageDraw.Draw(img).text((4 - box[0], 4 - box[1]), text, font=font, fill=rgba(t.get("color", 0xFF1E1E1E)),
                                 stroke_width=max(1, int(height / 14)), stroke_fill=(255, 255, 255, 200))
        img = img.rotate(-float(t.get("rot", 0)), expand=True, resample=Image.BICUBIC)
        pieces.append((img, to_px(t.get("x", 0), t.get("z", 0))))
    for st in list(stamps)[:50]:
        src = stamp_image(str(st.get("stamp", ""))) if stamp_image else None
        if src is None:
            continue
        w = max(4, min(1200, float(st.get("size", 300)) * k))
        img = src.convert("RGBA").resize((int(w), max(1, int(w * src.height / src.width))), Image.LANCZOS)
        img = img.rotate(-float(st.get("rot", 0)), expand=True, resample=Image.BICUBIC)
        pieces.append((img, to_px(st.get("x", 0), st.get("z", 0))))
    if not lines and not pieces:
        return None

    xs, ys = [], []
    for xy, _, width in lines:
        xs += [x - width for x, _ in xy] + [x + width for x, _ in xy]
        ys += [y - width for _, y in xy] + [y + width for _, y in xy]
    for img, (cx, cy) in pieces:
        xs += [cx - img.width / 2, cx + img.width / 2]
        ys += [cy - img.height / 2, cy + img.height / 2]
    x0, y0, x1, y1 = int(min(xs)) - 2, int(min(ys)) - 2, int(max(xs)) + 3, int(max(ys)) + 3
    canvas = Image.new("RGBA", (x1 - x0, y1 - y0), (0, 0, 0, 0))
    d = ImageDraw.Draw(canvas)
    editor = []
    for xy, colour, width in lines:
        local = [(x - x0, y - y0) for x, y in xy]
        if len(local) > 1:
            d.line(local, fill=colour, width=max(1, round(width)), joint="curve")
        r = width / 2
        for x, y in (local[0], local[-1]):
            d.ellipse([x - r, y - r, x + r, y + r], fill=colour)
        editor.append({"color": "#%02x%02x%02x" % colour[:3], "alpha": colour[3] / 255, "width": width,
                       "points": [[round(x, 1), round(y, 1)] for x, y in xy]})
    for img, (cx, cy) in pieces:
        canvas.alpha_composite(img, (int(cx - img.width / 2 - x0), int(cy - img.height / 2 - y0)))
    buf = io.BytesIO()
    canvas.save(buf, "PNG")
    return buf.getvalue(), [x0, y0, x1, y1], (editor if not pieces else None)
