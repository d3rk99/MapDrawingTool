// Player editor. Players paint straight onto the world's map texture (karta_co, the paper map in
// hand and on boards), so everything is in texture pixels (y down). World metres (x east, z north)
// are only used for the coordinate readout and for live markers, via the texture's calibration.
// The drawing lives in an off-screen "draft" canvas the size of the texture and is rebuilt from
// the list of operations, which is what makes undo/redo cheap.
const $ = (id) => document.getElementById(id);
const view = $("view"), ctx = view.getContext("2d");

const state = {
  world: null, size: 0, base: null, tex: null,
  scale: 1, ox: 0, oy: 0,                 // screen px per texture px, screen position of texture (0, 0)
  tool: "brush", ops: [], redo: [], current: null,
  draft: null, dctx: null,
  approved: [], approvedImages: new Map(),
  stamps: [], stamp: null, hover: null,
  icon: 2, pendingMarker: null,
};

// ------------------------------------------------------------- coordinates
const toScreen = (tx, ty) => [tx * state.scale + state.ox, ty * state.scale + state.oy];
const toTex = (sx, sy) => [(sx - state.ox) / state.scale, (sy - state.oy) / state.scale];
// pxPerMetre is [x, y]: Livonia's map is not square on its texture
const worldToTex = (x, z) => [state.tex.originPx[0] + x * state.tex.pxPerMetre[0], state.tex.originPx[1] + (state.size - z) * state.tex.pxPerMetre[1]];
const texToWorld = (tx, ty) => [(tx - state.tex.originPx[0]) / state.tex.pxPerMetre[0], state.size - (ty - state.tex.originPx[1]) / state.tex.pxPerMetre[1]];
const inWorld = (x, z) => x >= 0 && z >= 0 && x <= state.size && z <= state.size;
const onTexture = (tx, ty) => tx >= 0 && ty >= 0 && tx < state.tex.width && ty < state.tex.height;
const clampTex = (v, max) => Math.max(0, Math.min(max, v));
const metresToPx = (m) => m * (state.tex.pxPerMetre[0] + state.tex.pxPerMetre[1]) / 2;

function fitView() {
  const r = view.getBoundingClientRect();
  state.scale = Math.min(r.width / state.tex.width, r.height / state.tex.height);
  state.ox = (r.width - state.tex.width * state.scale) / 2;
  state.oy = (r.height - state.tex.height * state.scale) / 2;
}

function zoomAt(sx, sy, factor) {
  const r = view.getBoundingClientRect();
  const min = Math.min(r.width / state.tex.width, r.height / state.tex.height) / 1.5, max = 12;
  const next = Math.max(min, Math.min(max, state.scale * factor));
  const [tx, ty] = toTex(sx, sy);
  state.scale = next;
  state.ox = sx - tx * next;
  state.oy = sy - ty * next;
}

// ------------------------------------------------------------- drawing ops
function opacity() { return $("opacity").value / 100; }
function sizeMetres() { return Number($("size").value); }

function applyOp(c, op) {
  const k = 1, px = (x) => x, py = (y) => y;     // ops are already in texture px
  c.save();
  if (op.type === "stroke") {
    c.globalCompositeOperation = op.erase ? "destination-out" : "source-over";
    c.strokeStyle = op.color; c.globalAlpha = op.erase ? 1 : op.alpha;
    c.lineWidth = Math.max(1, op.width * k); c.lineCap = c.lineJoin = "round";
    // paint the whole stroke at once so its own overlaps don't stack up alpha
    if (!op.erase && op.alpha < 1) {
      const tmp = scratch(c.canvas.width, c.canvas.height);
      const t = tmp.getContext("2d");
      t.clearRect(0, 0, tmp.width, tmp.height);
      Object.assign(t, { strokeStyle: op.color, lineWidth: c.lineWidth, lineCap: "round", lineJoin: "round" });
      tracePath(t, op.points, px, py); t.stroke();
      c.drawImage(tmp, 0, 0);
    } else {
      tracePath(c, op.points, px, py); c.stroke();
    }
  } else if (op.type === "stamp") {
    const img = state.stamps[op.stamp]?.img;
    if (img) {
      c.globalAlpha = op.alpha;
      c.translate(px(op.x), py(op.y));
      c.rotate(op.rot * Math.PI / 180);
      const w = op.width * k, h = w * img.naturalHeight / img.naturalWidth;
      c.drawImage(img, -w / 2, -h / 2, w, h);
    }
  } else if (op.type === "text") {
    c.globalAlpha = op.alpha;
    c.translate(px(op.x), py(op.y));
    c.rotate(op.rot * Math.PI / 180);
    c.font = `700 ${Math.max(4, op.height * k)}px system-ui, sans-serif`;
    c.textAlign = "center"; c.textBaseline = "middle";
    c.lineWidth = Math.max(1, op.height * k / 8); c.strokeStyle = "#000";
    c.strokeText(op.text, 0, 0);
    c.fillStyle = op.color; c.fillText(op.text, 0, 0);
  }
  c.restore();
}

