"""Paints approved map submissions onto each world's map texture and writes the live markers.

Usage: python -I compose_layers.py <submissions dir> <stage dir> [<map id>=<world> ...]

The base texture is Web/static/maps/<world>.png (make it with make_basemap.py).
<submissions dir>/<map id>/<id>/submission.json, one per player submission. A map is one set of
drawings on one world (Web/app.py); without <map id>=<world> pairs, every folder named like a
world is that world's map. Only one map per world can be baked (one karta texture per world).
  {"kind": "layer",  "status": "approved", "image": "layer.png", "px": [x0, y0, x1, y1], ...}
  {"kind": "marker", "status": "approved", "x": 0, "z": 0, "text": "", "color": "#FFC00000", "icon": 0, ...}
px is where the PNG goes on the texture, in texture pixels (y down). Marker x/z are world metres.
Only "approved" submissions are used; layers stack in "created" order (oldest at the bottom).

Writes into <stage dir>:
  MapDrawingTool_<world>/config.cpp and <texture file> (PNG for now; build-mod.ps1 makes the PAA)
  profile/<map id>/markers.json  (copy to the server's $profile:MapDrawingTool\\markers.json)
  manifest.json                  (content hash, so an unchanged map is not rebuilt)
"""
import hashlib
import json
import os
import sys

from PIL import Image

# MDT_ROOT: the packaged MapDrawingTool.exe sets it to its own folder (same layout as the repo)
HERE = (os.path.join(os.environ["MDT_ROOT"], "Tools") if os.environ.get("MDT_ROOT")
        else os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(HERE)


def load_worlds():
    with open(os.path.join(HERE, "worlds.json"), encoding="utf-8") as f:
        return {k.lower(): v for k, v in json.load(f).items()}


def px_per_metre(info):
    """(x, y) texture pixels per metre. worlds.json pxPerMetre is one number, or [x, y] where the
    map is not square on its texture (Livonia)."""
    ppm = info["texture"]["pxPerMetre"]
    if isinstance(ppm, (int, float)):
        return float(ppm), float(ppm)
    return float(ppm[0]), float(ppm[1])


def base_path(world):
    return os.path.join(REPO, "Web", "static", "maps", world + ".png")


def load_submissions(world_dir):
    subs = []
    for name in sorted(os.listdir(world_dir)):
        path = os.path.join(world_dir, name, "submission.json")
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8") as f:
            sub = json.load(f)
        if sub.get("status") != "approved":
            continue
        sub["_dir"] = os.path.dirname(path)
        subs.append(sub)
    subs.sort(key=lambda s: (s.get("created", ""), s.get("id", "")))
    return subs


def argb(color):
    # "#AARRGGBB" or "#RRGGBB" -> signed 32-bit int as Enforce reads it
    c = color.lstrip("#")
    if len(c) == 6:
        c = "FF" + c
    v = int(c, 16)
    return v - (1 << 32) if v >= 1 << 31 else v


def marker_entry(sub):
    return {"x": float(sub["x"]), "z": float(sub["z"]), "text": str(sub.get("text", ""))[:64],
            "color": argb(sub.get("color", "#FFC00000")), "icon": int(sub.get("icon", 0))}


def paint_layers(world, layers, hasher=None):
    """The world's base texture with layers painted on, oldest at the bottom.
    layers: (image path, px rect) pairs. Returns (RGBA image, layer count)."""
    base = Image.open(base_path(world)).convert("RGBA")
    if hasher:
        with open(base_path(world), "rb") as f:
            hasher.update(f.read())
    for img_path, px in layers:
        x0, y0, x1, y1 = (int(v) for v in px)
        if not (0 <= x0 < x1 <= base.width and 0 <= y0 < y1 <= base.height):
            raise ValueError(f"{world}/{os.path.basename(os.path.dirname(img_path))}: px rect outside the texture")
        if hasher:
            with open(img_path, "rb") as f:
                hasher.update(f.read())
            hasher.update(json.dumps(px).encode())
        img = Image.open(img_path).convert("RGBA")
        if img.size != (x1 - x0, y1 - y0):
            img = img.resize((x1 - x0, y1 - y0), Image.LANCZOS)
        base.alpha_composite(img, (x0, y0))
    return base, len(layers)


def compose_world(world, info, subs, out_root, hasher, map_id=None):
    tex = info["texture"]
    try:
        base, layers = paint_layers(world, [(os.path.join(s["_dir"], s["image"]), s["px"])
                                            for s in subs if s.get("kind") == "layer"], hasher)
    except ValueError as e:
        raise SystemExit(str(e))

    addon = "MapDrawingTool_" + world
    addon_dir = os.path.join(out_root, addon)
    os.makedirs(addon_dir, exist_ok=True)
    base.convert("RGB").save(os.path.join(addon_dir, os.path.splitext(tex["file"])[0] + ".png"))
    required = ", ".join(f'"{a}"' for a in ["DZ_Data", "DZ_Structures_Signs"] + list(tex.get("afterAddons", [])))
    cfg = (f"class CfgPatches\r\n{{\r\n\tclass {addon}\r\n\t{{\r\n\t\tunits[] = {{}};\r\n\t\tweapons[] = {{}};\r\n"
           f"\t\trequiredVersion = 0.1;\r\n\t\trequiredAddons[] = {{{required}}};\r\n\t}};\r\n}};\r\n")
    with open(os.path.join(addon_dir, "config.cpp"), "w", encoding="ascii", newline="") as f:
        f.write(cfg)
    with open(os.path.join(out_root, addon + ".prefix"), "w", encoding="ascii") as f:
        f.write(tex["prefix"])

    markers = [marker_entry(s) for s in subs if s.get("kind") == "marker"]
    prof = os.path.join(out_root, "profile", map_id or world)
    os.makedirs(prof, exist_ok=True)
    with open(os.path.join(prof, "markers.json"), "w", encoding="utf-8") as f:
        json.dump({"markers": markers}, f, indent=1)
    return layers, len(markers)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    subs_root, out_root = sys.argv[1], sys.argv[2]
    worlds = load_worlds()
    pairs = [a.split("=", 1) for a in sys.argv[3:]]
    if not pairs:
        pairs = [(d, d.lower()) for d in sorted(os.listdir(subs_root))
                 if os.path.isdir(os.path.join(subs_root, d)) and d.lower() in worlds]
    seen = set()
    hasher = hashlib.sha256()
    for map_id, world in pairs:
        world = world.lower()
        if world not in worlds:
            raise SystemExit(f"Unknown world '{world}': add it to Tools/worlds.json")
        if world in seen:
            raise SystemExit(f"Two maps on {world}: only one can be baked into its texture")
        seen.add(world)
        if not os.path.isfile(base_path(world)):
            raise SystemExit(f"No base texture for {world}: run Tools/make_basemap.py {world}")
        map_dir = os.path.join(subs_root, map_id)
        subs = load_submissions(map_dir) if os.path.isdir(map_dir) else []
        layers, n_markers = compose_world(world, worlds[world], subs, out_root, hasher, map_id)
        print(f"{map_id} ({world}): {layers} layers painted, {n_markers} live markers")
    with open(os.path.join(out_root, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump({"layersHash": hasher.hexdigest()}, f, indent=1)
    print("layers hash", hasher.hexdigest()[:16])


if __name__ == "__main__":
    main()
