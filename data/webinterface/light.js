const $ = (id) => document.getElementById(id);

const ui = {
  product: "flow.io",
  rebootAfterWifi: false
};

function status(text) {
  $("status").textContent = text;
}

async function json(url, options) {
  const res = await fetch(url, options);
  const data = await res.json().catch(() => null);
  if (!res.ok || !data || data.ok === false) throw new Error(url);
  return data;
}

function form(values) {
  return new URLSearchParams(values);
}

function applyBrand(meta) {
  const profile = String((meta && (meta.profile || meta.profile_name)) || "").toLowerCase();
  const product = String((meta && meta.product_name) || "");
  const isDisplay = product === "Flow Connect Display" || profile.indexOf("flowconnectdisplay") >= 0 || profile.indexOf("flow_connect_display") >= 0;
  const name = isDisplay ? "Flow Connect Display" : profile === "micronova" ? "Pellet" : "Flow";
  const suffix = document.querySelector(".brand-io");
  $("brandName").textContent = name;
  if (suffix) suffix.textContent = isDisplay ? "" : ".io";
  $("brand").setAttribute("aria-label", isDisplay ? name : name + ".io");
  document.title = isDisplay ? name : name + ".io";
}

function applyCapabilities(meta) {
  ui.product = String((meta && meta.product_name) || ui.product);
  ui.rebootAfterWifi = !!(meta && meta.reboot_after_wifi_save);
}

let scanTimer = 0;

function scanStatus(text) {
  $("wifiScanStatus").textContent = text;
}

function renderWifiScan(data) {
  const nets = data && Array.isArray(data.networks) ? data.networks : [];
  const list = $("wifiSsidList");
  const current = ($("wifiSsid").value || "").trim();
  list.innerHTML = "";
  const manual = document.createElement("option");
  manual.value = "";
  manual.textContent = "Saisie manuelle";
  list.appendChild(manual);
  for (const net of nets) {
    if (!net || !net.ssid || net.hidden) continue;
    const opt = document.createElement("option");
    const secure = net.secure ? " (securise)" : " (ouvert)";
    const rssi = Number.isFinite(net.rssi) ? " " + net.rssi + " dBm" : "";
    opt.value = net.ssid;
    opt.textContent = (net.ssid + secure + rssi).slice(0, 72);
    list.appendChild(opt);
  }
  if (current) {
    for (const opt of list.options) {
      if (opt.value === current) {
        list.value = current;
        break;
      }
    }
  }
}

function updateScanStatus(data) {
  if (!data || data.ok !== true) {
    scanStatus("Scan réseau indisponible.");
    return;
  }
  const running = !!data.running || !!data.requested;
  const count = Number.isFinite(data.count) ? data.count : 0;
  const total = Number.isFinite(data.total_found) ? data.total_found : count;
  if (running) {
    scanStatus("Scan réseau en cours...");
    return;
  }
  scanStatus(count > 0
    ? "Scan réseau terminé : " + count + " réseaux affichés (" + total + " détectés)."
    : "Aucun réseau visible détecté.");
}

async function refreshWifiScan(trigger) {
  clearTimeout(scanTimer);
  try {
    if (trigger) {
      await json("/api/wifi/scan", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: form({ force: "1" })
      });
      scanStatus("Scan réseau lancé...");
    }

    let data = null;
    let lastErr = null;
    for (let attempt = 0; attempt < 8; attempt += 1) {
      try {
        data = await json("/api/wifi/scan", { cache: "no-store" });
        renderWifiScan(data);
        updateScanStatus(data);
        if (!data.running && !data.requested) break;
      } catch (err) {
        lastErr = err;
      }
      await new Promise((resolve) => setTimeout(resolve, 450));
    }

    if (!data && lastErr) {
      throw lastErr;
    }
    if (data && (data.running || data.requested)) {
      scanTimer = setTimeout(() => refreshWifiScan(false), 1200);
    }
  } catch (err) {
    scanStatus("Erreur scan réseau: " + err.message);
  }
}

async function loadAll() {
  try {
    const meta = await json("/api/web/meta");
    applyBrand(meta);
    applyCapabilities(meta);
    $("subtitle").textContent = "Configuration Initiale - " + (meta.firmware_version || "flow.io");
  } catch (err) {
    status("Meta indisponible");
  }

  try {
    const wifi = await json("/api/wifi/config");
    $("wifiEnabled").checked = !!wifi.enabled;
    $("wifiSsid").value = wifi.ssid || "";
    $("wifiPass").value = wifi.pass || "";
    status("Pret");
  } catch (err) {
    status("Erreur réseau: " + err.message);
  }
}

async function saveWifi() {
  try {
    const data = await json("/api/wifi/config", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: form({
        enabled: $("wifiEnabled").checked ? "1" : "0",
        ssid: $("wifiSsid").value,
        pass: $("wifiPass").value
      })
    });
    status((ui.rebootAfterWifi || data.reboot_scheduled)
      ? "Réseau enregistre. Redemarrage en cours..."
      : "Réseau enregistre. Redemarrage immediat...");
    await fetch("/api/system/reboot", { method: "POST" });
  } catch (err) {
    status("Erreur réseau: " + err.message);
  }
}

$("wifiScanBtn").addEventListener("click", () => refreshWifiScan(true));
$("wifiSsidList").addEventListener("change", () => {
  const value = $("wifiSsidList").value;
  if (value) $("wifiSsid").value = value;
});
$("wifiSave").addEventListener("click", () => saveWifi());

loadAll();