let _scratch = null;
function scratch(w, h) {
  if (!_scratch || _scratch.width !== w) { _scratch = document.createElement("canvas"); _scratch.width = w; _scratch.height = h; }
  return _scratch;
}

function tracePath(c, pts, px, py) {
  c.beginPath();
  c.moveTo(px(pts[0][0]), py(pts[0][1]));
  if (pts.length === 1) c.lineTo(px(pts[0][0]) + 0.01, py(pts[0][1]));
  for (const [x, z] of pts.slice(1)) c.lineTo(px(x), py(z));
}

function rebuildDraft() {
  state.dctx.clearRect(0, 0, state.draft.width, state.draft.height);
  for (const op of state.ops) applyOp(state.dctx, op);
  draw();
}

function commit(op) {
  state.ops.push(op);
  state.redo = [];
  applyOp(state.dctx, op);
  draw();
}

// ------------------------------------------------------------- rendering
function draw() {
  const r = view.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  if (view.width !== Math.round(r.width * dpr)) { view.width = Math.round(r.width * dpr); view.height = Math.round(r.height * dpr); }
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.fillStyle = "#0d0f11"; ctx.fillRect(0, 0, r.width, r.height);
  if (!state.base) return;

  const W = state.tex.width * state.scale, H = state.tex.height * state.scale;
  ctx.imageSmoothingEnabled = state.scale < 2;      // zoomed in: show real texture pixels
  ctx.drawImage(state.base, state.ox, state.oy, W, H);

  if ($("showApproved").checked) {
    for (const sub of state.approved) {
      if (sub.kind !== "layer") continue;
      const img = state.approvedImages.get(sub.id);
      if (!img) continue;
      const [x0, y0, x1, y1] = sub.px;
      const [sx, sy] = toScreen(x0, y0);
      ctx.drawImage(img, sx, sy, (x1 - x0) * state.scale, (y1 - y0) * state.scale);
    }
  }
  ctx.drawImage(state.draft, state.ox, state.oy, W, H);
  if (state.current) {             // stroke in progress, drawn live on top
    const op = state.current;
    ctx.save();
    ctx.globalAlpha = op.erase ? 1 : op.alpha;
    ctx.strokeStyle = op.erase ? "#fff8" : op.color;
    ctx.lineWidth = Math.max(1, op.width * state.scale); ctx.lineCap = ctx.lineJoin = "round";
    if (op.erase) ctx.setLineDash([6, 6]);
    tracePath(ctx, op.points, (x) => toScreen(x, 0)[0], (y) => toScreen(0, y)[1]);
    ctx.stroke(); ctx.restore();
  }
  if ($("showApproved").checked) {
    for (const sub of state.approved) {
      if (sub.kind === "marker") MDT.drawMarker(ctx, ...toScreen(...worldToTex(sub.x, sub.z)), sub);
    }
  }
  if (state.pendingMarker) MDT.drawMarker(ctx, ...toScreen(...worldToTex(state.pendingMarker.x, state.pendingMarker.z)), state.pendingMarker);
  drawCursor();
}

