"""Finds the DayZ install (game or dedicated server) and DayZ Tools' ImageToPAA on this PC.

Order: Web/config.json "dayzPath" / "imageToPaa" if set, then every Steam library (Steam's
registry entry and steamapps/libraryfolders.vdf). The game (DayZ) or the server (DayZServer)
both carry the map textures; DLC maps are in their "sakhal" / "nasdara" folders.
"""
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
CONFIG = os.path.join(REPO, "Web", "config.json")
TO_PAA_REL = os.path.join("DayZ Tools", "Bin", "ImageToPAA", "ImageToPAA.exe")


def _config():
    try:
        with open(CONFIG, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def steam_libraries():
    roots = []
    try:
        import winreg
        for hive, key in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"),
                          (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam")):
            try:
                with winreg.OpenKey(hive, key) as k:
                    for name in ("SteamPath", "InstallPath"):
                        try:
                            roots.append(os.path.normpath(winreg.QueryValueEx(k, name)[0]))
                        except OSError:
                            pass
            except OSError:
                pass
    except ImportError:                       # not Windows
        roots.append(os.path.expanduser("~/.steam/steam"))
    libs = []
    for root in roots:
        vdf = os.path.join(root, "steamapps", "libraryfolders.vdf")
        found = [root]
        try:
            with open(vdf, encoding="utf-8", errors="replace") as f:
                found += [p.replace("\\\\", "\\") for p in re.findall(r'"path"\s+"([^"]+)"', f.read())]
        except OSError:
            pass
        for lib in found:
            lib = os.path.normpath(lib)
            if os.path.normcase(lib) not in map(os.path.normcase, libs) and os.path.isdir(os.path.join(lib, "steamapps")):
                libs.append(lib)
    return libs


def dayz_roots():
    """DayZ game and server folders found, configured one first."""
    out = []
    cfg = _config().get("dayzPath")
    if cfg and os.path.isdir(cfg):
        out.append(os.path.normpath(cfg))
    for lib in steam_libraries():
        for name in ("DayZ", "DayZServer"):
            p = os.path.join(lib, "steamapps", "common", name)
            if os.path.isdir(p) and os.path.normcase(p) not in map(os.path.normcase, out):
                out.append(p)
    return out


def image_to_paa():
    cfg = _config().get("imageToPaa")
    if cfg and os.path.isfile(cfg):
        return cfg
    for lib in steam_libraries():
        p = os.path.join(lib, "steamapps", "common", TO_PAA_REL)
        if os.path.isfile(p):
            return p
    raise SystemExit("ImageToPAA not found. Install DayZ Tools from Steam (Library > Tools), "
                     "or set \"imageToPaa\" in Web/config.json to ImageToPAA.exe.")


def find_in_dayz(rel):
    """A file inside the game or server install (worlds.json "source"), or None."""
    for root in dayz_roots():
        p = os.path.join(root, rel)
        if os.path.isfile(p):
            return p
    return None
