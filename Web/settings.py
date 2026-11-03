"""Owner settings on the admin page: sign-in, who may submit, webhooks, public address.

GET  /api/admin/settings             the settings, secrets shown only as set / not set
PUT  /api/admin/settings             saves them to config.json and applies them at once
GET  /api/admin/discord/roles        the Discord server's roles (needs the optional bot token)
POST /api/admin/webhooks/test        sends a test message to one webhook

Secrets (Steam API key, Discord client secret and bot token, webhook URLs and secrets) never
leave the server: a field left empty keeps the saved value, "clear" removes it.
The bot token is optional and only lists role names so the owner can tick them; sign-in and
role checks work without it (access.py).
"""
import json
import os
import re
import secrets
import threading
import urllib.request

from flask import Blueprint, jsonify, request

import notify

bp = Blueprint("settings", __name__)
S = {}                       # cfg, maps, path, require_admin, app, public_url
save_lock = threading.Lock()
WHO = ("anyone", "signed-in", "listed")
EVENTS = ("submitted", "approved", "rejected", "deleted", "withdrawn")


def init(app, cfg, maps, config_path, require_admin, public_url):
    S.update(cfg=cfg, maps=maps, path=config_path, require_admin=require_admin, app=app, public_url=public_url)
    app.register_blueprint(bp)


def access_defaults():
    import access
    return dict(access.DEFAULT_RULES)


def _hint(url):
    """A webhook URL shown without its token: host and the last 4 characters."""
    m = re.match(r"https?://([^/]+)", url or "")
    return f"{m.group(1) if m else '?'}/…{url[-4:]}" if url else ""


RULE_KEYS = ("who", "steamIds", "discordRoles", "perHour", "maxPending", "allowGlobal", "allowPersonal",
             "approvePersonal", "maxDays", "defaultDays", "ingame")
FLAGS = ("allowGlobal", "allowPersonal", "approvePersonal", "ingame")


def _rules_out(r):
    return {k: r[k] for k in RULE_KEYS if k in r}


@bp.get("/api/admin/settings")
def get_settings():
    S["require_admin"]()
    cfg = S["cfg"]
    login = cfg.get("login") or {}
    d = login.get("discord") or {}
    return jsonify(
        publicUrl=cfg.get("publicUrl", ""),
        login={"steam": bool(login.get("steam")), "steamApiKeySet": bool(login.get("steamApiKey")),
               "discord": {"clientId": d.get("clientId", ""), "clientSecretSet": bool(d.get("clientSecret")),
                           "guildId": d.get("guildId", ""), "botTokenSet": bool(d.get("botToken"))},
               "discordRedirect": (S["public_url"]() or "<publicUrl>") + "/login/discord/return"},
        submitters=_rules_out({**access_defaults(), **(cfg.get("submitters") or {})}),
        serverKey=cfg.get("serverKey", ""),
        maps=[{"id": m["id"], "name": m["name"], "world": m["world"],
               "override": _rules_out((cfg.get("mapSubmitters") or {}).get(m["id"]) or {}) or None}
              for m in S["maps"].values()],
        webhooks=[{"id": h.get("id", ""), "urlHint": _hint(h.get("url")), "format": h.get("format", "json"),
                   "events": h.get("events") or ["submitted"], "maps": h.get("maps") or [],
                   "secretSet": bool(h.get("secret"))} for h in cfg.get("webhooks") or []])


class Bad(ValueError):
    pass


def _ids(values, pattern, what):
    out = []
    for v in values or []:
        v = str(v).strip()
        m = re.search(pattern, v)
        if not m:
            raise Bad(f"Not a {what}: {v[:40]}")
        if m.group(1) not in out:
            out.append(m.group(1))
    return out


def _rules_in(r, partial):
    """Checks one set of submitter rules. partial: a map override may leave keys out; the defaults
    may not, so missing keys there take the built-in default."""
    if not partial:
        r = {**access_defaults(), **r}
    out = {}
    if "who" in r or not partial:
        if r.get("who") not in WHO:
            raise Bad("Who may submit: anyone, signed-in or listed")
        out["who"] = r["who"]
    if "steamIds" in r or not partial:
        # a SteamID64, or a profile link that contains one
        out["steamIds"] = _ids(r.get("steamIds"), r"(7656\d{13})", "SteamID (17 digits, starts 7656)")
    if "discordRoles" in r or not partial:
        out["discordRoles"] = _ids(r.get("discordRoles"), r"^(\d{15,21})$", "Discord role ID")
    for key, label, low, high in (("perHour", "Submissions per hour", 1, 1000), ("maxPending", "Waiting at once", 1, 1000),
                                  ("maxDays", "Longest lifespan (days)", 0, 3650),
                                  ("defaultDays", "Default lifespan (days)", 0, 3650)):
        if key in r or not partial:
            try:
                n = int(r.get(key))
            except (TypeError, ValueError):
                raise Bad(f"{label}: a number")
            if not low <= n <= high:
                raise Bad(f"{label}: {low} to {high}")
            out[key] = n
    for key in FLAGS:
        if key in r or not partial:
            out[key] = bool(r.get(key))
    if out.get("maxDays") and (not out.get("defaultDays") or out["defaultDays"] > out["maxDays"]) and "defaultDays" in out:
        raise Bad("Default lifespan: 1 day up to the longest lifespan (0 = permanent is not allowed with a limit)")
    if "allowGlobal" in out and "allowPersonal" in out and not (out["allowGlobal"] or out["allowPersonal"]):
        raise Bad("Allow global drawings, personal drawings or both")
    return out