function drawCursor() {
  const h = state.hover;
  if (!h) return;
  const [sx, sy] = toScreen(h[0], h[1]);
  ctx.save();
  if (state.tool === "brush" || state.tool === "eraser") {
    ctx.strokeStyle = "#fff"; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.arc(sx, sy, Math.max(2, metresToPx(sizeMetres()) * state.scale / 2), 0, Math.PI * 2); ctx.stroke();
  } else if (state.tool === "stamp" && state.stamp != null) {
    const img = state.stamps[state.stamp].img;
    const w = metresToPx(sizeMetres()) * state.scale, hgt = w * img.naturalHeight / img.naturalWidth;
    ctx.globalAlpha = opacity() * 0.7;
    ctx.translate(sx, sy); ctx.rotate($("rotation").value * Math.PI / 180);
    ctx.drawImage(img, -w / 2, -hgt / 2, w, hgt);
  } else if (state.tool === "text" && $("paintText").value.trim()) {
    const hgt = metresToPx(sizeMetres()) * state.scale;
    ctx.globalAlpha = opacity() * 0.7;
    ctx.translate(sx, sy); ctx.rotate($("rotation").value * Math.PI / 180);
    ctx.font = `700 ${Math.max(4, hgt)}px system-ui, sans-serif`;
    ctx.textAlign = "center"; ctx.textBaseline = "middle";
    ctx.fillStyle = $("color").value; ctx.fillText($("paintText").value.trim(), 0, 0);
  }
  ctx.restore();
}

// ------------------------------------------------------------- input
let drag = null, spaceDown = false;

view.addEventListener("pointerdown", (e) => {
  view.setPointerCapture(e.pointerId);
  hidePopup();
  const pan = state.tool === "pan" || spaceDown || e.button === 1 || e.button === 2;
  if (pan) { drag = { pan: true, x: e.offsetX, y: e.offsetY }; return; }
  if (e.button !== 0) return;
  const [tx, ty] = toTex(e.offsetX, e.offsetY);
  if (!onTexture(tx, ty)) return;
  const rot = Number($("rotation").value);
  const size = round1(metresToPx(sizeMetres()));
  if (state.tool === "brush" || state.tool === "eraser") {
    state.current = { type: "stroke", erase: state.tool === "eraser", color: $("color").value, alpha: opacity(),
      width: size, points: [[round1(tx), round1(ty)]] };
    drag = { stroke: true };
    draw();
  } else if (state.tool === "stamp") {
    if (state.stamp == null) return say("Pick a stamp first", true);
    commit({ type: "stamp", stamp: state.stamp, x: round1(tx), y: round1(ty), width: size, rot, alpha: opacity() });
  } else if (state.tool === "text") {
    const text = $("paintText").value.trim();
    if (!text) return say("Type the text first", true);
    commit({ type: "text", text, x: round1(tx), y: round1(ty), height: size, rot, color: $("color").value, alpha: opacity() });
  } else if (state.tool === "marker") {
    const text = $("markerText").value.trim();
    if (!text) return say("Type a marker label first", true);
    const [x, z] = texToWorld(tx, ty);
    if (!inWorld(x, z)) return say("Markers go on the map itself, not the paper edge", true);
    state.pendingMarker = { x: round1(x), z: round1(z), text, icon: state.icon, color: "#FF" + $("color").value.slice(1).toUpperCase() };
    showPopup(e.offsetX, e.offsetY, text);
    draw();
  }
});

view.addEventListener("pointermove", (e) => {
  if (!state.tex) return;
  const [tx, ty] = toTex(e.offsetX, e.offsetY);
  const [x, z] = texToWorld(tx, ty);
  state.hover = [tx, ty];
  $("coords").textContent = inWorld(x, z) ? `X ${Math.round(x)}  Z ${Math.round(z)}` : "";
  if (drag?.pan) {
    state.ox += e.offsetX - drag.x; state.oy += e.offsetY - drag.y;
    drag.x = e.offsetX; drag.y = e.offsetY;
  } else if (drag?.stroke && state.current) {
    const pts = state.current.points, last = pts[pts.length - 1];
    const minStep = Math.max(0.5, 1 / state.scale);
    if (Math.hypot(tx - last[0], ty - last[1]) >= minStep)
      pts.push([round1(clampTex(tx, state.tex.width)), round1(clampTex(ty, state.tex.height))]);
  }
  draw();
});

