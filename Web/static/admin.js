// Owner approval queue for every map at once. The token stays in this browser (sessionStorage) and
// goes only to this server. Pending is one list (newest first, each card names its map); approved and
// rejected are grouped by map in sections that fold (remembered per browser).
const $ = (id) => document.getElementById(id);
let maps = [], byId = {}, subs = [], tab = "pending";
const bases = {};          // world -> Promise<Image>, loaded when a card first needs it

const token = () => $("token").value.trim();
const adminApi = (path, opts = {}) =>
  MDT.api(path, { ...opts, headers: { ...(opts.headers || {}), "X-Admin-Token": token(), "Content-Type": "application/json" } });

function say(text, error = false) {
  $("status").textContent = text;
  $("status").className = "status " + (error ? "error" : "good");
}

function folded() {
  try { return JSON.parse(localStorage.getItem("mdt.folded") || "{}"); } catch { return {}; }
}
function setFolded(key, value) {
  const f = folded();
  if (value) f[key] = 1; else delete f[key];
  try { localStorage.setItem("mdt.folded", JSON.stringify(f)); } catch {}
}

async function open() {
  try { sessionStorage.setItem("mdt.token", token()); } catch {}
  try {
    subs = await adminApi("/api/admin/submissions");
  } catch (err) {
    return say(err.message.startsWith("403") ? "Wrong admin token" : err.message, true);
  }
  say("");
  $("tabs").hidden = $("buildSection").hidden = false;
  render();
  pollBuild();
}

function render() {
  for (const s of ["pending", "approved", "rejected"])
    $("count-" + s).textContent = `(${subs.filter((x) => x.status === s).length})`;
  for (const b of $("tabs").children) b.classList.toggle("active", b.dataset.status === tab);
  const onSettings = tab === "settings";
  $("settings").hidden = !onSettings;
  $("list").hidden = $("buildSection").hidden = onSettings;
  if (onSettings) return Settings.show($("settings"));
  const list = $("list");
  list.replaceChildren();
  const shown = subs.filter((s) => s.status === tab && byId[s.map]).sort((a, b) => b.created.localeCompare(a.created));
  if (!shown.length) { list.textContent = "Nothing here."; return; }
  if (tab === "pending") {
    const cards = document.createElement("div");
    cards.className = "cards";
    for (const sub of shown) cards.append(card(sub));
    list.append(cards);
    return;
  }
  for (const m of maps) {
    const mine = shown.filter((s) => s.map === m.id);
    if (!mine.length) continue;
    const key = tab + ":" + m.id;
    const sec = document.createElement("details");
    sec.className = "mapgroup";
    sec.open = !folded()[key];
    const sum = document.createElement("summary");
    sum.textContent = `${m.name} · ${mine.length}`;
    sec.append(sum);
    const cards = document.createElement("div");
    cards.className = "cards";
    sec.append(cards);
    // cards (and their previews) are made when a section is first opened
    const fill = () => { if (!cards.childElementCount) for (const sub of mine) cards.append(card(sub)); };
    if (sec.open) fill();
    sec.addEventListener("toggle", () => { setFolded(key, !sec.open); if (sec.open) fill(); });
    list.append(sec);
  }
}

function card(sub) {
  const m = byId[sub.map];
  const el = document.createElement("article");
  el.className = "card";
  const badge = document.createElement("div");
  badge.className = "badge";
  // map, plus personal (only its author sees it) and drawn in game
  badge.textContent = [m.name, sub.scope === "personal" ? "personal" : "", sub.source === "game" ? "drawn in game" : ""]
    .filter(Boolean).join(" · ");
  const c = document.createElement("canvas");
  c.width = c.height = 512;
  el.append(badge, c);
  preview(c, sub, m);
  const h = document.createElement("h3");
  h.textContent = sub.kind === "marker" ? `📍 ${sub.text}` : (sub.title || "Untitled drawing");
  const meta = document.createElement("div");
  meta.className = "meta";
  const where = sub.kind === "marker" ? `X ${Math.round(sub.x)} Z ${Math.round(sub.z)}`
    : `${sub.px[2] - sub.px[0]}×${sub.px[3] - sub.px[1]} px on the map texture`;
  const life = !sub.expires ? "" : sub.expires < new Date().toISOString() ? " · expired"
    : ` · until ${sub.expires.replace("T", " ").slice(0, 16)}`;
  meta.textContent = `${sub.author}${sub.group ? " · " + sub.group : ""} · ${sub.created.replace("T", " ").slice(0, 16)} · ${where}${life}`;
  // who sent it: a Steam profile link, a Discord user id, or (anyone may submit) an address
  if (sub.by) {
    const [kind, id] = [sub.by.slice(0, sub.by.indexOf(":")), sub.by.slice(sub.by.indexOf(":") + 1)];
    const acct = document.createElement(kind === "steam" ? "a" : "span");
    acct.className = "account-id";
    acct.textContent = kind === "steam" ? `Steam ${id}` : kind === "discord" ? `Discord ${id}` : `address ${id}`;
    if (kind === "steam") { acct.href = `https://steamcommunity.com/profiles/${id}`; acct.target = "_blank"; acct.rel = "noopener"; }
    meta.append(document.createElement("br"), acct);
  }
  const row = document.createElement("div");
  row.className = "row";
  if (sub.status !== "approved") row.append(button("Approve", "ok", () => review(sub, "approved")));
  if (sub.status !== "rejected") row.append(button(sub.status === "approved" ? "Revoke" : "Reject", "bad", () => review(sub, "rejected")));
  if (sub.status !== "pending") row.append(button("Back to pending", "", () => review(sub, "pending")));
  if (sub.status === "rejected") row.append(button("Delete", "bad", () => remove(sub)));
  el.append(h, meta, row);
  return el;
}

