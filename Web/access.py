"""Who may submit: player sign-in (Steam, Discord), per-map access rules and rate limits.

Web/config.json:
  "publicUrl": "https://map.example.com"   address players use; sign-in returns there (else the
                                           address of the request)
  "login": {
    "steam": true,                         Steam sign-in (OpenID, no key needed)
    "steamApiKey": "",                     recommended: Steam names come from the Web API. Without it they
                                           come from the public profile, which Steam often throttles; the
                                           name then shows as "Steam <id>" and is retried every 2 minutes
    "discord": {"clientId": "", "clientSecret": "", "guildId": ""}
                                           Discord sign-in (an application from
                                           discord.com/developers, redirect <publicUrl>/login/discord/return).
                                           With guildId it also reads the player's roles in that server.
  },
  "submitters": {                          default for every map; a map can override any key, in its
                                           "maps" entry or in "mapSubmitters": {"<map id>": {...}}
    "who": "anyone" | "signed-in" | "listed",
    "steamIds": ["7656..."],               "listed": these SteamIDs may submit
    "discordRoles": ["1234..."],           "listed": members with any of these role IDs may submit
    "perHour": 10,                         submissions per player (or per address when anyone may) per hour
    "maxPending": 20,                      waiting for approval per player at once
    "allowGlobal": true,                   drawings everyone on the server sees
    "allowPersonal": true,                 drawings only their author sees (needs a SteamID: Steam
                                           sign-in, or drawn in game)
    "approvePersonal": false,              personal drawings wait for approval too
    "maxDays": 0, "defaultDays": 0,        lifespan in days (0 = no limit / permanent)
    "ingame": true                         players with a map and a pen can draw in game
  }

All of this can be set on the admin page (Settings), which writes config.json (settings.py).

Discord roles need no bot: the "guilds.members.read" sign-in scope lets the player's own login read
their roles in guildId. Role IDs: Discord settings > Advanced > Developer Mode, then right-click
the role > Copy Role ID. The roles are read at sign-in, so a role change applies on the next sign-in.
"""
import datetime
import hashlib
import hmac
import html
import json
import re
import secrets
import urllib.parse
import urllib.request

from flask import Blueprint, abort, has_request_context, jsonify, redirect, request, session

bp = Blueprint("access", __name__)
CFG = {}
MAPS = {}
SUBS_OF = None                   # callable: map id -> that map's submissions

DEFAULT_RULES = {"who": "anyone", "steamIds": [], "discordRoles": [], "perHour": 10, "maxPending": 20,
                 "allowGlobal": True, "allowPersonal": True, "approvePersonal": False, "maxDays": 0,
                 "defaultDays": 0, "ingame": True}
LIFESPANS = (1, 3, 7, 14, 30, 90, 0)       # days offered to players; 0 = permanent
STEAM_OPENID = "https://steamcommunity.com/openid/login"
DISCORD_API = "https://discord.com/api/v10"


def init(app, cfg, maps, subs_of):
    """Keeps the app's own config and maps dicts, so settings saved from /admin apply at once."""
    global CFG, MAPS, SUBS_OF
    CFG, MAPS, SUBS_OF = cfg, maps, subs_of
    app.secret_key = cfg["sessionSecret"]
    app.config.update(SESSION_COOKIE_HTTPONLY=True, SESSION_COOKIE_SAMESITE="Lax",
                      SESSION_COOKIE_SECURE=public_url().startswith("https://"),
                      PERMANENT_SESSION_LIFETIME=datetime.timedelta(days=30))
    app.register_blueprint(bp)


def public_url():
    url = CFG.get("publicUrl") or ""
    if not url and has_request_context():
        url = request.host_url
    return url.rstrip("/")


def providers():
    login = CFG.get("login") or {}
    out = []
    if login.get("steam"):
        out.append("steam")
    d = login.get("discord") or {}
    if d.get("clientId") and d.get("clientSecret"):
        out.append("discord")
    return out


def rules(map_id):
    """Default rules, then the map's own: "submitters" in its "maps" entry, then "mapSubmitters"
    (what the admin page's Settings saves)."""
    r = dict(DEFAULT_RULES)
    r.update(CFG.get("submitters") or {})
    r.update(MAPS.get(map_id, {}).get("submitters") or {})
    r.update((CFG.get("mapSubmitters") or {}).get(map_id) or {})
    return r


def user():
    return session.get("user")


