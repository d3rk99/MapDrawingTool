"""MapDrawingTool.exe: the web app without a Python install (built by Tools/build_exe.py).

Its folder has the same layout as the repo: Web\\static (pages, stamps, the map images), Tools\\worlds.json,
and what the owner keeps: Web\\config.json, Submissions, Build\\paper. The exe sets MDT_ROOT to that
folder so the modules find them.
"""
import os
import sys
import traceback


def main():
    frozen = getattr(sys, "frozen", False)
    root = os.path.dirname(sys.executable) if frozen else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.environ["MDT_ROOT"] = root
    os.chdir(root)
    if not frozen:
        sys.path[:0] = [os.path.join(root, "Web"), os.path.join(root, "Tools")]
    first = not os.path.isfile(os.path.join(root, "Web", "config.json"))
    import app  # noqa: E402  (writes Web\config.json with a new admin token on first run)
    host, port = app.CFG.get("host", "127.0.0.1"), int(app.CFG.get("port", 8640))
    print("Map Drawing Tool web app" + (f" {open(os.path.join(root, 'VERSION.txt')).read().strip()}"
                                         if os.path.isfile(os.path.join(root, "VERSION.txt")) else ""))
    print("Maps: " + (", ".join(app.MAPS) or "none (Web\\static\\maps is missing)"))
    if first:
        print(f"\nFirst start: your admin token is in {os.path.join(root, 'Web', 'config.json')} (keep it private).")
    print(f"\nAdmin page:   http://127.0.0.1:{port}/admin")
    print(f"Player page:  http://127.0.0.1:{port}/")
    print("Leave this window open while players use it. Close it to stop.\n")
    app.app.run(host=host, port=port, threaded=True)


if __name__ == "__main__":
    try:
        main()
    except SystemExit as e:
        if e.code not in (0, None):
            print(f"\n{e.code}")
            input("Press Enter to close.")
    except Exception:
        traceback.print_exc()
        input("\nSomething went wrong (above). Press Enter to close.")
