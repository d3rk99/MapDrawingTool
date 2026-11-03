// Shared by the editor and the approval page.
const MDT = {
  ICON_NAMES: ["Border crossing", "Broadleaf", "Camp", "Factory", "Fir", "Fire station", "Government", "Hill",
    "Monument", "Palm", "Police", "Station", "Store", "Tourism", "Transmitter", "Shelter", "Trail sign",
    "Viewpoint", "Vineyard", "Water pump"],

  async api(path, options = {}) {
    const res = await fetch(path, options);
    const body = res.headers.get("content-type")?.includes("json") ? await res.json() : {};
    if (!res.ok) throw new Error(body.error || `${res.status} ${res.statusText}`);
    return body;
  },

  loadImage(src) {
    return new Promise((resolve, reject) => {
      const img = new Image();
      img.onload = () => resolve(img);
      img.onerror = () => reject(new Error("Could not load " + src));
      img.src = src;
    });
  },

  // "#AARRGGBB" (the in-game format) <-> css colour
  cssColor(argb) {
    const c = argb.replace("#", "");
    if (c.length === 6) return "#" + c;
    return `rgba(${parseInt(c.slice(2, 4), 16)},${parseInt(c.slice(4, 6), 16)},${parseInt(c.slice(6, 8), 16)},${parseInt(c.slice(0, 2), 16) / 255})`;
  },

  icons: [],
  async loadIcons() {
    this.icons = await Promise.all(this.ICON_NAMES.map((_, i) => this.loadImage(`/static/icons/${i}.png`).catch(() => null)));
  },

  drawMarker(ctx, sx, sy, m) {
    const icon = this.icons[m.icon];
    const s = 20;
    if (icon) {
      // tint the white game icon with the marker colour
      ctx.save();
      ctx.shadowColor = "#000"; ctx.shadowBlur = 3;
      ctx.drawImage(this.tinted(icon, m.color), sx - s / 2, sy - s / 2, s, s);
      ctx.restore();
    } else {
      ctx.fillStyle = this.cssColor(m.color);
      ctx.beginPath(); ctx.arc(sx, sy, 6, 0, Math.PI * 2); ctx.fill();
    }
    if (m.text) {
      ctx.font = "600 13px system-ui, sans-serif";
      ctx.lineWidth = 3; ctx.strokeStyle = "#000c"; ctx.fillStyle = "#fff";
      ctx.strokeText(m.text, sx + s / 2 + 3, sy + 4);
      ctx.fillText(m.text, sx + s / 2 + 3, sy + 4);
    }
  },

  _tints: new Map(),
  tinted(img, color) {
    const key = img.src + color;
    if (!this._tints.has(key)) {
      const c = document.createElement("canvas");
      c.width = img.naturalWidth; c.height = img.naturalHeight;
      const x = c.getContext("2d");
      x.drawImage(img, 0, 0);
      x.globalCompositeOperation = "source-in";
      x.fillStyle = this.cssColor(color);
      x.fillRect(0, 0, c.width, c.height);
      this._tints.set(key, c);
    }
    return this._tints.get(key);
  },
};