def lifespans(r):
    """The lifespans (days, 0 = permanent) players may pick under these rules."""
    top = int(r.get("maxDays") or 0)
    out = [d for d in LIFESPANS if (d and (not top or d <= top)) or (not d and not top)]
    if top and top not in out:
        out.append(top)
    default = int(r.get("defaultDays") or 0)
    if default not in out and (not top or 0 < default <= top):
        out.append(default)
    return sorted(out, key=lambda d: d or 10 ** 6)


def options(map_id):
    """What players may choose on this map (sent to the editor and, in the manifest, to the game)."""
    r = rules(map_id)
    spans = lifespans(r)
    default = int(r.get("defaultDays") or 0)
    return {"allowGlobal": bool(r["allowGlobal"]), "allowPersonal": bool(r["allowPersonal"]),
            "approvePersonal": bool(r["approvePersonal"]), "ingame": bool(r["ingame"]), "lifespans": spans,
            "defaultDays": default if default in spans else spans[0]}


def personal_key(map_id, steam_id):
    """Unguessable part of a player's personal download links (SteamIDs are public)."""
    return hmac.new(CFG["sessionSecret"].encode(), f"{map_id}:{steam_id}".encode(), hashlib.sha256).hexdigest()[:24]


def check(map_id, u="session", ip=None):
    """(allowed, reason, identity) for submitting on this map now. identity is "steam:<id>",
    "discord:<id>" or "ip:<address>". u: the signed-in user (default: this request's session), or a
    user dict for a submission made in game (the game server vouches for the SteamID)."""
    r = rules(map_id)
    if u == "session":
        u = user()
    if r["who"] != "anyone" and not u:
        return False, "Sign in to submit on this map.", None
    if r["who"] == "listed":
        ok = (u["provider"] == "steam" and u["id"] in [str(s) for s in r["steamIds"]]) or \
             (u["provider"] == "discord" and set(u.get("roles", [])) & {str(x) for x in r["discordRoles"]})
        if not ok:
            return False, "Your account is not on this map's list of submitters. Ask the server owner.", None
    ident = f"{u['provider']}:{u['id']}" if u else "ip:" + (ip or (request.remote_addr if has_request_context() else "?"))
    since = (datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(hours=1)).strftime("%Y-%m-%dT%H:%M:%SZ")
    mine = [s for m in MAPS for s in SUBS_OF(m) if s.get("by") == ident]
    if sum(1 for s in mine if s.get("created", "") >= since) >= int(r["perHour"]):
        return False, f"Limit reached: {r['perHour']} submissions an hour. Try again later.", ident
    if sum(1 for s in mine if s.get("status") == "pending") >= int(r["maxPending"]):
        return False, f"You have {r['maxPending']} submissions waiting for approval already.", ident
    return True, "", ident


@bp.get("/api/me")
def api_me():
    _retry_steam_name()
    map_id = request.args.get("map", "")
    ok, why, _ = check(map_id) if map_id in MAPS else (False, "Unknown map", None)
    r = rules(map_id) if map_id in MAPS else DEFAULT_RULES
    return jsonify(user=user(), providers=providers(), canSubmit=ok, why=why, who=r["who"],
                   options=options(map_id) if map_id in MAPS else None)


@bp.post("/logout")
def logout():
    session.clear()
    return jsonify(ok=True)


def _sign_in(u):
    nxt = session.get("next", "/")
    session.clear()
    session.permanent = True
    session["user"] = u
    return redirect(nxt if nxt.startswith("/") and not nxt.startswith("//") else "/")


def _back():
    nxt = session.pop("next", "/")
    return redirect(nxt if nxt.startswith("/") and not nxt.startswith("//") else "/")


def _remember_next():
    nxt = request.args.get("next", "/")
    session["next"] = nxt if nxt.startswith("/") and not nxt.startswith("//") else "/"


def _get_json(url, headers=None, data=None):
    req = urllib.request.Request(url, data=data, headers={"User-Agent": "MapDrawingTool", **(headers or {})})
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.loads(r.read())


# ---------------------------------------------------------------- Steam (OpenID 2.0)

@bp.get("/login/steam")
def login_steam():
    if "steam" not in providers():
        abort(404)
    _remember_next()
    base = public_url()
    params = {"openid.ns": "http://specs.openid.net/auth/2.0", "openid.mode": "checkid_setup",
              "openid.return_to": base + "/login/steam/return", "openid.realm": base + "/",
              "openid.identity": "http://specs.openid.net/auth/2.0/identifier_select",
              "openid.claimed_id": "http://specs.openid.net/auth/2.0/identifier_select"}
    return redirect(STEAM_OPENID + "?" + urllib.parse.urlencode(params))