function button(text, cls, onclick) {
  const b = document.createElement("button");
  b.textContent = text; b.className = cls; b.onclick = onclick;
  return b;
}

function base(m) {
  if (!bases[m.world]) bases[m.world] = MDT.loadImage(m.basemap);
  return bases[m.world];
}

const worldToTex = (m, wx, wz) =>
  [m.originPx[0] + wx * m.pxPerMetre[0], m.originPx[1] + (m.size - wz) * m.pxPerMetre[1]];

// Shows the submission on its map's texture with some surrounding context.
async function preview(c, sub, m) {
  const x = c.getContext("2d");
  x.fillStyle = "#000"; x.fillRect(0, 0, c.width, c.height);
  let x0, y0, x1, y1;
  if (sub.kind === "marker") {
    const [tx, ty] = worldToTex(m, sub.x, sub.z);
    [x0, y0, x1, y1] = [tx - 80, ty - 80, tx + 80, ty + 80];
  } else {
    [x0, y0, x1, y1] = sub.px;
    const pad = Math.max(x1 - x0, y1 - y0) * 0.25 + 16;
    x0 -= pad; y0 -= pad; x1 += pad; y1 += pad;
  }
  const span = Math.max(x1 - x0, y1 - y0);              // square view centred on the submission
  x0 = (x0 + x1) / 2 - span / 2; y0 = (y0 + y1) / 2 - span / 2;
  const k = c.width / span;                              // canvas px per texture px
  x.imageSmoothingEnabled = k < 2;
  try { x.drawImage(await base(m), x0, y0, span, span, 0, 0, c.width, c.height); } catch {}
  const toC = (tx, ty) => [(tx - x0) * k, (ty - y0) * k];
  if (sub.kind === "marker") {
    MDT.drawMarker(x, ...toC(...worldToTex(m, sub.x, sub.z)), sub);
  } else {
    try {
      const res = await fetch(`/api/${m.id}/image/${sub.id}`, { headers: { "X-Admin-Token": token() } });
      if (!res.ok) throw new Error();
      const url = URL.createObjectURL(await res.blob());
      const img = await MDT.loadImage(url);
      URL.revokeObjectURL(url);
      const [px0, py0, px1, py1] = sub.px;
      const [sx, sy] = toC(px0, py0);
      x.drawImage(img, sx, sy, (px1 - px0) * k, (py1 - py0) * k);
      x.strokeStyle = "#c9a24a88"; x.setLineDash([4, 4]);
      x.strokeRect(sx, sy, (px1 - px0) * k, (py1 - py0) * k);
    } catch { x.fillStyle = "#fff"; x.fillText("Image not available", 10, 20); }
  }
}

async function review(sub, status) {
  try {
    await adminApi(`/api/admin/${sub.map}/review/${sub.id}`, { method: "POST", body: JSON.stringify({ status }) });
    sub.status = status;
    say(`Saved (${byId[sub.map].name}). It goes live in game within a minute or two.`);
    render();
  } catch (err) { say(err.message, true); }
}

async function remove(sub) {
  const what = sub.kind === "marker" ? `marker "${sub.text}"` : `"${sub.title || "Untitled drawing"}"`;
  if (!confirm(`Delete ${what} by ${sub.author} for good? This cannot be undone.`)) return;
  try {
    await adminApi(`/api/admin/${sub.map}/submissions/${sub.id}`, { method: "DELETE" });
    subs = subs.filter((s) => s !== sub);
    say(`Deleted (${byId[sub.map].name}).`);
    render();
  } catch (err) { say(err.message, true); }
}

let pollTimer = null;
async function pollBuild() {
  clearTimeout(pollTimer);
  try {
    const st = await adminApi("/api/admin/build");
    if (!st.available) { $("buildSection").classList.add("gone"); return; }
    $("build").disabled = st.running;
    $("buildLog").textContent = st.log ? `${st.running ? "Running" : st.ok ? "OK" : "FAILED"} ${st.finished || ""}\n\n${st.log}` : "No build yet.";
    if (st.running) pollTimer = setTimeout(pollBuild, 3000);
  } catch {}
}

$("build").onclick = async () => {
  try { await adminApi(`/api/admin/build?map=${encodeURIComponent($("buildMap").value)}`, { method: "POST" }); pollBuild(); }
  catch (err) { say(err.message, true); }
};
$("load").onclick = open;
$("token").addEventListener("keydown", (e) => { if (e.key === "Enter") open(); });
for (const b of $("tabs").children) b.onclick = () => { tab = b.dataset.status; render(); };

(async function init() {
  try { $("token").value = sessionStorage.getItem("mdt.token") || ""; } catch {}
  await MDT.loadIcons();
  maps = await MDT.api("/api/maps");      // maps: one set of drawings each, see Web/app.py
  byId = Object.fromEntries(maps.map((m) => [m.id, m]));
  for (const m of maps) $("buildMap").append(new Option(m.name, m.id));
  if (token()) open();
})().catch((err) => say(err.message, true));
