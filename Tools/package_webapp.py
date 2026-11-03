"""Builds the web app package server owners download: Build/release/MapDrawingTool-WebApp-<version>.zip

Usage: python -I Tools/package_webapp.py

Holds what an owner runs (Web, the tools it uses, setup.bat, start.bat, SERVER_SETUP.md), never
config.json, submissions, map images (made from the owner's own game install by setup.bat) or
the build and signing tools.
"""
import os
import re
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

FILES = ["Web/app.py", "Web/access.py", "Web/live.py", "Web/notify.py", "Web/settings.py", "Web/setup.py",
         "Web/requirements.txt", "Tools/compose_layers.py", "Tools/make_basemap.py", "Tools/pbo.py",
         "Tools/paths.py", "Tools/worlds.json"]
DIRS = ["Web/static"]
SKIP_DIRS = {"Web/static/maps", "Web/static/test"}
ROOT_FILES = {"Package/setup.bat": "setup.bat", "Package/start.bat": "start.bat",
              "Package/SERVER_SETUP.md": "SERVER_SETUP.md"}


def main():
    with open(os.path.join(REPO, "Mod", "mod.cpp"), encoding="utf-8") as f:
        version = re.search(r'version\s*=\s*"([^"]+)"', f.read()).group(1)
    out_dir = os.path.join(REPO, "Build", "release")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, f"MapDrawingTool-WebApp-{version}.zip")
    top = "MapDrawingTool"
    entries = {rel: rel for rel in FILES}
    entries.update(ROOT_FILES)
    for d in DIRS:
        for root, dirs, files in os.walk(os.path.join(REPO, d)):
            rel_root = os.path.relpath(root, REPO).replace(os.sep, "/")
            dirs[:] = [x for x in dirs if f"{rel_root}/{x}" not in SKIP_DIRS]
            for name in files:
                entries[f"{rel_root}/{name}"] = f"{rel_root}/{name}"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for src, dst in sorted(entries.items()):
            z.write(os.path.join(REPO, src), f"{top}/{dst}")
        z.writestr(f"{top}/VERSION.txt", version + "\n")
    print(f"{out}: {len(entries) + 1} files, {os.path.getsize(out) // 1024} KB")


if __name__ == "__main__":
    main()