@bp.get("/login/steam/return")
def login_steam_return():
    if "steam" not in providers():
        abort(404)
    args = request.args.to_dict()
    if args.get("openid.return_to", "").split("?")[0] != public_url() + "/login/steam/return":
        abort(400)
    # Steam confirms the signed answer itself, so nothing in the URL is trusted on its own
    args["openid.mode"] = "check_authentication"
    req = urllib.request.Request(STEAM_OPENID, data=urllib.parse.urlencode(args).encode(),
                                 headers={"User-Agent": "MapDrawingTool"})
    with urllib.request.urlopen(req, timeout=15) as r:
        if "is_valid:true" not in r.read().decode():
            abort(403)
    m = re.fullmatch(r"https://steamcommunity\.com/openid/id/(\d{17})", args.get("openid.claimed_id", ""))
    if not m:
        abort(403)
    sid = m.group(1)
    name = steam_name(sid)
    return _sign_in({"provider": "steam", "id": sid, "name": name or "Steam " + sid, "named": bool(name)})


def steam_name(sid):
    """The Steam display name, or "" (private profile, or Steam busy: it answers 429 now and then)."""
    key = (CFG.get("login") or {}).get("steamApiKey")
    try:
        if key:
            p = _get_json("https://api.steampowered.com/ISteamUser/GetPlayerSummaries/v2/?"
                          + urllib.parse.urlencode({"key": key, "steamids": sid}))["response"]["players"]
            return p[0]["personaname"][:32] if p else ""
        # without a key: the public profile's XML carries the name. Steam rate-limits it (429)
        # whenever it likes, so with many players set steamApiKey (the supported way).
        req = urllib.request.Request(f"https://steamcommunity.com/profiles/{sid}/?xml=1",
                                     headers={"User-Agent": "MapDrawingTool"})
        with urllib.request.urlopen(req, timeout=10) as r:
            m = re.search(r"<steamID><!\[CDATA\[(.*?)\]\]></steamID>", r.read().decode("utf-8", "replace"))
        return html.unescape(m.group(1).strip())[:32] if m else ""
    except Exception:
        return ""


def _retry_steam_name():
    """Signed in while Steam would not give the name: try again, at most every 2 minutes."""
    u = user()
    if not u or u["provider"] != "steam" or u.get("named", u["name"] != "Steam " + u["id"]):
        return
    now = datetime.datetime.now(datetime.timezone.utc).timestamp()
    if now - session.get("nameTried", 0) < 120:
        return
    session["nameTried"] = now
    name = steam_name(u["id"])
    if name:
        session["user"] = {**u, "name": name, "named": True}


# ---------------------------------------------------------------- Discord (OAuth2)

def _discord():
    return (CFG.get("login") or {}).get("discord") or {}


@bp.get("/login/discord")
def login_discord():
    if "discord" not in providers():
        abort(404)
    _remember_next()
    state = secrets.token_urlsafe(16)
    session["discordState"] = state
    d = _discord()
    scope = "identify guilds.members.read" if d.get("guildId") else "identify"
    params = {"client_id": d["clientId"], "response_type": "code", "scope": scope, "state": state,
              "redirect_uri": public_url() + "/login/discord/return", "prompt": "none"}
    return redirect("https://discord.com/oauth2/authorize?" + urllib.parse.urlencode(params))


@bp.get("/login/discord/return")
def login_discord_return():
    if "discord" not in providers():
        abort(404)
    if not request.args.get("state") or request.args.get("state") != session.pop("discordState", None):
        abort(403)
    if "code" not in request.args:
        return _back()                                   # the player cancelled
    d = _discord()
    token = _get_json(DISCORD_API + "/oauth2/token", headers={"Content-Type": "application/x-www-form-urlencoded"},
                      data=urllib.parse.urlencode({
                          "client_id": d["clientId"], "client_secret": d["clientSecret"],
                          "grant_type": "authorization_code", "code": request.args["code"],
                          "redirect_uri": public_url() + "/login/discord/return"}).encode())
    auth = {"Authorization": "Bearer " + token["access_token"]}
    me = _get_json(DISCORD_API + "/users/@me", headers=auth)
    u = {"provider": "discord", "id": me["id"], "name": (me.get("global_name") or me["username"])[:32], "roles": []}
    if d.get("guildId"):
        try:
            member = _get_json(f"{DISCORD_API}/users/@me/guilds/{d['guildId']}/member", headers=auth)
            u["roles"] = [str(r) for r in member.get("roles", [])]
            u["name"] = (member.get("nick") or u["name"])[:32]
        except Exception:
            pass                                          # not in the server: no roles
    return _sign_in(u)
