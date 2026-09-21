/*
 * Lap Timer Dev Controller — SPA logic (vanilla ES6, no framework/build/CDN).
 *
 * API contract this file drives (see
 * docs/superpowers/specs/2026-09-20-dev-controller-B-design.md §3, §7 and
 * docs/superpowers/plans/2026-09-20-plan-5.5-dev-controller.md Tasks 2/4/5/6):
 *
 *   GET  /api/status              -> {connected,proto,state,flags,batt_pct,
 *                                      batt_mv,free_kb,sessions,fw}
 *                                     503 {"connected":false} when the
 *                                     lap-timer link is down. Polled ~2s.
 *   GET  /api/config              -> lap-timer config JSON (schema-free on
 *                                     this side; rendered generically).
 *   POST /api/config              <- full desired config JSON; firmware
 *                                     diffs + applies changed keys.
 *                                     413 = a changed value exceeds the
 *                                     console line budget.
 *   GET  /api/sessions            -> {proto, sessions:[{id,mode,venue,laps,
 *                                      best_ms,log_kb,sum_kb,has_log}]} (design §14.3)
 *   GET  /api/session/<id>?fmt=   -> file download, fmt in
 *                                     vbo|nmea|json|log|sum
 *   GET  /api/stream              -> SSE (EventSource); each event's data is
 *                                     one JSON fused-sample or event record.
 *   GET  /api/logs                -> {logs:[{id,bytes}]}
 *   GET  /api/log/<id>            -> log file download
 *   POST /api/flash               <- multipart/form-data (.bin); stages +
 *                                     pushes cmd-OTA to the lap-timer.
 *
 * No field name for flash progress is specified in the design docs, so the
 * flash panel below (see submitFlash/pollFlashStatus) reads an optional
 * status.flash_pct / status.flashing if the firmware happens to send one,
 * and otherwise falls back to a disconnect/reconnect heuristic on
 * /api/status. See the "contract ambiguity" note in the commit message.
 */

