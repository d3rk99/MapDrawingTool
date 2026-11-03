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
import subprocess
import tempfile

from PIL import Image

import compose_layers
import paths
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
    with tempfile.TemporaryDirectory() as tmp:
        png = os.path.join(tmp, "live_ca.png")         # _ca: ImageToPAA keeps the alpha channel
        img.save(png)
        subprocess.run([paths.image_to_paa(), png, paa], check=True, capture_output=True, timeout=120)
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
        if entry.get("kind") != "strokes" or entry.get("rev") == 2:
            return entry                   # older stroke entries are rebuilt below
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
    hasher = hashlib.sha1()
    with open(compose_layers.base_path(world), "rb") as f:
        hasher.update(f.read())
    for img_path, px in layers:
        with open(img_path, "rb") as f:
            hasher.update(f.read())
        hasher.update(json.dumps(px).encode())
    name = "paper_" + hasher.hexdigest()[:16] + ".paa"
    paa = os.path.join(paper_dir, name)
    if not os.path.isfile(paa):
        os.makedirs(paper_dir, exist_ok=True)
        img, _ = compose_layers.paint_layers(world, layers)
        with tempfile.TemporaryDirectory() as tmp:
            png = os.path.join(tmp, "paper_co.png")     # _co: colour texture, like karta_co
            img.convert("RGB").save(png)
            subprocess.run([paths.image_to_paa(), png, paa + ".tmp"], check=True, capture_output=True, timeout=300)
        os.replace(paa + ".tmp", paa)
        for old in os.listdir(paper_dir):
            if old != name and old.startswith("paper_"):
                os.remove(os.path.join(paper_dir, old))
    size = os.path.getsize(paa)
    return {"id": "paper", "kind": "paper", "hash": name[6:-4], "bytes": size, "parts": -(-size // PART_BYTES)}


def paper_path(paper_dir):
    names = [n for n in os.listdir(paper_dir) if n.startswith("paper_") and n.endswith(".paa")] if os.path.isdir(paper_dir) else []
    return os.path.join(paper_dir, names[0]) if names else None


def json_part(paa, part):
    """One part of a PAA as {"d": [signed little-endian int32, ...]}, zero-padded to whole words."""
    data = open(paa, "rb").read()
    chunk = data[part * PART_BYTES:(part + 1) * PART_BYTES]
    if not chunk:
        return None
    chunk += b"\0" * (-len(chunk) % 4)
    words = [int.from_bytes(chunk[i:i + 4], "little", signed=True) for i in range(0, len(chunk), 4)]
    return json.dumps({"d": words}, separators=(",", ":"))


def manifest(map_id, world, world_info, approved, sub_path_of, markers, paper_dir):
    """What a game server relays to its players. "mapId" is the URL path clients download parts from."""
    layers = []
    for sub in approved:
        if sub.get("kind") != "layer":
            continue
        entry = ensure_live(world_info, sub_path_of(sub["id"]), sub)
        if entry["kind"] == "none":
            continue
        if "bytes" in entry:
            entry = {**entry, "parts": -(-entry["bytes"] // PART_BYTES)}    # live.json may predate PART_BYTES
        layers.append({"id": sub["id"], **entry})
    body = {"mapId": map_id, "world": world, "markers": markers, "layers": layers}
    paper = build_paper(world, approved, sub_path_of, paper_dir)
    if paper:
        body["paper"] = paper
    body["version"] = hashlib.sha1(json.dumps(body, sort_keys=True).encode()).hexdigest()[:16]
    return body
