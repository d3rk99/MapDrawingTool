"""Builds the web app as MapDrawingTool.exe (no Python needed on the owner's PC) and zips it:
Build/release/MapDrawingTool-WebApp-<version>.zip

Usage: python -I Tools/build_exe.py [path to a Python that has pyinstaller, flask and pillow]

One folder, not one file: a one-file exe unpacks itself to a temp folder on every start and is
flagged by antivirus more often. Next to the exe: Web/static (with the map images from
Web/static/maps, made by make_basemap.py), Tools/worlds.json, SERVER_SETUP.md, VERSION.txt. Never
config.json, submissions, Build/paper or signing tools.
"""
import os
import re
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
MODULES = ["app", "access", "live", "notify", "settings", "compose_layers", "paths", "pbo", "paa"]
WORLDS = ["chernarusplus", "enoch", "sakhal", "nasdara"]


def main():
    py = sys.argv[1] if len(sys.argv) > 1 else sys.executable
    with open(os.path.join(REPO, "Mod", "mod.cpp"), encoding="utf-8") as f:
        version = re.search(r'version\s*=\s*"([^"]+)"', f.read()).group(1)
    maps = os.path.join(REPO, "Web", "static", "maps")
    missing = [w for w in WORLDS for ext in (".png", ".jpg") if not os.path.isfile(os.path.join(maps, w + ext))]
    if missing:
        raise SystemExit(f"Map images missing in {maps}: {missing} (run Tools/make_basemap.py <world>)")
    work = os.path.join(REPO, "Build", "exe")
    shutil.rmtree(work, ignore_errors=True)
    cmd = [py, "-m", "PyInstaller", "--noconfirm", "--clean", "--onedir", "--console", "--name", "MapDrawingTool",
           "--distpath", os.path.join(work, "dist"), "--workpath", os.path.join(work, "work"),
           "--specpath", work, "--paths", os.path.join(REPO, "Web"), "--paths", os.path.join(REPO, "Tools")]
    for m in MODULES:
        cmd += ["--hidden-import", m]
    cmd.append(os.path.join(REPO, "Package", "MapDrawingTool.py"))
    subprocess.run(cmd, check=True)
    out = os.path.join(work, "dist", "MapDrawingTool")
    shutil.copytree(os.path.join(REPO, "Web", "static"), os.path.join(out, "Web", "static"),
                    ignore=shutil.ignore_patterns("test"))
    os.makedirs(os.path.join(out, "Tools"))
    shutil.copy(os.path.join(REPO, "Tools", "worlds.json"), os.path.join(out, "Tools"))
    shutil.copy(os.path.join(REPO, "Package", "SERVER_SETUP.md"), out)
    with open(os.path.join(out, "VERSION.txt"), "w") as f:
        f.write(version + "\n")
    for bad in ("config.json", "Submissions", "paper"):
        for root, dirs, files in os.walk(out):
            if bad in dirs or bad in files:
                raise SystemExit(f"{bad} ended up in the package ({root})")
    zpath = os.path.join(REPO, "Build", "release", f"MapDrawingTool-WebApp-{version}.zip")
    os.makedirs(os.path.dirname(zpath), exist_ok=True)
    count = 0
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for root, _, files in os.walk(out):
            for name in files:
                full = os.path.join(root, name)
                z.write(full, os.path.join("MapDrawingTool", os.path.relpath(full, out)))
                count += 1
    print(f"{zpath}: {count} files, {os.path.getsize(zpath) // (1024 * 1024)} MB")


if __name__ == "__main__":
    main()
