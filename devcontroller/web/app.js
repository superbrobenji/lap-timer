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
    root.innerHTML = "";
    if (!obj || typeof obj !== "object") {
      root.appendChild(el("p", { className: "muted", text: "No config data." }));
      return;
    }
    Object.keys(obj).forEach(function (k) {
      renderConfigNode(root, k, obj[k], [k]);
    });
  }

  function collectConfigForm() {
    var root = {};
    $all("#config-fields [data-path]").forEach(function (input) {
      var path = JSON.parse(input.dataset.path);
      var type = input.dataset.type;
      var val;
      if (type === "boolean") val = input.checked;
      else if (type === "number") val = input.value === "" ? 0 : Number(input.value);
      else val = input.value;
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
    var body;
    try {
      body = JSON.stringify(collectConfigForm());
    } catch (e) {
      showMsg(msg, "err", "Could not build config JSON: " + e.message);
      return;
    }
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

  function appendMonitorRow(rawData) {
    var pane = $("#monitor-pane");
    var text;
    try {
      var parsed = JSON.parse(rawData);
      text = new Date().toLocaleTimeString() + "  " + JSON.stringify(parsed);
    } catch (e) {
      text = new Date().toLocaleTimeString() + "  " + rawData;
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
