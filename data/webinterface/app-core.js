(function () {
  var versionKey = 'flow_web_asset_version';
  var themeKey = 'flow_web_theme';
  var scriptLoads = new Map();
  var cssLoads = new Map();

  function sleep(ms) {
    return new Promise(function (resolve) { setTimeout(resolve, ms); });
  }

  function parseRetryAfterMs(value, fallbackMs) {
    var raw = String(value || '').trim();
    if (!raw) return fallbackMs;
    var sec = Number(raw);
    if (Number.isFinite(sec) && sec >= 0) {
      return Math.max(120, Math.min(10000, Math.round(sec * 1000)));
    }
    var ts = Date.parse(raw);
    if (Number.isFinite(ts)) {
      return Math.max(120, Math.min(10000, ts - Date.now()));
    }
    return fallbackMs;
  }

  function setBootStatus(text, isError) {
    var bootStatus = document.getElementById('bootStatus');
    if (!bootStatus) return;
    bootStatus.textContent = text;
    bootStatus.className = isError ? 'boot-status error' : 'boot-status';
  }

  function getStoredVersion() {
    try {
      return String(localStorage.getItem(versionKey) || '').trim();
    } catch (err) {
      return '';
    }
  }

  function storeVersion(version) {
    if (!version) return;
    try {
      localStorage.setItem(versionKey, version);
    } catch (err) {}
  }

  function readStoredTheme() {
    try {
      return localStorage.getItem(themeKey) === 'dark' ? 'dark' : 'light';
    } catch (err) {
      return 'light';
    }
  }

  function applyStoredTheme() {
    var theme = readStoredTheme();
    try {
      document.documentElement.setAttribute('data-theme', theme);
      document.documentElement.style.colorScheme = theme;
    } catch (err) {}
    return theme;
  }

  function assetUrl(path, version) {
    if (!version) return path;
    return path + '?v=' + encodeURIComponent(version);
  }

  async function supervisorFetch(url, options, policy) {
    var cfg = policy || {};
    var retries = Number.isFinite(cfg.retries) ? cfg.retries : 4;
    var backoff = Array.isArray(cfg.backoffMs) && cfg.backoffMs.length
      ? cfg.backoffMs.slice()
      : [300, 700, 1500, 2600];

    var lastError = null;
    for (var attempt = 0; attempt <= retries; attempt += 1) {
      try {
        var response = await fetch(url, options || {});
        if (response.status !== 503) return response;
        if (attempt >= retries) return response;
        var retryAfterHeader = response.headers ? response.headers.get('Retry-After') : '';
        var fallback = backoff[Math.min(attempt, backoff.length - 1)] || 1200;
        var waitMs = parseRetryAfterMs(retryAfterHeader, fallback);
        setBootStatus('Supervisor occupé, nouvelle tentative...');
        await sleep(waitMs);
      } catch (err) {
        lastError = err;
        if (attempt >= retries) throw err;
        var networkWait = backoff[Math.min(attempt, backoff.length - 1)] || 1200;
        await sleep(networkWait);
      }
    }
    if (lastError) throw lastError;
    throw new Error('fetch_failed');
  }

  function loadScriptOnce(src, policy) {
    if (scriptLoads.has(src)) return scriptLoads.get(src);
    var promise = (async function () {
      var retries = policy && Number.isFinite(policy.retries) ? policy.retries : 3;
      var timeoutMs = policy && Number.isFinite(policy.timeoutMs) ? policy.timeoutMs : 9000;
      var backoff = policy && Array.isArray(policy.backoffMs) ? policy.backoffMs : [350, 900, 1800];
      for (var attempt = 0; attempt <= retries; attempt += 1) {
        try {
          await new Promise(function (resolve, reject) {
            var script = document.createElement('script');
            var settled = false;
            var timer = setTimeout(function () {
              if (settled) return;
              settled = true;
              reject(new Error('script_timeout'));
            }, timeoutMs);
            script.src = src;
            script.async = false;
            script.onload = function () {
              if (settled) return;
              settled = true;
              clearTimeout(timer);
              resolve();
            };
            script.onerror = function () {
              if (settled) return;
              settled = true;
              clearTimeout(timer);
              reject(new Error('script_error'));
            };
            document.body.appendChild(script);
          });
          return true;
        } catch (err) {
          if (attempt >= retries) throw err;
          await sleep(backoff[Math.min(attempt, backoff.length - 1)] || 1200);
        }
      }
      return false;
    })();

    scriptLoads.set(src, promise);
    promise.catch(function () {
      scriptLoads.delete(src);
    });
    return promise;
  }

  function loadCssOnce(href, policy) {
    if (cssLoads.has(href)) return cssLoads.get(href);
    var promise = (async function () {
      var retries = policy && Number.isFinite(policy.retries) ? policy.retries : 3;
      var timeoutMs = policy && Number.isFinite(policy.timeoutMs) ? policy.timeoutMs : 8000;
      var backoff = policy && Array.isArray(policy.backoffMs) ? policy.backoffMs : [250, 700, 1400];
      for (var attempt = 0; attempt <= retries; attempt += 1) {
        try {
          await new Promise(function (resolve, reject) {
            var link = document.createElement('link');
            var settled = false;
            var timer = setTimeout(function () {
              if (settled) return;
              settled = true;
              reject(new Error('css_timeout'));
            }, timeoutMs);
            link.rel = 'stylesheet';
            link.href = href;
            link.onload = function () {
              if (settled) return;
              settled = true;
              clearTimeout(timer);
              resolve();
            };
            link.onerror = function () {
              if (settled) return;
              settled = true;
              clearTimeout(timer);
              reject(new Error('css_error'));
            };
            document.head.appendChild(link);
          });
          return true;
        } catch (err) {
          if (attempt >= retries) throw err;
          await sleep(backoff[Math.min(attempt, backoff.length - 1)] || 1200);
        }
      }
      return false;
    })();

    cssLoads.set(href, promise);
    promise.catch(function () {
      cssLoads.delete(href);
    });
    return promise;
  }

  async function fetchShellMarkup(url) {
    var res = await supervisorFetch(url, { cache: 'no-store' }, { retries: 4 });
    if (!res.ok) throw new Error('shell');
    return res.text();
  }

  async function fetchWebMetaVersion() {
    var res = await supervisorFetch('/api/web/meta', { cache: 'no-store' }, { retries: 4 });
    if (!res.ok) throw new Error('meta');
    var data = await res.json();
    if (!data || data.ok !== true) throw new Error('meta');
    window.__FLOW_WEB_META__ = data;
    var version = '';
    if (typeof data.web_asset_version === 'string') {
      version = data.web_asset_version.trim();
    }
    return version;
  }

  async function bootstrap() {
    var root = document.getElementById('app-root');
    if (!root) return;
    applyStoredTheme();

    try {
      var version = '';
      try {
        version = await fetchWebMetaVersion();
      } catch (err) {
        version = getStoredVersion();
      }

      window.__FLOW_WEB_ASSET_VERSION__ = version;
      storeVersion(version);

      await loadCssOnce(assetUrl('/webinterface/app-core.css', version), { retries: 3 });
      root.className = '';
      root.innerHTML = await fetchShellMarkup(assetUrl('/webinterface/sh.html', version));
      await loadScriptOnce(assetUrl('/webinterface/app.js', version), { retries: 3 });
    } catch (err) {
      setBootStatus("Chargement de l'interface impossible.", true);
    }
  }

  window.FlowWebCore = {
    sleep: sleep,
    assetUrl: assetUrl,
    parseRetryAfterMs: parseRetryAfterMs,
    supervisorFetch: supervisorFetch,
    loadScriptOnce: loadScriptOnce,
    loadCssOnce: loadCssOnce,
    bootstrap: bootstrap,
    applyStoredTheme: applyStoredTheme,
    setBootStatus: setBootStatus
  };
})();