def _url(value, label, trim=True):
    value = str(value or "").strip()
    if trim:
        value = value.rstrip("/")
    if value and not re.fullmatch(r"https?://[^\s/]+(/[^\s]*)?", value):
        raise Bad(f"{label}: a web address starting with http:// or https://")
    return value


@bp.put("/api/admin/settings")
def put_settings():
    S["require_admin"]()
    body = request.get_json(silent=True) or {}
    cfg = S["cfg"]
    try:
        with save_lock:
            new = json.loads(json.dumps(cfg))               # work on a copy; apply only if all of it is valid
            new["publicUrl"] = _url(body.get("publicUrl"), "Public address")

            login = new.setdefault("login", {})
            bl = body.get("login") or {}
            clear = set(bl.get("clear") or [])
            login["steam"] = bool(bl.get("steam"))
            if bl.get("steamApiKey"):
                if not re.fullmatch(r"[0-9A-Fa-f]{32}", bl["steamApiKey"].strip()):
                    raise Bad("Steam API key: 32 letters and digits from steamcommunity.com/dev/apikey")
                login["steamApiKey"] = bl["steamApiKey"].strip()
            if "steamApiKey" in clear:
                login["steamApiKey"] = ""
            d = login.setdefault("discord", {})
            bd = bl.get("discord") or {}
            d["clientId"] = _ids([bd["clientId"]], r"^(\d{15,21})$", "Discord application ID")[0] if bd.get("clientId") else ""
            d["guildId"] = _ids([bd["guildId"]], r"^(\d{15,21})$", "Discord server ID")[0] if bd.get("guildId") else ""
            for field, key in (("clientSecret", "discordSecret"), ("botToken", "botToken")):
                if bd.get(field):
                    d[field] = bd[field].strip()
                if key in clear:
                    d[field] = ""

            new["submitters"] = _rules_in(body.get("submitters") or {}, partial=False)
            overrides = {}
            for mid, r in (body.get("mapSubmitters") or {}).items():
                if mid not in S["maps"]:
                    raise Bad(f"Unknown map {mid}")
                if r:
                    overrides[mid] = _rules_in(r, partial=True)
            new["mapSubmitters"] = overrides

            old_hooks = {h.get("id"): h for h in cfg.get("webhooks") or [] if h.get("id")}
            hooks = []
            for h in body.get("webhooks") or []:
                prev = old_hooks.get(h.get("id")) or {}
                url = _url(h.get("url"), "Webhook address", trim=False) or prev.get("url", "")
                if not url:
                    raise Bad("A webhook needs its address")
                events = [e for e in h.get("events") or [] if e in EVENTS]
                if not events:
                    raise Bad("A webhook needs at least one event")
                fmt = h.get("format") if h.get("format") in ("discord", "json") else "json"
                hook = {"id": prev.get("id") or secrets.token_hex(4), "url": url, "format": fmt, "events": events,
                        "maps": [m for m in h.get("maps") or [] if m in S["maps"]]}
                secret = h.get("secret") if h.get("secret") else ("" if h.get("clearSecret") else prev.get("secret", ""))
                if secret:
                    hook["secret"] = secret
                hooks.append(hook)
            new["webhooks"] = hooks
            if body.get("newServerKey"):
                new["serverKey"] = secrets.token_urlsafe(18)

            tmp = S["path"] + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(new, f, indent=1)
            os.replace(tmp, S["path"])
            cfg.clear()
            cfg.update(new)                                  # live: access.py and notify.py read this dict
            S["app"].config["SESSION_COOKIE_SECURE"] = cfg.get("publicUrl", "").startswith("https://")
    except Bad as e:
        return jsonify(error=str(e)), 400
    return get_settings()


@bp.get("/api/admin/discord/roles")
def discord_roles():
    """The roles of the configured Discord server, for ticking instead of typing IDs."""
    S["require_admin"]()
    d = (S["cfg"].get("login") or {}).get("discord") or {}
    if not d.get("botToken") or not d.get("guildId"):
        return jsonify(error="Needs the Discord server ID and a bot token"), 400
    req = urllib.request.Request(f"https://discord.com/api/v10/guilds/{d['guildId']}/roles",
                                 headers={"Authorization": "Bot " + d["botToken"], "User-Agent": "MapDrawingTool"})
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            roles = json.loads(r.read())
    except urllib.error.HTTPError as e:
        why = {401: "the bot token is wrong", 403: "the bot is not in that server",
               404: "no server with that ID"}.get(e.code, f"Discord said {e.code}")
        return jsonify(error=f"Could not read roles: {why}"), 502
    except Exception as e:
        return jsonify(error=f"Could not reach Discord: {e}"), 502
    roles = [r for r in roles if r["id"] != d["guildId"]]           # @everyone has the server's ID
    roles.sort(key=lambda r: -r.get("position", 0))
    return jsonify([{"id": r["id"], "name": r["name"], "color": f"#{r.get('color', 0):06x}"} for r in roles])


@bp.post("/api/admin/webhooks/test")
def webhook_test():
    S["require_admin"]()
    hid = (request.get_json(silent=True) or {}).get("id")
    hook = next((h for h in S["cfg"].get("webhooks") or [] if h.get("id") == hid), None)
    if not hook:
        return jsonify(error="Save the webhook first"), 400
    m = next(iter(S["maps"].values()))
    sample = {"id": "test", "kind": "marker", "status": "pending", "author": "Map Drawing Tool", "text": "Test message",
              "title": "", "created": ""}
    ok, why = notify.post_now(hook, "test", m, sample, S["public_url"]() + "/admin")
    return (jsonify(ok=True), 200) if ok else (jsonify(error=why), 502)
