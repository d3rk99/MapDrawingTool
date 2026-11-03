"""Notifications: webhooks called when a submission arrives, is approved, rejected or deleted.

Web/config.json "webhooks": [
  {"url": "https://discord.com/api/webhooks/...", "format": "discord",
   "events": ["submitted"], "maps": []},
  {"url": "https://example.com/hook", "format": "json", "secret": "shared secret",
   "events": ["submitted", "approved", "rejected", "deleted"], "maps": ["pvp"]}
]
  format  "discord": a message with a link to the approval page (Discord: channel settings >
          Integrations > Webhooks > New Webhook > Copy Webhook URL). Slack and others that
          take Discord-style webhooks work too.
          "json": {"event", "map", "mapName", "submission", "adminUrl"} posted as JSON. With a
          "secret", the X-MDT-Signature header is sha256=<HMAC-SHA256 of the body>.
  events  which of submitted, approved, rejected, deleted (default: submitted)
  maps    map ids it is for (default: all)

Sent in the background with a 10 s timeout; failures are printed and not retried.
"""
import hashlib
import hmac
import json
import threading
import urllib.request

COLORS = {"submitted": 0xC9A24A, "approved": 0x3FA34D, "rejected": 0xC0392B, "deleted": 0x7F8C8D, "withdrawn": 0x95A5A6,
          "test": 0x3498DB}
VERBS = {"submitted": "New submission waiting for approval", "approved": "Approved", "rejected": "Rejected",
         "deleted": "Deleted", "withdrawn": "Removed by its author", "test": "Test from the admin page"}


def send(cfg, event, map_info, sub, admin_url):
    hooks = [h for h in cfg.get("webhooks") or []
             if h.get("url") and event in (h.get("events") or ["submitted"])
             and (not h.get("maps") or map_info["id"] in h["maps"])]
    for h in hooks:
        threading.Thread(target=_post, args=(h, event, map_info, sub, admin_url), daemon=True).start()
    return len(hooks)


def _post(hook, event, map_info, sub, admin_url):
    ok, why = post_now(hook, event, map_info, sub, admin_url)
    if not ok:
        print(f"[webhook] {event} to {hook['url'][:40]}...: {why}")


def post_now(hook, event, map_info, sub, admin_url):
    """Sends one webhook now. (ok, reason)."""
    if hook.get("format") == "discord":
        what = f"📍 Marker \"{sub.get('text')}\"" if sub.get("kind") == "marker" else f"🖌️ {sub.get('title') or 'Untitled drawing'}"
        by = sub.get("author", "?") + (f" ({sub['group']})" if sub.get("group") else "")
        body = {"username": "Map Drawing Tool", "embeds": [{
            "title": f"{VERBS[event]}: {what}"[:256], "url": admin_url, "color": COLORS[event],
            "fields": [{"name": "Map", "value": map_info["name"], "inline": True},
                       {"name": "By", "value": by[:100], "inline": True}],
            "footer": {"text": "Open the approval page to review"} if event == "submitted" else None}]}
        if body["embeds"][0]["footer"] is None:
            del body["embeds"][0]["footer"]
    else:
        body = {"event": event, "map": map_info["id"], "mapName": map_info["name"], "submission": sub,
                "adminUrl": admin_url}
    data = json.dumps(body).encode()
    headers = {"Content-Type": "application/json", "User-Agent": "MapDrawingTool"}
    if hook.get("secret"):
        headers["X-MDT-Signature"] = "sha256=" + hmac.new(hook["secret"].encode(), data, hashlib.sha256).hexdigest()
    try:
        with urllib.request.urlopen(urllib.request.Request(hook["url"], data=data, headers=headers), timeout=10):
            return True, ""
    except Exception as e:
        return False, str(e)
