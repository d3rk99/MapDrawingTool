// Admin page, Settings tab: sign-in, who may submit, webhooks, public address (Web/settings.py).
// Secrets are never sent to the browser: their fields show whether one is saved, and stay empty
// unless the owner types a new one.
const Settings = (() => {
  let data = null, roles = null, rolesError = "";
  const WHO = { anyone: "Anyone (types a name)", "signed-in": "Anyone signed in (Steam or Discord)",
    listed: "Only listed Steam accounts or Discord roles" };
  const EVENTS = { submitted: "New submission", approved: "Approved", rejected: "Rejected", deleted: "Deleted",
    withdrawn: "Removed by its author" };

  function el(tag, attrs = {}, ...kids) {
    const e = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (k === "text") e.textContent = v;
      else if (k === "class") e.className = v;
      else if (k in e && typeof v !== "string") e[k] = v;
      else e.setAttribute(k, v);
    }
    e.append(...kids.filter((k) => k != null));
    return e;
  }
  const field = (label, input, hint) => el("label", { class: "setting" }, el("span", { text: label }), input,
    hint ? el("small", { class: "hint", text: hint }) : null);
  const secret = (isSet, placeholderUnset = "not set") => el("input", { type: "password", autocomplete: "off",
    placeholder: isSet ? "saved (leave empty to keep)" : placeholderUnset });

  // One set of submitter rules. partial (a map's own rules): values start from the default.
  function ruleEditor(r) {
    const who = el("select", {}, ...Object.entries(WHO).map(([v, t]) => el("option", { value: v, text: t })));
    who.value = r.who;
    const steam = el("textarea", { rows: 3, placeholder: "One SteamID64 or profile link per line" });
    steam.value = (r.steamIds || []).join("\n");
    const known = new Set((roles || []).map((x) => x.id));
    const picks = (roles || []).map((role) => {
      const box = el("input", { type: "checkbox", value: role.id });
      box.checked = (r.discordRoles || []).includes(role.id);
      return el("label", { class: "role" }, box, el("span", { text: role.name, style: `color:${role.color === "#000000" ? "inherit" : role.color}` }));
    });
    const other = el("textarea", { rows: 2, placeholder: roles ? "Other role IDs, one per line" : "Discord role IDs, one per line" });
    other.value = (r.discordRoles || []).filter((id) => !known.has(id)).join("\n");
    const perHour = el("input", { type: "number", min: 1, max: 1000, value: r.perHour });
    const maxPending = el("input", { type: "number", min: 1, max: 1000, value: r.maxPending });
    const flag = (key, text) => {
      const b = el("input", { type: "checkbox" }); b.checked = r[key] !== false && r[key] !== undefined ? !!r[key] : false;
      const l = el("label", { class: "chip" }, b, el("span", { text })); l.key = key; return l;
    };
    const flags = [flag("allowGlobal", "Drawings everyone sees"), flag("allowPersonal", "Personal drawings (only the author sees them)"),
      flag("approvePersonal", "Personal drawings need approval too"), flag("ingame", "Drawing in game with a map and a pen")];
    const maxDays = el("input", { type: "number", min: 0, max: 3650, value: r.maxDays ?? 0 });
    const defaultDays = el("input", { type: "number", min: 0, max: 3650, value: r.defaultDays ?? 0 });
    const listed = el("div", { class: "listed" },
      field("Steam accounts", steam),
      roles ? el("div", { class: "setting" }, el("span", { text: "Discord roles" }), el("div", { class: "roles" }, ...picks)) : null,
      field(roles ? "More role IDs" : "Discord roles", other,
        roles ? null : (rolesError || "Add a bot token and server ID above to pick roles by name, or paste role IDs.")));
    const sync = () => { listed.hidden = who.value !== "listed"; };
    who.onchange = sync; sync();
    const box = el("div", { class: "rules" }, field("Who may submit", who), listed,
      el("div", { class: "row" }, field("Submissions per hour", perHour), field("Waiting for approval at once", maxPending)),
      el("div", { class: "setting" }, el("span", { text: "What players may make" }), el("div", { class: "chips" }, ...flags)),
      el("div", { class: "row" },
        field("Longest lifespan (days)", maxDays, "0 = no limit, drawings may be permanent"),
        field("Default lifespan (days)", defaultDays, "0 = permanent. Players pick from 1, 3, 7, 14, 30, 90 days within the limit.")));
    const lines = (t) => t.value.split(/[\s,]+/).map((s) => s.trim()).filter(Boolean);
    box.get = () => ({ who: who.value, steamIds: lines(steam),
      discordRoles: [...picks.map((p) => p.querySelector("input")).filter((b) => b.checked).map((b) => b.value), ...lines(other)],
      perHour: +perHour.value, maxPending: +maxPending.value, maxDays: +maxDays.value, defaultDays: +defaultDays.value,
      ...Object.fromEntries(flags.map((l) => [l.key, l.querySelector("input").checked])) });
    return box;
  }

  function webhookRow(h) {
    const fmt = el("select", {}, el("option", { value: "discord", text: "Discord" }), el("option", { value: "json", text: "JSON (anything else)" }));
    fmt.value = h.format || "discord";
    const url = el("input", { type: "url", placeholder: h.urlHint ? `saved: ${h.urlHint} (leave empty to keep)` : "https://discord.com/api/webhooks/..." });
    const events = Object.entries(EVENTS).map(([v, t]) => {
      const b = el("input", { type: "checkbox", value: v }); b.checked = (h.events || ["submitted"]).includes(v);
      return el("label", { class: "chip" }, b, el("span", { text: t }));
    });
    const mapBoxes = maps.map((m) => {
      const b = el("input", { type: "checkbox", value: m.id }); b.checked = (h.maps || []).includes(m.id);
      return el("label", { class: "chip" }, b, el("span", { text: m.name }));
    });
    const sec = secret(h.secretSet, "optional");
    const secRow = field("Signing secret", sec, "Sent as X-MDT-Signature: sha256=<HMAC of the body>");
    const syncFmt = () => { secRow.hidden = fmt.value !== "json"; };
    fmt.onchange = syncFmt; syncFmt();
    const status = el("small", { class: "hint" });
    const row = el("fieldset", { class: "webhook" },
      el("div", { class: "row" }, field("Type", fmt), field("Address", url)),
      el("div", { class: "setting" }, el("span", { text: "Send on" }), el("div", { class: "chips" }, ...events)),
      el("div", { class: "setting" }, el("span", { text: "Maps (none ticked = all)" }), el("div", { class: "chips" }, ...mapBoxes)),
      secRow,
      el("div", { class: "row" },
        el("button", { type: "button", text: "Send test", onclick: async () => {
          if (!h.id) { status.textContent = "Save first, then test."; return; }
          status.textContent = "Sending...";
          try { await adminApi("/api/admin/webhooks/test", { method: "POST", body: JSON.stringify({ id: h.id }) }); status.textContent = "Sent. Check the channel."; }
          catch (err) { status.textContent = err.message; }
        } }),
        el("button", { type: "button", class: "bad", text: "Remove", onclick: () => row.remove() }), status));
    row.get = () => ({ id: h.id || "", format: fmt.value, url: url.value.trim(), secret: sec.value,
      events: events.map((l) => l.querySelector("input")).filter((b) => b.checked).map((b) => b.value),
      maps: mapBoxes.map((l) => l.querySelector("input")).filter((b) => b.checked).map((b) => b.value) });
    return row;
  }

  async function show(container) {
    container.replaceChildren(el("p", { text: "Loading settings..." }));
    try { data = await adminApi("/api/admin/settings"); }
    catch (err) { container.replaceChildren(el("p", { class: "status error", text: err.message })); return; }
    roles = null; rolesError = "";
    const d = data.login.discord;
    if (d.botTokenSet && d.guildId) {
      try { roles = await adminApi("/api/admin/discord/roles"); } catch (err) { rolesError = err.message; }
    }
    build(container);
  }

  function build(container) {
    const d = data.login.discord;
    const publicUrl = el("input", { type: "url", value: data.publicUrl, placeholder: "https://map.yourdomain.com" });
    const steamOn = el("input", { type: "checkbox", checked: data.login.steam });
    const steamKey = secret(data.login.steamApiKeySet);
    const clearKey = el("input", { type: "checkbox" });
    const clientId = el("input", { value: d.clientId, placeholder: "Application ID" });
    const clientSecret = secret(d.clientSecretSet);
    const guildId = el("input", { value: d.guildId, placeholder: "Right-click your server > Copy Server ID" });
    const botToken = secret(d.botTokenSet, "optional");
    const clearBot = el("input", { type: "checkbox" });
    const defaults = ruleEditor(data.submitters);
    const perMap = data.maps.map((m) => {
      const own = el("input", { type: "checkbox", checked: !!m.override });
      const ed = ruleEditor({ ...data.submitters, ...(m.override || {}) });
      const sync = () => { ed.hidden = !own.checked; };
      own.onchange = sync; sync();
      const sec = el("details", { class: "mapgroup", open: !!m.override },
        el("summary", { text: `${m.name}${m.override ? " · own rules" : " · uses the default"}` }),
        el("label", { class: "chip" }, own, el("span", { text: "This map has its own rules" })), ed);
      sec.get = () => (own.checked ? [m.id, ed.get()] : null);
      return sec;
    });
    const hookList = el("div", {}, ...data.webhooks.map(webhookRow));
    const serverKey = el("input", { readonly: "", value: data.serverKey, onfocus: function () { this.select(); } });
    const newKey = el("input", { type: "checkbox" });
    const status = el("p", { class: "status" });
    const save = el("button", { class: "primary", text: "Save settings", onclick: async () => {
      const clear = [];
      if (clearKey.checked) clear.push("steamApiKey");
      if (clearBot.checked) clear.push("botToken");
      const body = {
        publicUrl: publicUrl.value.trim(),
        login: { steam: steamOn.checked, steamApiKey: steamKey.value.trim(), clear,
          discord: { clientId: clientId.value.trim(), clientSecret: clientSecret.value.trim(), guildId: guildId.value.trim(), botToken: botToken.value.trim() } },
        submitters: defaults.get(), newServerKey: newKey.checked,
        mapSubmitters: Object.fromEntries(perMap.map((p) => p.get()).filter(Boolean)),
        webhooks: [...hookList.children].map((r) => r.get()),
      };
      save.disabled = true; status.textContent = "Saving..."; status.className = "status";
      try {
        data = await adminApi("/api/admin/settings", { method: "PUT", body: JSON.stringify(body) });
        await show(container);
        say("Settings saved. They apply now, no restart needed.");
      } catch (err) { status.textContent = err.message; status.className = "status error"; save.disabled = false; }
    } });

    container.replaceChildren(
      el("h2", { text: "Settings" }),
      el("section", { class: "panel" }, el("h3", { text: "Web address" }),
        field("Public address", publicUrl, "The address players use, e.g. https://map.yourdomain.com. Sign-in returns there, and admin links in notifications use it.")),
      el("section", { class: "panel" }, el("h3", { text: "Steam sign-in" }),
        el("label", { class: "chip" }, steamOn, el("span", { text: "Players can sign in with Steam" })),
        field("Steam Web API key", steamKey, "Free at steamcommunity.com/dev/apikey. Shows Steam names instead of numbers."),
        data.login.steamApiKeySet ? el("label", { class: "chip" }, clearKey, el("span", { text: "Remove the saved key" })) : null),
      el("section", { class: "panel" }, el("h3", { text: "Discord sign-in" }),
        el("p", { class: "hint", text: `Create an application at discord.com/developers. Under OAuth2 add this redirect: ${data.login.discordRedirect}` }),
        el("div", { class: "row" }, field("Application ID", clientId), field("Client secret", clientSecret)),
        field("Your Discord server ID", guildId, "Needed to check roles. Turn on Developer Mode in Discord settings > Advanced to copy IDs."),
        field("Bot token (optional)", botToken, "Only used to list your server's roles by name below. Add the bot to your server; it needs no permissions."),
        d.botTokenSet ? el("label", { class: "chip" }, clearBot, el("span", { text: "Remove the saved bot token" })) : null,
        rolesError ? el("p", { class: "status error", text: rolesError }) : roles ? el("p", { class: "hint", text: `${roles.length} roles found in your server.` }) : null),
      el("section", { class: "panel" }, el("h3", { text: "Who may submit" }),
        el("p", { class: "hint", text: "The default for every map. Signed-in Discord players get their roles checked when they sign in." }), defaults,
        el("h4", { text: "Per map" }), ...perMap),
      el("section", { class: "panel" }, el("h3", { text: "Game servers" }),
        el("p", { class: "hint", text: "Drawings made in game reach the web app through your DayZ server. Put this key in each server's profiles\\MapDrawingTool\\config.json as \"serverKey\", next to liveUrl." }),
        field("Server key", serverKey),
        el("label", { class: "chip" }, newKey, el("span", { text: "Make a new key when saving (servers with the old key stop sending in-game drawings)" }))),
      el("section", { class: "panel" }, el("h3", { text: "Notifications" }),
        el("p", { class: "hint", text: "Discord: channel settings > Integrations > Webhooks > New Webhook > Copy Webhook URL." }),
        hookList, el("button", { type: "button", text: "Add webhook", onclick: () => hookList.append(webhookRow({ format: "discord", events: ["submitted"] })) })),
      save, status);
  }

  return { show };
})();