function endDrag() {
  if (drag?.stroke && state.current) { const op = state.current; state.current = null; commit(op); }
  drag = null;
}
view.addEventListener("pointerup", endDrag);
view.addEventListener("pointercancel", endDrag);
view.addEventListener("pointerleave", () => { state.hover = null; $("coords").textContent = ""; draw(); });
view.addEventListener("contextmenu", (e) => e.preventDefault());
view.addEventListener("wheel", (e) => {
  e.preventDefault();
  zoomAt(e.offsetX, e.offsetY, Math.exp(-e.deltaY * 0.0015));
  draw();
}, { passive: false });

const round1 = (v) => Math.round(v * 10) / 10;

document.addEventListener("keydown", (e) => {
  if (e.target.matches("input, select, textarea")) return;
  if (e.code === "Space") { spaceDown = true; e.preventDefault(); }
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z") { e.preventDefault(); e.shiftKey ? redo() : undo(); }
  else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "y") { e.preventDefault(); redo(); }
  else if (!e.ctrlKey && !e.metaKey) {
    const tool = { b: "brush", e: "eraser", s: "stamp", t: "text", m: "marker", h: "pan" }[e.key.toLowerCase()];
    if (tool) setTool(tool);
  }
});
document.addEventListener("keyup", (e) => { if (e.code === "Space") spaceDown = false; });
window.addEventListener("resize", draw);

function undo() { if (state.ops.length) { state.redo.push(state.ops.pop()); rebuildDraft(); } }
function redo() { if (state.redo.length) { state.ops.push(state.redo.pop()); rebuildDraft(); } }

// ------------------------------------------------------------- panel
// each tool remembers its own size; a 40 m brush is a good line, a 40 m stamp is invisible
const toolSizes = { brush: 40, eraser: 120, stamp: 400, text: 250 };

function setTool(tool) {
  state.tool = tool;
  if (toolSizes[tool]) { $("size").value = toolSizes[tool]; $("size").dispatchEvent(new Event("input")); }
  for (const b of document.querySelectorAll("#tools button")) b.classList.toggle("active", b.dataset.tool === tool);
  $("stampSection").hidden = tool !== "stamp";
  $("markerSection").hidden = tool !== "marker";
  $("textRow").hidden = tool !== "text";
  $("rotRow").hidden = !(tool === "stamp" || tool === "text");
  $("sizeRow").hidden = $("opacity").parentElement.hidden = tool === "marker" || tool === "pan";
  view.style.cursor = tool === "pan" ? "grab" : "crosshair";
  draw();
}

function bindOutputs() {
  const fmt = { opacity: (v) => v + "%", size: (v) => v + " m", rotation: (v) => v + "°" };
  for (const [id, out] of [["opacity", "opacityOut"], ["size", "sizeOut"], ["rotation", "rotOut"]]) {
    const upd = () => { $(out).textContent = fmt[id]($(id).value); draw(); };
    $(id).addEventListener("input", upd); upd();
  }
  $("size").addEventListener("input", () => { if (toolSizes[state.tool]) toolSizes[state.tool] = Number($("size").value); });
  $("paintText").addEventListener("input", draw);
}

function say(text, error = false) {
  $("status").textContent = text;
  $("status").className = "status " + (error ? "error" : "good");
}

