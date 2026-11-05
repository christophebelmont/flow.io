(function () {
  "use strict";

  const el = (id) => document.getElementById(id);
  const state = {
    scanTimer: 0,
    saving: false
  };

  function setStatus(text, tone) {
    const node = el("provStatus");
    if (!node) return;
    node.textContent = text;
    node.dataset.tone = tone || "info";
  }

  function setBusy(busy) {
    const save = el("provWifiSave");
    const scan = el("provWifiScan");
    if (save) save.disabled = !!busy;
    if (scan) scan.disabled = !!busy;
  }

  async function apiJson(url, options, label) {
    const res = await fetch(url, options || {});
    const data = await res.json().catch(() => null);
    if (!res.ok || !data || data.ok === false) {
      const msg = data && data.err && data.err.code ? data.err.code : (label || url);
      throw new Error(msg);
    }
    return data;
  }

  function formBody(values) {
    return new URLSearchParams(values);
  }

  function postForm(values) {
    return {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: formBody(values)
    };
  }

  function toBool(value) {
    if (typeof value === "boolean") return value;
    if (typeof value === "number") return value !== 0;
    if (typeof value === "string") {
      const text = value.trim().toLowerCase();
      return text === "1" || text === "true" || text === "on" || text === "yes";
    }
    return false;
  }

  function renderNetworks(data) {
    const list = el("provWifiSsidList");
    if (!list) return;
    const current = (el("provWifiSsid").value || "").trim();
    const previous = list.value || "";
    const networks = data && Array.isArray(data.networks) ? data.networks : [];

    list.innerHTML = "";
    const manual = document.createElement("option");
    manual.value = "";
    manual.textContent = "Saisie manuelle";
    list.appendChild(manual);

    for (const net of networks) {
      if (!net || typeof net.ssid !== "string" || net.ssid.length === 0 || net.hidden) continue;
      const option = document.createElement("option");
      const secure = net.secure ? " (securise)" : " (ouvert)";
      const rssi = Number.isFinite(net.rssi) ? " " + net.rssi + " dBm" : "";
      const label = net.ssid + secure + rssi;
      option.value = net.ssid;
      option.textContent = label.length > 64 ? label.slice(0, 63) + "..." : label;
      list.appendChild(option);
    }

    const values = Array.from(list.options).map((option) => option.value);
    if (current && values.includes(current)) {
      list.value = current;
    } else if (previous && values.includes(previous)) {
      list.value = previous;
    }
  }

  function updateScanStatus(data) {
    if (!data || data.ok !== true) {
      setStatus("Scan reseau indisponible.", "error");
      return;
    }
    const running = !!data.running || !!data.requested;
    const count = Number.isFinite(data.count) ? data.count : 0;
    const total = Number.isFinite(data.total_found) ? data.total_found : count;
    if (running) {
      setStatus("Scan reseau en cours...", "info");
      return;
    }
    if (count > 0) {
      setStatus("Scan reseau termine : " + count + " reseaux affiches (" + total + " detectes).", "ok");
      return;
    }
    setStatus("Aucun reseau visible detecte.", "info");
  }

  async function refreshScan(start) {
    window.clearTimeout(state.scanTimer);
    try {
      if (start) {
        await apiJson("/api/wifi/scan", postForm({ force: "1" }), "scan");
        setStatus("Scan reseau lance...", "info");
      }
      const data = await apiJson("/api/wifi/scan", { cache: "no-store" }, "scan");
      renderNetworks(data);
      updateScanStatus(data);
      if (data.running || data.requested) {
        state.scanTimer = window.setTimeout(() => refreshScan(false), 1200);
      }
    } catch (err) {
      setStatus("Erreur scan reseau: " + err.message, "error");
    }
  }

  async function loadMeta() {
    try {
      const meta = await apiJson("/api/web/meta", { cache: "no-store" }, "meta");
      const chip = el("provFirmware");
      if (chip) chip.textContent = meta.firmware_version || meta.profile_name || "Provisioning";
    } catch (err) {
      const chip = el("provFirmware");
      if (chip) chip.textContent = "Provisioning";
    }
  }

  async function loadWifi() {
    try {
      const data = await apiJson("/api/wifi/config", { cache: "no-store" }, "wifi");
      el("provWifiEnabled").checked = toBool(data.enabled);
      el("provWifiSsid").value = data.ssid || "";
      el("provWifiPass").value = data.pass || "";
      setStatus("Configuration reseau prete.", "ok");
      refreshScan(true);
    } catch (err) {
      setStatus("Chargement reseau echoue: " + err.message, "error");
    }
  }

  async function saveWifi() {
    if (state.saving) return;
    const ssid = (el("provWifiSsid").value || "").trim();
    if (!ssid) {
      setStatus("Le SSID est requis.", "error");
      return;
    }

    state.saving = true;
    setBusy(true);
    setStatus("Enregistrement reseau...", "info");
    try {
      const data = await apiJson("/api/wifi/config", postForm({
        enabled: el("provWifiEnabled").checked ? "1" : "0",
        ssid,
        pass: el("provWifiPass").value || ""
      }), "wifi");

      setStatus("Configuration reseau enregistree. Redemarrage en cours...", "ok");
      if (!data.reboot_scheduled) {
        await fetch("/api/system/reboot", { method: "POST" }).catch(() => null);
      }
    } catch (err) {
      state.saving = false;
      setBusy(false);
      setStatus("Enregistrement reseau echoue: " + err.message, "error");
    }
  }

  function bind() {
    el("provWifiScan").addEventListener("click", () => refreshScan(true));
    el("provWifiSave").addEventListener("click", saveWifi);
    el("provWifiSsidList").addEventListener("change", () => {
      const selected = el("provWifiSsidList").value;
      if (selected) el("provWifiSsid").value = selected;
    });
    el("provTogglePass").addEventListener("click", () => {
      const input = el("provWifiPass");
      const button = el("provTogglePass");
      const show = input.type === "password";
      input.type = show ? "text" : "password";
      button.textContent = show ? "Masquer" : "Voir";
      button.setAttribute("aria-pressed", show ? "true" : "false");
      button.setAttribute("aria-label", show ? "Masquer le mot de passe" : "Afficher le mot de passe");
    });
  }

  bind();
  loadMeta();
  loadWifi();
}());