(function () {
  "use strict";

  /* ---------- tiny DOM helpers ---------- */

  function $(sel, root) { return (root || document).querySelector(sel); }
  function $all(sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); }

  function el(tag, attrs, children) {
    var node = document.createElement(tag);
    if (attrs) {
      Object.keys(attrs).forEach(function (k) {
        if (k === "text") node.textContent = attrs[k];
        else if (k in node) node[k] = attrs[k];
        else node.setAttribute(k, attrs[k]);
      });
    }
    (children || []).forEach(function (c) { node.appendChild(c); });
    return node;
  }

  function showMsg(container, kind, text) {
    container.hidden = false;
    container.className = "msg msg--" + kind;
    container.textContent = text;
  }

  function hideMsg(container) {
    container.hidden = true;
    container.textContent = "";
  }

  function fmtKb(kb) {
    if (kb === undefined || kb === null) return "?";
    if (kb >= 1024) return (kb / 1024).toFixed(1) + " MB";
    return kb + " KB";
  }

  function fmtBytes(b) {
    if (b === undefined || b === null) return "?";
    if (b >= 1024 * 1024) return (b / (1024 * 1024)).toFixed(2) + " MB";
    if (b >= 1024) return (b / 1024).toFixed(1) + " KB";
    return b + " B";
  }

  function fmtMs(ms) {
    if (ms === undefined || ms === null) return "-";
    var m = Math.floor(ms / 60000);
    var s = Math.floor((ms % 60000) / 1000);
    var frac = ms % 1000;
    return m + ":" + String(s).padStart(2, "0") + "." + String(frac).padStart(3, "0");
  }

  /** fetch() that never throws on HTTP error status; returns {ok, status, data, raw} */
  function fetchJson(url, opts) {
    return fetch(url, opts).then(function (res) {
      return res.text().then(function (text) {
        var data = null;
        if (text) {
          try { data = JSON.parse(text); } catch (e) { data = null; }
        }
        return { ok: res.ok, status: res.status, data: data, raw: text };
      });
    }, function (err) {
      return { ok: false, status: 0, data: null, raw: "", networkError: err };
    });
  }

  /* ---------- tabs ---------- */

  function initTabs() {
    var buttons = $all(".tabs__btn");
    buttons.forEach(function (btn) {
      btn.addEventListener("click", function () {
        buttons.forEach(function (b) { b.classList.remove("is-active"); });
        $all(".panel").forEach(function (p) { p.classList.remove("is-active"); });
        btn.classList.add("is-active");
        var panel = $("#panel-" + btn.dataset.tab);
        if (panel) panel.classList.add("is-active");
      });
    });
    if (buttons.length) buttons[0].click();
  }

  /* ---------- status header (poll GET /api/status every ~2s) ---------- */

  function refreshStatus() {
    fetchJson("/api/status").then(function (res) {
      var bar = $("#status-bar");
      var dotText = $("#status-text");
      var details = $("#status-details");

      if (!res.data) {
        bar.className = "status-bar status-bar--unknown";
        dotText.textContent = "Dev controller unreachable";
        details.textContent = "";
        return;
      }

      var d = res.data;
      var connected = !!d.connected;
      bar.className = "status-bar status-bar--" + (connected ? "connected" : "disconnected");
      dotText.textContent = connected ? "Lap-timer connected" : "Lap-timer not connected";

      var parts = [];
      if (d.fw !== undefined) parts.push("fw " + d.fw);
      if (d.state !== undefined) parts.push("state " + d.state);
      if (d.batt_pct !== undefined) {
        var battTxt = d.batt_pct + "%";
        if (d.batt_mv !== undefined) battTxt += " (" + d.batt_mv + " mV)";
        parts.push("batt " + battTxt);
      }
      if (d.free_kb !== undefined) parts.push("free " + fmtKb(d.free_kb));
      if (d.sessions !== undefined) parts.push(d.sessions + " sessions");
      details.textContent = parts.join("  ·  ");
    });
  }

  function initStatus() {
    refreshStatus();
    setInterval(refreshStatus, 2000);
  }

  /* ---------- config (generic form built from whatever GET /api/config returns) ---------- */

  function setNestedValue(root, path, value) {
    var node = root;
    for (var i = 0; i < path.length - 1; i++) {
      var key = path[i];
      if (typeof node[key] !== "object" || node[key] === null) node[key] = {};
      node = node[key];
    }
    node[path[path.length - 1]] = value;
  }

  /** Converts any object whose own keys are exactly "0".."n-1" into an array (recursive). */
  function arrayify(node) {
    if (node === null || typeof node !== "object" || Array.isArray(node)) return node;
    var keys = Object.keys(node);
    keys.forEach(function (k) { node[k] = arrayify(node[k]); });
    if (keys.length > 0 && keys.every(function (k, i) { return k === String(i); })) {
      return keys.map(function (k) { return node[k]; });
    }
    return node;
  }

  /* Field schema: friendly labels/units/controls for known config paths, grouped into sections.
   * Anything the lap-timer returns that ISN'T covered here still renders (generically) under
   * "Advanced" -- so a new firmware field is never dropped from the round-trip save. */
  var CONFIG_SECTIONS = [
    { title: "General", fields: [
      { path: ["units"], label: "Speed units", control: "segmented", rerenderOnChange: true,
        options: [["kmh", "km/h"], ["mph", "mph"]] },
      { path: ["mode"], label: "Mode", control: "segmented",
        options: [["lap", "Lap timer"], ["drag", "Drag"]] }
    ] },
    { title: "Lap timing", fields: [
      { composite: "range", label: "Lap time (min to max)", min: ["lap", "min_lap_s"], max: ["lap", "max_lap_s"],
        unit: "s", rangeMin: 1, help: "Valid lap window: laps outside it are rejected. Min must be less than max." },
      { path: ["lap", "gate_rearm_m"], label: "Gate re-arm distance", unit: "m", control: "number", min: 0,
        help: "Leave the start/finish by this far before it can trigger again." },
      { path: ["lap", "pit_speed_kmh"], label: "Pit speed threshold", unit: "km/h", control: "number", min: 0 },
      { path: ["lap", "pit_time_s"], label: "Pit dwell time", unit: "s", control: "number", min: 0 },
      { path: ["lap", "default_layout"], label: "Default track layout", control: "list",
        help: "Sector gate IDs; leave empty to auto-detect the layout." }
    ] },
    { title: "Drag", fields: [
      { path: ["drag", "rollout"], label: "1-ft rollout", control: "toggle",
        help: "Start the clock after a 1-foot rollout (drag-strip convention)." },
      { path: ["drag", "launch_g"], label: "Launch threshold", unit: "g", control: "slider",
        scale: 100, min: 0.05, max: 1, step: 0.01,
        help: "Forward acceleration that arms a run." },
      { composite: "benches", label: "Speed benches", kmh: ["drag", "benches_kmh"], mph: ["drag", "benches_mph"],
        help: "Report elapsed time at each of these speeds, e.g. 100, 200, 300." }
    ] },
    { title: "Power & sleep", fields: [
      { path: ["power", "pit_after_s"], label: "Enter PIT after", unit: "s", control: "number", min: 0,
        help: "Idle time before entering the low-power PIT state." },
      { path: ["power", "park_after_s"], label: "Enter PARK after", unit: "s", control: "number", min: 0,
        readout: "duration", help: "Idle time before deep-sleep PARK (must be longer than the PIT delay)." },
      { path: ["power", "shutdown_mv"], label: "Shutdown voltage", unit: "V", control: "slider",
        scale: 1000, min: 3, max: 4.2, step: 0.01, help: "Auto power-off below this pack voltage." },
      { path: ["power", "conn_idle_s"], label: "Peer idle timeout", unit: "s", control: "number", min: 0,
        readout: "duration", help: "Drop the dev-controller link after this long with no traffic." }
    ] },
    { title: "Display", fields: [
      { path: ["display", "live_clock"], label: "Live clock", control: "toggle" },
      { path: ["display", "full_refresh_every"], label: "Full refresh every", unit: "partials", control: "number", min: 1 },
      { path: ["display", "rotation"], label: "Rotation", control: "segmented",
        options: [[0, "0"], [90, "90"], [180, "180"], [270, "270"]] },
      { path: ["display", "invert"], label: "Invert colours", control: "toggle" }
    ] },
    { title: "Connectivity (BLE)", fields: [
      { path: ["ble", "name"], label: "BLE device name", control: "text" },
      { path: ["ble", "adv_s"], label: "Advertise timeout", unit: "s", control: "number", min: 0 }
    ] },
    { title: "Logging", fields: [
      { path: ["log", "fused_hz"], label: "Fused-log rate", unit: "Hz", control: "number", min: 1,
        help: "How often GPS+IMU samples are logged and streamed." }
    ] },
    { title: "Sensors", fields: [
      { path: ["gps", "dyn_model"], label: "GPS dynamic model", control: "select",
        options: [[0, "Portable"], [2, "Stationary"], [3, "Pedestrian"], [4, "Automotive"],
                  [5, "Sea"], [6, "Airborne <1g"], [7, "Airborne <2g"], [8, "Airborne <4g"], [9, "Wrist"]],
        help: "Tunes the GPS filter for the expected motion (Automotive for bikes/cars)." },
      { path: ["gps", "rate_hz"], label: "GPS update rate", unit: "Hz", control: "number", min: 0,
        help: "0 = firmware default." },
      { path: ["imu", "mot_thr"], label: "Motion threshold", control: "number", min: 0,
        help: "Movement needed to wake from sleep (higher = less sensitive)." },
      { path: ["imu", "mot_dur_ms"], label: "Motion duration", unit: "ms", control: "number", min: 0,
        help: "Sustained motion for this long before waking." }
    ] }
  ];

  function getNested(obj, path) {
    var n = obj;
    for (var i = 0; i < path.length; i++) {
      if (n === null || typeof n !== "object") return undefined;
      n = n[path[i]];
    }
    return n;
  }

  function prettyKey(k) {
    return String(k).replace(/_/g, " ").replace(/\b\w/g, function (c) { return c.toUpperCase(); });
  }

  /* Config paths a spec covers (for the rendered-set tracking + present-check). */
  function specPaths(spec) {
    if (spec.composite === "range") return [spec.min, spec.max];
    if (spec.composite === "benches") return [spec.kmh, spec.mph];
    return [spec.path];
  }

  /* Whole seconds -> "m:ss". */
  function fmtDurS(s) {
    s = Math.max(0, Math.round(Number(s) || 0));
    return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
  }

  /* Renders one friendly field row; every input keeps the data-path/data-type contract
   * collectConfigForm reads (composites emit several, plus hidden preserves for round-trip). */
  function renderFieldSpec(container, spec, obj) {
    /* --- composite: lap-time range (one control, two stored values, min<max enforced live) --- */
    if (spec.composite === "range") {
      var vmin = getNested(obj, spec.min), vmax = getNested(obj, spec.max);
      var rrow = el("div", { className: "cfg-row" });
      rrow.appendChild(el("label", { className: "cfg-row__label", text: spec.label }));
      var rctl = el("div", { className: "cfg-row__control cfg-range" });
      var minIn = el("input", { type: "number", value: String(vmin) });
      var maxIn = el("input", { type: "number", value: String(vmax) });
      if (spec.rangeMin !== undefined) { minIn.min = String(spec.rangeMin); maxIn.min = String(spec.rangeMin); }
      minIn.dataset.path = JSON.stringify(spec.min); minIn.dataset.type = "number";
      maxIn.dataset.path = JSON.stringify(spec.max); maxIn.dataset.type = "number";
      var ro = el("span", { className: "cfg-readout" });
      function updRange() {
        var mn = Number(minIn.value), mx = Number(maxIn.value);
        ro.textContent = "max = " + fmtDurS(mx);
        var bad = !(mn < mx);
        minIn.classList.toggle("cfg-invalid", bad); maxIn.classList.toggle("cfg-invalid", bad);
      }
      minIn.addEventListener("input", updRange); maxIn.addEventListener("input", updRange);
      rctl.appendChild(minIn);
      rctl.appendChild(el("span", { className: "cfg-range__dash", text: "to" }));
      rctl.appendChild(maxIn);
      rctl.appendChild(el("span", { className: "cfg-unit", text: spec.unit }));
      rctl.appendChild(ro);
      rrow.appendChild(rctl);
      if (spec.help) rrow.appendChild(el("p", { className: "cfg-help", text: spec.help }));
      updRange();
      container.appendChild(rrow);
      return;
    }
    /* --- composite: speed benches -> show only the active-unit set; preserve the other (hidden) --- */
    if (spec.composite === "benches") {
      var isMph = getNested(obj, ["units"]) === "mph";
      var activePath = isMph ? spec.mph : spec.kmh;
      var otherPath = isMph ? spec.kmh : spec.mph;
      var activeUnit = isMph ? "mph" : "km/h";
      var av = getNested(obj, activePath), ov = getNested(obj, otherPath);
      var brow = el("div", { className: "cfg-row" });
      brow.appendChild(el("label", { className: "cfg-row__label", text: spec.label }));
      var bctl = el("div", { className: "cfg-row__control" });
      var listIn = el("input", { type: "text", placeholder: "comma-separated",
        value: Array.isArray(av) ? av.join(", ") : "" });
      listIn.dataset.path = JSON.stringify(activePath); listIn.dataset.type = "numarray";
      bctl.appendChild(listIn);
      bctl.appendChild(el("span", { className: "cfg-unit", text: activeUnit }));
      if (otherPath) {   /* keep the inactive unit's benches in the round-trip, untouched */
        var hid = el("input", { type: "hidden", value: Array.isArray(ov) ? ov.join(", ") : "" });
        hid.dataset.path = JSON.stringify(otherPath); hid.dataset.type = "numarray";
        bctl.appendChild(hid);
      }
      brow.appendChild(bctl);
      brow.appendChild(el("p", { className: "cfg-help",
        text: (spec.help || "") + " Showing " + activeUnit + " (follows the Units setting)." }));
      container.appendChild(brow);
      return;
    }

    /* --- simple specs --- */
    var value = getNested(obj, spec.path);
    var id = "cfg_" + spec.path.join("_");
    var row = el("div", { className: "cfg-row" });
    row.appendChild(el("label", { className: "cfg-row__label", text: spec.label, htmlFor: id }));
    var wrap = el("div", { className: "cfg-row__control" });
    var input, dtype, readout = null;

    if (spec.control === "toggle") {
      input = el("input", { type: "checkbox", checked: !!value });
      dtype = "boolean";
    } else if (spec.control === "select" || spec.control === "segmented") {
      var numeric = spec.options.length > 0 && typeof spec.options[0][0] === "number";
      dtype = numeric ? "number" : "string";
      if (spec.control === "segmented") {
        input = el("input", { type: "hidden", value: String(value) });
        var seg = el("div", { className: "cfg-seg" });
        spec.options.forEach(function (o) {
          var btn = el("button", { type: "button", className: "cfg-seg__btn", text: o[1] });
          if (String(o[0]) === String(value)) btn.classList.add("is-active");
          btn.addEventListener("click", function () {
            input.value = String(o[0]);
            $all(".cfg-seg__btn", seg).forEach(function (b) { b.classList.remove("is-active"); });
            btn.classList.add("is-active");
            /* some toggles (Units) change what other fields show -> refresh from current values */
            if (spec.rerenderOnChange) renderConfigForm(collectConfigForm());
          });
          seg.appendChild(btn);
        });
        wrap.appendChild(seg);
      } else {
        input = el("select", {});
        spec.options.forEach(function (o) {
          var opt = el("option", { value: String(o[0]), text: o[1] });
          if (String(o[0]) === String(value)) opt.selected = true;
          input.appendChild(opt);
        });
      }
    } else if (spec.control === "slider") {
      var scale = spec.scale || 1;
      var disp = (value === null || value === undefined) ? 0 : (value / scale);
      var slider = el("input", { type: "range", value: String(disp),
        min: String(spec.min), max: String(spec.max), step: String(spec.step || "any") });
      slider.className = "cfg-slider";
      input = el("input", { type: "number", step: String(spec.step || "any"), value: String(disp) });
      if (spec.min !== undefined) input.min = String(spec.min);
      if (spec.max !== undefined) input.max = String(spec.max);
      dtype = "number";
      input.dataset.scale = String(scale);
      slider.addEventListener("input", function () { input.value = slider.value; });
      input.addEventListener("input", function () { slider.value = input.value; });
      wrap.classList.add("cfg-row__control--slider");
      wrap.appendChild(slider);
    } else if (spec.control === "list") {
      input = el("input", { type: "text", placeholder: "comma-separated",
        value: Array.isArray(value) ? value.join(", ") : ((value === null || value === undefined) ? "" : String(value)) });
      dtype = "numarray";
    } else if (spec.control === "number") {
      input = el("input", { type: "number", step: spec.step || "any",
        value: (value === null || value === undefined) ? "" : String(value) });
      if (spec.min !== undefined) input.min = String(spec.min);
      if (spec.max !== undefined) input.max = String(spec.max);
      dtype = "number";
    } else {
      input = el("input", { type: "text",
        value: (value === null || value === undefined) ? "" : String(value) });
      dtype = "string";
    }
    input.id = id;
    input.dataset.path = JSON.stringify(spec.path);
    input.dataset.type = dtype;
    wrap.appendChild(input);
    if (spec.unit) wrap.appendChild(el("span", { className: "cfg-unit", text: spec.unit }));
    if (spec.readout === "duration") {
      readout = el("span", { className: "cfg-readout", text: "(" + fmtDurS(value) + ")" });
      input.addEventListener("input", function () { readout.textContent = "(" + fmtDurS(input.value) + ")"; });
      wrap.appendChild(readout);
    }
    row.appendChild(wrap);
    if (spec.help) row.appendChild(el("p", { className: "cfg-help", text: spec.help }));
    container.appendChild(row);
  }

  function renderConfigNode(container, label, value, path) {
    if (Array.isArray(value)) {
      var afs = el("fieldset", {}, [el("legend", { text: label })]);
      if (value.length === 0) {
        afs.appendChild(el("p", { className: "muted", text: "(empty list)" }));
      }
      value.forEach(function (item, i) {
        renderConfigNode(afs, "[" + i + "]", item, path.concat(String(i)));
      });
      container.appendChild(afs);
      return;
    }
    if (value !== null && typeof value === "object") {
      var ofs = el("fieldset", {}, [el("legend", { text: label })]);
      Object.keys(value).forEach(function (k) {
        renderConfigNode(ofs, k, value[k], path.concat(k));
      });
      container.appendChild(ofs);
      return;
    }

    // primitive leaf
    var row = el("div", { className: "config-field" });
    row.appendChild(el("label", { text: label, htmlFor: "cfg_" + path.join("_") }));
    var input;
    var dataType = typeof value;
    if (dataType === "boolean") {
      input = el("input", { type: "checkbox", checked: !!value });
    } else if (dataType === "number") {
      input = el("input", { type: "number", value: String(value), step: "any" });
    } else {
      input = el("input", { type: "text", value: value === null ? "" : String(value) });
      dataType = "string";
    }
    input.id = "cfg_" + path.join("_");
    input.dataset.path = JSON.stringify(path);
    input.dataset.type = dataType;
    if (path.length === 1 && path[0] === "version") input.disabled = true; // firmware-owned
    row.appendChild(input);
    container.appendChild(row);
  }

  function renderConfigForm(obj) {
    var root = $("#config-fields");
    /* Preserve which sections are open across a re-render (a Units change refreshes the whole form). */
    var openTitles = {}, hadState = false;
    $all("details.cfg-section", root).forEach(function (d) {
      hadState = true;
      var s = d.querySelector("summary");
      if (d.open && s) openTitles[s.textContent] = true;
    });
    function sectionOpen(title, dflt) { return hadState ? !!openTitles[title] : dflt; }

    root.innerHTML = "";
    if (!obj || typeof obj !== "object") {
      root.appendChild(el("p", { className: "muted", text: "No config data." }));
      return;
    }
    var rendered = {};   // JSON.stringify(path) -> true

    if (obj.version !== undefined) {
      root.appendChild(el("p", { className: "cfg-version muted",
        text: "Config schema v" + obj.version + " (firmware-owned)" }));
      rendered[JSON.stringify(["version"])] = true;
    }

    CONFIG_SECTIONS.forEach(function (section, si) {
      var present = section.fields.filter(function (f) {
        return specPaths(f).some(function (p) { return getNested(obj, p) !== undefined; });
      });
      if (!present.length) return;
      var card = el("details", { className: "cfg-section" });
      card.open = sectionOpen(section.title, si === 0);   // General open by default; keep user's toggles on re-render
      card.appendChild(el("summary", { className: "cfg-section__title", text: section.title }));
      present.forEach(function (f) {
        renderFieldSpec(card, f, obj);
        specPaths(f).forEach(function (p) { rendered[JSON.stringify(p)] = true; });
      });
      root.appendChild(card);
    });

    /* Battery calibration: a compact table (Point | ADC mV | True mV), not stacked raw fields. */
    var cal = getNested(obj, ["battery", "cal"]);
    if (Array.isArray(cal) && cal.length) {
      var bcard = el("details", { className: "cfg-section" });
      bcard.open = sectionOpen("Battery calibration", false);
      bcard.appendChild(el("summary", { className: "cfg-section__title", text: "Battery calibration" }));
      bcard.appendChild(el("p", { className: "cfg-help",
        text: "Two-point ADC calibration (raw reading to true millivolts). Set during power bring-up." }));
      var keys = (cal[0] && typeof cal[0] === "object") ? Object.keys(cal[0]) : [];
      var tbl = el("table", { className: "cfg-caltable" });
      tbl.appendChild(el("tr", {}, [el("th", { text: "Point" })].concat(keys.map(function (k) {
        return el("th", { text: (k === "adc_mv") ? "ADC (mV)" : (k === "true_mv") ? "True (mV)" : prettyKey(k) });
      }))));
      cal.forEach(function (pt, i) {
        var tr = el("tr", {}, [el("td", { text: String(i + 1) })]);
        keys.forEach(function (k) {
          var inp = el("input", { type: "number",
            value: (pt && pt[k] !== undefined && pt[k] !== null) ? String(pt[k]) : "" });
          inp.dataset.path = JSON.stringify(["battery", "cal", String(i), k]);
          inp.dataset.type = "number";
          rendered[JSON.stringify(["battery", "cal", String(i), k])] = true;
          tr.appendChild(el("td", {}, [inp]));
        });
        tbl.appendChild(tr);
      });
      rendered[JSON.stringify(["battery", "cal"])] = true;
      bcard.appendChild(tbl);
      root.appendChild(bcard);
    }

    /* fallback: unrecognised leaves -> Advanced (collapsed; usually empty now). */
    var adv = el("details", { className: "cfg-section cfg-section--advanced" });
    adv.open = sectionOpen("Advanced (unrecognised fields)", false);
    adv.appendChild(el("summary", { className: "cfg-section__title", text: "Advanced (unrecognised fields)" }));
    if (renderUnrenderedLeaves(adv, obj, [], rendered) > 0) root.appendChild(adv);
  }

  /* Recursively render any leaf/array the schema didn't render (via generic renderConfigNode, so the
   * data-path/data-type contract collectConfigForm reads still holds). Returns the count added. */
  function renderUnrenderedLeaves(container, node, path, rendered) {
    if (node !== null && typeof node === "object" && !Array.isArray(node)) {
      var added = 0;
      Object.keys(node).forEach(function (k) {
        added += renderUnrenderedLeaves(container, node[k], path.concat(k), rendered);
      });
      return added;
    }
    if (path.length === 0 || rendered[JSON.stringify(path)]) return 0;
    renderConfigNode(container, prettyKey(path[path.length - 1]), node, path);
    return 1;
  }

  function collectConfigForm() {
    var root = {};
    $all("#config-fields [data-path]").forEach(function (input) {
      var path = JSON.parse(input.dataset.path);
      var type = input.dataset.type;
      var val;
      if (type === "boolean") val = input.checked;
      else if (type === "number") {
        val = input.value === "" ? 0 : Number(input.value);
        if (input.dataset.scale) val = Math.round(val * Number(input.dataset.scale));  // display unit -> stored (g->x0.01g, V->mV)
      }
      else if (type === "numarray") {
        val = input.value.split(",").map(function (s) { return Number(s.trim()); })
          .filter(function (n) { return !isNaN(n); });
      } else val = input.value;
      setNestedValue(root, path, val);
    });
    return arrayify(root);
  }

  function loadConfig() {
    var msg = $("#config-status");
    hideMsg(msg);
    $("#config-fields").innerHTML = '<p class="muted">Loading&hellip;</p>';
    fetchJson("/api/config").then(function (res) {
      if (res.status === 503) {
        showMsg(msg, "err", "Lap-timer not connected — config unavailable.");
        $("#config-fields").innerHTML = '<p class="muted">Not connected.</p>';
        return;
      }
      if (!res.data) {
        showMsg(msg, "err", "Could not load config (bad response).");
        $("#config-fields").innerHTML = '<p class="muted">Load failed.</p>';
        return;
      }
      renderConfigForm(res.data);
    });
  }

  function saveConfig(evt) {
    evt.preventDefault();
    var msg = $("#config-status");
    var saveBtn = $("#config-save");
    hideMsg(msg);
    var cfg, body;
    try {
      cfg = collectConfigForm();
    } catch (e) {
      showMsg(msg, "err", "Could not build config JSON: " + e.message);
      return;
    }
    /* Ordered-pair validation: these fields bound each other. */
    var errs = [];
    var lmin = getNested(cfg, ["lap", "min_lap_s"]), lmax = getNested(cfg, ["lap", "max_lap_s"]);
    if (typeof lmin === "number" && typeof lmax === "number" && lmin >= lmax)
      errs.push("Min lap time must be less than max.");
    var pit = getNested(cfg, ["power", "pit_after_s"]), park = getNested(cfg, ["power", "park_after_s"]);
    if (typeof pit === "number" && typeof park === "number" && pit >= park)
      errs.push("PIT delay must be less than PARK delay.");
    if (errs.length) { showMsg(msg, "err", errs.join(" ")); return; }
    body = JSON.stringify(cfg);
    saveBtn.disabled = true;
    fetchJson("/api/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: body
    }).then(function (res) {
      saveBtn.disabled = false;
      if (res.ok) {
        showMsg(msg, "ok", "Config saved.");
        return;
      }
      if (res.status === 413) {
        showMsg(msg, "err", "Value too large — a changed field exceeds the console line limit.");
      } else if (res.status === 503) {
        showMsg(msg, "err", "Lap-timer not connected — save failed.");
      } else {
        var detail = (res.data && (res.data.error || res.data.msg)) || res.raw || res.status;
        showMsg(msg, "err", "Save failed: " + detail);
      }
    });
  }

  function initConfig() {
    $("#config-form").addEventListener("submit", saveConfig);
    $("#config-reload").addEventListener("click", loadConfig);
    loadConfig();
  }

  /* ---------- sessions ---------- */

  var SESSION_FORMATS = ["vbo", "nmea", "json", "log", "sum"];

  function renderSessionsTable(list) {
    var body = $("#sessions-body");
    body.innerHTML = "";
    if (!list || list.length === 0) {
      body.appendChild(el("tr", {}, [el("td", { colSpan: 8, className: "muted", text: "No sessions." })]));
      return;
    }
    list.forEach(function (s) {
      var tr = el("tr", {}, [
        el("td", { text: s.id !== undefined ? String(s.id) : "?" }),
        el("td", { text: s.mode !== undefined ? String(s.mode) : "?" }),
        el("td", { text: s.venue !== undefined ? String(s.venue) : "?" }),
        el("td", { text: s.laps !== undefined ? String(s.laps) : "?" }),
        el("td", { text: fmtMs(s.best_ms) }),
        el("td", { text: fmtKb(s.log_kb) }),
        el("td", { text: fmtKb(s.sum_kb) })
      ]);
      var dl = el("td");
      SESSION_FORMATS.forEach(function (fmt) {
        if (fmt === "log" && s.has_log === false) {
          dl.appendChild(el("span", { className: "muted", text: "log ", style: "margin-right:8px" }));
          return;
        }
        var href = "/api/session/" + encodeURIComponent(s.id) + "?fmt=" + fmt;
        dl.appendChild(el("a", { href: href, text: fmt }));
      });
      tr.appendChild(dl);
      body.appendChild(tr);
    });
  }

  function loadSessions() {
    var msg = $("#sessions-status");
    hideMsg(msg);
    fetchJson("/api/sessions").then(function (res) {
      if (res.status === 503) {
        showMsg(msg, "err", "Lap-timer not connected — sessions unavailable.");
        renderSessionsTable([]);
        return;
      }
      if (!res.data || !Array.isArray(res.data.sessions)) {
        showMsg(msg, "err", "Could not load sessions (bad response).");
        renderSessionsTable([]);
        return;
      }
      renderSessionsTable(res.data.sessions);
    });
  }

  function initSessions() {
    $("#sessions-reload").addEventListener("click", loadSessions);
    loadSessions();
  }

  /* ---------- live monitor (SSE) ---------- */

  var monitorSource = null;
  var monitorRowCount = 0;
  var MONITOR_MAX_ROWS = 200;

  // EV_* names (components/core/include/core/event.h) for readable EVENT rows.
  var EVENT_NAMES = {
    1: "VENUE_FOUND", 2: "LAYOUT_LOCKED", 3: "ARMED", 4: "SECTOR", 5: "LAP_COMPLETE",
    6: "FIX_LOST", 7: "FIX_OK", 8: "DRAG_ARMED", 9: "DRAG_LAUNCH", 10: "DRAG_GATE",
    11: "DRAG_DONE", 12: "MOTION", 13: "STILL", 14: "CALIB_DONE", 15: "FAULT"
  };

  // Formats one decoded /api/stream record (see linkhost_stream_to_json) into a readable line.
  // Falls back to the raw JSON for a shape this function does not recognize.
  function formatStreamRecord(ts, parsed) {
    if (parsed.t === "fused") {
      var lean = (parsed.lean_cdeg / 100).toFixed(1);
      var g = (parsed.g_mg / 1000).toFixed(2);
      var yaw = (parsed.yaw_cdps / 100).toFixed(1);
      return ts + "  FUSED  lean " + lean + "°  g " + g + "  yaw " + yaw + "°/s";
    }
    if (parsed.t === "event") {
      var name = EVENT_NAMES[parsed.code];
      var label = name ? "code=" + parsed.code + " (" + name + ")" : "code=" + parsed.code;
      return ts + "  EVENT  " + label + "  arg16=" + parsed.arg16 + " arg32=" + parsed.arg32 +
             " arg32b=" + parsed.arg32b;
    }
    return ts + "  " + JSON.stringify(parsed);
  }

  function appendMonitorRow(rawData) {
    var pane = $("#monitor-pane");
    var ts = new Date().toLocaleTimeString();
    var text;
    try {
      var parsed = JSON.parse(rawData);
      if (parsed && typeof parsed.info === "string") {
        text = ts + "  " + parsed.info;
      } else {
        text = formatStreamRecord(ts, parsed);
      }
    } catch (e) {
      text = ts + "  " + rawData;
    }
    pane.appendChild(el("div", { text: text }));
    monitorRowCount++;
    while (pane.childNodes.length > MONITOR_MAX_ROWS) {
      pane.removeChild(pane.firstChild);
    }
    pane.scrollTop = pane.scrollHeight;
    $("#monitor-count").textContent = monitorRowCount + " rows";
  }

  function stopMonitor() {
    if (monitorSource) {
      monitorSource.close();
      monitorSource = null;
    }
    $("#monitor-toggle").textContent = "Start";
  }

  function startMonitor() {
    if (monitorSource) return;
    var lastErrorAt = 0;
    monitorSource = new EventSource("/api/stream");
    monitorSource.onopen = function () { appendMonitorRow('{"info":"stream connected"}'); };
    monitorSource.onmessage = function (evt) { appendMonitorRow(evt.data); };
    monitorSource.onerror = function () {
      if (monitorSource && monitorSource.readyState === EventSource.CLOSED) {
        // Server rejected the request (e.g. 503, lap-timer not connected) —
        // the browser will not retry on its own; reset the UI.
        appendMonitorRow('{"info":"stream closed (lap-timer not connected?)"}');
        stopMonitor();
        return;
      }
      // Transient drop — the browser retries automatically; avoid spamming
      // a row on every retry attempt.
      var now = Date.now();
      if (now - lastErrorAt > 5000) {
        appendMonitorRow('{"info":"stream reconnecting…"}');
        lastErrorAt = now;
      }
    };
    $("#monitor-toggle").textContent = "Stop";
  }

  function initMonitor() {
    $("#monitor-toggle").addEventListener("click", function () {
      if (monitorSource) stopMonitor(); else startMonitor();
    });
  }

  /* ---------- logs ---------- */

  function renderLogsTable(list) {
    var body = $("#logs-body");
    body.innerHTML = "";
    if (!list || list.length === 0) {
      body.appendChild(el("tr", {}, [el("td", { colSpan: 3, className: "muted", text: "No logs." })]));
      return;
    }
    list.forEach(function (l) {
      var tr = el("tr", {}, [
        el("td", { text: l.id !== undefined ? String(l.id) : "?" }),
        el("td", { text: fmtBytes(l.bytes) })
      ]);
      var dl = el("td", {}, [el("a", { href: "/api/log/" + encodeURIComponent(l.id), text: "download" })]);
      tr.appendChild(dl);
      body.appendChild(tr);
    });
  }

  function loadLogs() {
    var msg = $("#logs-status");
    hideMsg(msg);
    fetchJson("/api/logs").then(function (res) {
      if (!res.data || !Array.isArray(res.data.logs)) {
        showMsg(msg, "err", "Could not load logs (bad response).");
        renderLogsTable([]);
        return;
      }
      renderLogsTable(res.data.logs);
    });
  }

  function initLogs() {
    $("#logs-reload").addEventListener("click", loadLogs);
    loadLogs();
  }

  /* ---------- flash ---------- */

  var flashPollTimer = null;
  var flashInFlight = false;

  function setFlashProgress(pct, label) {
    var wrap = $("#flash-progress-wrap");
    wrap.hidden = false;
    $("#flash-progress-bar").style.width = Math.max(0, Math.min(100, pct)) + "%";
    $("#flash-progress-label").textContent = label !== undefined ? label : Math.round(pct) + "%";
  }

  function setFlashBusy(busy) {
    flashInFlight = busy;
    $("#flash-submit").disabled = busy;
    $("#flash-file").disabled = busy;
  }

  /**
   * After the upload completes, the lap-timer OTA-applies + reboots. There is
   * no documented progress field on GET /api/status, so if the firmware
   * happens to expose status.flash_pct / status.flashing this is used;
   * otherwise this just waits for a disconnect-then-reconnect as the
   * "apply done" signal.
   */
  function pollFlashStatus() {
    var sawDisconnect = false;
    var attempts = 0;
    var MAX_ATTEMPTS = 150; // ~150s at 1s interval
    clearInterval(flashPollTimer);
    flashPollTimer = setInterval(function () {
      attempts++;
      fetchJson("/api/status").then(function (res) {
        var d = res.data;
        if (d && typeof d.flash_pct === "number") {
          setFlashProgress(d.flash_pct, "applying " + d.flash_pct + "%");
        } else if (d && d.flashing) {
          setFlashProgress(90, "applying update…");
        }
        var connected = !!(d && d.connected);
        if (!connected) sawDisconnect = true;
        if (connected && sawDisconnect) {
          clearInterval(flashPollTimer);
          setFlashProgress(100, "done — reconnected" + (d.fw ? " (fw " + d.fw + ")" : ""));
          setFlashBusy(false);
        } else if (attempts >= MAX_ATTEMPTS) {
          clearInterval(flashPollTimer);
          showMsg($("#flash-status"), "err", "Timed out waiting for the lap-timer to reconnect after flashing.");
          setFlashBusy(false);
        }
      });
    }, 1000);
  }

  function submitFlash(evt) {
    evt.preventDefault();
    if (flashInFlight) return;
    var msg = $("#flash-status");
    hideMsg(msg);
    var fileInput = $("#flash-file");
    var file = fileInput.files && fileInput.files[0];
    if (!file) {
      showMsg(msg, "err", "Choose a .bin file first.");
      return;
    }

    setFlashBusy(true);
    setFlashProgress(0, "uploading 0%");

    var form = new FormData();
    form.append("firmware", file, file.name);

    var xhr = new XMLHttpRequest();
    xhr.open("POST", "/api/flash");
    xhr.upload.onprogress = function (e) {
      if (e.lengthComputable) {
        var pct = (e.loaded / e.total) * 100;
        setFlashProgress(pct, "uploading " + Math.round(pct) + "%");
      }
    };
    xhr.onload = function () {
      if (xhr.status >= 200 && xhr.status < 300) {
        setFlashProgress(100, "uploaded, staging…");
        showMsg(msg, "info", "Upload complete; lap-timer is applying the update.");
        pollFlashStatus();
      } else {
        var detail = xhr.responseText || xhr.status;
        if (xhr.status === 409) {
          showMsg(msg, "err", "A flash is already in progress.");
        } else if (xhr.status === 412 || xhr.status === 423) {
          showMsg(msg, "err", "Precondition failed — charge the lap-timer (≥3800 mV) or connect a charger, then retry.");
        } else {
          showMsg(msg, "err", "Flash upload failed: " + detail);
        }
        setFlashBusy(false);
      }
    };
    xhr.onerror = function () {
      showMsg(msg, "err", "Network error during upload.");
      setFlashBusy(false);
    };
    xhr.send(form);
  }

  function initFlash() {
    $("#flash-form").addEventListener("submit", submitFlash);
  }

  /* ---------- boot ---------- */

  document.addEventListener("DOMContentLoaded", function () {
    initTabs();
    initStatus();
    initConfig();
    initSessions();
    initMonitor();
    initLogs();
    initFlash();
  });
})();