// built-in stamps: simple shapes, drawn white-on-colour so they read on the dark map
const BUILTIN_STAMPS = {
  flag: '<path d="M14 6v52" stroke="#000" stroke-width="5"/><path d="M14 6v52" stroke="#fff" stroke-width="3"/><path d="M16 8h34l-8 10 8 10H16z" fill="COL" stroke="#000" stroke-width="2"/>',
  skull: '<path d="M32 6c-13 0-22 9-22 21 0 7 3 12 8 15v8h28v-8c5-3 8-8 8-15 0-12-9-21-22-21z" fill="#eee" stroke="#000" stroke-width="2"/><circle cx="24" cy="28" r="5"/><circle cx="40" cy="28" r="5"/><path d="M29 40h6l-3-6z"/><path d="M24 50v8M32 50v8M40 50v8" stroke="#000" stroke-width="2"/>',
  swords: '<g stroke="#000" stroke-width="7" stroke-linecap="round"><path d="M12 12l38 38M52 12L14 50"/></g><g stroke="COL" stroke-width="4" stroke-linecap="round"><path d="M12 12l38 38M52 12L14 50"/></g><path d="M40 54l10-10M24 54L14 44" stroke="#000" stroke-width="5"/>',
  tent: '<path d="M32 8L4 56h56z" fill="COL" stroke="#000" stroke-width="3"/><path d="M32 30l-10 26h20z" fill="#111"/>',
  star: '<path d="M32 4l8 18 20 2-15 13 5 20-18-11-18 11 5-20L4 24l20-2z" fill="COL" stroke="#000" stroke-width="2.5"/>',
  cross: '<path d="M12 12l40 40M52 12L12 52" stroke="#000" stroke-width="13" stroke-linecap="round"/><path d="M12 12l40 40M52 12L12 52" stroke="COL" stroke-width="8" stroke-linecap="round"/>',
  ring: '<circle cx="32" cy="32" r="24" fill="none" stroke="#000" stroke-width="9"/><circle cx="32" cy="32" r="24" fill="none" stroke="COL" stroke-width="5"/>',
  arrow: '<path d="M32 4l22 26H40v30H24V30H10z" fill="COL" stroke="#000" stroke-width="3" stroke-linejoin="round"/>',
  danger: '<path d="M32 5L3 57h58z" fill="#f2c200" stroke="#000" stroke-width="3" stroke-linejoin="round"/><path d="M32 22v18" stroke="#000" stroke-width="6" stroke-linecap="round"/><circle cx="32" cy="49" r="3.5"/>',
};

async function addStamp(img, title) {
  state.stamps.push({ img, title });
  const i = state.stamps.length - 1;
  const b = document.createElement("button");
  b.title = title;
  b.append(img.cloneNode());
  b.onclick = () => selectStamp(i);
  $("stamps").append(b);
  return i;
}

function selectStamp(i) {
  state.stamp = i;
  [...$("stamps").children].forEach((b, j) => b.classList.toggle("active", j === i));
  draw();
}

