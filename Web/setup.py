"""First-time setup for server owners (setup.bat runs it after installing the Python packages).

Finds DayZ (game or server) and DayZ Tools through Steam, makes the map images the editor shows
from the game's own map textures, and writes Web/config.json with a new admin token.
Run it again after a DayZ update that changes a map, or to add a map whose DLC you installed.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TOOLS = os.path.join(REPO, "Tools")
sys.path.insert(0, TOOLS)
import paths  # noqa: E402


def main():
    print("Map Drawing Tool web app setup\n")
    roots = paths.dayz_roots()
    if not roots:
        sys.exit("No DayZ or DayZ Server install found through Steam. Install one, or set \"dayzPath\" in "
                 "Web\\config.json to its folder, then run setup again.")
    print("DayZ found:      " + "\n                 ".join(roots))
    print("ImageToPAA:      " + paths.image_to_paa())       # exits with instructions when missing

    import json
    with open(os.path.join(TOOLS, "worlds.json"), encoding="utf-8") as f:
        worlds = json.load(f)
    made = []
    for world, info in worlds.items():
        print(f"\n{info.get('name', world)}: ", end="", flush=True)
        r = subprocess.run([sys.executable, "-I", os.path.join(TOOLS, "make_basemap.py"), world],
                           capture_output=True, text=True)
        if r.returncode == 0:
            made.append(world)
            print("map image ready")
        elif r.returncode == 2:
            print("not installed, skipped")
        else:
            print("FAILED\n" + (r.stdout + r.stderr).strip()[-800:])
    if not made:
        sys.exit("\nNo map images could be made.")

    sys.path.insert(0, HERE)
    import app  # noqa: E402,F401  (writes config.json with a new admin token on first run)
    print(f"\nMaps ready: {', '.join(made)}")
    print(f"Settings file: {os.path.join(HERE, 'config.json')} (holds your admin token: keep it private)")
    print("\nNext: run start.bat, open http://127.0.0.1:8640/admin, paste the admin token from config.json,")
    print("and set everything else on the Settings tab. SERVER_SETUP.md has the full steps.")


if __name__ == "__main__":
    main()
