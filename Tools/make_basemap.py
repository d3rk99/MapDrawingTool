"""Converts a world's map texture (the paper map in hand) into the PNG the editor draws on and the
web app paints approved drawings onto.

Usage: python -I make_basemap.py <world> [source.paa]

Default source is the world's "texture.source" in worlds.json: a PBO inside the DayZ game or
server install (found through Steam, see paths.py), with "entry" the PAA inside it. Sakhal and
Nasdara come from their DLC folders. A world whose files are not installed is skipped (exit 2).
Pass another PAA to start from a customised map, e.g. Memento Mori's MMmap karta_co.paa.
Writes Web/static/maps/<world>.png (what gets painted on) and <world>.jpg (what the editor shows).

Calibration (worlds.json originPx, pxPerMetre, size): texture pixel = originPx + (X, size - Z) *
pxPerMetre. Checked 2026-11-03 by drawing each mission's mapgrouppos.xml buildings on the texture
and fitting offset and scale so they land on the map's buildings (Chernarus came out at its known
22, 22, 0.125). Livonia's map is not square on its texture, hence [x, y] scales.
"""
import json
import os
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)     # python -I leaves the script's own folder off the path
import paths  # noqa: E402
import pbo  # noqa: E402

REPO = os.path.dirname(HERE)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    world = sys.argv[1].lower()
    with open(os.path.join(HERE, "worlds.json"), encoding="utf-8") as f:
        tex = json.load(f)[world]["texture"]
    source = sys.argv[2] if len(sys.argv) > 2 else paths.find_in_dayz(tex["source"])
    if not source:
        print(f"{world}: {tex['source']} not found in a DayZ or DayZ Server install, skipped")
        sys.exit(2)
    out = os.path.join(REPO, "Web", "static", "maps", world + ".png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        if source.lower().endswith(".pbo"):
            data, _, entries = pbo.read_entries(source)
            want = tex["entry"].lower()
            match = [e for e in entries if e[0].lower() == want]
            if not match or match[0][1]:
                raise SystemExit(f"{want} not found (or compressed) in {source}")
            name, _, _, datasize, offset = match[0]
            paa = os.path.join(tmp, "source.paa")
            with open(paa, "wb") as f:
                f.write(data[offset:offset + datasize])
            source = paa
        png = os.path.join(tmp, "base.png")
        subprocess.run([paths.image_to_paa(), source, png], check=True, capture_output=True)
        img = Image.open(png).convert("RGB")
        img.save(out, optimize=True)
        img.save(out[:-4] + ".jpg", quality=90)   # lighter copy the editor shows
    print(f"{out} (+ .jpg): {img.width}x{img.height} from {source}")


if __name__ == "__main__":
    main()