async function loadBuiltinStamps() {
  for (const [name, body] of Object.entries(BUILTIN_STAMPS)) {
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="256" height="256">${body.replaceAll("COL", "#d03030")}</svg>`;
    await addStamp(await MDT.loadImage("data:image/svg+xml;charset=utf-8," + encodeURIComponent(svg)), name);
  }
  selectStamp(0);
}

$("stampFile").addEventListener("change", async (e) => {
  const file = e.target.files[0];
  e.target.value = "";
  if (!file) return;
  if (file.size > 4 * 1024 * 1024) return say("Stamp image must be under 4 MB", true);
  const url = URL.createObjectURL(file);
  try {
    const img = await MDT.loadImage(url);
    // keep the original pixels in a canvas so ops can redraw after the object URL is gone
    const c = document.createElement("canvas");
    const s = Math.min(1, 1024 / Math.max(img.naturalWidth, img.naturalHeight));
    c.width = Math.round(img.naturalWidth * s); c.height = Math.round(img.naturalHeight * s);
    c.getContext("2d").drawImage(img, 0, 0, c.width, c.height);
    const copy = await MDT.loadImage(c.toDataURL("image/png"));
    selectStamp(await addStamp(copy, file.name));
  } catch (err) {
    say(err.message, true);
  } finally {
    URL.revokeObjectURL(url);
  }
});

function buildIcons() {
  MDT.ICON_NAMES.forEach((name, i) => {
    const b = document.createElement("button");
    b.title = name;
    if (MDT.icons[i]) b.append(MDT.icons[i].cloneNode());
    else b.textContent = i;
    b.onclick = () => { state.icon = i; [...$("icons").children].forEach((x, j) => x.classList.toggle("active", j === i)); };
    $("icons").append(b);
  });
  $("icons").children[state.icon].classList.add("active");
}

// ------------------------------------------------------------- markers
function showPopup(sx, sy, text) {
  const p = $("markerPopup");
  $("popupText").textContent = text;
  p.hidden = false;
  const r = view.getBoundingClientRect();
  p.style.left = Math.min(sx + 14, r.width - 250) + "px";
  p.style.top = Math.min(sy + 14, r.height - 110) + "px";
}
function hidePopup() { $("markerPopup").hidden = true; state.pendingMarker = null; }

$("popupCancel").onclick = () => { hidePopup(); draw(); };
$("popupOk").onclick = async () => {
  const m = state.pendingMarker;
  if (!m) return;
  try {
    const sent = await MDT.api(`/api/${state.world}/submissions`, {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ kind: "marker", ...m, ...who(), title: "" }),
    });
    say(sent.status === "approved" ? `Marker "${m.text}" added to your own map.` : `Marker "${m.text}" sent for approval.`);
  } catch (err) { say(err.message, true); }
  refreshAccount();
  hidePopup(); draw();
};

// ------------------------------------------------------------- submit
function who() {
  const author = $("author").value.trim();
  try { localStorage.setItem("mdt.author", author); localStorage.setItem("mdt.group", $("group").value.trim()); } catch {}
  return { author, group: $("group").value.trim(), title: $("title").value.trim(),
    scope: $("scope").value, days: +$("days").value };
}

const daysLabel = (d) => (d === 0 ? "Permanent" : d === 1 ? "1 day" : `${d} days`);

// Who sees it (everyone / only me) and how long, as the owner allows on this map (Web/access.py).
// Personal drawings belong to a Steam account: the game shows them to that player only.
function showOptions(me) {
  const o = me.options || { allowGlobal: true, allowPersonal: false, lifespans: [0], defaultDays: 0 };
  const steam = me.user && me.user.provider === "steam";
  const [glob, pers] = $("scope").options;
  glob.disabled = !o.allowGlobal;
  pers.disabled = !o.allowPersonal || !steam;
  if ($("scope").selectedOptions[0]?.disabled) $("scope").value = o.allowGlobal ? "global" : "personal";
  $("scopeRow").hidden = !(o.allowGlobal && o.allowPersonal);
  const hint = !o.allowPersonal ? "" : !steam ? "Sign in with Steam to keep drawings on your own map only."
    : $("scope").value === "personal" ? (o.approvePersonal ? "Only you will see it, after the owner approves it."
      : "Only you will see it. It goes straight to your map, no approval needed.") : "";
  $("scopeHint").textContent = hint;
  $("scopeHint").hidden = !hint;
  const keep = +$("days").value;
  $("days").replaceChildren(...o.lifespans.map((d) => new Option(daysLabel(d), d)));
  $("days").value = o.lifespans.includes(keep) && $("days").dataset.touched ? keep : o.defaultDays;
  $("daysRow").hidden = o.lifespans.length < 2;
}
$("days").onchange = () => { $("days").dataset.touched = "1"; };
$("scope").onchange = () => { if (state.me) showOptions(state.me); };

function draftBounds() {
  const { width: w, height: h } = state.draft;
  const data = state.dctx.getImageData(0, 0, w, h).data;
  let x0 = w, y0 = h, x1 = -1, y1 = -1;
  for (let y = 0; y < h; y++) {
    const row = y * w * 4;
    for (let x = 0; x < w; x++) {
      if (data[row + x * 4 + 3] > 2) {
        if (x < x0) x0 = x; if (x > x1) x1 = x;
        if (y < y0) y0 = y; if (y > y1) y1 = y;
      }
    }
  }
  return x1 < 0 ? null : [x0, y0, x1 + 1, y1 + 1];
}

// A drawing made only of brush strokes is also sent as lines; the in-game map draws those sharp
// at any zoom. Anything with eraser, stamps or text goes live as the (softer) image instead.
function liveStrokes() {
  if (!state.ops.length || !state.ops.every((op) => op.type === "stroke" && !op.erase)) return null;
  return state.ops.map((op) => ({ color: op.color, alpha: op.alpha, width: op.width, points: op.points }));
}

// ------------------------------------------------------------- sign-in (Web/access.py)
// Signed in, the name comes from Steam or Discord. The owner can require sign-in, or limit
// submitting to listed SteamIDs or Discord roles; the server checks, this only explains.
const PROVIDERS = { steam: "Sign in with Steam", discord: "Sign in with Discord" };
async function refreshAccount() {
  let me;
  try { me = await MDT.api(`/api/me?map=${encodeURIComponent(state.world)}`); } catch { return; }
  state.me = me;
  const back = encodeURIComponent(location.pathname + location.search);
  $("whoami").textContent = me.user ? `Signed in as ${me.user.name} (${me.user.provider === "steam" ? "Steam" : "Discord"})` : "";
  $("signout").hidden = !me.user;
  $("signin").replaceChildren(...(me.user ? [] : me.providers.map((p) => {
    const a = document.createElement("a");
    a.className = "button"; a.href = `/login/${p}?next=${back}`; a.textContent = PROVIDERS[p];
    return a;
  })));
  $("authorRow").hidden = !!me.user;
  showOptions(me);
  $("access").hidden = me.canSubmit;
  $("access").textContent = me.why;
  $("submit").disabled = !me.canSubmit;
}
$("signout").onclick = async () => { await MDT.api("/logout", { method: "POST" }); refreshAccount(); };

$("submit").onclick = async () => {
  const meta = who();
  if (!meta.author && !state.me?.user) return say("Enter your name first", true);
  const b = draftBounds();
  if (!b) return say("Draw something first", true);
  const [x0, y0, x1, y1] = b;
  const crop = document.createElement("canvas");
  crop.width = x1 - x0; crop.height = y1 - y0;
  crop.getContext("2d").drawImage(state.draft, x0, y0, crop.width, crop.height, 0, 0, crop.width, crop.height);
  $("submit").disabled = true;
  say("Sending...");
  try {
    const sent = await MDT.api(`/api/${state.world}/submissions`, {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ kind: "layer", image: crop.toDataURL("image/png"), px: [x0, y0, x1, y1], strokes: liveStrokes(), ...meta }),
    });
    say(sent.status === "approved" ? "Saved to your own map. It appears in game within a minute or two."
      : "Drawing sent. It appears in game within a minute or two of the owner approving it.");
    state.ops = []; state.redo = []; rebuildDraft();
  } catch (err) {
    say(err.message, true);
  } finally {
    $("submit").disabled = false;
    refreshAccount();
  }
};

$("undo").onclick = undo;
$("redo").onclick = redo;
$("clear").onclick = () => { if (state.ops.length && confirm("Clear your whole drawing?")) { state.ops = []; state.redo = []; rebuildDraft(); } };
$("showApproved").onchange = draw;
for (const b of document.querySelectorAll("#tools button")) b.onclick = () => setTool(b.dataset.tool);

// ------------------------------------------------------------- startup
async function loadWorld(w) {
  if (state.ops.length && !confirm("Switching maps discards your current drawing. Continue?")) {
    $("world").value = state.world; return;
  }
  state.world = w.id; state.size = w.size; state.tex = w;
  try { history.replaceState(null, "", "?map=" + encodeURIComponent(w.id)); } catch {}
  state.base = await MDT.loadImage(w.basemap);
  state.draft = document.createElement("canvas");
  state.draft.width = w.width; state.draft.height = w.height;
  state.dctx = state.draft.getContext("2d", { willReadFrequently: true });
  state.ops = []; state.redo = [];
  fitView();
  refreshAccount();
  state.approved = await MDT.api(`/api/${w.id}/approved`);
  state.approvedImages.clear();
  await Promise.all(state.approved.filter((s) => s.kind === "layer").map(async (s) => {
    try { state.approvedImages.set(s.id, await MDT.loadImage(`/api/${w.id}/image/${s.id}`)); } catch {}
  }));
  draw();
}

(async function init() {
  try { $("author").value = localStorage.getItem("mdt.author") || ""; $("group").value = localStorage.getItem("mdt.group") || ""; } catch {}
  bindOutputs();
  await MDT.loadIcons();
  buildIcons();
  await loadBuiltinStamps();
  setTool("brush");
  // one entry per map (a server's set of drawings); ?map=<id> picks one, so owners can link each server's map
  const maps = await MDT.api("/api/maps");
  if (!maps.length) return say("No maps set up on this server yet.", true);
  for (const m of maps) $("world").append(new Option(m.name, m.id));
  $("world").onchange = () => loadWorld(maps.find((m) => m.id === $("world").value));
  const wanted = new URLSearchParams(location.search).get("map");
  const first = maps.find((m) => m.id === wanted) || maps[0];
  $("world").value = first.id;
  await loadWorld(first);
})().catch((err) => say(err.message, true));
