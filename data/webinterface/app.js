    const drawer = document.getElementById('drawer');
    const overlay = document.getElementById('overlay');
    const menuToggles = Array.from(document.querySelectorAll('[data-menu-toggle]'));
    const menuItems = Array.from(document.querySelectorAll('.menu-item'));
    const pages = Array.from(document.querySelectorAll('.page'));
    const mobileTopbarTitle = document.getElementById('mobileTopbarTitle');
    const desktopPageTitle = document.getElementById('desktopPageTitle');
    const headerNetworkIcon = document.getElementById('headerNetworkIcon');
    const networkConfigIcon = document.getElementById('networkConfigIcon');
    const applyNetworkIcon = document.getElementById('applyNetworkIcon');
    const headerWifiDot = document.getElementById('headerWifiDot');
    const headerWifiStatus = document.getElementById('headerWifiStatus');
    const headerReachabilityDot = document.getElementById('headerReachabilityDot');
    const headerDeviceStatus = document.getElementById('headerDeviceStatus');
    const headerClockLabel = document.getElementById('headerClockLabel');
    const headerClockStatus = document.getElementById('headerClockStatus');
    const themeToggle = document.getElementById('themeToggle');
    const flowWebAssetVersionStorageKey = 'flow_web_asset_version';
    const flowWebThemeStorageKey = 'flow_web_theme';
    const deferredVisualAssetsStateKey = 'flow_web_deferred_visual_assets';
    const upgradeUiSessionStorageKey = 'flow_upgrade_ui_session';
    const upgradeStatusPollActiveMs = 900;
    const upgradeStatusPollReconnectMs = 5000;
    const upgradeStatusPollDoneMs = 7000;
    const upgradeStatusPollIdleMs = 15000;
    const upgradeStatusPollErrorMs = 10000;
    const rebootActionDelaySeconds = 5;
    const deferredMenuAssetStartDelayMs = 520;
    const deferredMenuAssetStepMs = 140;
    const deferredMenuAssetReloadDelayMs = 1400;
    const deferredMenuAssetReloadStepMs = 850;
    const deferredMenuAssetReloadFallbackDelayMs = 6500;
    const deviceReachabilityProbeMs = 5000;
    const deviceReachabilityFetchTimeoutMs = 2500;
    const deviceReachabilityLostThreshold = 3;
    const remoteMenuIconFontLinkId = 'flowMenuIconFontRemote';
    const remoteMenuIconFontHref = 'https://fonts.googleapis.com/icon?family=Material+Symbols+Rounded&display=block';
    const remoteMenuIconLigatures = {
      'icon-measures': 'water_damage',
      'icon-pool': 'pool',
      'icon-io': 'lan',
      'icon-calibration': 'science',
      'icon-terminal': 'list_alt',
      'icon-activity': 'history',
      'icon-system': 'system_update_alt',
      'icon-flowcfg': 'settings',
      'icon-info': 'info'
    };
    const infoRefreshActiveMs = 10000;
    const infoSupervisorRefreshMs = 1000;
    const infoFlowRefreshActiveMs = 10000;
    const infoFlowRefreshIdleMs = 10000;
    const cfgI18nDebugEnabled = false;
    const flowStatusDebugEnabled = true;
    let webAssetVersion = '';
    let loadedWebAssetVersion = '';
    let supervisorFirmwareVersion = '-';
    let nextionDisplayVersion = '';
    let nextionDisplayVersionDetected = false;
    let nextionDisplayVersionCompatible = false;
    let nextionDisplayExpectedVersion = '';
    let supervisorUptimeMs = 0;
    let supervisorHeap = {};
    let webProfileName = 'Supervisor';
    let webDeviceName = 'flowio';
    let infoLastMac = '-';
    let webProfileKey = 'supervisor';
    let webLocalConfigLabel = 'Config Store Supervisor';
    let webLocalRuntime = false;
    let webRemoteConfigEnabled = true;
    let hideMenuSvg = false;
    let disableWebIcons = false;
    let unifyStatusCardIcons = false;
    let flowStatusLiveTimer = null;
    let pageLoadToken = 0;
    let currentPageId = '';
    let deferredVisualAssetsScheduled = false;
    let menuAssetsActivated = false;
    let deferredMenuAssetsArmed = false;
    let fieldApplyCheckIcon = '✓';
    let networkMode = 'none';
    let networkTransport = 'none';
    let currentFlowTimeSourceLabel = '';
    let useRemoteMenuIcons = false;
    let remoteMenuIconFontReady = false;
    let remoteMenuIconFontPromise = null;
    let appHeaderClockTimer = null;
    let deviceReachabilityTimer = null;
    let deviceReachabilityInFlight = false;
    let deviceReachabilityMisses = 0;
    let deviceReachabilityReachable = false;
    const pendingSystemActionCountdowns = new Map();
    let activeColorPickerPopover = null;
    let webUiLocale = 'fr';
    let webUiLocaleProbeInFlight = false;
    let webUiLocaleProbePromise = null;
    let webUiLocaleNextProbeAt = 0;
    const webUiLocaleProbeActiveMs = 5000;
    const webUiLocaleProbeIdleMs = 15000;
    let webUiI18n = { fr: {}, en: {} };
    const webUiLocaleBundleState = {
      loaded: {},
      loading: {}
    };

    function normalizeWebUiLocale(raw) {
      const value = String(raw || '').trim().toLowerCase().replace('_', '-');
      if (!value) return 'fr';
      if (value.startsWith('en')) return 'en';
      return 'fr';
    }

    function currentWebLocaleTag() {
      return webUiLocale === 'en' ? 'en-US' : 'fr-FR';
    }

    function tr(key, fallback) {
      const localized = webUiI18n[webUiLocale] && webUiI18n[webUiLocale][key];
      if (typeof localized === 'string' && localized.length > 0) return localized;
      const fr = webUiI18n.fr && webUiI18n.fr[key];
      if (typeof fr === 'string' && fr.length > 0) return fr;
      if (typeof fallback === 'string' && fallback.length > 0) return fallback;
      return key;
    }

    function cfgI18nDebugLog(message, details) {
      if (!cfgI18nDebugEnabled) return;
      if (!window || !window.console || typeof window.console.info !== 'function') return;
      if (typeof details === 'undefined') {
        window.console.info('[cfg-i18n] ' + String(message || ''));
        return;
      }
      window.console.info('[cfg-i18n] ' + String(message || ''), details);
    }

    function flowStatusDebugLog(message, details) {
      if (!flowStatusDebugEnabled) return;
      if (!window || !window.console || typeof window.console.warn !== 'function') return;
      if (typeof details === 'undefined') {
        window.console.warn('[flow-status] ' + String(message || ''));
        return;
      }
      window.console.warn('[flow-status] ' + String(message || ''), details);
    }

    function cfgDocTr(token, fallback) {
      const key = String(token || '').trim();
      if (!key) return String(fallback || '');
      const localized = flowCfgDocI18nMap && flowCfgDocI18nMap[key];
      if (typeof localized === 'string' && localized.length > 0) return localized;
      if (typeof fallback === 'string' && fallback.length > 0) return fallback;
      return key;
    }

    function webI18nAssetUrlForLocale(locale) {
      const base = assetUrl('/webinterface/i18n/' + locale + '.json');
      const sep = base.indexOf('?') >= 0 ? '&' : '?';
      return base + sep + 'l=' + encodeURIComponent(locale);
    }

    async function ensureWebUiLocaleBundle(locale, forceReload) {
      const normalized = normalizeWebUiLocale(locale);
      if (!forceReload && webUiLocaleBundleState.loaded[normalized]) return true;
      if (webUiLocaleBundleState.loading[normalized]) {
        return webUiLocaleBundleState.loading[normalized];
      }

      const promise = (async () => {
        try {
          const response = await fetchWithBusyRetry(
            webI18nAssetUrlForLocale(normalized),
            { cache: 'no-store' }
          );
          const payload = await response.json().catch(() => null);
          const source = payload && typeof payload === 'object' && payload.translations && typeof payload.translations === 'object'
            ? payload.translations
            : payload;
          if (!response.ok || !source || typeof source !== 'object') return false;

          const mapped = {};
          Object.keys(source).forEach((rawKey) => {
            if (typeof source[rawKey] !== 'string') return;
            const key = String(rawKey || '').trim();
            if (!key) return;
            mapped[key] = source[rawKey];
          });
          webUiI18n[normalized] = mapped;
          webUiLocaleBundleState.loaded[normalized] = true;
          return true;
        } catch (err) {
          return false;
        } finally {
          delete webUiLocaleBundleState.loading[normalized];
        }
      })();

      webUiLocaleBundleState.loading[normalized] = promise;
      return promise;
    }

    function applyStaticTranslations() {
      document.querySelectorAll('[data-i18n]').forEach((node) => {
        const key = String(node.getAttribute('data-i18n') || '').trim();
        if (!key) return;
        const fallback = String(node.textContent || '').trim();
        node.textContent = tr(key, fallback);
      });
      document.querySelectorAll('[data-i18n-placeholder]').forEach((node) => {
        const key = String(node.getAttribute('data-i18n-placeholder') || '').trim();
        if (!key) return;
        const fallback = String(node.getAttribute('placeholder') || '').trim();
        node.setAttribute('placeholder', tr(key, fallback));
      });
      document.querySelectorAll('[data-i18n-title]').forEach((node) => {
        const key = String(node.getAttribute('data-i18n-title') || '').trim();
        if (!key) return;
        const fallback = String(node.getAttribute('title') || '').trim();
        node.setAttribute('title', tr(key, fallback));
      });
      document.querySelectorAll('[data-i18n-aria-label]').forEach((node) => {
        const key = String(node.getAttribute('data-i18n-aria-label') || '').trim();
        if (!key) return;
        const fallback = String(node.getAttribute('aria-label') || '').trim();
        node.setAttribute('aria-label', tr(key, fallback));
      });
    }

    function applyWebUiLocale(locale) {
      const normalized = normalizeWebUiLocale(locale);
      if (webUiLocale === normalized) {
        applyStaticTranslations();
        syncMobileTopbarTitle(getActivePageId());
        updateInfoLoadButtonsText();
        updateThemeToggleUi(currentThemePreference());
        refreshCfgDocLocaleRuntime(false).catch(() => {});
        ensureWebUiLocaleBundle(normalized, false).then((loaded) => {
          if (!loaded || webUiLocale !== normalized) return;
          applyStaticTranslations();
          syncMobileTopbarTitle(getActivePageId());
          updateInfoLoadButtonsText();
          updateThemeToggleUi(currentThemePreference());
          applyProfileUiText();
          syncMenuIconFallbacks();
          renderInfoPanel();
          refreshPoolMeasuresView();
          if (getActivePageId() === 'page-pool' && poolConfigLoadedOnce) {
            loadPoolConfig(true).catch(() => {});
          }
          refreshCfgDocLocaleRuntime(false).catch(() => {});
        }).catch(() => {});
        return;
      }
      webUiLocale = normalized;
      document.documentElement.lang = webUiLocale;
      document.body.setAttribute('data-ui-locale', webUiLocale);
      const knownLocalLabels = new Set([
        'Config Store Supervisor',
        'Config Store Micronova',
        'Supervisor Config Store',
        'Micronova Config Store'
      ]);
      if (knownLocalLabels.has(String(webLocalConfigLabel || '').trim())) {
        webLocalConfigLabel = isMicronovaProfile()
          ? tr('cfg.local.micronova', 'Config Store Micronova')
          : tr('cfg.local.supervisor', 'Config Store Supervisor');
      }
      applyStaticTranslations();
      syncMobileTopbarTitle(getActivePageId());
      updateInfoLoadButtonsText();
      updateThemeToggleUi(currentThemePreference());
      applyProfileUiText();
      syncMenuIconFallbacks();
      renderInfoPanel();
      refreshPoolMeasuresView();
      refreshCfgDocLocaleRuntime(true).catch(() => {});

      ensureWebUiLocaleBundle(normalized, false).then((loaded) => {
        if (!loaded || webUiLocale !== normalized) return;
        applyStaticTranslations();
        syncMobileTopbarTitle(getActivePageId());
        updateInfoLoadButtonsText();
        updateThemeToggleUi(currentThemePreference());
        applyProfileUiText();
        syncMenuIconFallbacks();
        renderInfoPanel();
        refreshPoolMeasuresView();
        if (getActivePageId() === 'page-pool' && poolConfigLoadedOnce) {
          loadPoolConfig(true).catch(() => {});
        }
        refreshCfgDocLocaleRuntime(true).catch(() => {});
      }).catch(() => {});
    }

    async function fetchConfiguredWebUiLocale() {
      const res = await fetchWithBusyRetry('/api/supervisorcfg/module?name=system', { cache: 'no-store' });
      const data = await res.json().catch(() => null);
      if (!res.ok || !data || data.ok !== true || !data.data || typeof data.data !== 'object') {
        cfgI18nDebugLog('supervisorcfg/system unavailable for locale probe', {
          ok: !!(data && data.ok),
          status: res ? res.status : 0
        });
        return null;
      }
      const payload = data.data;
      cfgI18nDebugLog('supervisorcfg/system locale payload', payload);
      const tryLocale = (value) => {
        const raw = String(value || '').trim();
        if (!raw) return '';
        return normalizeWebUiLocale(raw);
      };

      const direct = tryLocale(payload.lang);
      if (direct) {
        cfgI18nDebugLog('locale resolved from key `lang`', { locale: direct });
        return direct;
      }

      const keyedCandidates = ['system/lang', 'system.lang', 'web/lang', 'web.lang'];
      for (const key of keyedCandidates) {
        if (!Object.prototype.hasOwnProperty.call(payload, key)) continue;
        const candidate = tryLocale(payload[key]);
        if (candidate) {
          cfgI18nDebugLog('locale resolved from key `' + key + '`', { locale: candidate });
          return candidate;
        }
      }

      const nestedCandidates = ['system', 'web'];
      for (const groupKey of nestedCandidates) {
        const group = payload[groupKey];
        if (!group || typeof group !== 'object') continue;
        const candidate = tryLocale(group.lang);
        if (candidate) {
          cfgI18nDebugLog('locale resolved from nested `' + groupKey + '.lang`', { locale: candidate });
          return candidate;
        }
      }

      cfgI18nDebugLog('locale probe failed: no matching key found', Object.keys(payload));
      return null;
    }

    async function refreshWebUiLocale(forceRefresh) {
      const now = Date.now();
      const probeWindow = (!document.hidden && getActivePageId() === 'page-info')
        ? webUiLocaleProbeActiveMs
        : webUiLocaleProbeIdleMs;
      if (!forceRefresh && now < webUiLocaleNextProbeAt) return;
      if (webUiLocaleProbeInFlight) {
        if (webUiLocaleProbePromise) {
          await webUiLocaleProbePromise.catch(() => {});
        }
        return;
      }
      webUiLocaleProbeInFlight = true;
      webUiLocaleProbePromise = (async () => {
        try {
          const next = await fetchConfiguredWebUiLocale();
          if (next) applyWebUiLocale(next);
        } catch (err) {
        } finally {
          webUiLocaleProbeInFlight = false;
          webUiLocaleNextProbeAt = Date.now() + probeWindow;
          webUiLocaleProbePromise = null;
        }
      })();
      await webUiLocaleProbePromise;
    }

    function normalizeNetworkMode(value) {
      const raw = String(value || '').trim().toLowerCase();
      if (raw === 'ap' || raw === 'accesspoint' || raw === 'access_point') return 'ap';
      if (raw === 'station' || raw === 'sta') return 'station';
      if (raw === 'ethernet') return 'ethernet';
      return 'none';
    }

    function normalizeNetworkType(value) {
      const raw = String(value || '').trim().toLowerCase();
      if (raw === 'ethernet' || raw === 'eth' || raw === 'wired') return 'ethernet';
      if (raw === 'wifi' || raw === 'wi-fi' || raw === 'station' || raw === 'sta') return 'wifi';
      return '';
    }

    function normalizeNetworkTransport(value) {
      const raw = normalizeNetworkType(value);
      return raw || 'none';
    }

    function isAccessPointMode() {
      return normalizeNetworkMode(networkMode) === 'ap';
    }

    function applyMenuIconSourcePreference(useRemote) {
      useRemoteMenuIcons = !!useRemote;
      document.body.classList.toggle('menu-icons-remote', useRemoteMenuIcons);
      document.body.classList.toggle('menu-icons-letter-fallback', false);
      syncMenuIconFallbacks();
    }

    function menuIconLigatureForNode(iconNode) {
      if (!iconNode || !iconNode.classList) return '';
      const iconClasses = Object.keys(remoteMenuIconLigatures);
      for (let i = 0; i < iconClasses.length; i += 1) {
        const cls = iconClasses[i];
        if (iconNode.classList.contains(cls)) return remoteMenuIconLigatures[cls];
      }
      return '';
    }

    function ensureRemoteMenuIconFontLoaded() {
      if (remoteMenuIconFontReady) return Promise.resolve(true);
      if (remoteMenuIconFontPromise) return remoteMenuIconFontPromise;

      remoteMenuIconFontPromise = new Promise((resolve) => {
        let link = document.getElementById(remoteMenuIconFontLinkId);
        if (link) {
          remoteMenuIconFontReady = link.getAttribute('data-flow-ready') === '1';
          if (remoteMenuIconFontReady) {
            resolve(true);
            return;
          }
          link.remove();
        }
        link = document.createElement('link');
        link.id = remoteMenuIconFontLinkId;
        link.rel = 'stylesheet';
        link.href = remoteMenuIconFontHref;
        let done = false;
        const finalize = (ok) => {
          if (done) return;
          done = true;
          remoteMenuIconFontReady = !!ok;
          if (remoteMenuIconFontReady) {
            link.setAttribute('data-flow-ready', '1');
          }
          resolve(remoteMenuIconFontReady);
        };
        link.onload = () => finalize(true);
        link.onerror = () => finalize(false);
        document.head.appendChild(link);
        setTimeout(() => finalize(false), 2600);
      }).finally(() => {
        remoteMenuIconFontPromise = null;
      });

      return remoteMenuIconFontPromise;
    }

    function menuFallbackLetter(label) {
      const text = String(label || '').trim();
      if (!text) return '?';
      return Array.from(text)[0].toLocaleUpperCase(currentWebLocaleTag());
    }

    function iconCheckText() {
      return '✓';
    }

    function syncMenuIconFallbacks() {
      const useLetterFallback = document.body.classList.contains('menu-icons-letter-fallback');
      menuItems.forEach((item) => {
        if (!item) return;
        const icon = item.querySelector('.ico');
        const label = item.querySelector('.label');
        if (!icon || !label) return;
        const fallback = menuFallbackLetter(label.textContent);
        icon.setAttribute('data-fallback-text', fallback);
        if (disableWebIcons || useLetterFallback) {
          icon.textContent = fallback;
          return;
        }
        if (useRemoteMenuIcons) {
          icon.textContent = menuIconLigatureForNode(icon);
          return;
        }
        icon.textContent = '';
      });
    }

    function syncRenderedCheckFallbacks() {
      const checkText = iconCheckText();
      document.querySelectorAll('.step-ic.done').forEach((node) => {
        if (!node) return;
        node.textContent = checkText;
      });
      document.querySelectorAll('.status-flag-check.is-true').forEach((node) => {
        if (!node) return;
        node.textContent = checkText;
      });
      document.querySelectorAll('.measure-domain-chip-check').forEach((node) => {
        if (!node) return;
        node.textContent = checkText;
      });
      document.querySelectorAll('.control-field-apply').forEach((node) => {
        if (!node) return;
        node.textContent = checkText;
      });
    }

    function applyIconUsagePreference(disabled) {
      disableWebIcons = !!disabled;
      document.body.classList.toggle('web-icons-disabled', disableWebIcons);
      document.body.classList.toggle('menu-icons-letter-fallback', false);
      fieldApplyCheckIcon = iconCheckText();
      syncMenuIconFallbacks();
      syncRenderedCheckFallbacks();
    }

    function applyMenuIconPreference(hidden) {
      hideMenuSvg = !!hidden;
      document.body.classList.toggle('menu-icons-disabled', hideMenuSvg);
    }

    function applyStatusIconPreference(unified) {
      unifyStatusCardIcons = !!unified;
    }

    function resolveSupervisorFirmwareVersion() {
      return '-';
    }

    function ingestWebProfileMeta(data) {
      if (!data || typeof data !== 'object') return;
      const rawProfile = String(data.profile_name || data.profile || '').trim();
      if (rawProfile) {
        webProfileName = rawProfile;
        webProfileKey = rawProfile.toLowerCase();
      }
      const rawDeviceName = String(data.devicename || data.deviceName || '').trim();
      webDeviceName = rawDeviceName || 'flowio';
      webLocalRuntime = data.local_runtime === true;
      const label = String(data.local_config_label || '').trim();
      webLocalConfigLabel = label || (isMicronovaProfile()
        ? tr('cfg.local.micronova', 'Config Store Micronova')
        : tr('cfg.local.supervisor', 'Config Store Supervisor'));
      webRemoteConfigEnabled = data.remote_config_enabled !== false;
      runtimeMeasureDomainKeys = runtimeDomainsForProfile();
      ensureRuntimeDomainState();
      runtimeManifestDomainCache = null;
      runtimeManifestDomainLoadPromise = null;
      if (isMicronovaProfile()) {
        poolMeasureDomainState.micronova.active = true;
      }
      if (webLocalRuntime && !isMicronovaProfile()) {
        logSourceMeta.supervisor.label = 'flow.io';
      } else {
        logSourceMeta.supervisor.label = 'Supervisor';
      }
      applyLogSourceUi();
      document.body.classList.toggle('profile-micronova', isMicronovaProfile());
      applyProfileUiText();
      renderPoolMeasureDomainButtons();
    }

    async function applyMenuIconModeFromMeta(data) {
      const mode = normalizeNetworkMode(data && data.network_mode);
      networkMode = mode;
      networkTransport = normalizeNetworkTransport(data && (data.network_transport || data.transport));
      const remoteReady = await ensureRemoteMenuIconFontLoaded().catch(() => false);
      applyMenuIconSourcePreference(remoteReady);
    }

    function isMicronovaProfile() {
      return webProfileKey === 'micronova';
    }

    function isWaveshareProfile() {
      const key = String(webProfileKey || '').trim().toLowerCase();
      return key === 'waveshare'
        || key === 'flowios3'
        || key === 'esp32s3'
        || key === 'esp32-s3'
        || key === 'flowio-s3'
        || key.indexOf('waveshare') >= 0
        || key.indexOf('flowios3') >= 0;
    }

    function isFlowIOProfile() {
      return String(webProfileKey || '').trim().toLowerCase() === 'flowio';
    }

    function isSupervisorProfile() {
      const key = String(webProfileKey || '').trim().toLowerCase();
      return key === 'supervisor' || key.indexOf('supervisor') === 0;
    }

    function runtimeDomainsForProfile() {
      return isMicronovaProfile()
        ? ['micronova', 'alarm']
        : ['mode', 'equipements', 'sondes', 'alarm'];
    }

    function createRuntimeDomainState() {
      return { active: false, loading: false, entries: [], values: [], sondeSlots: [], alarmSlots: [], error: '', requestSeq: 0 };
    }

    function ensureRuntimeDomainState() {
      runtimeMeasureDomainKeys.forEach((domainKey) => {
        if (!poolMeasureDomainState[domainKey]) {
          poolMeasureDomainState[domainKey] = createRuntimeDomainState();
        }
      });
    }

    function setLabelForInput(inputId, text) {
      const label = document.querySelector('label[for="' + inputId + '"]');
      if (label) label.textContent = text;
    }

    function hideFieldForInput(inputId, hidden) {
      const input = document.getElementById(inputId);
      const field = input ? input.closest('.field') : null;
      if (field) field.hidden = !!hidden;
    }

    function setSystemActionVisible(buttonId, visible) {
      const button = document.getElementById(buttonId);
      const action = button ? button.closest('.system-action') : null;
      if (action) action.hidden = !visible;
    }

    function setPageMenuVisible(pageId, visible) {
      const item = document.querySelector('[data-page="' + pageId + '"]');
      const page = document.getElementById(pageId);
      if (item) item.hidden = !visible;
      if (page) page.hidden = !visible;
    }

    function setBrandWordmark(firstPart) {
      const first = String(firstPart || '').trim() || 'Flow';
      document.querySelectorAll('.brand-flow').forEach((node) => {
        node.textContent = first;
      });
      document.querySelectorAll('.brand-wordmark,.mobile-title,.drawer-user').forEach((node) => {
        node.setAttribute('aria-label', first + '.io');
      });
      document.title = first + '.io';
    }

    function applyProfileUiText() {
      if (!document.body) return;
      setBrandWordmark(isMicronovaProfile() ? 'Pellet' : 'Flow');
      if (isMicronovaProfile()) {
        setPageMenuVisible('page-calibration', false);
        setPageMenuVisible('page-pool', false);
      }
      if (rebootDeviceTargetSelect) {
        const labelsByTarget = {
          supervisor: isMicronovaProfile() ? 'Micronova' : 'Supervisor',
          flow_soft: 'flow.io soft',
          flow_hard: 'flow.io hard',
          nextion: 'Nextion',
          factory_reset: 'Init Usine'
        };
        const blockValues = isMicronovaProfile()
          ? new Set(['flow_soft', 'flow_hard', 'nextion', 'factory_reset'])
          : (isWaveshareProfile() ? new Set(['supervisor', 'flow_hard']) : new Set());
        const hiddenValues = isWaveshareProfile()
          ? new Set(['supervisor', 'flow_hard'])
          : new Set();
        Array.from(rebootDeviceTargetSelect.options || []).forEach((option) => {
          if (!option) return;
          if (Object.prototype.hasOwnProperty.call(labelsByTarget, option.value)) {
            option.text = labelsByTarget[option.value];
          }
          option.disabled = blockValues.has(option.value);
          option.hidden = hiddenValues.has(option.value);
        });
        if (blockValues.has(rebootDeviceTargetSelect.value)) {
          const fallbackOption = Array.from(rebootDeviceTargetSelect.options || [])
            .find((option) => option && !option.disabled && !option.hidden);
          rebootDeviceTargetSelect.value = fallbackOption ? fallbackOption.value : 'supervisor';
        }
        if (factoryResetDeviceActionBtn) {
          factoryResetDeviceActionBtn.hidden = blockValues.has('factory_reset');
          factoryResetDeviceActionBtn.disabled = blockValues.has('factory_reset');
        }
      }
    }

    try {
      const browserLocale = (navigator && navigator.language) ? String(navigator.language) : '';
      webUiLocale = normalizeWebUiLocale(browserLocale);
    } catch (err) {
    }

    try {
      loadedWebAssetVersion = String(window.__FLOW_WEB_ASSET_VERSION__ || '').trim();
    } catch (err) {
      loadedWebAssetVersion = '';
    }
    webAssetVersion = loadedWebAssetVersion;
    if (!webAssetVersion) {
      webAssetVersion = getStorageValue(localStorage, flowWebAssetVersionStorageKey);
    }

    supervisorFirmwareVersion = resolveSupervisorFirmwareVersion();
    try {
      const initialMeta = window.__FLOW_WEB_META__;
      if (initialMeta && typeof initialMeta === 'object') {
        const rawProfile = String(initialMeta.profile_name || initialMeta.profile || '').trim();
        if (rawProfile) {
          webProfileName = rawProfile;
          webProfileKey = rawProfile.toLowerCase();
        }
        const rawDeviceName = String(initialMeta.devicename || initialMeta.deviceName || '').trim();
        webDeviceName = rawDeviceName || 'flowio';
        const label = String(initialMeta.local_config_label || '').trim();
        webLocalConfigLabel = label || (isMicronovaProfile()
          ? tr('cfg.local.micronova', 'Config Store Micronova')
          : tr('cfg.local.supervisor', 'Config Store Supervisor'));
        webLocalRuntime = initialMeta.local_runtime === true;
        webRemoteConfigEnabled = initialMeta.remote_config_enabled !== false;
        networkMode = normalizeNetworkMode(initialMeta.network_mode);
        networkTransport = normalizeNetworkTransport(initialMeta.network_transport || initialMeta.transport);
      }
    } catch (err) {
    }

    function assetUrl(path) {
      if (!webAssetVersion) return path;
      const sep = path.indexOf('?') >= 0 ? '&' : '?';
      return path + sep + 'v=' + encodeURIComponent(webAssetVersion);
    }

    async function fetchWithBusyRetry(url, options, fetchImpl) {
      if (typeof fetchImpl === 'function') {
        return fetchImpl(url, options);
      }
      if (window.FlowWebCore && typeof window.FlowWebCore.supervisorFetch === 'function') {
        return window.FlowWebCore.supervisorFetch(url, options, { retries: 4 });
      }
      return fetch(url, options);
    }

    function getStorageValue(storage, key) {
      try {
        return String(storage.getItem(key) || '').trim();
      } catch (err) {
        return '';
      }
    }

    function setStorageValue(storage, key, value) {
      try {
        storage.setItem(key, value);
      } catch (err) {
      }
    }

    function normalizeThemePreference(raw) {
      return String(raw || '').trim().toLowerCase() === 'dark' ? 'dark' : 'light';
    }

    function currentThemePreference() {
      return normalizeThemePreference(getStorageValue(localStorage, flowWebThemeStorageKey));
    }

    function themeToggleLabel(theme) {
      const isDark = theme === 'dark';
      if (webUiLocale === 'en') return isDark ? 'Light mode' : 'Dark mode';
      return isDark ? 'Mode clair' : 'Mode sombre';
    }

    function themeToggleTitle(theme) {
      const isDark = theme === 'dark';
      if (webUiLocale === 'en') return isDark ? 'Switch to light mode' : 'Switch to dark mode';
      return isDark ? 'Activer le mode clair' : 'Activer le mode sombre';
    }

    function updateThemeToggleUi(theme) {
      if (!themeToggle) return;
      const currentTheme = normalizeThemePreference(theme);
      const label = themeToggleLabel(currentTheme);
      const title = themeToggleTitle(currentTheme);
      const wrapper = themeToggle.closest('.theme-switch');
      const labelNode = wrapper ? wrapper.querySelector('.theme-toggle-label') : null;
      themeToggle.checked = currentTheme === 'dark';
      themeToggle.setAttribute('aria-label', title);
      if (wrapper) wrapper.setAttribute('title', title);
      if (labelNode) labelNode.textContent = label;
    }

    function applyThemePreference(theme, persist) {
      const currentTheme = normalizeThemePreference(theme);
      document.documentElement.setAttribute('data-theme', currentTheme);
      document.documentElement.style.colorScheme = currentTheme;
      if (persist) {
        setStorageValue(localStorage, flowWebThemeStorageKey, currentTheme);
      }
      updateThemeToggleUi(currentTheme);
    }

    async function fetchJsonResponse(url, options, fetchImpl) {
      const res = await fetchWithBusyRetry(url, options, fetchImpl);
      const data = await res.json().catch(() => null);
      return { res, data };
    }

    function extractApiErrorMessage(data, fallback) {
      if (!data || typeof data !== 'object') return fallback;
      const err = data.err;
      if (!err || typeof err !== 'object') return fallback;

      const msg = typeof err.msg === 'string' ? err.msg.trim() : '';
      const code = typeof err.code === 'string' ? err.code.trim() : '';
      const where = typeof err.where === 'string' ? err.where.trim() : '';
      const debugDetail = typeof err.detail === 'string' ? err.detail.trim() : '';
      const detail = msg || [code, where].filter(Boolean).join(' @ ');
      const composed = [detail, debugDetail].filter(Boolean).join(' | ');
      if (!composed) return fallback;
      return fallback ? (fallback + ' : ' + composed) : composed;
    }

    function normalizeUpgradeHttpErrorMessage(rawMessage, fallback) {
      const normalizedFallback = String(fallback || tr('updates.err.updateGeneric', 'Erreur de mise à jour.')).trim();
      let raw = String(rawMessage || '').trim().replace(/^error:\s*/i, '');
      if (normalizedFallback && raw.toLowerCase().startsWith(normalizedFallback.toLowerCase())) {
        raw = raw.slice(normalizedFallback.length).replace(/^\s*:\s*/, '').trim() || raw;
      }
      if (!raw) return normalizedFallback;
      const lower = raw.toLowerCase();
      const folded = typeof lower.normalize === 'function'
        ? lower.normalize('NFD').replace(/[\u0300-\u036f]/g, '')
        : lower;

      if (folded.includes('serveur http injoignable') || folded.includes('failed to fetch') || folded.includes('load failed')) {
        return tr('updates.err.serverUnreachable', 'Serveur HTTP d’upgrade non joignable.');
      }

      if (folded.includes('manifest introuvable (404)')) {
        return tr('updates.err.manifestNotFound', 'Manifest d’upgrade introuvable sur le serveur (404).');
      }

      if (folded.includes('fichier de mise a jour introuvable (404)')) {
        return tr('updates.err.fileNotFound', 'Fichier de mise à jour introuvable sur le serveur (404).');
      }

      const httpStatusMatch = folded.match(/(?:erreur\s+http|http)\s+(-?\d+)/);
      if (httpStatusMatch && httpStatusMatch[1]) {
        const status = httpStatusMatch[1];
        if (status === '404') return tr('updates.err.fileNotFound', 'Fichier de mise à jour introuvable sur le serveur (404).');
        if (status.charAt(0) === '-') return tr('updates.err.serverUnreachable', 'Serveur HTTP d’upgrade non joignable.');
        return tr('updates.err.httpStatus', 'Erreur HTTP {status} renvoyée par le serveur d’upgrade.')
          .replace('{status}', status);
      }

      return raw;
    }

    function ensureOkJsonResponse(response, message) {
      if (!response.res.ok || !response.data || response.data.ok !== true) {
        throw new Error(extractApiErrorMessage(response.data, message));
      }
      return response.data;
    }

    async function fetchOkJson(url, options, message, fetchImpl) {
      return ensureOkJsonResponse(await fetchJsonResponse(url, options, fetchImpl), message);
    }

    function createUrlEncodedBody(values) {
      const body = new URLSearchParams();
      Object.keys(values || {}).forEach((key) => {
        const value = values[key];
        body.set(key, value === null || typeof value === 'undefined' ? '' : String(value));
      });
      return body.toString();
    }

    function createFormPostOptions(values) {
      return {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
        body: createUrlEncodedBody(values)
      };
    }

    function utf8ByteLength(value) {
      const text = String(value || '');
      if (typeof TextEncoder === 'function') {
        return new TextEncoder().encode(text).length;
      }
      return text.length;
    }

    function runAsyncTaskSafely(task) {
      return Promise.resolve().then(task).catch(() => {});
    }

    function createIntervalRunner(task, delayMs) {
      let timer = null;
      return {
        start() {
          if (timer) return;
          timer = setInterval(() => {
            runAsyncTaskSafely(task);
          }, delayMs);
        },
        stop() {
          if (!timer) return;
          clearInterval(timer);
          timer = null;
        }
      };
    }

    function createTimeoutRunner(task) {
      let timer = null;
      return {
        schedule(delayMs) {
          this.stop();
          timer = setTimeout(() => {
            timer = null;
            runAsyncTaskSafely(task);
          }, delayMs);
        },
        stop() {
          if (!timer) return;
          clearTimeout(timer);
          timer = null;
        }
      };
    }

    function bindClickAction(el, handler) {
      if (!el) return;
      el.addEventListener('click', () => {
        runAsyncTaskSafely(handler);
      });
    }

    function getActivePageId() {
      if (currentPageId) return currentPageId;
      const active = document.querySelector('.page.active');
      return active ? active.id : '';
    }

    function buildNodeGrid(className, items) {
      const nodes = (items || []).filter((item) => item && typeof item.nodeType === 'number');
      if (nodes.length === 0) return null;
      const wrapper = document.createElement('div');
      wrapper.className = className;
      nodes.forEach((node) => wrapper.appendChild(node));
      return wrapper;
    }

    function currentDeferredVisualAssetsVersion() {
      return (webAssetVersion || loadedWebAssetVersion || 'noversion').trim() || 'noversion';
    }

    function getDeferredVisualAssetsWarmState() {
      return getStorageValue(localStorage, deferredVisualAssetsStateKey);
    }

    function hasWarmDeferredVisualAssets() {
      return getDeferredVisualAssetsWarmState() === currentDeferredVisualAssetsVersion();
    }

    function markDeferredVisualAssetsWarm() {
      setStorageValue(localStorage, deferredVisualAssetsStateKey, currentDeferredVisualAssetsVersion());
    }

    function navigationType() {
      try {
        const navEntries = performance.getEntriesByType('navigation');
        if (navEntries && navEntries[0] && typeof navEntries[0].type === 'string') {
          return navEntries[0].type;
        }
      } catch (err) {
      }
      try {
        if (performance && performance.navigation) {
          if (performance.navigation.type === 1) return 'reload';
          if (performance.navigation.type === 0) return 'navigate';
        }
      } catch (err) {
      }
      return '';
    }

    function isReloadNavigation() {
      return navigationType() === 'reload';
    }

    function activateMenuAssets() {
      if (menuAssetsActivated) return;
      menuAssetsActivated = true;
      markDeferredVisualAssetsWarm();
    }

    function armDeferredMenuAssets() {
      if (deferredMenuAssetsArmed || menuAssetsActivated) return;
      deferredMenuAssetsArmed = true;
      activateMenuAssets();
    }

    function scheduleDeferredVisualAssets() {
      if (deferredVisualAssetsScheduled) return;
      deferredVisualAssetsScheduled = true;
      armDeferredMenuAssets();
    }

    async function loadWebMeta(options) {
      try {
        const data = await fetchOkJson('/api/web/meta', { cache: 'no-store' }, 'meta web indisponible');
        const currentUpgradeSession = readUpgradeUiSession();
        if (currentUpgradeSession && currentUpgradeSession.awaitingReconnect) {
          handleUpgradeReconnectSuccess();
        }
        ingestWebProfileMeta(data);

        if (typeof data.web_asset_version === 'string') {
          const announcedVersion = data.web_asset_version.trim();
          if (announcedVersion) {
            const previousVersion = (webAssetVersion || '').trim();
            webAssetVersion = announcedVersion;
            if (previousVersion && previousVersion !== announcedVersion) {
              runtimeManifestDomainCache = null;
              runtimeManifestDomainLoadPromise = null;
            }
            setStorageValue(localStorage, flowWebAssetVersionStorageKey, announcedVersion);
            if (loadedWebAssetVersion && loadedWebAssetVersion !== announcedVersion) {
              const reloadKey = 'flow_web_asset_reload_once';
              try {
                if (sessionStorage.getItem(reloadKey) !== announcedVersion) {
                  sessionStorage.setItem(reloadKey, announcedVersion);
                  window.location.reload();
                  return;
                }
              } catch (err) {
                window.location.reload();
                return;
              }
            }
            try {
              if (sessionStorage.getItem('flow_web_asset_reload_once') === announcedVersion) {
                sessionStorage.removeItem('flow_web_asset_reload_once');
              }
            } catch (err) {
            }
          }
        }
        applyIconUsagePreference(false);
        applyMenuIconPreference(false);
        applyStatusIconPreference(!!data.unify_status_card_icons);
        await applyMenuIconModeFromMeta(data);
        await refreshWebUiLocale(false);
        if (!disableWebIcons && hasWarmDeferredVisualAssets()) {
          activateMenuAssets(false);
        }
        scheduleDeferredVisualAssets();
        if (typeof data.firmware_version === 'string') {
          const trimmed = data.firmware_version.trim();
          if (trimmed) {
            supervisorFirmwareVersion = trimmed;
          }
        }
        const rawNextionVersion = String(data.nextion_display_version || '').trim();
        nextionDisplayVersion = rawNextionVersion && rawNextionVersion !== '0' ? rawNextionVersion : '';
        nextionDisplayVersionDetected = Object.prototype.hasOwnProperty.call(data, 'nextion_display_version_detected')
          ? data.nextion_display_version_detected === true
          : !!nextionDisplayVersion;
        nextionDisplayVersionCompatible = Object.prototype.hasOwnProperty.call(data, 'nextion_display_version_compatible')
          ? data.nextion_display_version_compatible === true
          : nextionDisplayVersionDetected;
        nextionDisplayExpectedVersion = String(data.nextion_display_expected_version || '').trim();
        supervisorUptimeMs = Number(data.upms) || 0;
        supervisorHeap = (data.heap && typeof data.heap === 'object') ? data.heap : {};
        renderUpgradeCatalog();
        refreshAppHeader(getActivePageId());
        if (isPageActive('page-status')) {
          refreshFlowStatus(false).catch(() => {});
        }
      } catch (err) {
        scheduleDeferredVisualAssets();
      }
    }

    function isMobileLayout() {
      return window.innerWidth <= 900;
    }

    function resolvePageMenuLabel(pageId) {
      const item = document.querySelector('[data-page="' + pageId + '"]');
      if (!item) return '';
      const label = item.querySelector('.label');
      return String(label && label.textContent ? label.textContent : '').trim();
    }

    function formatHeaderNetworkStatus() {
      try {
        const wifiDomain = (flowStatusDomainCache.wifi && flowStatusDomainCache.wifi.data && flowStatusDomainCache.wifi.data.ok === true)
          ? flowStatusDomainCache.wifi.data
          : null;
        const wifi = (wifiDomain && wifiDomain.wifi && typeof wifiDomain.wifi === 'object') ? wifiDomain.wifi : null;
        if (wifi) {
          if (typeof wifi.rdy === 'boolean') {
            return wifi.rdy ? tr('info.state.connected', 'Connecté') : tr('info.state.disconnected', 'Déconnecté');
          }
          const type = formatInfoNetworkType(wifi.typ);
          if (type && type !== '-') return type;
        }
      } catch (err) {
      }
      const mode = normalizeNetworkMode(networkMode);
      if (mode === 'ap') return 'AP';
      const type = currentFlowNetworkType();
      if (type) return formatInfoNetworkType(type);
      if (mode === 'station') return tr('info.netType.wifi', 'Wifi');
      if (mode === 'ethernet') return tr('info.netType.ethernet', 'Ethernet');
      return '-';
    }

    function effectiveNetworkType(wifi, domainReady) {
      const transport = normalizeNetworkType(networkTransport);
      if (transport) return transport;

      const ready = wifi && typeof wifi.rdy === 'boolean'
        ? wifi.rdy
        : !!domainReady;
      if (!ready) return '';

      const runtimeType = normalizeNetworkType(wifi && wifi.typ);
      if (runtimeType) return runtimeType;

      const mode = normalizeNetworkMode(networkMode);
      if (mode === 'ethernet') return 'ethernet';
      if (mode === 'station') return 'wifi';
      return '';
    }

    function currentFlowNetworkType() {
      try {
        const wifiDomain = (flowStatusDomainCache.wifi && flowStatusDomainCache.wifi.data && flowStatusDomainCache.wifi.data.ok === true)
          ? flowStatusDomainCache.wifi.data
          : null;
        const wifi = (wifiDomain && wifiDomain.wifi && typeof wifiDomain.wifi === 'object') ? wifiDomain.wifi : null;
        const type = effectiveNetworkType(wifi, !!wifiDomain);
        if (type) return type;
      } catch (err) {
      }
      const transport = normalizeNetworkType(networkTransport);
      if (transport) return transport;
      const mode = normalizeNetworkMode(networkMode);
      if (mode === 'ethernet') return 'ethernet';
      if (mode === 'station') return 'wifi';
      return '';
    }

    function currentFlowNetworkIcon() {
      return currentFlowNetworkType() === 'ethernet' ? 'settings_ethernet' : 'wifi';
    }

    function isHeaderWifiConnected() {
      try {
        const wifiDomain = (flowStatusDomainCache.wifi && flowStatusDomainCache.wifi.data && flowStatusDomainCache.wifi.data.ok === true)
          ? flowStatusDomainCache.wifi.data
          : null;
        const wifi = (wifiDomain && wifiDomain.wifi && typeof wifiDomain.wifi === 'object') ? wifiDomain.wifi : null;
        if (wifi && typeof wifi.rdy === 'boolean') return wifi.rdy;
      } catch (err) {
      }
      if (normalizeNetworkType(networkTransport)) return true;
      const mode = normalizeNetworkMode(networkMode);
      return mode === 'station' || mode === 'ethernet';
    }

    function refreshAppHeader(pageId) {
      const activePage = pageId || getActivePageId();
      const label = resolvePageMenuLabel(activePage) || webProfileName || 'flow.io';
      if (desktopPageTitle) {
        desktopPageTitle.textContent = label;
      }
      if (headerWifiStatus) {
        headerWifiStatus.textContent = formatHeaderNetworkStatus();
      }
      if (headerNetworkIcon) {
        headerNetworkIcon.textContent = currentFlowNetworkIcon();
      }
      if (networkConfigIcon) {
        networkConfigIcon.textContent = currentFlowNetworkIcon();
      }
      if (applyNetworkIcon) {
        applyNetworkIcon.textContent = currentFlowNetworkType() === 'ethernet' ? 'settings_ethernet' : 'wifi_protected_setup';
      }
      if (headerWifiDot) {
        headerWifiDot.classList.toggle('is-connected', isHeaderWifiConnected());
      }
      renderHeaderReachability();
    }

    function refreshAppHeaderClock() {
      if (headerClockLabel) {
        const baseLabel = tr('header.time', 'Heure');
        headerClockLabel.textContent = currentFlowTimeSourceLabel
          ? baseLabel + ' (' + currentFlowTimeSourceLabel + ')'
          : baseLabel;
      }
      if (!headerClockStatus) return;
      headerClockStatus.textContent = new Date().toLocaleTimeString(currentWebLocaleTag());
    }

    function startAppHeaderClock() {
      refreshAppHeaderClock();
      if (appHeaderClockTimer || !headerClockStatus) return;
      appHeaderClockTimer = setInterval(refreshAppHeaderClock, 1000);
    }

    function syncHeaderTimeSourceFromSystemDomain() {
      try {
        const systemDomain = (flowStatusDomainCache.system && flowStatusDomainCache.system.data && flowStatusDomainCache.system.data.ok === true)
          ? flowStatusDomainCache.system.data
          : null;
        const time = (systemDomain && systemDomain.time && typeof systemDomain.time === 'object') ? systemDomain.time : null;
        const nextTimeLabel = flowTimeHeaderLabel(time);
        if (nextTimeLabel) {
          currentFlowTimeSourceLabel = nextTimeLabel;
          refreshAppHeaderClock();
        }
      } catch (err) {
      }
    }

    function renderHeaderReachability() {
      const unreachable = deviceReachabilityMisses >= deviceReachabilityLostThreshold;
      const reachable = deviceReachabilityReachable && !unreachable;
      if (headerDeviceStatus) {
        headerDeviceStatus.textContent = unreachable
          ? tr('header.device.unreachable', 'Non joignable')
          : (reachable ? tr('header.device.reachable', 'Joignable') : tr('header.device.pending', 'Vérification...'));
      }
      if (headerReachabilityDot) {
        headerReachabilityDot.classList.toggle('is-reachable', reachable);
        headerReachabilityDot.classList.toggle('is-pending', !reachable && !unreachable);
        headerReachabilityDot.classList.toggle('is-unreachable', unreachable);
      }
    }

    async function fetchHeaderReachabilityHello() {
      const supportsAbort = typeof AbortController === 'function';
      const controller = supportsAbort ? new AbortController() : null;
      const timeoutId = controller
        ? setTimeout(() => {
            try {
              controller.abort();
            } catch (err) {
            }
          }, deviceReachabilityFetchTimeoutMs)
        : null;
      try {
        const res = await fetch('/api/web/meta', {
          cache: 'no-store',
          signal: controller ? controller.signal : undefined
        });
        if (!res.ok) throw new Error('hello_http_' + res.status);
        const data = await res.json().catch(() => null);
        if (!data || data.ok !== true) throw new Error('hello_payload');
      } finally {
        if (timeoutId) clearTimeout(timeoutId);
      }
    }

    async function probeHeaderReachability() {
      if (deviceReachabilityInFlight) return;
      deviceReachabilityInFlight = true;
      try {
        await fetchHeaderReachabilityHello();
        deviceReachabilityMisses = 0;
        deviceReachabilityReachable = true;
      } catch (err) {
        deviceReachabilityMisses = Math.min(deviceReachabilityLostThreshold, deviceReachabilityMisses + 1);
      } finally {
        deviceReachabilityInFlight = false;
        renderHeaderReachability();
      }
    }

    function startHeaderReachabilityProbe() {
      renderHeaderReachability();
      probeHeaderReachability().catch(() => {});
      if (deviceReachabilityTimer || !headerDeviceStatus) return;
      deviceReachabilityTimer = setInterval(() => {
        if (document.hidden) return;
        probeHeaderReachability().catch(() => {});
        refreshAppHeaderTime(false).catch(() => {});
      }, deviceReachabilityProbeMs);
    }

    async function refreshAppHeaderWifi(forceRefresh) {
      try {
        await fetchFlowStatusDomain('wifi', !!forceRefresh, 'header');
      } catch (err) {
      }
      refreshAppHeader(getActivePageId());
    }

    async function refreshAppHeaderTime(forceRefresh) {
      try {
        await fetchFlowStatusDomain('system', !!forceRefresh, 'header');
        syncHeaderTimeSourceFromSystemDomain();
      } catch (err) {
      }
      refreshAppHeaderClock();
    }

    function syncMobileTopbarTitle(pageId) {
      if (!mobileTopbarTitle) return;
      const label = resolvePageMenuLabel(pageId) || webProfileName;
      mobileTopbarTitle.textContent = label;
      refreshAppHeader(pageId);
    }

    function formatInfoBytes(value) {
      const n = Number(value);
      if (!Number.isFinite(n) || n < 0) return '-';
      if (n >= 1024 * 1024) return (n / (1024 * 1024)).toFixed(2) + ' MB';
      if (n >= 1024) return (n / 1024).toFixed(1) + ' kB';
      return String(Math.trunc(n)) + ' B';
    }

    function formatInfoUptime(ms) {
      const totalMs = Number(ms);
      if (!Number.isFinite(totalMs) || totalMs < 0) return '-';
      const totalSec = Math.floor(totalMs / 1000);
      const d = Math.floor(totalSec / 86400);
      const h = Math.floor((totalSec % 86400) / 3600);
      const m = Math.floor((totalSec % 3600) / 60);
      const s = totalSec % 60;
      if (d > 0) return d + 'j ' + h + 'h ' + m + 'm';
      if (h > 0) return h + 'h ' + m + 'm';
      if (m > 0) return m + 'm ' + s + 's';
      return s + 's';
    }

    function deriveInfoPressure(heap) {
      const free = Number(heap && heap.free) || 0;
      const largest = Number(heap && heap.largest) || 0;
      const frag = Number(heap && heap.frag) || 0;

      // Supervisor nominal profile (ESP32 sans PSRAM):
      // - state is derived from current free/largest/frag only
      // - min_free is informational and intentionally excluded from pressure state
      // - each level requires all criteria to avoid false positives near nominal baseline
      if (free < 20000 && largest < 8000 && frag > 45) return 'panic';
      if (free < 24000 && largest < 12000 && frag > 35) return 'critical';
      if (free < 28000 && largest < 16000 && frag > 28) return 'shedding';
      if (free < 30000 && largest < 20000 && frag > 20) return 'constrained';
      return 'normal';
    }

    function buildInfoMetricRow(title, value) {
      const esc = (input) => String(input || '-')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
      return (
        '<div class="info-row">' +
          '<span class="info-row-key">' + esc(title || '') + '</span>' +
          '<span class="info-row-value">' + esc(value || '-') + '</span>' +
        '</div>'
      );
    }

    function splitInfoFirmwareVersion(fullVersion) {
      const raw = String(fullVersion || '').trim();
      if (!raw || raw === '-') return { version: '-', build: '-' };
      const plusIndex = raw.indexOf('+');
      if (plusIndex < 0) return { version: raw, build: '-' };
      return {
        version: raw.slice(0, plusIndex).trim() || '-',
        build: formatInfoBuildStamp(raw.slice(plusIndex + 1))
      };
    }

    function formatInfoBuildStamp(stamp) {
      const raw = String(stamp || '').trim();
      const match = raw.match(/^(\d{4})(\d{2})(\d{2})[._-]?(\d{2})(\d{2})(\d{2})?$/);
      if (match) return match[1] + '.' + match[2] + '.' + match[3] + '-' + match[4] + match[5];
      return raw || '-';
    }

    function normalizeInfoMac(mac) {
      const raw = String(mac || '').trim();
      return raw ? raw.toUpperCase() : '-';
    }

    function setInfoFlowDomainLoading(domainKey, loading) {
      if (!Object.prototype.hasOwnProperty.call(infoFlowDomainLoading, domainKey)) return;
      const isLoading = !!loading;
      infoFlowDomainLoading[domainKey] = isLoading;
      const node = infoFlowLoaderNodes[domainKey];
      if (!node) return;
      node.classList.toggle('is-loading', isLoading);
      node.disabled = isLoading;
      node.setAttribute('aria-busy', isLoading ? 'true' : 'false');
    }

    function infoFlowDomainLabel(domainKey) {
      if (domainKey === 'system') return tr('info.flowSystem', 'Système flow.io');
      if (domainKey === 'wifi') return tr('info.flowNetwork', tr('info.flowWifi', 'Réseau flow.io'));
      if (domainKey === 'mqtt') return tr('info.flowMqtt', 'MQTT flow.io');
      return formatRuntimeDomainLabel(domainKey);
    }

    function updateInfoLoadButtonsText() {
      const keys = Object.keys(infoFlowLoaderNodes);
      keys.forEach((domainKey) => {
        const node = infoFlowLoaderNodes[domainKey];
        if (!node) return;
        const label = tr('info.loadDomain', 'Charger');
        const domain = infoFlowDomainLabel(domainKey);
        const text = label + ' ' + domain;
        node.setAttribute('title', text);
        node.setAttribute('aria-label', text);
      });
    }

    function infoRuntimeValueMap(values) {
      const map = new Map();
      (Array.isArray(values) ? values : []).forEach((item) => {
        if (!item || typeof item !== 'object') return;
        const id = Number(item.id);
        if (!Number.isFinite(id) || id <= 0) return;
        map.set(id, item);
      });
      return map;
    }

    function infoRuntimeValueAvailable(item) {
      if (!item || typeof item !== 'object') return false;
      if (item.status === 'unavailable' || item.status === 'not_found') return false;
      return Object.prototype.hasOwnProperty.call(item, 'value');
    }

    function infoRuntimeValue(valueById, runtimeId, fallback) {
      const item = valueById.get(Number(runtimeId));
      return infoRuntimeValueAvailable(item) ? item.value : fallback;
    }

    function cacheInfoRuntimeDomain(domainKey, entries, values) {
      const cacheEntry = flowStatusDomainCache[domainKey];
      if (!cacheEntry) return null;

      const domainEntries = Array.isArray(entries) ? entries : [];
      const valueById = infoRuntimeValueMap(values);
      const hasAnyValue = domainEntries.some((entry) => {
        const id = Number(entry && entry.id);
        if (!Number.isFinite(id) || id <= 0) return false;
        return infoRuntimeValueAvailable(valueById.get(id));
      });
      let data = {
        ok: false,
        err: {
          code: domainEntries.length ? 'RuntimeUnavailable' : 'RuntimeIdsMissing',
          where: 'info.runtime.' + domainKey
        }
      };

      if (hasAnyValue && domainKey === 'system') {
        data = {
          ok: true,
          fw: infoRuntimeValue(valueById, 1801, ''),
          upms: infoRuntimeValue(valueById, 1802, 0),
          time: {
            rdy: !!infoRuntimeValue(valueById, 1301, false),
            src: infoRuntimeValue(valueById, 1302, 'none'),
            qlt: infoRuntimeValue(valueById, 1303, '')
          },
          heap: {
            free: infoRuntimeValue(valueById, 1803, 0),
            min_free: infoRuntimeValue(valueById, 1804, 0)
          }
        };
      } else if (hasAnyValue && domainKey === 'wifi') {
        const previousWifi = (cacheEntry.data && cacheEntry.data.ok === true && cacheEntry.data.wifi && typeof cacheEntry.data.wifi === 'object')
          ? cacheEntry.data.wifi
          : {};
        const previousMac = normalizeInfoMac(previousWifi.mac);
        const rssiItem = valueById.get(1003);
        data = {
          ok: true,
          wifi: {
            rdy: !!infoRuntimeValue(valueById, 1001, false),
            typ: infoRuntimeValue(valueById, 1004, 'wifi'),
            ip: normalizeIpValue(infoRuntimeValue(valueById, 1002, '')),
            mac: previousMac !== '-' ? previousMac : '',
            rssi: infoRuntimeValue(valueById, 1003, null),
            hrss: infoRuntimeValueAvailable(rssiItem)
          }
        };
      } else if (hasAnyValue && domainKey === 'mqtt') {
        data = {
          ok: true,
          mqtt: {
            rdy: !!infoRuntimeValue(valueById, 2101, false),
            srv: infoRuntimeValue(valueById, 2102, ''),
            rxdrp: infoRuntimeValue(valueById, 2103, 0),
            prsf: infoRuntimeValue(valueById, 2104, 0),
            hndf: infoRuntimeValue(valueById, 2105, 0),
            ovr: infoRuntimeValue(valueById, 2106, 0)
          }
        };
      }

      cacheEntry.data = data;
      cacheEntry.fetchedAt = Date.now();
      return data;
    }

    async function refreshInfoFlowDomain(domainKey, forceRefresh) {
      if (!isInfoPageVisible()) return null;
      const cleanDomain = String(domainKey || '').trim().toLowerCase();
      if (!infoFlowDomainKeys.includes(cleanDomain)) return null;
      const cacheEntry = flowStatusDomainCache[cleanDomain];
      const cacheValid = isFlowStatusDomainCacheValid(cleanDomain);
      const shouldFetch = !!forceRefresh || !cacheValid || !(cacheEntry && cacheEntry.data);
      if (!shouldFetch) {
        renderInfoPanel();
        return cacheEntry ? cacheEntry.data : null;
      }

      setInfoFlowDomainLoading(cleanDomain, true);
      try {
        flowStatusDebugLog('info domain fetch start', {
          domain: cleanDomain,
          endpoint: '/api/runtime/values',
          force: !!forceRefresh
        });
        const entries = Array.isArray(infoRuntimeDomainEntries[cleanDomain])
          ? infoRuntimeDomainEntries[cleanDomain]
          : [];
        const ids = entries.map((entry) => Number(entry && entry.id)).filter((id) => Number.isFinite(id) && id > 0);
        const values = ids.length ? await fetchRuntimeValues(ids) : [];
        cacheInfoRuntimeDomain(cleanDomain, entries, values);
        infoFlowLastSuccessAt = Date.now();
        renderInfoPanel();
        const nextEntry = flowStatusDomainCache[cleanDomain];
        return nextEntry ? nextEntry.data : null;
      } finally {
        setInfoFlowDomainLoading(cleanDomain, false);
      }
    }

    async function refreshInfoFlowDomains(forceRefresh) {
      if (!isInfoPageVisible()) return null;
      const refreshWindowMs = (
        !document.hidden && getActivePageId() === 'page-info'
      ) ? infoFlowRefreshActiveMs : infoFlowRefreshIdleMs;
      const now = Date.now();
      if (infoFlowRefreshPromise) {
        return infoFlowRefreshPromise;
      }
      if (!forceRefresh && (now - infoFlowLastAttemptAt) < refreshWindowMs) {
        return null;
      }
      infoFlowLastAttemptAt = now;

      const promise = (async () => {
        for (const domainKey of infoFlowDomainKeys) {
          try {
            await refreshInfoFlowDomain(domainKey, !!forceRefresh);
          } catch (err) {
          }
        }
      })();

      infoFlowRefreshPromise = promise.finally(() => {
        infoFlowRefreshPromise = null;
        renderInfoPanel();
      });
      return infoFlowRefreshPromise;
    }

    function renderInfoMetricRows(node, rows) {
      if (!node) return;
      const safeRows = Array.isArray(rows) ? rows : [];
      node.innerHTML = safeRows.map((row) => buildInfoMetricRow(row[0], row[1])).join('');
    }

    function formatInfoBoolean(value, trueText, falseText) {
      if (typeof value !== 'boolean') return '-';
      return value ? (trueText || tr('info.state.connected', 'Connecté')) : (falseText || tr('info.state.disconnected', 'Déconnecté'));
    }

    function formatInfoDbm(value) {
      const n = Number(value);
      return Number.isFinite(n) ? (String(Math.trunc(n)) + ' dBm') : '-';
    }

    function formatInfoCount(value) {
      const n = Number(value);
      return Number.isFinite(n) ? String(Math.trunc(n)) : '-';
    }

    function formatInfoNetworkType(value) {
      const normalized = normalizeNetworkType(value);
      if (normalized === 'ethernet') return tr('info.netType.ethernet', 'Ethernet');
      if (normalized === 'wifi') return tr('info.netType.wifi', 'Wifi');
      const raw = String(value || '').trim().toLowerCase();
      if (!raw || raw === 'none' || raw === 'ap' || raw === 'accesspoint' || raw === 'access_point') {
        return tr('info.state.disconnected', 'Déconnecté');
      }
      return raw ? raw : '-';
    }

    function renderInfoPanel() {
      const systemDomain = (flowStatusDomainCache.system && flowStatusDomainCache.system.data && flowStatusDomainCache.system.data.ok === true)
        ? flowStatusDomainCache.system.data
        : null;
      const flowHeap = (systemDomain && systemDomain.heap && typeof systemDomain.heap === 'object') ? systemDomain.heap : {};

      const wifiDomain = (flowStatusDomainCache.wifi && flowStatusDomainCache.wifi.data && flowStatusDomainCache.wifi.data.ok === true)
        ? flowStatusDomainCache.wifi.data
        : null;
      const wifi = (wifiDomain && wifiDomain.wifi && typeof wifiDomain.wifi === 'object') ? wifiDomain.wifi : {};

      const mqttDomain = (flowStatusDomainCache.mqtt && flowStatusDomainCache.mqtt.data && flowStatusDomainCache.mqtt.data.ok === true)
        ? flowStatusDomainCache.mqtt.data
        : null;
      const mqtt = (mqttDomain && mqttDomain.mqtt && typeof mqttDomain.mqtt === 'object') ? mqttDomain.mqtt : {};
      const time = (systemDomain && systemDomain.time && typeof systemDomain.time === 'object') ? systemDomain.time : null;
      if (time) {
        syncHeaderTimeSourceFromSystemDomain();
      }
      const fullFirmware = systemDomain ? fmtFlowStatusVal(systemDomain.fw) : (supervisorFirmwareVersion || '-');
      const firmwareParts = splitInfoFirmwareVersion(fullFirmware);
      const currentMac = wifiDomain ? normalizeInfoMac(wifi.mac) : '-';
      if (currentMac !== '-') infoLastMac = currentMac;
      const mac = currentMac !== '-' ? currentMac : infoLastMac;
      const deviceName = String(systemDomain && systemDomain.devicename ? systemDomain.devicename : webDeviceName || 'flowio').trim() || 'flowio';
      const infoNetworkType = effectiveNetworkType(wifi, !!wifiDomain);
      const infoRows = [
        [tr('info.row.deviceName', 'Nom de l’appareil'), deviceName],
        [tr('info.row.firmwareVersion', 'Version firmware'), firmwareParts.version],
        [tr('info.row.buildVersion', 'Version build'), firmwareParts.build],
        [tr('info.row.uptime', 'Uptime'), systemDomain ? formatInfoUptime(systemDomain.upms) : formatInfoUptime(supervisorUptimeMs)],
        [tr('info.row.ip', 'Adresse IP'), wifiDomain ? normalizeIpValue(wifi.ip) : '-'],
        [tr('info.row.mac', 'Adresse MAC'), mac],
        [tr('info.row.networkType', 'Type réseau'), wifiDomain ? formatInfoNetworkType(infoNetworkType) : '-'],
        [tr('info.row.signal', 'Signal'), (wifiDomain && wifi.hrss) ? formatInfoDbm(wifi.rssi) : '-'],
        [tr('info.row.mqtt', 'MQTT'), mqttDomain ? formatInfoBoolean(!!mqtt.rdy, tr('info.state.connected', 'Connecté'), tr('info.state.disconnected', 'Déconnecté')) : '-'],
        [tr('info.row.time', 'Heure'), time ? flowTimeStatusLabel(time) : '-']
      ];
      if (systemDomain) {
        infoRows.push([tr('info.row.heapFree', 'Heap libre'), formatInfoBytes(flowHeap.free)]);
      }
      renderInfoMetricRows(infoGrid, infoRows);

      if (infoStatusChip) {
        infoStatusChip.textContent = tr('info.updatedAt', 'Mise à jour') + ': ' + new Date().toLocaleTimeString(currentWebLocaleTag());
      }
      refreshAppHeader(getActivePageId());
    }

    function isDrawerExpanded() {
      return isMobileLayout()
        ? drawer.classList.contains('mobile-open')
        : !drawer.classList.contains('collapsed');
    }

    function setMobileDrawerOpen(open) {
      drawer.classList.toggle('mobile-open', open);
      overlay.classList.toggle('visible', open);
    }

    function closeMobileDrawer() {
      if (isMobileLayout()) {
        setMobileDrawerOpen(false);
      }
    }

    function currentUpgradeStatusPollDelayMs() {
      const current = readUpgradeUiSession();
      const phase = String(current && current.phase ? current.phase : 'idle');
      if (current && (current.awaitingReconnect || phase === 'reboot' || phase === 'reconnect')) {
        return upgradeStatusPollReconnectMs;
      }
      if (phase === 'target' || phase === 'download' || phase === 'flash') {
        return upgradeStatusPollActiveMs;
      }
      if (phase === 'done') return upgradeStatusPollDoneMs;
      if (phase === 'error') return upgradeStatusPollErrorMs;
      return upgradeStatusPollIdleMs;
    }

    function scheduleNextUpgradeStatusPoll(delayMs) {
      if (document.hidden || getActivePageId() !== 'page-system') return;
      const nextDelay = Math.max(0, Number.isFinite(delayMs) ? delayMs : currentUpgradeStatusPollDelayMs());
      upgradeStatusPoller.schedule(nextDelay);
    }

    async function pollUpgradeStatusTick() {
      if (document.hidden || getActivePageId() !== 'page-system') return;
      await refreshUpgradeStatus();
      scheduleNextUpgradeStatusPoll();
    }

    function startUpgradeStatusPolling(immediate) {
      if (immediate) {
        scheduleNextUpgradeStatusPoll(0);
        return;
      }
      scheduleNextUpgradeStatusPoll();
    }

    function stopUpgradeStatusPolling() {
      upgradeStatusPoller.stop();
    }

    function isInfoPageVisible() {
      return !document.hidden && getActivePageId() === 'page-info';
    }

    async function pollInfoRuntimeTick() {
      if (!isInfoPageVisible()) {
        stopInfoPolling();
        return;
      }
      try {
        await refreshInfoFlowDomains(true);
      } catch (err) {
      }
      renderInfoPanel();
    }

    async function pollInfoSupervisorTick() {
      if (!isInfoPageVisible()) {
        stopInfoPolling();
        return;
      }
      try {
        await loadWebMeta({ skipDrawerRuntimeRender: true });
      } catch (err) {
      }
      renderInfoPanel();
    }

    function startInfoPolling() {
      if (!isInfoPageVisible()) return;
      infoRuntimePoller.start();
      infoSupervisorPoller.start();
    }

    function stopInfoPolling() {
      infoRuntimePoller.stop();
      infoSupervisorPoller.stop();
    }

    let flowRemoteFetchQueue = Promise.resolve();

    function fetchFlowRemoteQueued(url, options) {
      const queued = flowRemoteFetchQueue
        .catch(() => {})
        .then(() => fetchWithBusyRetry(url, options));
      flowRemoteFetchQueue = queued.catch(() => {});
      return queued;
    }

    function fetchFlowCfgEndpoint(url, options) {
      return isWaveshareProfile()
        ? fetchWithBusyRetry(url, options)
        : fetchFlowRemoteQueued(url, options);
    }

    function waitMs(ms) {
      return new Promise((resolve) => setTimeout(resolve, ms));
    }

    function isPageActive(pageId) {
      const el = document.getElementById(pageId);
      return !!(el && el.classList.contains('active'));
    }

    function stopFlowStatusLiveTimer() {
      if (!flowStatusLiveTimer) return;
      clearInterval(flowStatusLiveTimer);
      flowStatusLiveTimer = null;
    }

    function schedulePageTask(pageId, token, delayMs, task) {
      const run = () => {
        if (token !== pageLoadToken || !isPageActive(pageId)) return;
        Promise.resolve().then(task).catch(() => {});
      };
      if (delayMs > 0) {
        setTimeout(run, delayMs);
      } else {
        run();
      }
    }

    function showPage(pageId, options) {
      const opts = options || {};
      if (flowCfgLocalApplyBusyDepth > 0 && currentPageId === 'page-control' && pageId !== 'page-control') {
        if (flowCfgStatus) {
          flowCfgStatus.textContent = tr('cfg.apply.waitBeforeLeaving', 'Application en cours : attendez la confirmation avant de quitter la configuration.');
        }
        return;
      }
      const deferredHeavyMs = Math.max(0, Number(opts.deferHeavyMs) || 0);
      const pageToken = ++pageLoadToken;
      currentPageId = pageId;
      if (pageId !== 'page-activity-log') {
        cancelActivityLogRefresh();
      }
      if (pageId !== 'page-info') {
        stopInfoPolling();
      }
      pages.forEach((el) => el.classList.toggle('active', el.id === pageId));
      menuItems.forEach((el) => el.classList.toggle('active', el.dataset.page === pageId));
      syncMobileTopbarTitle(pageId);
      if (!logsOverlayOpen) setWsStatusText(tr('terminal.inactive', 'inactif'));
      if (pageId === 'page-activity-log') {
        schedulePageTask(pageId, pageToken, deferredHeavyMs, () => refreshActivityLog(false));
      }
      if (pageId === 'page-pool-measures') {
        schedulePageTask(pageId, pageToken, deferredHeavyMs, () => onPoolMeasuresPageShown());
      } else {
        stopPoolMeasuresTimer();
      }
      if (pageId === 'page-pool') {
        schedulePageTask(pageId,
                         pageToken,
                         deferredHeavyMs > 0 ? (deferredHeavyMs + 120) : 0,
                         () => onPoolConfigPageShown(false));
      }
      if (pageId === 'page-io-summary') {
        schedulePageTask(pageId, pageToken, deferredHeavyMs, () => onIoSummaryPageShown());
      } else {
        stopIoSummaryTimer();
      }
      if (pageId === 'page-calibration') {
        schedulePageTask(pageId,
                         pageToken,
                         deferredHeavyMs > 0 ? (deferredHeavyMs + 180) : 0,
                         () => onCalibrationPageShown());
      }
      if (pageId === 'page-status') {
        schedulePageTask(pageId, pageToken, deferredHeavyMs, () => refreshFlowStatus(false));
      } else {
        stopFlowStatusLiveTimer();
      }
      if (pageId === 'page-system') {
        schedulePageTask(pageId,
                         pageToken,
                         deferredHeavyMs > 0 ? (deferredHeavyMs + 180) : 0,
                         () => onUpgradePageShown());
      }
      if (pageId === 'page-wifi') {
        schedulePageTask(pageId,
                         pageToken,
                         deferredHeavyMs > 0 ? (deferredHeavyMs + 180) : 0,
                         () => onWifiPageShown());
      }
      if (pageId === 'page-control') {
        schedulePageTask(pageId,
                         pageToken,
                         deferredHeavyMs > 0 ? (deferredHeavyMs + 220) : 0,
                         () => onControlPageShown());
      } else {
        stopFlowCfgRetry();
      }
      if (pageId === 'page-info') {
        schedulePageTask(pageId,
                         pageToken,
                         deferredHeavyMs > 0 ? (deferredHeavyMs + 120) : 0,
                         async () => {
                           await loadWebMeta({ skipDrawerRuntimeRender: true });
                           ensureRemoteMenuIconFontLoaded().catch(() => false);
                           await refreshInfoFlowDomains(true);
                           renderInfoPanel();
                           if (pageToken !== pageLoadToken || !isPageActive('page-info') || !isInfoPageVisible()) return;
                           startInfoPolling();
                         });
      }
      if (pageId !== 'page-system') {
        stopUpgradeStatusPolling();
      }
      closeMobileDrawer();
    }

    function resolveInitialPageId() {
      try {
        const params = new URLSearchParams(window.location.search || '');
        let requestedPage = String(params.get('page') || '').trim();
        if (requestedPage === 'page-status') {
          requestedPage = 'page-pool-measures';
        }
        if (requestedPage && pages.some((el) => el.id === requestedPage)) {
          return requestedPage;
        }
      } catch (err) {
      }
      const activePage = document.querySelector('.page.active');
      if (activePage && activePage.id) {
        return activePage.id;
      }
      return 'page-pool-measures';
    }

    menuItems.forEach((item) => item.addEventListener('click', () => showPage(item.dataset.page)));

    menuToggles.forEach((btn) => btn.addEventListener('click', () => {
      if (isMobileLayout()) {
        setMobileDrawerOpen(!drawer.classList.contains('mobile-open'));
      } else {
        if (hideMenuSvg) return;
        drawer.classList.toggle('collapsed');
      }
    }));

    overlay.addEventListener('click', closeMobileDrawer);
    window.addEventListener('resize', () => {
      if (!isMobileLayout()) {
        setMobileDrawerOpen(false);
      }
    });

    const term = document.getElementById('term');
    const wsStatus = document.getElementById('wsStatus');
    const logSourceSelect = document.getElementById('logSourceSelect');
    const bootLogDumpBtn = document.getElementById('bootLogDumpBtn');
    const toggleAutoscrollInput = document.getElementById('toggleAutoscroll');
    const logsOverlay = document.getElementById('logsOverlay');
    const openLogsOverlayBtn = document.getElementById('openLogsOverlay');
    const closeLogsOverlayBtn = document.getElementById('closeLogsOverlay');
    const activityLogList = document.getElementById('activityLogList');
    const activityLogStatus = document.getElementById('activityLogStatus');
    const activityRefreshBtn = document.getElementById('activityRefreshBtn');
    const activityPurgeBtn = document.getElementById('activityPurgeBtn');
    const activityPrevBtn = document.getElementById('activityPrevBtn');
    const activityNextBtn = document.getElementById('activityNextBtn');
    const activityRangeText = document.getElementById('activityRangeText');
    const activityFilterBtns = Array.from(document.querySelectorAll('[data-activity-filter]'));
    let autoScrollEnabled = true;
    let logsOverlayOpen = false;
    let activityFilter = 'all';
    let activityWindowShiftHours = 0;
    let activityRequestToken = 0;
    let activityAbortController = null;
    let activityLoadedEvents = [];
    let activityLoadedStats = null;

    const checkUpdatesBtn = document.getElementById('checkUpdates');
    const cancelUpgradeUiBtn = document.getElementById('cancelUpgradeUi');
    const upgradeCards = document.getElementById('upgradeCards');
    const upgradeTableBody = document.getElementById('upgradeTableBody');
    const upgradeProgressBar = document.getElementById('upgradeProgressBar');
    const upgradePct = document.getElementById('upgradePct');
    const upgradeJourneyLabel = document.getElementById('upgradeJourneyLabel');
    const upgradeSteps = document.getElementById('upgradeSteps');
    const upgradeFooterStatus = document.getElementById('upgradeFooterStatus');
    const upgradeEta = document.getElementById('upgradeEta');
    const upStatusChip = document.getElementById('upStatusChip');

    const wifiEnabled = document.getElementById('wifiEnabled');
    const wifiSsid = document.getElementById('wifiSsid');
    const wifiSsidList = document.getElementById('wifiSsidList');
    const wifiPass = document.getElementById('wifiPass');
    const toggleWifiPassBtn = document.getElementById('toggleWifiPass');
    const scanWifiBtn = document.getElementById('scanWifi');
    const applyWifiCfgBtn = document.getElementById('applyWifiCfg');
    const wifiConfigStatus = document.getElementById('wifiConfigStatus');
    const rebootDeviceTargetSelect = document.getElementById('rebootDeviceTarget');
    const rebootDeviceActionBtn = document.getElementById('rebootDeviceAction');
    const factoryResetDeviceActionBtn = document.getElementById('factoryResetDeviceAction');
    const systemStatusText = document.getElementById('systemStatusText');
    const infoStatusChip = document.getElementById('infoStatusChip');
    const infoGrid = document.getElementById('infoGrid');
    const infoSystemGrid = document.getElementById('infoSystemGrid');
    const infoWifiGrid = document.getElementById('infoWifiGrid');
    const infoMqttGrid = document.getElementById('infoMqttGrid');
    const infoSystemLoader = document.getElementById('infoSystemLoader');
    const infoWifiLoader = document.getElementById('infoWifiLoader');
    const infoMqttLoader = document.getElementById('infoMqttLoader');
    const flowStatusRefreshBtn = document.getElementById('flowStatusRefresh');
    const flowStatusChip = document.getElementById('flowStatusChip');
    const flowStatusGrid = document.getElementById('flowStatusGrid');
    const flowStatusRaw = document.getElementById('flowStatusRaw');
    const ioSummaryCards = document.getElementById('ioSummaryCards');
    const ioSummaryTables = document.getElementById('ioSummaryTables');
    const poolMeasuresRefreshBtn = document.getElementById('poolMeasuresRefresh');
    const poolMeasuresDomains = document.getElementById('poolMeasuresDomains');
    const poolMeasuresStatus = document.getElementById('poolMeasuresStatus');
    const poolMeasuresGrid = document.getElementById('poolMeasuresGrid');
    const poolConfigRefreshBtn = document.getElementById('poolConfigRefresh');
    const poolConfigTitle = document.getElementById('poolConfigTitle');
    const poolConfigSummary = document.getElementById('poolConfigSummary');
    const poolHeroState = document.getElementById('poolHeroState');
    const poolFiltrationStart = document.getElementById('poolFiltrationStart');
    const poolFiltrationStop = document.getElementById('poolFiltrationStop');
    const poolFiltrationFill = document.getElementById('poolFiltrationFill');
    const poolModeBadges = document.getElementById('poolModeBadges');
    const poolDisinfectionModes = document.getElementById('poolDisinfectionModes');
    const poolAlarmCard = document.getElementById('poolAlarmCard');
    const poolConfigGrid = document.getElementById('poolConfigGrid');
    const calibrationSensorSelect = document.getElementById('calibrationSensorSelect');
    const calibrationLoadBtn = document.getElementById('calibrationLoadBtn');
    const calibrationComputeBtn = document.getElementById('calibrationComputeBtn');
    const calibrationApplyBtn = document.getElementById('calibrationApplyBtn');
    const calibrationPoint1Measured = document.getElementById('calibrationPoint1Measured');
    const calibrationPoint1Reference = document.getElementById('calibrationPoint1Reference');
    const calibrationPoint1LiveBtn = document.getElementById('calibrationPoint1LiveBtn');
    const calibrationPoint2Measured = document.getElementById('calibrationPoint2Measured');
    const calibrationPoint2Reference = document.getElementById('calibrationPoint2Reference');
    const calibrationPoint2LiveBtn = document.getElementById('calibrationPoint2LiveBtn');
    const calibrationSingleMeasured = document.getElementById('calibrationSingleMeasured');
    const calibrationSingleReference = document.getElementById('calibrationSingleReference');
    const calibrationSingleLiveBtn = document.getElementById('calibrationSingleLiveBtn');
    const calibrationTwoPointFields = document.getElementById('calibrationTwoPointFields');
    const calibrationOnePointFields = document.getElementById('calibrationOnePointFields');
    const calibrationModeHint = document.getElementById('calibrationModeHint');
    const calibrationIoModule = document.getElementById('calibrationIoModule');
    const calibrationC0Current = document.getElementById('calibrationC0Current');
    const calibrationC1Current = document.getElementById('calibrationC1Current');
    const calibrationPreview = document.getElementById('calibrationPreview');
    const calibrationChecks = document.getElementById('calibrationChecks');
    const calibrationStatus = document.getElementById('calibrationStatus');
    const calibrationStatusChip = document.getElementById('calibrationStatusChip');
    const flowCfgRefreshBtn = document.getElementById('flowCfgRefresh');
    const flowCfgExportBtn = document.getElementById('flowCfgExport');
    const flowCfgImportBtn = document.getElementById('flowCfgImport');
    const flowCfgImportFileInput = document.getElementById('flowCfgImportFile');
    const flowCfgApplyBtn = document.getElementById('flowCfgApply');
    const flowCfgTree = document.getElementById('flowCfgTree');
    const flowCfgPathLabel = document.getElementById('flowCfgCurrentPath');
    const flowCfgPathMeta = document.getElementById('flowCfgPathMeta');
    const flowCfgFields = document.getElementById('flowCfgFields');
    const flowCfgApplyBusy = document.getElementById('flowCfgApplyBusy');
    const flowCfgStatus = document.getElementById('flowCfgStatus');
    const flowCfgBackupStatus = document.getElementById('flowCfgBackupStatus');
    const flowCfgBackupProgress = document.getElementById('flowCfgBackupProgress');
    const flowCfgBackupProgressLabel = document.getElementById('flowCfgBackupProgressLabel');
    const flowCfgBackupPct = document.getElementById('flowCfgBackupPct');
    const flowCfgBackupProgressBar = document.getElementById('flowCfgBackupProgressBar');
    const flowCfgBackupProgressDot = document.getElementById('flowCfgBackupProgressDot');
    const flowCfgTreePane = flowCfgTree ? flowCfgTree.closest('.cfg-pane') : null;
    const flowCfgDetailPane = flowCfgFields ? flowCfgFields.closest('.cfg-pane') : null;
    let flowCfgCurrentModule = '';
    let flowCfgCurrentData = {};
    let flowCfgCurrentPdmExtension = null;
    let flowCfgChildrenCache = {};
    let flowCfgPath = [];
    let flowCfgExpandedNodes = new Set();
    let flowCfgRootExpanded = true;
    let cfgTreeSelectedSource = 'flow';
    let cfgDocSources = [];
    let flowCfgDocsLoaded = false;
    let flowCfgDocIndex = null;
    let flowCfgDocIndexUnavailable = false;
    const flowCfgDocModuleCache = new Map();
    const flowCfgDocModuleLoadPromises = new Map();
    let flowCfgDocIndexPromise = null;
    let flowCfgDocI18nLocale = '';
    let flowCfgDocI18nMap = {};
    let flowCfgDocI18nPromise = null;
    let flowCfgTreeLoadingDepth = 0;
    let flowCfgDetailLoadingDepth = 0;
    let flowCfgLoadPromise = null;
    let flowCfgRetryTimer = null;
    let flowCfgFlowOnlyFailureStreak = 0;
    let flowCfgLocalApplyBusyDepth = 0;
    let flowCfgApplyBtnSavedText = '';
    let wifiConfigLoadedOnce = false;
    let flowCfgLoadedOnce = false;
    let calibrationLoadedOnce = false;
    let calibrationContext = null;
    let calibrationComputed = null;
    let upgradeManifestState = { manifest: null, manifestUrl: '', baseUrl: '' };
    let cfgTreeAliases = [];
    let cfgTreeVirtualBranches = [];
    const cfgTreeNodeTextNames = { supervisor: {}, flow: {} };
    const cfgTreeNodeTextNamePending = { supervisor: new Set(), flow: new Set() };
    const poolLogicDeviceIoOutputNames = { supervisor: {}, flow: {} };
    let supCfgCurrentModule = '';
    let supCfgCurrentData = {};
    let supCfgCurrentPdmExtension = null;
    let supCfgTreePath = '';
    let supCfgChildrenCache = {};
    let supCfgExpandedNodes = new Set();
    let supCfgRootExpanded = true;
    const ioOutputPdmLabels = Object.freeze({
      0: 'Filtration',
      1: 'Pompe pH',
      2: 'Pompe chlore',
      3: 'Robot',
      4: 'Pompe remplissage',
      5: 'Electrolyse',
      6: 'Eclairage',
      7: 'Chauffage eau',
      8: 'COMP01',
      9: 'COMP02',
      10: 'COMP03',
      11: 'COMP04',
      12: 'COMP05',
      13: 'COMP06',
      14: 'COMP07',
      15: 'COMP08'
    });
    let wifiScanAutoRequested = false;
    let flowStatusReqSeq = 0;
    let ioSummaryReqSeq = 0;
    let ioSummaryLoadedOnce = false;
    let ioTopologyCache = null;
    fieldApplyCheckIcon = iconCheckText();
    const flowCfgBackupFormat = 'flowio-configstore-backup';
    const flowCfgBackupVersion = 1;
    const flowCfgBackupRedactedToken = '__REDACTED__';
    const flowCfgBackupPatchTargetBytes = 1300;
    let flowCfgBackupBusy = false;
    const flowStatusDomainTtlMs = 20000;
    const calibrationSensorDefs = Object.freeze({
      ph: {
        key: 'ph',
        label: 'pH',
        mode: 'two',
        poollogicKey: 'ph_io_id',
        ioSlot: 1,
        runtimeUiId: 2203,
        recommendedSpan: 1.5,
        warningOffset: 1.0
      },
      orp: {
        key: 'orp',
        label: 'ORP',
        mode: 'two',
        poollogicKey: 'dis_io_id',
        ioSlot: 0,
        runtimeUiId: 2204,
        recommendedSpan: 120,
        warningOffset: 120
      },
      psi: {
        key: 'psi',
        label: 'Pression PSI',
        mode: 'two',
        poollogicKey: 'psi_io_id',
        ioSlot: 2,
        runtimeUiId: 2206,
        recommendedSpan: 0.4,
        warningOffset: 0.6
      },
      water_temp: {
        key: 'water_temp',
        label: 'Température eau',
        mode: 'one',
        poollogicKey: 'wat_temp_io_id',
        ioSlot: 4,
        runtimeUiId: 2201,
        recommendedSpan: 0,
        warningOffset: 2.0
      },
      air_temp: {
        key: 'air_temp',
        label: 'Température air',
        mode: 'one',
        poollogicKey: 'air_temp_io_id',
        ioSlot: 5,
        runtimeUiId: 2202,
        recommendedSpan: 0,
        warningOffset: 2.0
      }
    });
    const flowStatusDomainKeys = ['system', 'wifi', 'mqtt', 'pool', 'i2c'];
    const flowStatusDomainCache = {
      system: { data: null, fetchedAt: 0 },
      wifi: { data: null, fetchedAt: 0 },
      mqtt: { data: null, fetchedAt: 0 },
      pool: { data: null, fetchedAt: 0 },
      i2c: { data: null, fetchedAt: 0 }
    };
    const infoFlowDomainKeys = ['system', 'wifi', 'mqtt'];
    const infoRuntimeDomainEntries = Object.freeze({
      system: Object.freeze([
        Object.freeze({ id: 1301 }),
        Object.freeze({ id: 1302 }),
        Object.freeze({ id: 1303 }),
        Object.freeze({ id: 1801 }),
        Object.freeze({ id: 1802 }),
        Object.freeze({ id: 1803 }),
        Object.freeze({ id: 1804 })
      ]),
      wifi: Object.freeze([
        Object.freeze({ id: 1001 }),
        Object.freeze({ id: 1002 }),
        Object.freeze({ id: 1003 }),
        Object.freeze({ id: 1004 })
      ]),
      mqtt: Object.freeze([
        Object.freeze({ id: 2101 }),
        Object.freeze({ id: 2102 }),
        Object.freeze({ id: 2103 }),
        Object.freeze({ id: 2104 }),
        Object.freeze({ id: 2105 }),
        Object.freeze({ id: 2106 })
      ])
    });
    const infoFlowLoaderNodes = {
      system: infoSystemLoader,
      wifi: infoWifiLoader,
      mqtt: infoMqttLoader
    };
    const infoFlowDomainLoading = {
      system: false,
      wifi: false,
      mqtt: false
    };
    let infoFlowLastAttemptAt = 0;
    let infoFlowLastSuccessAt = 0;
    let infoFlowRefreshPromise = null;
    let runtimeMeasureDomainKeys = runtimeDomainsForProfile();
    let runtimeManifestDomainCache = null;
    let runtimeManifestDomainLoadPromise = null;
    const poolMeasureDomainState = {
      mode: createRuntimeDomainState(),
      sondes: createRuntimeDomainState(),
      micronova: createRuntimeDomainState(),
      alarm: createRuntimeDomainState()
    };
    let poolConfigLoadedOnce = false;
    let poolConfigReqSeq = 0;
    const poolConfigModuleDefs = Object.freeze([
      Object.freeze({ module: 'poollogic/modes', titleKey: 'pool.card.modes.title', title: 'Pilotage général', icon: 'tune', noteKey: 'pool.card.modes.note', note: 'Ces interrupteurs définissent si PoolLogic pilote la piscine et quelle stratégie de traitement est retenue.' }),
      Object.freeze({ module: 'poollogic/filtration', titleKey: 'pool.card.filtration.title', title: 'Filtration', icon: 'waves', noteKey: 'pool.card.filtration.note', note: 'La plage de filtration combine contraintes horaires et température d’eau pour protéger le bassin.' }),
      Object.freeze({ module: 'poollogic/heater', titleKey: 'pool.card.heater.title', title: 'Chauffage', icon: 'thermostat', noteKey: 'pool.card.heater.note', note: 'Le chauffage suit sa consigne seulement quand le mode automatique le permet.' }),
      Object.freeze({ module: 'poollogic/refill', titleKey: 'pool.card.refill.title', title: 'Remplissage', icon: 'water_drop', noteKey: 'pool.card.refill.note', note: 'Le remplissage garde une durée minimale pour éviter les cycles trop courts.' }),
      Object.freeze({ module: 'poollogic/safety', titleKey: 'pool.card.safety.title', title: 'Protections', icon: 'health_and_safety', noteKey: 'pool.card.safety.note', note: 'Seuils de pression, hors gel et bascule hiver utilisés par les automatismes.' }),
      Object.freeze({ module: 'poollogic/regulation', titleKey: 'pool.card.regulation.title', title: 'Régulation', icon: 'speed', noteKey: 'pool.card.regulation.note', note: 'Temporisations communes aux régulateurs pH et désinfection.' }),
      Object.freeze({ module: 'poollogic/robot', titleKey: 'pool.card.robot.title', title: 'Robot', icon: 'smart_toy', noteKey: 'pool.card.robot.note', note: 'Fenêtre de lancement et durée du nettoyage automatique.' })
    ]);
    const poolDisinfectionModeDefs = Object.freeze([
      Object.freeze({
        key: 'chlorine',
        typeValue: 0,
        module: 'poollogic/chlorine',
        titleKey: 'pool.disinfection.chlorine.title',
        title: 'Chlore / Brome',
        icon: 'science',
        accent: 'blue',
        noteKey: 'pool.disinfection.chlorine.note',
        note: 'Régulation liquide pilotée par la mesure ORP et une pompe doseuse.'
      }),
      Object.freeze({
        key: 'swg',
        typeValue: 1,
        module: 'poollogic/swg',
        titleKey: 'pool.disinfection.swg.title',
        title: 'Électrolyse',
        icon: 'bolt',
        accent: 'cyan',
        noteKey: 'pool.disinfection.swg.note',
        note: 'Production au sel, soit sur consigne ORP, soit en continu pendant la filtration.'
      }),
      Object.freeze({
        key: 'o2',
        typeValue: 2,
        module: 'poollogic/o2',
        titleKey: 'pool.disinfection.o2.title',
        title: 'Oxygène actif',
        icon: 'bubble_chart',
        accent: 'green',
        noteKey: 'pool.disinfection.o2.note',
        note: 'Dosage hebdomadaire calculé depuis le volume du bassin, la charge et la température.'
      })
    ]);
    const poolMeasureDomainAnimations = {};
    const upgradeReconnectFetchTimeoutMs = 1400;
    const upgradeTargetDefs = {
      flowios3: { manifestKey: 'flowios3', target: 'flowios3', endpoint: '/fwupdate/waveshare', label: 'FlowIOS3', order: 10 },
      waveshare: { manifestKey: 'waveshare', target: 'waveshare', endpoint: '/fwupdate/waveshare', label: 'Waveshare', order: 10 },
      esp32s3: { manifestKey: 'esp32s3', target: 'esp32s3', endpoint: '/fwupdate/waveshare', label: 'ESP32-S3', order: 11 },
      'flowios3-spiffs': { manifestKey: 'flowios3-spiffs', target: 'spiffs', endpoint: '/fwupdate/spiffs', label: 'SPIFFS FlowIOS3', order: 39 },
      'esp32s3-spiffs': { manifestKey: 'esp32s3-spiffs', target: 'spiffs', endpoint: '/fwupdate/spiffs', label: 'SPIFFS ESP32-S3', order: 39 },
      'waveshare-spiffs': { manifestKey: 'waveshare-spiffs', target: 'spiffs', endpoint: '/fwupdate/spiffs', label: 'SPIFFS Waveshare', order: 39 },
      nextion: { manifestKey: 'nextion', target: 'nextion', endpoint: '/fwupdate/nextion', label: 'Nextion', order: 30 },
      spiffs: { manifestKey: 'spiffs', target: 'spiffs', endpoint: '/fwupdate/spiffs', label: 'SPIFFS', order: 40 }
    };
    const upgradeComponentDefs = [
      {
        key: 'flowio',
        title: 'FlowIOS3',
        subtitle: 'Firmware Waveshare',
        icon: 'layers',
        tone: 'blue',
        commentsAvailable: 'Ajout de nouvelles fonctionnalités et améliorations système',
        commentsCurrent: 'Firmware système actuel'
      },
      {
        key: 'spiffs',
        title: 'SPIFFS',
        subtitle: 'Fichiers système',
        icon: 'memory',
        tone: 'green',
        commentsAvailable: 'Nouveaux fichiers de configuration et ressources',
        commentsCurrent: 'Fichiers système actuels'
      },
      {
        key: 'nextion',
        title: 'Nextion',
        subtitle: 'Interface écran',
        icon: 'display_settings',
        tone: 'orange',
        commentsAvailable: 'Nouvelle interface écran disponible',
        commentsCurrent: 'Interface écran actuelle'
      }
    ];

    const wsProto = location.protocol === 'https:' ? 'wss' : 'ws';
    const logSocketPath = '/wslog';
    const logSourceMeta = {
      supervisor: { cmd: 'src:supervisor', label: 'Supervisor', statusBusy: 'occupé (1 terminal max)' },
      flowio: { cmd: 'src:flowio', label: 'flow.io', statusBusy: 'occupé (1 terminal max)' }
    };
    let logSource = 'supervisor';
    let logSocket = null;
    let upgradeUiStatusMuted = false;
    const upgradeStatusPoller = createTimeoutRunner(() => pollUpgradeStatusTick());
    const infoRuntimePoller = createIntervalRunner(() => pollInfoRuntimeTick(), infoRefreshActiveMs);
    const infoSupervisorPoller = createIntervalRunner(() => pollInfoSupervisorTick(), infoSupervisorRefreshMs);
    const upgradeReconnectStageTimer = createTimeoutRunner(() => enterUpgradeReconnectPhase());
    const upgradeReconnectCompletionTimer = createTimeoutRunner(() => markUpgradeUiCompletedAfterReconnect());
    const upgradeReconnectMonitor = createIntervalRunner(() => probeUpgradeReconnect(), 1500);
    const poolMeasuresPoller = createIntervalRunner(() => {
      if (getActivePageId() !== 'page-pool-measures' || document.hidden) return;
      return refreshPoolMeasures(false);
    }, 10000);
    const ioSummaryPoller = createIntervalRunner(() => {
      if (getActivePageId() !== 'page-io-summary' || document.hidden) return;
      return refreshIoSummary(false);
    }, 10000);
    const wifiScanPoller = createTimeoutRunner(() => refreshWifiScanStatus(false));

    function activeLogSourceMeta() {
      return logSourceMeta[logSource] || logSourceMeta.supervisor;
    }

    function applyLogSourceUi() {
      const localOnly = webLocalRuntime === true;
      if (logSourceSelect) {
        logSourceSelect.hidden = localOnly;
        logSourceSelect.disabled = localOnly;
      }
      if (localOnly && logSource !== 'supervisor') {
        setLogSource('supervisor');
      } else if (logSourceSelect && logSourceSelect.value !== logSource) {
        logSourceSelect.value = logSource;
      }
    }

    function setWsStatusText(status) {
      if (!wsStatus) return;
      const meta = activeLogSourceMeta();
      wsStatus.textContent = meta.label + ' : ' + status;
    }

    const ansiState = { fg: null };
    const ansiFgMap = {
      30: '#94a3b8', 31: '#ef4444', 32: '#22c55e', 33: '#f59e0b',
      34: '#60a5fa', 35: '#f472b6', 36: '#22d3ee', 37: '#e2e8f0',
      90: '#64748b', 91: '#f87171', 92: '#4ade80', 93: '#fbbf24',
      94: '#93c5fd', 95: '#f9a8d4', 96: '#67e8f9', 97: '#f8fafc'
    };
    const ansiRe = /\u001b\[([0-9;]*)m/g;

    function applySgrCodes(rawCodes) {
      const codes = rawCodes === '' ? [0] : rawCodes.split(';').map((v) => parseInt(v, 10)).filter(Number.isFinite);
      for (const code of codes) {
        if (code === 0 || code === 39) {
          ansiState.fg = null;
          continue;
        }
        if (Object.prototype.hasOwnProperty.call(ansiFgMap, code)) {
          ansiState.fg = ansiFgMap[code];
        }
      }
    }

    function decodeAnsiLine(rawLine) {
      let out = '';
      let cursor = 0;
      let lineColor = ansiState.fg;
      rawLine.replace(ansiRe, (full, codes, idx) => {
        out += rawLine.slice(cursor, idx);
        applySgrCodes(codes);
        if (ansiState.fg) lineColor = ansiState.fg;
        cursor = idx + full.length;
        return '';
      });
      out += rawLine.slice(cursor);
      return { text: out, color: lineColor };
    }

    function closeLogSocket() {
      if (!logSocket) return;
      logSocket.onopen = null;
      logSocket.onclose = null;
      logSocket.onerror = null;
      logSocket.onmessage = null;
      try {
        logSocket.close();
      } catch (err) {
        // Ignore close errors on stale sockets.
      }
      logSocket = null;
    }

    function connectLogSocket() {
      if (!logsOverlayOpen || document.hidden) {
        closeLogSocket();
        setWsStatusText(tr('terminal.inactive', 'inactif'));
        return;
      }
      closeLogSocket();
      setWsStatusText(tr('terminal.connecting', 'connexion...'));
      const meta = activeLogSourceMeta();
      const socket = new WebSocket(wsProto + '://' + location.host + logSocketPath);
      logSocket = socket;
      socket.onopen = () => {
        if (socket !== logSocket) return;
        setWsStatusText(tr('terminal.connected', 'connecté'));
        try {
          socket.send(meta.cmd);
        } catch (err) {
        }
      };
      socket.onclose = (ev) => {
        if (socket !== logSocket) return;
        logSocket = null;
        const code = ev && Number.isFinite(ev.code) ? ev.code : 0;
        if (code === 1008) {
          setWsStatusText(meta.statusBusy);
        } else {
          const disconnected = tr('terminal.disconnected', 'déconnecté');
          setWsStatusText(code ? (disconnected + ' (' + code + ')') : disconnected);
        }
      };
      socket.onerror = () => {
        if (socket !== logSocket) return;
        setWsStatusText(tr('terminal.error', 'erreur'));
      };
      socket.onmessage = (ev) => {
        if (socket !== logSocket) return;
        appendTerminalLine(String(ev.data || ''), true);
      };
    }

    function appendTerminalLine(raw, decodeAnsi) {
      if (!term) return;
      const parsed = decodeAnsi ? decodeAnsiLine(raw) : { text: raw, color: null };
      const row = document.createElement('div');
      row.className = 'log-line';
      if (parsed.color) row.style.color = parsed.color;
      row.textContent = parsed.text;
      term.appendChild(row);
      while (term.childNodes.length > 1200) term.removeChild(term.firstChild);
      if (autoScrollEnabled) term.scrollTop = term.scrollHeight;
    }

    async function requestBootLogDump() {
      const limit = 64;
      let offset = 0;
      let loaded = 0;
      let entries = 0;
      let dropped = 0;
      let state = 'unknown';

      if (bootLogDumpBtn) bootLogDumpBtn.disabled = true;
      setWsStatusText('Boot logs : chargement...');
      try {
        while (true) {
          const response = await fetch('/api/logs/boot?offset=' + encodeURIComponent(offset) + '&limit=' + limit);
          if (!response.ok) {
            throw new Error('HTTP ' + response.status);
          }
          const page = await response.json();
          const lines = Array.isArray(page.lines) ? page.lines : [];
          entries = Number(page.entries) || 0;
          dropped = Number(page.dropped) || 0;
          state = String(page.state || 'unknown');

          if (offset === 0) {
            appendTerminalLine(
              '===== BOOT LOGS BEGIN - ' + entries + ' lignes disponibles, dropped=' + dropped + ', state=' + state + ' =====',
              false
            );
          }
          for (const line of lines) {
            appendTerminalLine(String(line || ''), false);
          }

          loaded += Number(page.count) || lines.length;
          setWsStatusText('Boot logs : ' + loaded + '/' + entries + ' lignes affichees, ' + dropped + ' perdues, etat=' + state);

          if (page.complete || page.next == null || Number(page.count) === 0) {
            break;
          }
          offset = Number(page.next);
          if (!Number.isFinite(offset) || offset < 0) {
            break;
          }
          await new Promise((resolve) => setTimeout(resolve, 10));
        }
        appendTerminalLine('===== BOOT LOGS END - ' + loaded + '/' + entries + ' lignes affichees =====', false);
        setWsStatusText('Boot logs : ' + loaded + '/' + entries + ' lignes affichees, ' + dropped + ' perdues, etat=' + state);
      } catch (err) {
        const msg = 'Impossible de charger les logs de boot : ' + (err && err.message ? err.message : String(err));
        appendTerminalLine(msg, false);
        setWsStatusText(msg);
        console.error(err);
      } finally {
        if (bootLogDumpBtn) bootLogDumpBtn.disabled = false;
      }
    }

    function openLogsOverlay() {
      if (!logsOverlay) return;
      logsOverlay.hidden = false;
      logsOverlay.setAttribute('aria-hidden', 'false');
      logsOverlayOpen = true;
      if (term) term.textContent = '';
      connectLogSocket();
    }

    function closeLogsOverlay() {
      if (!logsOverlay) return;
      logsOverlayOpen = false;
      closeLogSocket();
      logsOverlay.hidden = true;
      logsOverlay.setAttribute('aria-hidden', 'true');
      setWsStatusText(tr('terminal.inactive', 'inactif'));
    }

    function activityEventDate(ev) {
      const epoch = Number(ev && ev.epoch_s) || 0;
      if (epoch > 0) return new Date(epoch * 1000);
      return null;
    }

    function formatActivityTime(date) {
      if (!date) return '--:--:--';
      return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
    }

    function formatActivityDay(date) {
      if (!date) return 'Date inconnue';
      const dayText = date.toLocaleDateString([], { day: 'numeric', month: 'long', year: 'numeric' });
      const startOfDay = new Date(date.getFullYear(), date.getMonth(), date.getDate()).getTime();
      const now = new Date();
      const startOfToday = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();
      const dayOffset = Math.round((startOfToday - startOfDay) / 86400000);
      if (dayOffset === 0) return 'Aujourd’hui · ' + dayText;
      if (dayOffset === 1) return 'Hier · ' + dayText;
      return dayText;
    }

    function formatActivityRelative(date) {
      if (!date) return 'heure non synchronisée';
      const diffSec = Math.max(0, Math.round((Date.now() - date.getTime()) / 1000));
      if (diffSec < 60) return diffSec <= 3 ? 'Maintenant' : ('Il y a ' + diffSec + ' secondes');
      const diffMin = Math.round(diffSec / 60);
      if (diffMin < 60) return 'Il y a ' + diffMin + ' min';
      const diffHour = Math.round(diffMin / 60);
      if (diffHour < 24) return 'Il y a ' + diffHour + ' h';
      const diffDay = Math.round(diffHour / 24);
      return 'Il y a ' + diffDay + ' j';
    }

    function activityMatchesFilter(ev) {
      const date = activityEventDate(ev);
      if (date) {
        const end = Date.now() - (activityWindowShiftHours * 3 * 3600000);
        const start = end - (3 * 3600000);
        const ts = date.getTime();
        if (ts < start || ts > end) return false;
      } else if (activityWindowShiftHours !== 0) {
        return false;
      }
      if (activityFilter === 'all') return true;
      if (activityFilter === 'poollogic') return ev.domain_name === 'poollogic' || ev.domain_name === 'pooldevice';
      if (activityFilter === 'manual') return ev.source_name === 'manual';
      if (activityFilter === 'safety') return ev.domain_name === 'alarm' || ev.source_name === 'safety' || ev.severity_name === 'warning' || ev.severity_name === 'alarm';
      if (activityFilter === 'system') return ev.domain_name === 'system';
      return true;
    }

    function updateActivityRangeText() {
      if (!activityRangeText) return;
      const end = new Date(Date.now() - (activityWindowShiftHours * 3 * 3600000));
      const start = new Date(end.getTime() - (3 * 3600000));
      activityRangeText.textContent =
        start.toLocaleDateString([], { day: 'numeric', month: 'short' }) + ' à ' +
        start.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }) + ' - ' +
        end.toLocaleDateString([], { day: 'numeric', month: 'short' }) + ' à ' +
        end.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    }

    function activityWindowBoundsMs() {
      const end = Date.now() - (activityWindowShiftHours * 3 * 3600000);
      return { start: end - (3 * 3600000), end };
    }

    function cancelActivityLogRefresh() {
      activityRequestToken += 1;
      const controller = activityAbortController;
      activityAbortController = null;
      if (controller) controller.abort();
    }

    function renderActivityLog(events, stats) {
      if (!activityLogList) return;
      activityLogList.innerHTML = '';
      updateActivityRangeText();
      const filtered = (Array.isArray(events) ? events : [])
        .filter(activityMatchesFilter)
        .sort((a, b) => {
          const ae = Number(a.epoch_s) || 0;
          const be = Number(b.epoch_s) || 0;
          if (ae !== be) return be - ae;
          return (Number(b.seq) || 0) - (Number(a.seq) || 0);
        });
      if (!filtered.length) {
        const empty = document.createElement('div');
        empty.className = 'activity-empty';
        empty.textContent = 'Aucune activité pour ce filtre.';
        activityLogList.appendChild(empty);
        if (activityLogStatus) {
          if (stats) {
            activityLogStatus.textContent =
              '0/' + (Number(stats.entries) || 0) +
              ' événement(s), persistés=' + (Number(stats.persisted) || 0);
          } else {
            activityLogStatus.textContent = 'Aucune activité.';
          }
        }
        return;
      }
      let currentDay = '';
      let currentDayEvents = null;
      filtered.forEach((ev) => {
        const date = activityEventDate(ev);
        const day = formatActivityDay(date);
        if (day !== currentDay) {
          currentDay = day;
          const daySection = document.createElement('section');
          daySection.className = 'activity-day';
          const dayNode = document.createElement('div');
          dayNode.className = 'activity-day-title';
          dayNode.textContent = day;
          currentDayEvents = document.createElement('div');
          currentDayEvents.className = 'activity-day-events';
          daySection.appendChild(dayNode);
          daySection.appendChild(currentDayEvents);
          activityLogList.appendChild(daySection);
        }
        const row = document.createElement('article');
        row.className = 'activity-row activity-severity-' + String(ev.severity_name || 'info');
        const time = document.createElement('time');
        time.className = 'activity-row-time';
        time.textContent = formatActivityTime(date);
        if (date) time.dateTime = date.toISOString();
        const rail = document.createElement('div');
        rail.className = 'activity-row-rail';
        const icon = document.createElement('span');
        icon.className = 'ui-msr activity-row-icon';
        icon.setAttribute('aria-hidden', 'true');
        icon.textContent = String(ev.icon || 'history');
        rail.appendChild(icon);
        const main = document.createElement('div');
        main.className = 'activity-row-main';
        const title = document.createElement('div');
        title.className = 'activity-row-title';
        const strong = document.createElement('strong');
        strong.textContent = String(ev.title || 'Activité');
        title.appendChild(strong);
        const meta = document.createElement('div');
        meta.className = 'activity-row-meta';
        meta.textContent = formatActivityRelative(date);
        main.appendChild(title);
        if (ev.detail) {
          const detail = document.createElement('div');
          detail.className = 'activity-row-detail';
          detail.textContent = String(ev.detail);
          main.appendChild(detail);
        }
        main.appendChild(meta);
        row.appendChild(time);
        row.appendChild(rail);
        row.appendChild(main);
        currentDayEvents.appendChild(row);
      });
      activityLogList.querySelectorAll('.activity-day-events').forEach((dayEvents) => {
        const rows = dayEvents.querySelectorAll('.activity-row');
        if (!rows.length) return;
        rows[0].classList.add('is-first');
        rows[rows.length - 1].classList.add('is-last');
      });
      if (activityLogStatus && stats) {
        activityLogStatus.textContent =
          filtered.length + '/' + (Number(stats.entries) || filtered.length) +
          ' événement(s), persistés=' + (Number(stats.persisted) || 0);
      }
    }

    async function refreshActivityLog(showBusy) {
      if (!activityLogList) return;
      if (showBusy && activityLogStatus) activityLogStatus.textContent = 'Chargement du journal...';

      if (activityAbortController) activityAbortController.abort();
      const controller = new AbortController();
      activityAbortController = controller;
      const requestToken = ++activityRequestToken;
      const limit = 32;
      let offset = 0;
      const events = [];
      const seenSequences = new Set();
      let stats = null;
      const windowBounds = activityWindowBoundsMs();

      try {
        while (true) {
          const url =
            '/api/activity/logs?order=desc&offset=' + encodeURIComponent(offset) +
            '&limit=' + limit;
          const response = await fetch(url, { cache: 'no-store', signal: controller.signal });
          if (!response.ok) throw new Error('HTTP ' + response.status);
          const page = await response.json();
          if (requestToken !== activityRequestToken) return;

          stats = page;
          const pageEvents = Array.isArray(page.events) ? page.events : [];
          pageEvents.forEach((event) => {
            const sequence = Number(event && event.seq) || 0;
            if (sequence > 0 && seenSequences.has(sequence)) return;
            if (sequence > 0) seenSequences.add(sequence);
            events.push(event);
          });

          const reachedWindowStart = pageEvents.some((event) => {
            const epoch = Number(event && event.epoch_s) || 0;
            return epoch > 0 && (epoch * 1000) <= windowBounds.start;
          });
          if (reachedWindowStart || page.complete || page.next == null || Number(page.count) === 0) break;

          offset = Number(page.next);
          if (!Number.isFinite(offset) || offset < 0 || events.length >= 768) break;
        }

        if (requestToken !== activityRequestToken) return;
        activityLoadedEvents = events;
        activityLoadedStats = stats;
        renderActivityLog(activityLoadedEvents, activityLoadedStats);
      } catch (err) {
        if (err && err.name === 'AbortError') return;
        throw err;
      } finally {
        if (activityAbortController === controller) {
          activityAbortController = null;
        }
      }
    }

    async function purgeActivityLog() {
      if (!confirm('Confirmer la purge du Journal d’Activité ? Cette action efface l’historique en mémoire et dans le SPIFFS.')) {
        return;
      }
      if (activityPurgeBtn) activityPurgeBtn.disabled = true;
      if (activityLogStatus) activityLogStatus.textContent = 'Purge du journal...';
      try {
        const response = await fetch('/api/activity/purge', { method: 'POST', cache: 'no-store' });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const payload = await response.json().catch(() => ({}));
        if (payload && payload.ok === false) throw new Error('Purge refusée');
        activityWindowShiftHours = 0;
        await refreshActivityLog(false);
        if (activityLogStatus) activityLogStatus.textContent = 'Journal purgé.';
      } catch (err) {
        if (activityLogStatus) activityLogStatus.textContent = 'Purge impossible: ' + (err && err.message ? err.message : String(err));
      } finally {
        if (activityPurgeBtn) activityPurgeBtn.disabled = false;
      }
    }

    function setLogSource(source) {
      let normalized = String(source || '').trim().toLowerCase();
      if (webLocalRuntime && normalized === 'flowio') {
        normalized = 'supervisor';
      }
      logSource = Object.prototype.hasOwnProperty.call(logSourceMeta, normalized) ? normalized : 'supervisor';
      if (logSourceSelect && logSourceSelect.value !== logSource) {
        logSourceSelect.value = logSource;
      }
      if (logsOverlayOpen) {
        if (term) term.textContent = '';
        connectLogSocket();
      } else {
        setWsStatusText(tr('terminal.inactive', 'inactif'));
      }
    }

    function refreshAutoscrollUi() {
      toggleAutoscrollInput.checked = autoScrollEnabled;
      toggleAutoscrollInput.setAttribute('aria-checked', autoScrollEnabled ? 'true' : 'false');
      toggleAutoscrollInput.setAttribute(
        'title',
        autoScrollEnabled ? 'Défilement auto activé' : 'Défilement auto désactivé'
      );
    }

    const iconeOeilOuvert = 'Cacher';
    const iconeOeilBarre = 'Voir';

    function mettreAJourEtatVisibiliteMotDePasse(inputEl, toggleBtn, labelAfficher, labelMasquer) {
      if (!inputEl || !toggleBtn) return;
      const isVisible = inputEl.type === 'text';
      toggleBtn.innerHTML = isVisible ? iconeOeilOuvert : iconeOeilBarre;
      toggleBtn.setAttribute('aria-pressed', isVisible ? 'true' : 'false');
      toggleBtn.setAttribute('aria-label', isVisible ? labelMasquer : labelAfficher);
      toggleBtn.setAttribute('title', isVisible ? 'Mot de passe en clair' : 'Mot de passe masqué');
    }

    function basculerVisibiliteMotDePasse(inputEl, toggleBtn, labelAfficher, labelMasquer) {
      if (!inputEl || !toggleBtn) return;
      const isMasked = inputEl.type === 'password';
      inputEl.type = isMasked ? 'text' : 'password';
      mettreAJourEtatVisibiliteMotDePasse(inputEl, toggleBtn, labelAfficher, labelMasquer);
    }
    if (toggleAutoscrollInput) toggleAutoscrollInput.addEventListener('change', () => {
      autoScrollEnabled = !!toggleAutoscrollInput.checked;
      refreshAutoscrollUi();
      if (autoScrollEnabled && term) term.scrollTop = term.scrollHeight;
    });
    if (logSourceSelect) {
      logSourceSelect.value = 'supervisor';
      logSourceSelect.addEventListener('change', () => {
        setLogSource(logSourceSelect.value);
      });
    }
    if (bootLogDumpBtn) {
      bootLogDumpBtn.addEventListener('click', requestBootLogDump);
    }
    if (openLogsOverlayBtn) {
      openLogsOverlayBtn.addEventListener('click', openLogsOverlay);
    }
    if (closeLogsOverlayBtn) {
      closeLogsOverlayBtn.addEventListener('click', closeLogsOverlay);
    }
    if (logsOverlay) {
      logsOverlay.addEventListener('click', (ev) => {
        if (ev.target === logsOverlay) closeLogsOverlay();
      });
    }
    document.addEventListener('keydown', (ev) => {
      if (ev.key === 'Escape' && logsOverlayOpen) closeLogsOverlay();
    });
    if (activityRefreshBtn) {
      activityRefreshBtn.addEventListener('click', () => refreshActivityLog(true).catch((err) => {
        if (activityLogStatus) activityLogStatus.textContent = 'Journal indisponible: ' + (err && err.message ? err.message : String(err));
      }));
    }
    if (activityPurgeBtn) {
      activityPurgeBtn.addEventListener('click', () => {
        purgeActivityLog().catch((err) => {
          if (activityLogStatus) activityLogStatus.textContent = 'Purge impossible: ' + (err && err.message ? err.message : String(err));
        });
      });
    }
    if (activityPrevBtn) {
      activityPrevBtn.addEventListener('click', () => {
        activityWindowShiftHours += 1;
        updateActivityRangeText();
        refreshActivityLog(false).catch(() => {});
      });
    }
    if (activityNextBtn) {
      activityNextBtn.addEventListener('click', () => {
        activityWindowShiftHours = Math.max(0, activityWindowShiftHours - 1);
        updateActivityRangeText();
        refreshActivityLog(false).catch(() => {});
      });
    }
    activityFilterBtns.forEach((btn) => {
      btn.addEventListener('click', () => {
        activityFilter = String(btn.dataset.activityFilter || 'all');
        activityFilterBtns.forEach((el) => el.classList.toggle('is-active', el === btn));
        renderActivityLog(activityLoadedEvents, activityLoadedStats);
      });
    });
    applyLogSourceUi();
    refreshAutoscrollUi();
    logSource = 'supervisor';
    setWsStatusText(tr('terminal.inactive', 'inactif'));
    if (flowCfgApplyBtn) flowCfgApplyBtn.disabled = true;

    function setUpgradeProgress(value) {
      const p = Math.max(0, Math.min(100, Number(value) || 0));
      if (upgradeProgressBar) {
        upgradeProgressBar.style.width = p + '%';
        upgradeProgressBar.classList.toggle('is-complete', p >= 100);
      }
      if (upgradePct) {
        upgradePct.textContent = p + '%';
      }
    }

    function setUpgradeMessage(text) {
      const message = String(text || '').trim() || tr('updates.none', 'Aucune opération en cours.');
      if (upgradeFooterStatus) {
        upgradeFooterStatus.innerHTML = '<span class="sdot"></span>' + message;
      }
    }

    function readUpgradeUiSession() {
      const raw = getStorageValue(sessionStorage, upgradeUiSessionStorageKey);
      if (!raw) return null;
      try {
        const parsed = JSON.parse(raw);
        return parsed && typeof parsed === 'object' ? parsed : null;
      } catch (err) {
        return null;
      }
    }

    function writeUpgradeUiSession(session) {
      if (!session || typeof session !== 'object') return;
      setStorageValue(sessionStorage, upgradeUiSessionStorageKey, JSON.stringify(session));
    }

    function clearUpgradeUiSession() {
      stopUpgradeReconnectFlow();
      try {
        sessionStorage.removeItem(upgradeUiSessionStorageKey);
      } catch (err) {
      }
    }

    function upgradeTargetLabel(target) {
      const key = String(target || '').trim().toLowerCase();
      if (key === 'flowios3' || key === 'esp32s3') return 'FlowIOS3';
      if (key === 'waveshare') return 'FlowIOS3';
      if (key === 'spiffs') return 'SPIFFS';
      if (key === 'nextion') return 'Nextion';
      return 'Firmware';
    }

    function upgradeUsesReconnect(target) {
      const key = String(target || '').trim().toLowerCase();
      return key === 'flowios3' || key === 'esp32s3' || key === 'waveshare' || key === 'spiffs';
    }

    function upgradeStepDefinitions(target) {
      return [
        { id: 'target', label: tr('updates.step.target', 'Initialisation') },
        { id: 'download', label: tr('updates.step.download', 'Connexion') },
        { id: 'flash', label: tr('updates.step.flash', 'Mise à jour') },
        { id: 'reboot', label: tr('updates.step.reboot', 'Redémarrage') },
        { id: 'reconnect', label: tr('updates.step.reconnect', 'Reconnexion') }
      ];
    }

    function upgradePhaseIndex(phase) {
      if (phase === 'target') return 0;
      if (phase === 'download') return 1;
      if (phase === 'flash') return 2;
      if (phase === 'reboot') return 3;
      if (phase === 'reconnect') return 4;
      if (phase === 'done') return 5;
      return -1;
    }

    function upgradePhasePercent(session) {
      const phase = String(session && session.phase ? session.phase : 'idle');
      const progress = Math.max(0, Math.min(100, Number(session && session.backendProgress) || 0));
      const reconnectProgress = Math.max(0, Math.min(100, Number(session && session.reconnectProgress) || 0));
      if (phase === 'target') return 1;
      if (phase === 'download') return 1 + Math.round(progress * 0.04);
      if (phase === 'flash') return 5 + Math.round(progress * 0.90);
      if (phase === 'reboot') return 97;
      if (phase === 'reconnect') return 97 + Math.round(reconnectProgress * 0.03);
      if (phase === 'done') return 100;
      if (phase === 'error') return Math.max(6, Math.min(96, Number(session && session.lastPercent) || 12));
      return 0;
    }

    function upgradeStepProgress(stepId, state, session) {
      if (state === 'done') return 100;
      if (state !== 'active') return null;
      const phase = String(session && session.phase ? session.phase : '');
      if (stepId !== phase) return null;
      if (phase === 'target') return 100;
      if (phase === 'download' || phase === 'flash') {
        return Math.max(0, Math.min(100, Number(session && session.backendProgress) || 0));
      }
      if (phase === 'reboot') return 100;
      if (phase === 'reconnect') {
        return Math.max(0, Math.min(100, Number(session && session.reconnectProgress) || 0));
      }
      if (phase === 'done') return 100;
      return null;
    }

    function upgradeStepStatusLabel(stepId, state, session) {
      if (state === 'done') return tr('updates.step.status.done', 'Terminé');
      const progress = upgradeStepProgress(stepId, state, session);
      if (state === 'active') {
        return progress !== null
          ? tr('updates.step.status.inProgressPct', 'En cours ({pct}%)').replace('{pct}', String(progress))
          : tr('updates.step.status.inProgress', 'En cours');
      }
      if (state === 'pending') return tr('updates.step.status.pending', 'En attente');
      if (state === 'error') return tr('updates.step.status.error', 'Erreur');
      return tr('updates.step.status.pending', 'En attente');
    }

    function upgradeStepState(stepId, session) {
      const phase = String(session && session.phase ? session.phase : 'idle');
      if (phase === 'idle') return 'pending';
      if (phase === 'error') {
        const failedStep = String(session && session.failedStep ? session.failedStep : 'flash');
        const failedIndex = upgradePhaseIndex(failedStep);
        const stepIndex = upgradePhaseIndex(stepId);
        if (stepIndex < failedIndex) return 'done';
        if (stepId === failedStep) return 'error';
        return 'pending';
      }
      const activeIndex = upgradePhaseIndex(phase);
      const stepIndex = upgradePhaseIndex(stepId);
      if (stepIndex < activeIndex) return 'done';
      if (stepIndex === activeIndex) return phase === 'done' ? 'done' : 'active';
      return 'pending';
    }

    function renderUpgradeSteps(session) {
      if (!upgradeSteps) return;
      const defs = upgradeStepDefinitions(session && session.target);
      upgradeSteps.innerHTML = '';
      defs.forEach((step) => {
        const state = upgradeStepState(step.id, session);
        const row = document.createElement('div');
        row.className = 'step-row';

        const icon = document.createElement('span');
        icon.className = 'step-ic ' + state;
        if (state === 'active') {
          const activeDot = document.createElement('span');
          activeDot.className = 'step-active-dot';
          activeDot.setAttribute('aria-hidden', 'true');
          icon.appendChild(activeDot);
        } else if (state === 'done') {
          const doneIcon = document.createElement('span');
          doneIcon.className = 'ui-msr';
          doneIcon.setAttribute('aria-hidden', 'true');
          doneIcon.textContent = 'check';
          icon.appendChild(doneIcon);
        } else if (state === 'error') {
          const errIcon = document.createElement('span');
          errIcon.className = 'ui-msr';
          errIcon.setAttribute('aria-hidden', 'true');
          errIcon.textContent = 'close';
          icon.appendChild(errIcon);
        } else {
          const pendingIcon = document.createElement('span');
          pendingIcon.className = 'ui-msr';
          pendingIcon.setAttribute('aria-hidden', 'true');
          pendingIcon.textContent = 'radio_button_unchecked';
          icon.appendChild(pendingIcon);
        }
        row.appendChild(icon);

        const meta = document.createElement('span');
        meta.className = 'step-meta';

        const label = document.createElement('span');
        label.className = 'step-lbl ' + state;
        label.textContent = step.label;
        meta.appendChild(label);

        const sub = document.createElement('span');
        sub.className = 'step-sub ' + state;
        sub.textContent = upgradeStepStatusLabel(step.id, state, session);
        meta.appendChild(sub);

        row.appendChild(meta);

        upgradeSteps.appendChild(row);
      });
    }

    function isUpgradeUiCancelable(session) {
      const phase = String(session && session.phase ? session.phase : 'idle');
      return !!(session && (session.awaitingReconnect || phase === 'target' || phase === 'download' || phase === 'flash' || phase === 'reboot' || phase === 'reconnect'));
    }

    function syncUpgradeCancelButton(session) {
      if (!cancelUpgradeUiBtn) return;
      const canCancel = isUpgradeUiCancelable(session);
      cancelUpgradeUiBtn.hidden = !canCancel;
      cancelUpgradeUiBtn.disabled = !canCancel;
    }

    function renderUpgradeJourney(session) {
      const safeSession = session && typeof session === 'object' ? session : { phase: 'idle', target: '' };
      const phase = String(safeSession.phase || 'idle');
      const detail = String(safeSession.detail || '');
      const targetLabel = upgradeTargetLabel(safeSession.target);
      const stateLabel = phase === 'idle'
        ? tr('updates.phase.idle', 'Prêt')
        : phase === 'target'
          ? tr('updates.phase.target', 'Cible sélectionnée')
          : phase === 'download'
            ? tr('updates.phase.download', 'Téléchargement')
            : phase === 'flash'
              ? tr('updates.phase.flash', 'Mise à jour')
              : phase === 'reboot'
                ? tr('updates.phase.reboot', 'Redémarrage')
                : phase === 'reconnect'
                  ? tr('updates.phase.reconnect', 'Attente de Reconnection')
                  : phase === 'done'
                    ? tr('updates.phase.done', 'Mise à jour terminée')
                    : tr('updates.phase.error', 'Erreur');

      if (upgradeJourneyLabel) {
        upgradeJourneyLabel.textContent = safeSession.target
          ? (tr('updates.progress', 'Statut de l’upgrade') + ' · ' + targetLabel)
          : tr('updates.progress', 'Statut de l’upgrade');
      }
      setUpgradeProgress(upgradePhasePercent(safeSession));
      setUpgradeMessage(detail || (phase === 'idle' ? tr('updates.none', 'Aucune opération en cours.') : stateLabel));
      renderUpgradeSteps(safeSession);
      if (upStatusChip) {
        upStatusChip.textContent = stateLabel;
      }
      syncUpgradeCancelButton(safeSession);
    }

    function updateUpgradeUiSession(patch) {
      const current = readUpgradeUiSession() || {
        phase: 'idle',
        target: '',
        detail: tr('updates.none', 'Aucune opération en cours.'),
        backendProgress: 0,
        lastPercent: 0,
        awaitingReconnect: false,
        reconnectShown: false,
        reconnectProgress: 0
      };
      const next = Object.assign({}, current, patch || {});
      next.lastPercent = upgradePhasePercent(next);
      writeUpgradeUiSession(next);
      renderUpgradeJourney(next);
      return next;
    }

    function startUpgradeUiSession(target) {
      stopUpgradeReconnectFlow();
      upgradeUiStatusMuted = false;
      return updateUpgradeUiSession({
        phase: 'target',
        target: target,
        detail: tr('updates.detail.targetSelected', 'Sélection de la cible {target}.')
          .replace('{target}', upgradeTargetLabel(target)),
        backendProgress: 0,
        awaitingReconnect: false,
        reconnectShown: false,
        reconnectProgress: 0,
        failedStep: ''
      });
    }

    function cancelUpgradeUiSession() {
      upgradeUiStatusMuted = true;
      stopUpgradeStatusPolling();
      clearUpgradeUiSession();
      renderUpgradeJourney({
        phase: 'idle',
        target: '',
        detail: tr('updates.none', 'Aucune opération en cours.')
      });
    }

    function stopUpgradeReconnectFlow() {
      upgradeReconnectStageTimer.stop();
      upgradeReconnectCompletionTimer.stop();
      upgradeReconnectMonitor.stop();
    }

    function scheduleUpgradeReconnectPhase(delayMs) {
      upgradeReconnectStageTimer.schedule(Math.max(0, Number(delayMs) || 0));
    }

    function startUpgradeReconnectMonitor() {
      upgradeReconnectMonitor.start();
    }

    function scheduleUpgradeReconnectCompletion(delayMs) {
      upgradeReconnectCompletionTimer.schedule(Math.max(0, Number(delayMs) || 0));
    }

    function markUpgradeUiAwaitingReconnect() {
      const current = readUpgradeUiSession();
      if (!current || !current.awaitingReconnect) return null;
      return updateUpgradeUiSession({
        phase: 'reconnect',
        detail: tr('updates.detail.awaitReconnect', 'Attente de Reconnection.'),
        reconnectShown: true,
        reconnectProgress: Math.max(5, Math.min(95, Number(current.reconnectProgress) || 0))
      });
    }

    function markUpgradeUiCompletedAfterReconnect() {
      const current = readUpgradeUiSession();
      if (!current || !current.awaitingReconnect) return null;
      stopUpgradeReconnectFlow();
      return updateUpgradeUiSession({
        phase: 'done',
        detail: tr('updates.detail.done', 'Mise à jour terminée.'),
        backendProgress: 100,
        awaitingReconnect: false,
        reconnectShown: true,
        reconnectProgress: 100,
        failedStep: ''
      });
    }

    function handleUpgradeReconnectSuccess() {
      const current = readUpgradeUiSession();
      if (!current || !current.awaitingReconnect) return null;
      if (!current.reconnectShown || current.phase === 'reboot') {
        upgradeReconnectStageTimer.stop();
        upgradeReconnectMonitor.stop();
        markUpgradeUiAwaitingReconnect();
        scheduleUpgradeReconnectCompletion(320);
        return readUpgradeUiSession();
      }
      return markUpgradeUiCompletedAfterReconnect();
    }

    function incrementUpgradeReconnectProgress() {
      const current = readUpgradeUiSession();
      if (!current || !current.awaitingReconnect) return null;
      const nextProgress = Math.max(5, Math.min(95, (Number(current.reconnectProgress) || 0) + 12));
      return updateUpgradeUiSession({
        phase: 'reconnect',
        detail: tr('updates.detail.awaitReconnect', 'Attente de Reconnection.'),
        reconnectShown: true,
        reconnectProgress: nextProgress
      });
    }

    function enterUpgradeReconnectPhase() {
      const current = readUpgradeUiSession();
      if (!current || !current.awaitingReconnect) return null;
      markUpgradeUiAwaitingReconnect();
      startUpgradeReconnectMonitor();
      return readUpgradeUiSession();
    }

    async function fetchUpgradeReconnectHeartbeat() {
      const supportsAbort = typeof AbortController === 'function';
      const controller = supportsAbort ? new AbortController() : null;
      const timeoutId = controller
        ? setTimeout(() => {
            try {
              controller.abort();
            } catch (err) {
            }
          }, upgradeReconnectFetchTimeoutMs)
        : null;
      try {
        return await fetchOkJson('/api/web/meta', {
          cache: 'no-store',
          signal: controller ? controller.signal : undefined
        }, 'meta web indisponible');
      } finally {
        if (timeoutId) clearTimeout(timeoutId);
      }
    }

    async function probeUpgradeReconnect() {
      const current = readUpgradeUiSession();
      if (!current || !current.awaitingReconnect) {
        stopUpgradeReconnectFlow();
        return;
      }
      try {
        await fetchUpgradeReconnectHeartbeat();
        handleUpgradeReconnectSuccess();
      } catch (err) {
        incrementUpgradeReconnectProgress();
      }
    }

    function resumeUpgradeReconnectFlow() {
      const current = readUpgradeUiSession();
      if (!current || !current.awaitingReconnect) return;
      if (current.reconnectShown || current.phase === 'reconnect') {
        startUpgradeReconnectMonitor();
        return;
      }
      scheduleUpgradeReconnectPhase(700);
    }

    function updateUpgradeView(data) {
      if (!data || data.ok !== true) return;
      const current = readUpgradeUiSession();
      const state = String(data.state || 'idle');
      const target = String(data.target || (current && current.target) || '').trim().toLowerCase();
      const progress = Math.max(0, Math.min(100, Number(data.progress) || 0));
      const msg = String(data.msg || '').trim();

      if (upgradeUiStatusMuted) {
        if (state !== 'idle' && state !== 'done' && state !== 'error') return;
        upgradeUiStatusMuted = false;
      }

      if (state === 'idle') {
        if (current && current.awaitingReconnect) {
          handleUpgradeReconnectSuccess();
        } else if (!current || current.phase === 'idle') {
          clearUpgradeUiSession();
          renderUpgradeJourney({ phase: 'idle', target: '', detail: tr('updates.none', 'Aucune opération en cours.') });
        }
        return;
      }

      if (state === 'queued') {
        stopUpgradeReconnectFlow();
        updateUpgradeUiSession({
          phase: 'target',
          target: target,
          detail: tr('updates.detail.targetSelected', 'Sélection de la cible {target}.')
            .replace('{target}', upgradeTargetLabel(target)),
          backendProgress: progress,
          awaitingReconnect: false,
          reconnectShown: false,
          reconnectProgress: 0,
          failedStep: ''
        });
        return;
      }

      if (state === 'downloading') {
        stopUpgradeReconnectFlow();
        updateUpgradeUiSession({
          phase: 'download',
          target: target,
          detail: 'Connexion au serveur.',
          backendProgress: progress,
          awaitingReconnect: false,
          reconnectShown: false,
          reconnectProgress: 0,
          failedStep: ''
        });
        return;
      }

      if (state === 'flashing') {
        stopUpgradeReconnectFlow();
        updateUpgradeUiSession({
          phase: 'flash',
          target: target,
          detail: 'Mise à jour en cours.',
          backendProgress: progress,
          awaitingReconnect: false,
          reconnectShown: false,
          reconnectProgress: 0,
          failedStep: ''
        });
        return;
      }

      if (state === 'rebooting') {
        stopUpgradeReconnectFlow();
        updateUpgradeUiSession({
          phase: 'reboot',
          target: target,
          detail: 'Redémarrage.',
          backendProgress: 100,
          awaitingReconnect: true,
          reconnectShown: false,
          reconnectProgress: 0,
          failedStep: ''
        });
        scheduleUpgradeReconnectPhase(900);
        return;
      }

      if (state === 'done') {
        if (upgradeUsesReconnect(target)) {
          stopUpgradeReconnectFlow();
          updateUpgradeUiSession({
            phase: 'reboot',
            target: target,
            detail: tr('updates.phase.reboot', 'Redémarrage') + '.',
            backendProgress: 100,
            awaitingReconnect: true,
            reconnectShown: false,
            reconnectProgress: 0,
            failedStep: ''
          });
          scheduleUpgradeReconnectPhase(900);
        } else {
          stopUpgradeReconnectFlow();
          updateUpgradeUiSession({
            phase: 'done',
            target: target,
            detail: tr('updates.detail.done', 'Mise à jour terminée.'),
            backendProgress: 100,
            awaitingReconnect: false,
            reconnectShown: true,
            reconnectProgress: 100,
            failedStep: ''
          });
        }
        return;
      }

      if (state === 'error') {
        stopUpgradeReconnectFlow();
        updateUpgradeUiSession({
          phase: 'error',
          target: target,
          detail: normalizeUpgradeHttpErrorMessage(msg, tr('updates.err.updateGeneric', 'Erreur de mise à jour.')),
          backendProgress: progress,
          awaitingReconnect: false,
          reconnectShown: false,
          reconnectProgress: 0,
          failedStep: current && current.phase && current.phase !== 'idle' ? current.phase : 'flash'
        });
      }
    }

    function normalizeFirmwareVersionForCompare(value) {
      return String(value || '').trim().split('+')[0].replace(/^v/i, '');
    }

    function compareFirmwareVersions(a, b) {
      const left = normalizeFirmwareVersionForCompare(a).split(/[.-]/).map((part) => Number.parseInt(part, 10));
      const right = normalizeFirmwareVersionForCompare(b).split(/[.-]/).map((part) => Number.parseInt(part, 10));
      const len = Math.max(left.length, right.length);
      for (let i = 0; i < len; ++i) {
        const av = Number.isFinite(left[i]) ? left[i] : 0;
        const bv = Number.isFinite(right[i]) ? right[i] : 0;
        if (av > bv) return 1;
        if (av < bv) return -1;
      }
      return 0;
    }

    function manifestArtifactList(manifest, key) {
      if (!manifest || typeof manifest !== 'object') return [];
      const artifacts = (manifest.artifacts && typeof manifest.artifacts === 'object') ? manifest.artifacts : manifest;
      const artifact = artifacts[key];
      if (Array.isArray(artifact)) {
        return artifact.filter((entry) => entry && typeof entry === 'object');
      }
      if (artifact && typeof artifact === 'object' && Array.isArray(artifact.versions)) {
        return artifact.versions
          .filter((entry) => entry && typeof entry === 'object')
          .map((entry) => Object.assign({}, artifact, entry, { versions: undefined }));
      }
      if (artifact && typeof artifact === 'object' && (artifact.version || artifact.path || artifact.url)) {
        return [artifact];
      }
      if (artifact && typeof artifact === 'object') {
        return Object.keys(artifact)
          .map((version) => {
            const entry = artifact[version];
            return entry && typeof entry === 'object' ? Object.assign({ version: version }, entry) : null;
          })
          .filter(Boolean);
      }
      return [];
    }

    function manifestBaseUrl(manifestUrl) {
      const url = String(manifestUrl || '').trim();
      const idx = url.lastIndexOf('/');
      return idx >= 0 ? url.slice(0, idx + 1) : '';
    }

    function joinManifestArtifactUrl(baseUrl, artifact) {
      if (!artifact || typeof artifact !== 'object') return '';
      const raw = String(artifact.url || artifact.path || '').trim();
      if (!raw) return '';
      if (/^https?:\/\//i.test(raw)) return raw;
      return String(baseUrl || '') + raw.replace(/^\/+/, '');
    }

    function formatManifestBuildDate(artifact) {
      if (!artifact || typeof artifact !== 'object') return '';
      return String(artifact.build_date || artifact.built_at || artifact.date || '').trim();
    }

    function endpointForUpgradeTarget(target) {
      const key = String(target || '').trim().toLowerCase();
      if (key === 'flowios3' || key === 'esp32s3') return '/fwupdate/waveshare';
      if (key === 'waveshare') return '/fwupdate/waveshare';
      if (key === 'spiffs') return '/fwupdate/spiffs';
      if (key === 'nextion') return '/fwupdate/nextion';
      return '';
    }

    function manifestTargetDef(key) {
      return upgradeTargetDefs[String(key || '').trim().toLowerCase()] || null;
    }

    function manifestCategoryVisibleForProfile(category) {
      const key = String(category || '').trim().toLowerCase();
      if (!key) return false;
      if (isMicronovaProfile() || isSupervisorProfile()) {
        return key === 'flowios3' || key === 'esp32s3' || key === 'waveshare'
          || key === 'spiffs' || key === 'flowios3-spiffs' || key === 'esp32s3-spiffs' || key === 'waveshare-spiffs'
          || key === 'nextion';
      }
      if (isWaveshareProfile()) {
        return key === 'flowios3' || key === 'esp32s3' || key === 'waveshare'
          || key === 'spiffs' || key === 'flowios3-spiffs' || key === 'esp32s3-spiffs' || key === 'waveshare-spiffs'
          || key === 'nextion';
      }
      if (isFlowIOProfile()) {
        return key === 'flowio';
      }
      return true;
    }

    function resolveArtifactTarget(category, artifact) {
      const explicit = String(artifact && (artifact.target || artifact.update_target) ? (artifact.target || artifact.update_target) : '').trim().toLowerCase();
      if (explicit) return explicit;
      const def = manifestTargetDef(category);
      return def && def.target ? def.target : String(category || '').trim().toLowerCase();
    }

    function resolveArtifactEndpoint(category, artifact, target) {
      const categoryKey = String(category || '').trim().toLowerCase();
      if (categoryKey === 'flowios3' || categoryKey === 'esp32s3' || categoryKey === 'waveshare') {
        return '/fwupdate/waveshare';
      }
      const explicit = String(artifact && (artifact.route || artifact.endpoint || artifact.update_route) ? (artifact.route || artifact.endpoint || artifact.update_route) : '').trim();
      if (explicit) {
        if (/^\/fwupdate\//.test(explicit)) return explicit;
        if (/^fwupdate\//.test(explicit)) return '/' + explicit;
        return endpointForUpgradeTarget(explicit);
      }
      const def = manifestTargetDef(category);
      return def && def.endpoint ? def.endpoint : endpointForUpgradeTarget(target);
    }

    function formatManifestArtifactTitle(category, artifact) {
      const def = manifestTargetDef(category);
      const title = String(artifact && (artifact.title || artifact.name) ? (artifact.title || artifact.name) : '').trim();
      if (title) return title;
      const label = String(artifact && artifact.label ? artifact.label : '').trim();
      if (label) return label;
      return def && def.label ? def.label : String(category || 'Firmware');
    }

    function manifestArtifactEntries(manifest, manifestUrl) {
      if (!manifest || typeof manifest !== 'object') return [];
      const baseUrl = manifestBaseUrl(manifestUrl);
      const artifacts = (manifest.artifacts && typeof manifest.artifacts === 'object') ? manifest.artifacts : manifest;
      return Object.keys(artifacts)
        .reduce((entries, category) => {
          if (!manifestCategoryVisibleForProfile(category)) return entries;
          const def = manifestTargetDef(category);
          const orderBase = def && Number.isFinite(def.order) ? def.order : 1000;
          manifestArtifactList(manifest, category)
            .filter((artifact) => joinManifestArtifactUrl(baseUrl, artifact))
            .sort((a, b) => compareFirmwareVersions(String(b.version || ''), String(a.version || '')))
            .forEach((artifact, index) => {
              const target = resolveArtifactTarget(category, artifact);
              entries.push({
                category: category,
                artifact: artifact,
                title: formatManifestArtifactTitle(category, artifact),
                version: String(artifact.version || '').trim() || 'version inconnue',
                buildDate: formatManifestBuildDate(artifact) || '-',
                notes: String(artifact.notes || artifact.release_notes || '').trim() || 'Notes de version indisponibles.',
                url: joinManifestArtifactUrl(baseUrl, artifact),
                target: target,
                endpoint: resolveArtifactEndpoint(category, artifact, target),
                order: orderBase + index / 100
              });
            });
          return entries;
        }, [])
        .sort((a, b) => {
          if (a.order !== b.order) return a.order - b.order;
          return compareFirmwareVersions(String(b.version || ''), String(a.version || ''));
        });
    }

    function setUpgradeCardsEmpty(text) {
      renderUpgradeCatalog();
      if (text) setUpgradeMessage(text);
    }

    function setUpgradeCardsError(detail) {
      renderUpgradeCatalog({ error: detail || tr('updates.err.checkGeneric', 'Échec de la vérification.') });
    }

    function splitUpgradeVersionStamp(rawVersion, fallbackBuildDate) {
      const raw = String(rawVersion || '').trim();
      const plusIndex = raw.indexOf('+');
      const main = (plusIndex >= 0 ? raw.slice(0, plusIndex) : raw).trim();
      const plusBuild = plusIndex >= 0 ? raw.slice(plusIndex + 1).trim() : '';
      return {
        version: main || '-',
        build: formatUpgradeBuildStamp(plusBuild || fallbackBuildDate || '')
      };
    }

    function formatUpgradeBuildStamp(rawValue) {
      const raw = String(rawValue || '').trim();
      if (!raw || raw === '-') return '-';
      const compact = raw.match(/^(\d{4})(\d{2})(\d{2})[._-]?(\d{2})(\d{2})(\d{2})$/);
      if (compact) {
        return compact[3] + '/' + compact[2] + '/' + compact[1] + ' ' + compact[4] + ':' + compact[5] + ':' + compact[6];
      }
      const parsed = Date.parse(raw);
      if (Number.isFinite(parsed)) {
        const d = new Date(parsed);
        return d.toLocaleDateString(currentWebLocaleTag()) + ' ' + d.toLocaleTimeString(currentWebLocaleTag(), {
          hour: '2-digit',
          minute: '2-digit',
          second: '2-digit'
        });
      }
      return raw;
    }

    function upgradeBuildStampValue(rawValue) {
      const raw = String(rawValue || '').trim();
      if (!raw || raw === '-') return 0;
      const compact = raw.match(/^(\d{4})(\d{2})(\d{2})[._-]?(\d{2})(\d{2})(\d{2})$/);
      if (compact) {
        return Number(compact[1] + compact[2] + compact[3] + compact[4] + compact[5] + compact[6]);
      }
      const parsed = Date.parse(raw);
      return Number.isFinite(parsed) ? parsed : 0;
    }

    function compareUpgradeArtifacts(a, b) {
      const versionCompare = compareFirmwareVersions(String(a && a.version ? a.version : ''), String(b && b.version ? b.version : ''));
      if (versionCompare !== 0) return versionCompare;
      const aStamp = splitUpgradeVersionStamp(a && a.version, formatManifestBuildDate(a)).build;
      const bStamp = splitUpgradeVersionStamp(b && b.version, formatManifestBuildDate(b)).build;
      const dateCompare = upgradeBuildStampValue(aStamp) - upgradeBuildStampValue(bStamp);
      if (dateCompare > 0) return 1;
      if (dateCompare < 0) return -1;
      return 0;
    }

    function upgradeManifestKeysForComponent(componentKey) {
      const key = String(componentKey || '').trim().toLowerCase();
      if (key === 'flowio') {
        return ['flowios3', 'waveshare', 'esp32s3'];
      }
      if (key === 'spiffs') {
        return ['spiffs', 'flowios3-spiffs', 'esp32s3-spiffs', 'waveshare-spiffs'];
      }
      return [key];
    }

    function latestUpgradeEntryForComponent(componentKey, manifest, manifestUrl) {
      if (!manifest || typeof manifest !== 'object') return null;
      const baseUrl = manifestBaseUrl(manifestUrl);
      const entries = [];
      upgradeManifestKeysForComponent(componentKey).forEach((category) => {
        manifestArtifactList(manifest, category)
          .filter((artifact) => joinManifestArtifactUrl(baseUrl, artifact))
          .forEach((artifact) => {
            const target = resolveArtifactTarget(category, artifact);
            const split = splitUpgradeVersionStamp(artifact.version, formatManifestBuildDate(artifact));
            entries.push({
              category: category,
              artifact: artifact,
              title: formatManifestArtifactTitle(category, artifact),
              version: split.version,
              buildDate: split.build,
              notes: String(artifact.notes || artifact.release_notes || '').trim(),
              url: joinManifestArtifactUrl(baseUrl, artifact),
              target: target,
              endpoint: resolveArtifactEndpoint(category, artifact, target)
            });
          });
      });
      if (!entries.length) return null;
      entries.sort((a, b) => compareUpgradeArtifacts(b.artifact, a.artifact));
      return entries[0];
    }

    function formatDetectedNextionVersion(rawValue) {
      const raw = String(rawValue || '').trim();
      if (!raw || raw === '0') return '-';
      if (raw.indexOf('.') >= 0) return raw;
      const n = Number.parseInt(raw, 10);
      if (!Number.isFinite(n) || n <= 0) return raw;
      if (n >= 100) {
        return Math.floor(n / 100) + '.' + (Math.floor(n / 10) % 10) + '.' + (n % 10);
      }
      return raw;
    }

    function currentUpgradeVersionForComponent(componentKey) {
      const key = String(componentKey || '').trim().toLowerCase();
      if (key === 'nextion') {
        return splitUpgradeVersionStamp(formatDetectedNextionVersion(nextionDisplayVersion), '');
      }
      const supervisor = String(supervisorFirmwareVersion || '').trim();
      const flow = String(window.__flowIoFirmwareVersion || '').trim();
      const firmware = supervisor && supervisor !== '-' ? supervisor : flow;
      return splitUpgradeVersionStamp(firmware && firmware !== '-' ? firmware : '-', '');
    }

    function buildUpgradeComponentRows() {
      const manifest = upgradeManifestState && upgradeManifestState.manifest;
      const manifestUrl = upgradeManifestState && upgradeManifestState.manifestUrl;
      return upgradeComponentDefs.map((def) => {
        const current = currentUpgradeVersionForComponent(def.key);
        const latest = latestUpgradeEntryForComponent(def.key, manifest, manifestUrl);
        const available = latest
          ? { version: latest.version, build: latest.buildDate }
          : { version: '-', build: '-' };
        const nextionUnavailable = def.key === 'nextion'
          && (!nextionDisplayVersionDetected || !nextionDisplayVersionCompatible);
        const comparableCurrent = current.version && current.version !== '-';
        const comparableAvailable = available.version && available.version !== '-';
        const updateAvailable = !nextionUnavailable
          && comparableAvailable
          && (!comparableCurrent || compareFirmwareVersions(available.version, current.version) > 0);
        const unavailableMessage = nextionDisplayVersionDetected
          ? tr('updates.nextion.incompatible', 'Version Nextion incompatible')
          : tr('updates.nextion.notDetected', 'Nextion non détecté');
        const expectedVersion = formatDetectedNextionVersion(nextionDisplayExpectedVersion);
        const unavailableComment = nextionDisplayVersionDetected && expectedVersion !== '-'
          ? tr('updates.nextion.incompatibleDetail', 'Version attendue : {version}.')
            .replace('{version}', expectedVersion)
          : tr('updates.nextion.notDetectedDetail', 'Vérifiez la connexion de l’écran puis redémarrez le système.');
        return Object.assign({}, def, {
          current: current,
          available: available,
          updateAvailable: updateAvailable,
          unavailable: nextionUnavailable,
          unavailableMessage: nextionUnavailable ? unavailableMessage : '',
          entry: latest,
          comments: nextionUnavailable
            ? unavailableComment
            : (latest && latest.notes
              ? latest.notes
              : (updateAvailable ? def.commentsAvailable : def.commentsCurrent))
        });
      });
    }

    function appendUpgradeVersionCell(parent, versionInfo) {
      const wrap = document.createElement('div');
      wrap.className = 'update-version-stack';
      const version = document.createElement('strong');
      version.textContent = (versionInfo && versionInfo.version) || '-';
      const build = document.createElement('span');
      build.textContent = (versionInfo && versionInfo.build) || '-';
      wrap.appendChild(version);
      wrap.appendChild(build);
      parent.appendChild(wrap);
    }

    function createUpgradeComponentBadge(row, sizeClass) {
      const badge = document.createElement('span');
      badge.className = 'update-component-badge update-component-' + row.tone + (sizeClass ? ' ' + sizeClass : '');
      const icon = document.createElement('span');
      icon.className = 'ui-msr';
      icon.setAttribute('aria-hidden', 'true');
      icon.textContent = row.icon;
      badge.appendChild(icon);
      return badge;
    }

    function createUpgradeStatusBadge(row) {
      const badge = document.createElement('span');
      badge.className = 'update-status-badge ' + (row.unavailable
        ? 'is-unavailable'
        : (row.updateAvailable ? 'is-available' : 'is-current'));
      badge.textContent = row.unavailable
        ? row.unavailableMessage
        : (row.updateAvailable
          ? tr('updates.status.available', 'Mise à jour disponible')
          : tr('updates.status.current', 'À jour'));
      return badge;
    }

    function createUpgradeActionButton(row) {
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'update-action-btn';
      const icon = document.createElement('span');
      icon.className = 'ui-msr';
      icon.setAttribute('aria-hidden', 'true');
      icon.textContent = 'system_update_alt';
      const label = document.createElement('span');
      label.textContent = tr('updates.updateButton', 'Mettre à jour');
      button.appendChild(icon);
      button.appendChild(label);
      const entry = row && row.entry;
      button.disabled = !(entry && entry.endpoint && entry.url);
      button.title = button.disabled
        ? tr('updates.checkRequired', 'Vérifiez les mises à jour avant de lancer cette action.')
        : tr('updates.updateButton', 'Mettre à jour');
      bindClickAction(button, () => {
        if (!entry) return;
        if (!confirmUpgradeLaunch(entry)) return;
        return startUpgrade(entry.target, entry.url, entry.endpoint);
      });
      return button;
    }

    function renderUpgradeSummaryCards(rows) {
      if (!upgradeCards) return;
      upgradeCards.innerHTML = '';
      upgradeCards.classList.remove('has-error');
      rows.forEach((row) => {
        const card = document.createElement('article');
        card.className = 'update-summary-card update-summary-' + row.tone + (row.unavailable ? ' is-disabled' : '');
        if (row.unavailable) card.setAttribute('aria-disabled', 'true');
        card.appendChild(createUpgradeComponentBadge(row, 'update-component-badge-lg'));

        const body = document.createElement('div');
        body.className = 'update-summary-body';
        const title = document.createElement('h3');
        title.textContent = row.title + ' ';
        const subtitle = document.createElement('span');
        subtitle.textContent = '(' + row.subtitle + ')';
        title.appendChild(subtitle);
        body.appendChild(title);

        const currentLine = document.createElement('div');
        currentLine.className = 'update-summary-line';
        currentLine.appendChild(document.createTextNode(tr('updates.currentVersion', 'Version actuelle')));
        const currentPill = document.createElement('b');
        currentPill.textContent = row.current.version || '-';
        currentLine.appendChild(currentPill);
        body.appendChild(currentLine);

        const availableLine = document.createElement('div');
        availableLine.className = 'update-summary-line';
        availableLine.appendChild(document.createTextNode(tr('updates.availableVersion', 'Version disponible')));
        const availablePill = document.createElement('b');
        availablePill.className = row.updateAvailable ? 'is-green' : '';
        availablePill.textContent = row.available.version || '-';
        availableLine.appendChild(availablePill);
        body.appendChild(availableLine);
        card.appendChild(body);

        const stateIcon = document.createElement('span');
        stateIcon.className = 'ui-msr update-summary-state';
        stateIcon.setAttribute('aria-hidden', 'true');
        stateIcon.textContent = row.unavailable ? 'link_off' : (row.updateAvailable ? 'arrow_upward' : 'horizontal_rule');
        card.appendChild(stateIcon);

        const foot = document.createElement('div');
        foot.className = 'update-summary-foot';
        const dot = document.createElement('span');
        dot.className = 'update-dot ' + (row.unavailable ? 'is-gray' : (row.updateAvailable ? 'is-green' : 'is-blue'));
        foot.appendChild(dot);
        foot.appendChild(document.createTextNode(row.unavailable
          ? row.unavailableMessage
          : (row.updateAvailable
            ? tr('updates.status.available', 'Mise à jour disponible')
            : tr('updates.status.current', 'À jour'))));
        card.appendChild(foot);

        upgradeCards.appendChild(card);
      });
    }

    function renderUpgradeTable(rows) {
      if (!upgradeTableBody) return;
      upgradeTableBody.innerHTML = '';
      rows.forEach((row) => {
        const trEl = document.createElement('tr');
        if (row.unavailable) {
          trEl.className = 'is-disabled';
        }

        const componentCell = document.createElement('td');
        const component = document.createElement('div');
        component.className = 'update-component-cell';
        component.appendChild(createUpgradeComponentBadge(row, ''));
        const copy = document.createElement('div');
        const title = document.createElement('strong');
        title.textContent = row.title;
        const sub = document.createElement('span');
        sub.textContent = '(' + row.subtitle + ')';
        copy.appendChild(title);
        copy.appendChild(sub);
        component.appendChild(copy);
        componentCell.appendChild(component);
        trEl.appendChild(componentCell);

        const currentCell = document.createElement('td');
        appendUpgradeVersionCell(currentCell, row.current);
        trEl.appendChild(currentCell);

        const availableCell = document.createElement('td');
        appendUpgradeVersionCell(availableCell, row.available);
        trEl.appendChild(availableCell);

        const statusCell = document.createElement('td');
        statusCell.appendChild(createUpgradeStatusBadge(row));
        trEl.appendChild(statusCell);

        const commentsCell = document.createElement('td');
        commentsCell.textContent = row.comments || '-';
        trEl.appendChild(commentsCell);

        const actionCell = document.createElement('td');
        actionCell.appendChild(createUpgradeActionButton(row));
        trEl.appendChild(actionCell);

        upgradeTableBody.appendChild(trEl);
      });
    }

    function renderUpgradeCatalog(options) {
      const rows = buildUpgradeComponentRows();
      renderUpgradeSummaryCards(rows);
      renderUpgradeTable(rows);
      if (options && options.error) {
        setUpgradeMessage(tr('updates.err.checkGeneric', 'Échec de la vérification.') + ' : ' + options.error);
      }
    }

    function resetUpgradeManifestSelections(text) {
      upgradeManifestState = { manifest: null, manifestUrl: '', baseUrl: '' };
      setUpgradeCardsEmpty(text || tr('updates.empty', 'Cliquez sur « Vérifier les mises à jour ».'));
    }

    function confirmUpgradeLaunch(entry) {
      const version = String(entry && entry.version ? entry.version : 'x.x.x').trim() || 'x.x.x';
      const target = upgradeTargetLabel(entry && entry.target ? entry.target : '');
      return confirm(
        tr('updates.confirmLaunch', 'Confirmer la mise à jour de {target} vers la version {version} ?')
          .replace('{target}', target)
          .replace('{version}', version)
      );
    }

    function confirmRebootLaunch(selectedAction) {
      const action = String(selectedAction || 'supervisor');
      const messages = {
        supervisor: isMicronovaProfile()
          ? tr('updates.confirmRebootMicronova', 'Confirmer le redémarrage de Micronova ?')
          : tr('updates.confirmRebootSupervisor', 'Confirmer le redémarrage du Supervisor ?'),
        flow_soft: tr('updates.confirmRebootFlowSoft', 'Confirmer le redémarrage logiciel de flow.io ?'),
        flow_hard: tr('updates.confirmRebootFlowHard', 'Confirmer le redémarrage matériel de flow.io ?'),
        nextion: tr('updates.confirmRebootNextion', 'Confirmer le redémarrage de Nextion ?'),
        factory_reset: tr('updates.confirmFactoryReset', 'Confirmer l\'initialisation usine de flow.io ? Cette action efface la configuration distante.')
      };
      return confirm(messages[action] || messages.supervisor);
    }

    function populateUpgradeManifestSelections(data) {
      const manifest = data && data.manifest && typeof data.manifest === 'object' ? data.manifest : null;
      const manifestUrl = String(data && data.manifest_url ? data.manifest_url : '').trim();
      upgradeManifestState = { manifest: manifest, manifestUrl: manifestUrl, baseUrl: manifestBaseUrl(manifestUrl) };
      renderUpgradeCatalog();
    }

    function describeManifestUpdates(data) {
      const manifest = data && data.manifest && typeof data.manifest === 'object' ? data.manifest : null;
      if (!manifest) return 'Manifest indisponible.';
      const rows = buildUpgradeComponentRows();
      const available = rows
        .filter((row) => row.updateAvailable)
        .map((row) => row.title + ' ' + row.current.version + ' -> ' + row.available.version);
      const listed = rows
        .filter((row) => row.available.version && row.available.version !== '-')
        .map((row) => row.title + ' ' + row.available.version);
      if (available.length > 0) {
        return 'Mise(s) à jour disponible(s) : ' + available.join(', ') + '.';
      }
      if (listed.length > 0) {
        return 'Manifest vérifié. Versions disponibles : ' + listed.join(', ') + '.';
      }
      return 'Manifest vérifié, aucun firmware listé.';
    }

    async function checkFirmwareUpdates() {
      if (checkUpdatesBtn) {
        checkUpdatesBtn.disabled = true;
        checkUpdatesBtn.classList.add('is-pending');
      }
      try {
        setUpgradeCardsEmpty(tr('updates.checking', 'Vérification du manifest...'));
        setUpgradeMessage(tr('updates.checking', 'Vérification du manifest...'));
        const started = await fetchOkJson(
          '/api/fwupdate/check',
          { method: 'POST', cache: 'no-store' },
          'échec lancement vérification'
        );
        const requestId = Number(started && started.request_id);
        if (!Number.isFinite(requestId) || requestId <= 0) {
          throw new Error('identifiant de vérification invalide');
        }

        const deadlineMs = Date.now() + 85000;
        let data = null;
        while (Date.now() < deadlineMs) {
          data = await fetchOkJson(
            '/api/fwupdate/check?request_id=' + encodeURIComponent(String(requestId)),
            { cache: 'no-store' },
            'échec vérification'
          );
          if (data && data.state === 'ready') break;
          if (!data || (data.state !== 'queued' && data.state !== 'downloading')) {
            throw new Error('état de vérification inattendu');
          }
          await waitMs(400);
        }
        if (!data || data.state !== 'ready') {
          throw new Error('délai de vérification du manifest dépassé');
        }
        populateUpgradeManifestSelections(data);
        setUpgradeMessage(describeManifestUpdates(data));
      } catch (err) {
        const errMsg = normalizeUpgradeHttpErrorMessage(String(err || ''), tr('updates.err.checkGeneric', 'Échec de la vérification.'));
        setUpgradeCardsError(errMsg);
        setUpgradeMessage(tr('updates.err.checkGeneric', 'Échec de la vérification.') + ' : ' + errMsg);
      } finally {
        if (checkUpdatesBtn) {
          checkUpdatesBtn.disabled = false;
          checkUpdatesBtn.classList.remove('is-pending');
        }
      }
    }

    async function refreshUpgradeStatus() {
      try {
        updateUpgradeView(await fetchOkJson('/api/fwupdate/status', { cache: 'no-store' }, 'échec lecture état'));
      } catch (err) {
        const current = readUpgradeUiSession();
        if (current && (current.awaitingReconnect || current.phase === 'reboot')) {
          enterUpgradeReconnectPhase();
          return;
        }
        setUpgradeMessage('Échec de lecture de l\'état : ' + err);
      }
    }

    async function startUpgrade(target, url, endpoint) {
      try {
        startUpgradeUiSession(target);
        startUpgradeStatusPolling(true);
        const selectedUrl = String(url || '').trim();
        if (!selectedUrl) {
          throw new Error('aucune image sélectionnée, lancez Vérifier');
        }
        const route = String(endpoint || endpointForUpgradeTarget(target)).trim();
        if (!route) {
          throw new Error('route de mise à jour indisponible');
        }
        await fetchOkJson(route, createFormPostOptions({ url: selectedUrl }), 'échec démarrage');
        await refreshUpgradeStatus();
      } catch (err) {
        stopUpgradeReconnectFlow();
        updateUpgradeUiSession({
          phase: 'error',
          target: target,
          detail: 'Échec de la mise à jour : ' + err,
          backendProgress: 0,
          awaitingReconnect: false,
          reconnectShown: false,
          reconnectProgress: 0,
          failedStep: 'target'
        });
        setUpgradeMessage('Échec de la mise à jour : ' + err);
      }
    }

    async function onWifiPageShown() {
      if (!wifiConfigLoadedOnce) {
        wifiConfigLoadedOnce = true;
        await loadWifiConfig();
      }
      if (!wifiScanAutoRequested) {
        wifiScanAutoRequested = true;
        await refreshWifiScanStatus(true);
      } else {
        await refreshWifiScanStatus(false);
      }
    }

    async function onControlPageShown() {
      const shouldShowInitialTreeSkeleton = !flowCfgLoadedOnce;
      if (shouldShowInitialTreeSkeleton) {
        beginFlowCfgLoading('Chargement de la configuration distante...', { tree: true, detail: false });
      }
      try {
        await ensureFlowCfgLoaded(false);
        await refreshCfgDocLocaleRuntime(true);
      } finally {
        if (shouldShowInitialTreeSkeleton) {
          endFlowCfgLoading({ tree: true, detail: false });
        }
      }
    }

    async function onCalibrationPageShown() {
      if (!calibrationLoadedOnce) {
        calibrationLoadedOnce = true;
        await loadCalibrationSensorConfig(true);
        return;
      }
      if (!calibrationContext || !calibrationContext.ioModule) {
        await loadCalibrationSensorConfig(false);
      }
    }

    function fmtFlowStatusVal(v) {
      if (v === null || typeof v === 'undefined') return '-';
      if (typeof v === 'string') {
        const trimmed = v.trim();
        if (!trimmed || /^__FLOW_[A-Z0-9_]+__$/.test(trimmed)) return '-';
        return trimmed;
      }
      return String(v);
    }

    function flowTimeSourceLabel(src) {
      const key = String(src || '').trim().toLowerCase();
      if (key === 'ntp') return 'NTP';
      if (key === 'internal_rtc') return 'RTC';
      if (key === 'manual') return 'manuel';
      return '';
    }

    function flowTimeQualityLabel(quality) {
      const key = String(quality || '').trim().toLowerCase();
      if (key === 'ntp_synced') return 'NTP';
      if (key === 'rtc_trusted') return currentWebLocaleTag().toLowerCase().startsWith('en') ? 'RTC' : 'RTC';
      if (key === 'manual') return currentWebLocaleTag().toLowerCase().startsWith('en') ? 'manual' : 'manuel';
      if (key === 'rtc_untrusted') return 'RTC?';
      if (key === 'invalid') return '';
      return '';
    }

    function flowTimeIsReady(time) {
      if (!time || typeof time !== 'object') return false;
      return !!time.rdy || !!flowTimeQualityLabel(time.qlt) || !!flowTimeSourceLabel(time.src);
    }

    function flowTimeHeaderLabel(time) {
      if (!time || typeof time !== 'object') return '';
      const quality = flowTimeQualityLabel(time.qlt);
      if (quality) return quality;
      const source = flowTimeSourceLabel(time.src);
      if (source) return source;
      return flowTimeIsReady(time) ? tr('info.state.synced', 'Synchronisée') : tr('info.state.unsynced', 'Non synchronisée');
    }

    function flowTimeStatusLabel(time) {
      if (!time || typeof time !== 'object') return '';
      if (!flowTimeIsReady(time)) return tr('info.state.unsynced', 'Non synchronisée');
      const source = flowTimeQualityLabel(time.qlt) || flowTimeSourceLabel(time.src);
      return source
        ? tr('info.state.synced', 'Synchronisée') + ' (' + source + ')'
        : tr('info.state.synced', 'Synchronisée');
    }

    function fmtFlowCount(v) {
      const n = Number(v);
      return Number.isFinite(n) ? String(Math.max(0, Math.round(n))) : '-';
    }

    function buildFlowStatusBoolIcon(v) {
      const ok = !!v;
      const span = document.createElement('span');
      span.className = ok ? 'status-bool is-true' : 'status-bool is-false';
      span.setAttribute('role', 'img');
      span.setAttribute('aria-label', ok ? 'OK' : 'NOK');
      span.title = ok ? 'OK' : 'NOK';
      span.textContent = ok ? 'OK' : 'KO';
      return span;
    }

    function buildFlowStatusReadonlySwitch(v) {
      const on = !!v;
      const sw = document.createElement('span');
      sw.className = 'md3-switch status-toggle-readonly';
      sw.setAttribute('role', 'img');
      sw.setAttribute('aria-label', on ? 'Actif' : 'Inactif');
      sw.title = on ? 'Actif' : 'Inactif';

      const input = document.createElement('input');
      input.type = 'checkbox';
      input.checked = on;
      input.disabled = true;
      input.tabIndex = -1;
      input.setAttribute('aria-hidden', 'true');

      const track = document.createElement('span');
      track.className = 'md3-track';

      const thumb = document.createElement('span');
      thumb.className = 'md3-thumb';

      sw.appendChild(input);
      sw.appendChild(track);
      sw.appendChild(thumb);
      return sw;
    }

    function buildFlowReadonlyStateTile(label, value, options) {
      const stateKnown = typeof value === 'boolean';
      const opts = options && typeof options === 'object' ? options : {};
      const activeText = typeof opts.activeText === 'string' && opts.activeText.trim()
        ? opts.activeText.trim()
        : 'Actif';
      const inactiveText = typeof opts.inactiveText === 'string' && opts.inactiveText.trim()
        ? opts.inactiveText.trim()
        : 'Inactif';
      const unknownText = typeof opts.unknownText === 'string' && opts.unknownText.trim()
        ? opts.unknownText.trim()
        : 'Indisponible';

      const tile = document.createElement('div');
      tile.className = 'status-state-tile ' + (stateKnown ? (value ? 'is-true' : 'is-false') : 'is-empty');
      tile.setAttribute('role', 'img');
      tile.setAttribute(
        'aria-label',
        label + ' : ' + (stateKnown ? (value ? activeText : inactiveText) : unknownText)
      );

      const title = document.createElement('div');
      title.className = 'status-state-title';
      title.textContent = label;
      tile.appendChild(title);

      const state = document.createElement('div');
      state.className = 'status-state-value';

      const dot = document.createElement('span');
      dot.className = 'status-state-dot';
      state.appendChild(dot);

      const text = document.createElement('span');
      text.textContent = stateKnown ? (value ? activeText : inactiveText) : unknownText;
      state.appendChild(text);

      tile.appendChild(state);
      return tile;
    }

    function buildFlowReadonlyStateGrid(items) {
      return buildNodeGrid('status-state-grid', items);
    }

    function fmtFlowUptime(ms) {
      if (!Number.isFinite(ms) || ms < 0) return '-';
      const sec = Math.floor(ms / 1000);
      if (sec < 60) return sec + (sec > 1 ? ' secondes' : ' seconde');
      const min = Math.floor(sec / 60);
      if (min < 60) return min + (min > 1 ? ' minutes' : ' minute');
      const hours = Math.floor(min / 60);
      if (hours < 24) return hours + (hours > 1 ? ' heures' : ' heure');
      const days = Math.floor(hours / 24);
      if (days < 30) return days + (days > 1 ? ' jours' : ' jour');
      const months = Math.floor(days / 30);
      return months + (months > 1 ? ' mois' : ' mois');
    }

    function fmtFlowRelativeAge(ms) {
      if (!Number.isFinite(ms) || ms < 0) return '-';
      const sec = Math.floor(ms / 1000);
      if (sec < 60) return sec + ' s';
      const min = Math.floor(sec / 60);
      if (min < 60) return min + ' min';
      const hours = Math.floor(min / 60);
      if (hours < 24) return hours + ' h';
      const days = Math.floor(hours / 24);
      if (days < 30) return days + ' j';
      const months = Math.floor(days / 30);
      return months + ' mois';
    }

    function fmtFlowBytes(bytes) {
      const n = Number(bytes);
      if (!Number.isFinite(n) || n < 0) return '-';
      if (n < 1024) return Math.round(n) + ' B';
      if (n < (1024 * 1024)) return Math.round(n / 1024) + ' kB';
      return (n / (1024 * 1024)).toFixed(1) + ' MB';
    }

    function fmtFlowFixed(value, decimals, unit) {
      if (value === null || typeof value === 'undefined') return '-';
      if (typeof value === 'string' && value.trim().length === 0) return '-';
      const n = Number(value);
      if (!Number.isFinite(n)) return '-';
      const rendered = decimals > 0 ? n.toFixed(decimals) : String(Math.round(n));
      return unit ? (rendered + ' ' + unit) : rendered;
    }

    function clampFlowValue(v, min, max) {
      if (v < min) return min;
      if (v > max) return max;
      return v;
    }

    function rssiToPercent(rssi) {
      const n = Number(rssi);
      if (!Number.isFinite(n)) return null;
      const bounded = clampFlowValue(n, -95, -50);
      return Math.round(((bounded + 95) / 45) * 100);
    }

    function describeFlowRssi(percent) {
      if (!Number.isFinite(percent)) return 'Indisponible';
      if (percent >= 75) return 'Tres bon';
      if (percent >= 55) return 'Bon';
      if (percent >= 35) return 'Correct';
      return 'Faible';
    }

    function buildFlowRssiGauge(rssi, hasRssi) {
      const wrapper = document.createElement('div');
      wrapper.className = 'status-gauge' + (hasRssi ? '' : ' is-empty');

      const label = document.createElement('span');
      label.className = 'status-gauge-label';

      const track = document.createElement('span');
      track.className = 'status-gauge-track';
      const fill = document.createElement('span');
      fill.className = 'status-gauge-fill';

      if (hasRssi) {
        const percent = rssiToPercent(rssi);
        fill.style.width = (percent === null ? 0 : percent) + '%';
        label.textContent = describeFlowRssi(percent) + ' (' + fmtFlowStatusVal(rssi) + ' dBm)';
      } else {
        fill.style.width = '0%';
        label.textContent = 'Signal indisponible';
      }

      track.appendChild(fill);
      wrapper.appendChild(track);
      wrapper.appendChild(label);
      return wrapper;
    }

    function fmtFlowGaugeNumber(value, decimals) {
      const n = Number(value);
      if (!Number.isFinite(n)) return '-';
      return decimals > 0 ? n.toFixed(decimals) : String(Math.round(n));
    }

    function createFlowFiveZoneBands(config) {
      const min = Number(config && config.min);
      const max = Number(config && config.max);
      const criticalLowEnd = Number(config && config.criticalLowEnd);
      const warningLowEnd = Number(config && config.warningLowEnd);
      const warningHighStart = Number(config && config.warningHighStart);
      const criticalHighStart = Number(config && config.criticalHighStart);

      if (
        !Number.isFinite(min) ||
        !Number.isFinite(max) ||
        !Number.isFinite(criticalLowEnd) ||
        !Number.isFinite(warningLowEnd) ||
        !Number.isFinite(warningHighStart) ||
        !Number.isFinite(criticalHighStart)
      ) {
        return [];
      }

      if (
        !(min < criticalLowEnd) ||
        !(criticalLowEnd < warningLowEnd) ||
        !(warningLowEnd < warningHighStart) ||
        !(warningHighStart < criticalHighStart) ||
        !(criticalHighStart < max)
      ) {
        return [];
      }

      return [
        { from: min, to: criticalLowEnd, color: config.criticalLowColor || '#D14C66' },
        { from: criticalLowEnd, to: warningLowEnd, color: config.warningLowColor || '#F0B255' },
        { from: warningLowEnd, to: warningHighStart, color: config.okColor || '#2F9E68' },
        { from: warningHighStart, to: criticalHighStart, color: config.warningHighColor || '#F0B255' },
        { from: criticalHighStart, to: max, color: config.criticalHighColor || '#D14C66' }
      ];
    }

    function resolveFlowGaugeValueColor(value, bands) {
      const numericValue = Number(value);
      if (!Number.isFinite(numericValue)) return '#102B4C';
      if (!Array.isArray(bands) || bands.length === 0) return '#102B4C';
      const minBound = Number(bands[0].from);
      const maxBound = Number(bands[bands.length - 1].to);
      const clampedValue = (
        Number.isFinite(minBound) &&
        Number.isFinite(maxBound) &&
        maxBound > minBound
      ) ? clampFlowValue(numericValue, minBound, maxBound) : numericValue;
      for (let i = 0; i < bands.length; ++i) {
        const band = bands[i];
        const from = Number(band.from);
        const to = Number(band.to);
        if (!Number.isFinite(from) || !Number.isFinite(to)) continue;
        if (clampedValue >= from && clampedValue <= to) {
          return band.color || '#102B4C';
        }
      }
      return '#102B4C';
    }

    function buildFlowThresholdValueNode(config) {
      const value = Number(config && config.value);
      const hasValue = Number.isFinite(value);
      const min = Number(config && config.min);
      const max = Number(config && config.max);
      const unit = typeof (config && config.unit) === 'string' ? config.unit.trim() : '';
      const decimals = Math.max(0, Number(config && config.decimals) || 0);
      const bands = Array.isArray(config && config.bands) ? config.bands : [];

      const node = document.createElement('span');
      node.className = 'status-threshold-value' + (hasValue ? '' : ' is-empty');
      if (!hasValue) {
        node.textContent = 'Indisponible';
        return node;
      }
      const text = fmtFlowGaugeNumber(value, decimals);
      node.textContent = unit ? (text + ' ' + unit) : text;

      const activeBands = (Number.isFinite(min) && Number.isFinite(max) && max > min)
        ? bands
            .map((band) => ({
              from: clampFlowValue(Number(band.from), min, max),
              to: clampFlowValue(Number(band.to), min, max),
              color: band.color || '#102B4C'
            }))
            .filter((band) => Number.isFinite(band.from) && Number.isFinite(band.to) && band.to > band.from)
        : [];
      const color = resolveFlowGaugeValueColor(value, activeBands);
      if (color) node.style.color = color;
      return node;
    }

    function appendFlowStatusRow(kv, label, value) {
      const line = document.createElement('div');
      const key = document.createElement('span');
      key.textContent = label;
      const renderedValue = document.createElement('b');
      if (value && typeof value === 'object' && typeof value.nodeType === 'number') {
        renderedValue.appendChild(value);
      } else {
        renderedValue.textContent = fmtFlowStatusVal(value);
      }
      line.appendChild(key);
      line.appendChild(renderedValue);
      kv.appendChild(line);
    }

    function createFlowLiveValue(kind, initialMs, nowMs) {
      const node = document.createElement('span');
      node.dataset.flowLive = kind;
      node.dataset.baseMs = String(Number(initialMs) || 0);
      node.dataset.baseNow = String(Number(nowMs) || Date.now());
      updateFlowLiveValue(node, Date.now());
      return node;
    }

    function updateFlowLiveValue(node, nowMs) {
      if (!node) return;
      const kind = String(node.dataset.flowLive || '');
      if (kind !== 'uptime') return;
      const baseMs = Number(node.dataset.baseMs) || 0;
      const baseNow = Number(node.dataset.baseNow) || nowMs;
      const value = baseMs + Math.max(0, nowMs - baseNow);
      node.textContent = formatRuntimeDurationMs(value);
    }

    function refreshFlowStatusLiveValues() {
      if (!isPageActive('page-status') || !flowStatusGrid) return;
      const nowMs = Date.now();
      flowStatusGrid.querySelectorAll('[data-flow-live]').forEach((node) => updateFlowLiveValue(node, nowMs));
    }

    function ensureFlowStatusLiveTimer() {
      if (flowStatusLiveTimer) return;
      flowStatusLiveTimer = setInterval(() => {
        refreshFlowStatusLiveValues();
      }, 1000);
    }

    function buildFlowStatusCardIcon(iconKey, ok, label) {
      const iconText = {
        wifi: 'wifi',
        ethernet: 'settings_ethernet',
        supervisor: 'SUP',
        system: 'SYS',
        mqtt: 'MQ',
        pool: 'PL',
        pump: 'PP'
      };
      const span = document.createElement('span');
      span.className = ok ? 'status-card-icon is-true' : 'status-card-icon is-false';
      if (iconKey === 'wifi' || iconKey === 'ethernet') {
        span.classList.add('status-card-icon-msr');
      }
      span.setAttribute('role', 'img');
      span.setAttribute('aria-label', label || (ok ? 'OK' : 'NOK'));
      span.title = label || (ok ? 'OK' : 'NOK');
      span.textContent = iconText[iconKey] || iconText.system;
      return span;
    }

    function buildMqttStatsStrip(items) {
      const wrapper = document.createElement('div');
      wrapper.className = 'status-mqtt-strip';
      (items || []).forEach((item) => {
        const numericValue = Number(item ? item.value : null);
        const hasIssue = Number.isFinite(numericValue) && numericValue > 0;
        const cell = document.createElement('div');
        cell.className = 'status-mqtt-metric ' + (hasIssue ? 'is-alert' : 'is-ok');
        if (item && typeof item.title === 'string' && item.title.trim()) {
          cell.title = item.title.trim() + ' : ' + fmtFlowCount(item.value);
          cell.setAttribute('aria-label', cell.title);
        }

        const label = document.createElement('span');
        label.className = 'status-mqtt-metric-label';
        label.textContent = String(item && item.label ? item.label : '').slice(0, 1) || '-';

        const value = document.createElement('strong');
        value.className = 'status-mqtt-metric-value';
        value.textContent = fmtFlowCount(item ? item.value : null);

        cell.appendChild(label);
        cell.appendChild(value);
        wrapper.appendChild(cell);
      });
      return wrapper;
    }

    function normalizeIpValue(ip) {
      if (typeof ip === 'string') {
        const trimmed = ip.trim();
        return trimmed || '-';
      }
      if (Array.isArray(ip)) {
        return ip.map((part) => String(part)).join('.') || '-';
      }
      if (ip && typeof ip === 'object') {
        const keys = Object.keys(ip).sort((a, b) => Number(a) - Number(b));
        if (keys.length > 0) {
          return keys.map((key) => String(ip[key])).join('.') || '-';
        }
      }
      if (typeof ip === 'number' && Number.isFinite(ip)) {
        return String(ip);
      }
      return '-';
    }

    function buildFlowStatusFromDomains(domainData) {
      const merged = { ok: true };
      let anyDomainOk = false;

      const system = domainData.system;
      if (system && system.ok === true) {
        anyDomainOk = true;
        merged.fw = system.fw || '';
        merged.upms = system.upms ?? 0;
        merged.heap = (system.heap && typeof system.heap === 'object') ? system.heap : {};
        if (system.time && typeof system.time === 'object') {
          merged.time = system.time;
        }
      }

      const wifi = domainData.wifi;
      if (wifi && wifi.ok === true && wifi.wifi && typeof wifi.wifi === 'object') {
        anyDomainOk = true;
        merged.wifi = Object.assign({}, wifi.wifi, { ip: normalizeIpValue(wifi.wifi.ip) });
      }

      const mqtt = domainData.mqtt;
      if (mqtt && mqtt.ok === true && mqtt.mqtt && typeof mqtt.mqtt === 'object') {
        anyDomainOk = true;
        merged.mqtt = mqtt.mqtt;
      }

      const pool = domainData.pool;
      if (pool && pool.ok === true && pool.pool && typeof pool.pool === 'object') {
        anyDomainOk = true;
        merged.pool = pool.pool;
      }

      const i2c = domainData.i2c;
      if (i2c && i2c.ok === true && i2c.i2c && typeof i2c.i2c === 'object') {
        anyDomainOk = true;
        merged.i2c = i2c.i2c;
      }

      return anyDomainOk ? merged : null;
    }

    function getCachedFlowStatusData() {
      const domainData = {};
      flowStatusDomainKeys.forEach((domainKey) => {
        domainData[domainKey] = flowStatusDomainCache[domainKey].data;
      });
      return buildFlowStatusFromDomains(domainData);
    }

    function isFlowStatusDomainCacheValid(domainKey, nowMs) {
      const cacheEntry = flowStatusDomainCache[domainKey];
      if (!cacheEntry || !cacheEntry.data) return false;
      const now = Number.isFinite(nowMs) ? nowMs : Date.now();
      return (now - cacheEntry.fetchedAt) < flowStatusDomainTtlMs;
    }

    function cacheFlowStatusFromAggregate(data, fetchedAtMs) {
      if (!data || typeof data !== 'object') return;
      const stamp = Number.isFinite(fetchedAtMs) ? fetchedAtMs : Date.now();

      if (
        (typeof data.fw === 'string' && data.fw.length > 0) ||
        typeof data.upms !== 'undefined' ||
        (data.heap && typeof data.heap === 'object')
      ) {
        flowStatusDomainCache.system.data = {
          ok: true,
          devicename: data.devicename || '',
          fw: data.fw || '',
          upms: data.upms ?? 0,
          heap: (data.heap && typeof data.heap === 'object') ? data.heap : {}
        };
        flowStatusDomainCache.system.fetchedAt = stamp;
      }

      if (data.wifi && typeof data.wifi === 'object') {
        const wifiData = Object.assign({}, data.wifi, { ip: normalizeIpValue(data.wifi.ip) });
        const wifiMac = normalizeInfoMac(wifiData.mac);
        if (wifiMac !== '-') infoLastMac = wifiMac;
        flowStatusDomainCache.wifi.data = { ok: true, wifi: wifiData };
        flowStatusDomainCache.wifi.fetchedAt = stamp;
      }

      if (data.mqtt && typeof data.mqtt === 'object') {
        flowStatusDomainCache.mqtt.data = { ok: true, mqtt: data.mqtt };
        flowStatusDomainCache.mqtt.fetchedAt = stamp;
      }

      if (data.pool && typeof data.pool === 'object') {
        flowStatusDomainCache.pool.data = { ok: true, pool: data.pool };
        flowStatusDomainCache.pool.fetchedAt = stamp;
      }

      if (data.i2c && typeof data.i2c === 'object') {
        flowStatusDomainCache.i2c.data = { ok: true, i2c: data.i2c };
        flowStatusDomainCache.i2c.fetchedAt = stamp;
      }

      if (data.time && typeof data.time === 'object') {
        const systemData = flowStatusDomainCache.system.data && typeof flowStatusDomainCache.system.data === 'object'
          ? flowStatusDomainCache.system.data
          : { ok: true };
        systemData.time = data.time;
        flowStatusDomainCache.system.data = systemData;
        flowStatusDomainCache.system.fetchedAt = stamp;
      }
    }

    async function fetchFlowStatusAggregate(forceRefresh, sourceTag) {
      // Legacy path kept for compatibility. Info page no longer uses aggregate.
      // Dashboard status is built from per-domain calls.
      flowStatusDebugLog('aggregate fetch path disabled', {
        force: !!forceRefresh,
        src: String(sourceTag || '').trim() || 'unknown'
      });
      throw new Error('aggregate flow status disabled');
    }

    async function fetchFlowStatusDomain(domainKey, forceRefresh, sourceTag) {
      const cacheEntry = flowStatusDomainCache[domainKey];
      const now = Date.now();
      const cacheValid = isFlowStatusDomainCacheValid(domainKey, now);
      if (!forceRefresh && cacheValid) {
        return cacheEntry.data;
      }

      try {
        const src = String(sourceTag || '').trim().toLowerCase();
        let endpoint = '/api/flow/status/domain?d=' + encodeURIComponent(domainKey);
        if (src) {
          endpoint += '&src=' + encodeURIComponent(src);
        }
        const { res, data } = await fetchJsonResponse(
          endpoint,
          { cache: 'no-store' },
          fetchFlowRemoteQueued
        );
        if (!data || typeof data !== 'object') {
          throw new Error('statut ' + domainKey + ' invalide');
        }
        // For per-domain runtime fetches, keep structured `ok:false` payloads
        // as valid responses so Info cards can show unavailable state cleanly.
        if (!res.ok) {
          const fallback = extractApiErrorMessage(data, 'statut ' + domainKey + ' indisponible');
          throw new Error(fallback);
        }
        if (data.ok !== true) {
          flowStatusDebugLog('domain fetch returned ok=false', {
            domain: domainKey,
            src: src || 'unknown',
            status: res.status,
            err: data && data.err ? data.err : null,
            body: data || null
          });
        }
        cacheEntry.data = data;
        cacheEntry.fetchedAt = Date.now();
        return data;
      } catch (err) {
        if (cacheEntry.data) {
          return cacheEntry.data;
        }
        throw err;
      }
    }

    function appendFlowStatusCard(config) {
      const card = document.createElement('div');
      card.className = 'status-card';
      if (config.cardClass) {
        card.classList.add(config.cardClass);
      }

      const head = document.createElement('div');
      head.className = 'status-card-head';
      const titleBlock = document.createElement('div');

      const heading = document.createElement('h3');
      heading.textContent = config.title;
      titleBlock.appendChild(heading);

      if (config.summary) {
        const summary = document.createElement('p');
        summary.className = 'status-card-summary';
        summary.textContent = config.summary;
        titleBlock.appendChild(summary);
      }

      head.appendChild(titleBlock);
      head.appendChild(buildFlowStatusCardIcon(config.icon, !!config.ok, config.iconLabel));
      card.appendChild(head);

      const rows = Array.isArray(config.rows) ? config.rows : [];
      if (rows.length > 0) {
        const kv = document.createElement('div');
        kv.className = 'status-kv';
        rows.forEach((row) => appendFlowStatusRow(kv, row[0], row[1]));
        card.appendChild(kv);
      }
      (config.extras || []).forEach((extra) => {
        if (!extra || typeof extra.nodeType !== 'number') return;
        card.appendChild(extra);
      });
      flowStatusGrid.appendChild(card);
    }

    function createSkeletonLine(className, widthPercent) {
      const line = document.createElement('div');
      line.className = className ? ('skeleton-line ' + className) : 'skeleton-line';
      if (Number.isFinite(widthPercent) && widthPercent > 0) {
        line.style.width = widthPercent + '%';
      }
      return line;
    }

    function appendFlowStatusSkeletonCard() {
      const card = document.createElement('div');
      card.className = 'status-card status-card-skeleton';
      const title = createSkeletonLine('skeleton-title', 46);
      card.appendChild(title);

      const kv = document.createElement('div');
      kv.className = 'status-kv';
      const widths = [
        [44, 24],
        [40, 30],
        [35, 20],
        [48, 26]
      ];
      widths.forEach((pair) => {
        const row = document.createElement('div');
        row.appendChild(createSkeletonLine('skeleton-key', pair[0]));
        row.appendChild(createSkeletonLine('skeleton-value', pair[1]));
        kv.appendChild(row);
      });
      card.appendChild(kv);
      flowStatusGrid.appendChild(card);
    }

    function renderFlowStatusSkeleton() {
      flowStatusChip.textContent = 'chargement...';
      flowStatusGrid.innerHTML = '';
      for (let i = 0; i < 6; ++i) {
        appendFlowStatusSkeletonCard();
      }
      flowStatusRaw.hidden = true;
      flowStatusRaw.classList.remove('is-skeleton');
      flowStatusRaw.innerHTML = '';
    }

    function renderFlowStatus(data) {
      const wifi = (data && typeof data.wifi === 'object') ? data.wifi : {};
      const mqtt = (data && typeof data.mqtt === 'object') ? data.mqtt : {};
      const pool = (data && typeof data.pool === 'object') ? data.pool : {};
      const time = (data && typeof data.time === 'object') ? data.time : {};
      const heap = (data && data.heap && typeof data.heap === 'object') ? data.heap : {};
      const i2c = (data && data.i2c && typeof data.i2c === 'object') ? data.i2c : {};
      const firmware = fmtFlowStatusVal(data.fw);
      window.__flowIoFirmwareVersion = firmware;
      const uptimeMs = Number(data.upms) || 0;
      const wifiReady = !!wifi.rdy;
      const wifiIp = normalizeIpValue(wifi.ip);
      const wifiHasRssi = !!wifi.hrss;
      const wifiRssi = wifi.rssi ?? '-';
      const networkType = effectiveNetworkType(wifi, true);
      const networkIsEthernet = networkType === 'ethernet';
      const mqttReady = !!mqtt.rdy;
      if (data && Object.prototype.hasOwnProperty.call(data, 'time')) {
        currentFlowTimeSourceLabel = flowTimeHeaderLabel(time);
      }
      refreshAppHeaderClock();
      const mqttServer = fmtFlowStatusVal(mqtt.srv);
      const mqttRxDrop = mqtt.rxdrp ?? 0;
      const mqttParseFail = mqtt.prsf ?? 0;
      const mqttHandlerFail = mqtt.hndf ?? 0;
      const mqttOversizeDrop = mqtt.ovr ?? 0;
      const i2cLinkOk = !!i2c.lnk;
      const mqttIssueCount = (Number(mqttRxDrop) || 0) +
        (Number(mqttParseFail) || 0) +
        (Number(mqttHandlerFail) || 0) +
        (Number(mqttOversizeDrop) || 0);
      const waterTemp = pool.wat;
      const airTemp = pool.air;
      const phValue = pool.ph;
      const orpValue = pool.orp;
      const hasPoolModes = !!pool.has;
      const autoModeOn = hasPoolModes ? !!pool.auto : null;
      const winterModeOn = hasPoolModes ? !!pool.wint : null;
      const filtrationOn = (typeof pool.fil === 'boolean') ? pool.fil : null;
      const poolMetricsReady =
        Number.isFinite(Number(waterTemp)) ||
        Number.isFinite(Number(airTemp)) ||
        Number.isFinite(Number(phValue)) ||
        Number.isFinite(Number(orpValue));
      const poolStatesReady =
        typeof autoModeOn === 'boolean' ||
        typeof filtrationOn === 'boolean' ||
        typeof winterModeOn === 'boolean';
      const heapFree = ('free' in heap) ? heap.free : null;
      const heapMin = ('min_free' in heap) ? heap.min_free : null;
      const systemReady = firmware !== '-' || uptimeMs > 0 || heapFree !== null;
      const supervisorHeapFree = ('free' in supervisorHeap) ? supervisorHeap.free : null;
      const supervisorHeapMin = ('min_free' in supervisorHeap) ? supervisorHeap.min_free : null;
      const supervisorReady =
        supervisorFirmwareVersion !== '-' ||
        supervisorUptimeMs > 0 ||
        supervisorHeapFree !== null;
      const poolMetricRows = [
        [
          'Temperature eau',
          buildFlowThresholdValueNode({
            value: waterTemp,
            min: 0,
            max: 40,
            unit: '°C',
            decimals: 1,
            bands: createFlowFiveZoneBands({
              min: 0,
              criticalLowEnd: 8,
              warningLowEnd: 14,
              warningHighStart: 30,
              criticalHighStart: 34,
              max: 40
            })
          })
        ],
        [
          'Temperature air',
          buildFlowThresholdValueNode({
            value: airTemp,
            min: -10,
            max: 45,
            unit: '°C',
            decimals: 1,
            bands: createFlowFiveZoneBands({
              min: -10,
              criticalLowEnd: 0,
              warningLowEnd: 8,
              warningHighStart: 28,
              criticalHighStart: 35,
              max: 45
            })
          })
        ],
        [
          'pH',
          buildFlowThresholdValueNode({
            value: phValue,
            min: 6.4,
            max: 8.4,
            decimals: 2,
            bands: createFlowFiveZoneBands({
              min: 6.4,
              criticalLowEnd: 6.8,
              warningLowEnd: 7.0,
              warningHighStart: 7.6,
              criticalHighStart: 7.8,
              max: 8.4
            })
          })
        ],
        [
          'ORP',
          buildFlowThresholdValueNode({
            value: orpValue,
            min: 350,
            max: 900,
            unit: 'mV',
            decimals: 0,
            bands: createFlowFiveZoneBands({
              min: 350,
              criticalLowEnd: 500,
              warningLowEnd: 620,
              warningHighStart: 760,
              criticalHighStart: 820,
              max: 900
            })
          })
        ]
      ];
      const poolStateGrid = buildFlowReadonlyStateGrid([
        buildFlowReadonlyStateTile('Mode auto', autoModeOn, {
          activeText: 'Actif',
          inactiveText: 'Manuel'
        }),
        buildFlowReadonlyStateTile('Filtration', filtrationOn, {
          activeText: 'En marche',
          inactiveText: 'Arret'
        }),
        buildFlowReadonlyStateTile('Hivernage', winterModeOn, {
          activeText: 'Actif',
          inactiveText: 'Arret'
        })
      ]);

      flowStatusGrid.innerHTML = '';
      const networkRows = [
        [tr('info.row.ip', 'Adresse IP'), wifiIp],
        [tr('info.row.networkType', 'Type réseau'), formatInfoNetworkType(networkType)]
      ];
      if (!networkIsEthernet) {
        networkRows.push([tr('info.row.signal', 'Signal'), buildFlowRssiGauge(wifiRssi, wifiHasRssi)]);
      }
      appendFlowStatusCard({
        title: tr('header.wifi', 'Réseau'),
        icon: networkIsEthernet ? 'ethernet' : 'wifi',
        ok: wifiReady,
        iconLabel: wifiReady ? tr('info.state.connected', 'Connecté') : tr('info.state.disconnected', 'Déconnecté'),
        rows: networkRows
      });
      appendFlowStatusCard({
        title: 'MQTT',
        cardClass: 'status-card-mqtt',
        icon: 'mqtt',
        ok: mqttReady,
        iconLabel: mqttReady ? 'MQTT connecte' : 'MQTT deconnecte',
        rows: [
          ['Serveur', mqttServer]
        ],
        extras: [
          buildMqttStatsStrip([
            {
              label: 'Anomalies',
              title: 'Anomalies MQTT totalisees (ignores + contenu invalide + erreurs de traitement)',
              value: mqttIssueCount
            },
            {
              label: 'Ignores',
              title: 'Messages MQTT ignores ou rejetes (drops RX + payload trop volumineux)',
              value: (Number(mqttRxDrop) || 0) + (Number(mqttOversizeDrop) || 0)
            },
            {
              label: 'Contenu',
              title: 'Messages MQTT recus mais invalides ou impossibles a parser',
              value: mqttParseFail
            },
            {
              label: 'Traitement',
              title: 'Messages MQTT recus mais ayant echoue pendant le traitement applicatif',
              value: mqttHandlerFail
            }
          ])
        ]
      });
      appendFlowStatusCard({
        title: 'Mesures Bassin',
        cardClass: 'status-card-pool-metrics',
        icon: 'pool',
        ok: poolMetricsReady,
        iconLabel: poolMetricsReady ? 'Mesures bassin disponibles' : 'Mesures bassin indisponibles',
        rows: poolMetricRows
      });
      appendFlowStatusCard({
        title: 'Etats Bassin',
        icon: 'pump',
        ok: poolStatesReady,
        iconLabel: poolStatesReady ? 'Etats bassin disponibles' : 'Etats bassin indisponibles',
        rows: [],
        extras: poolStateGrid ? [poolStateGrid] : []
      });
      appendFlowStatusCard({
        title: 'Superviseur',
        icon: 'system',
        ok: supervisorReady,
        iconLabel: supervisorReady ? 'Superviseur disponible' : 'Superviseur indisponible',
        rows: [
          ['Firmware', supervisorFirmwareVersion],
          ['Uptime', createFlowLiveValue('uptime', supervisorUptimeMs, Date.now())],
          ['Heap libre', fmtFlowBytes(supervisorHeapFree)],
          ['Heap min', fmtFlowBytes(supervisorHeapMin)]
        ]
      });
      appendFlowStatusCard({
        title: 'flow.io',
        icon: 'system',
        ok: systemReady,
        iconLabel: systemReady ? 'Systeme joignable' : 'Systeme indisponible',
        rows: [
          ['Firmware', firmware],
          ['Uptime', createFlowLiveValue('uptime', uptimeMs, Date.now())],
          ['Heap libre', fmtFlowBytes(heapFree)],
          ['Heap min', fmtFlowBytes(heapMin)]
        ]
      });

      flowStatusChip.textContent = i2cLinkOk
        ? 'flow.io disponible'
        : 'Connexion flow.io a verifier';
      flowStatusRaw.hidden = true;
      flowStatusRaw.classList.remove('is-skeleton');
      flowStatusRaw.innerHTML = '';
      refreshFlowStatusLiveValues();
      const pageStatus = document.getElementById('page-status');
      if (pageStatus && pageStatus.classList.contains('active')) {
        ensureFlowStatusLiveTimer();
      } else {
        stopFlowStatusLiveTimer();
      }
    }

    async function refreshFlowStatus(forceRefresh) {
      const reqSeq = ++flowStatusReqSeq;
      const cachedData = !forceRefresh ? getCachedFlowStatusData() : null;
      if (cachedData) {
        renderFlowStatus(cachedData);
      } else {
        renderFlowStatusSkeleton();
      }
      try {
        const domainData = {};
        for (const domainKey of flowStatusDomainKeys) {
          domainData[domainKey] = await fetchFlowStatusDomain(domainKey, !!forceRefresh, 'status');
          if (reqSeq !== flowStatusReqSeq) return;
        }
        const data = buildFlowStatusFromDomains(domainData);
        if (!data) throw new Error('statut indisponible');
        renderFlowStatus(data);
      } catch (err) {
        if (reqSeq !== flowStatusReqSeq) return;
        const fallbackData = getCachedFlowStatusData();
        if (fallbackData) {
          renderFlowStatus(fallbackData);
          flowStatusChip.textContent = 'statut affiche depuis le cache local';
          return;
        }
        flowStatusRaw.hidden = true;
        flowStatusRaw.classList.remove('is-skeleton');
        flowStatusChip.textContent = 'erreur lecture statut';
        flowStatusGrid.innerHTML = '';
        flowStatusRaw.innerHTML = '';
      }
    }

    function stopIoSummaryTimer() {
      ioSummaryPoller.stop();
    }

    function startIoSummaryTimer() {
      ioSummaryPoller.start();
    }

    function ioSummaryStateLabel(state) {
      const key = String(state || '').trim().toLowerCase();
      if (key === 'active') return tr('io.state.active', 'Actif');
      if (key === 'sleeping') return tr('io.state.sleeping', 'Veille');
      if (key === 'manually_disabled') return tr('io.state.manuallyDisabled', 'Désactivé manuellement');
      if (key === 'error') return tr('io.state.error', 'Erreur');
      return key || '-';
    }

    function ioSummaryStateClass(state) {
      const key = String(state || '').trim().toLowerCase();
      if (key === 'active') return 'is-active';
      if (key === 'manually_disabled') return 'is-disabled';
      if (key === 'error') return 'is-error';
      return 'is-sleeping';
    }

    function ioSummaryText(value, fallback) {
      const text = String(value === null || value === undefined ? '' : value).trim();
      return text || fallback || '-';
    }

    function ioSummaryNumber(value) {
      const n = Number(value);
      return Number.isFinite(n) ? n : 0;
    }

    function ioSummarySlotLabel(row) {
      const kind = ioSummaryText(row && row.io_slot, '');
      const idx = Number(row && row.io_slot_index);
      if (!kind) return '-';
      return Number.isFinite(idx) ? (kind + ' #' + idx) : kind;
    }

    function ioSummaryIoIdLabel(row) {
      const explicit = String(row && row.io_id_label !== null && row.io_id_label !== undefined ? row.io_id_label : '').trim();
      if (explicit) return explicit;
      const id = Number(row && row.io_id);
      if (!Number.isFinite(id) || id === 65535) return tr('io.unassigned', 'Non affecté');
      return String(id);
    }

    function createIoDirectionBadge(direction) {
      const key = String(direction || '').trim().toLowerCase();
      const badge = document.createElement('span');
      badge.className = 'io-direction-badge ' + (key === 'input' ? 'is-input' : key === 'output' ? 'is-output' : 'is-unknown');

      const label = key === 'input'
        ? tr('io.direction.input', 'Entrée')
        : key === 'output'
          ? tr('io.direction.output', 'Sortie')
          : tr('io.direction.unknown', 'Inconnu');
      badge.title = label;
      badge.setAttribute('aria-label', label);
      badge.tabIndex = 0;

      const icon = document.createElement('span');
      icon.className = 'io-direction-icon';
      icon.setAttribute('aria-hidden', 'true');
      icon.textContent = key === 'input' ? '⇥' : key === 'output' ? '↦' : '?';

      badge.appendChild(icon);
      return badge;
    }

    function createIoOptionalCell(value) {
      const content = document.createElement('span');
      if (value !== null && value !== undefined) content.textContent = String(value).trim();
      return content;
    }

    function appendIoSummaryCard(title, value, summary, state) {
      if (!ioSummaryCards) return;
      const card = document.createElement('div');
      card.className = 'status-card io-summary-card';

      const head = document.createElement('div');
      head.className = 'status-card-head';
      const copy = document.createElement('div');
      const h3 = document.createElement('h3');
      h3.textContent = title;
      const p = document.createElement('p');
      p.className = 'status-card-summary';
      p.textContent = summary;
      copy.appendChild(h3);
      copy.appendChild(p);

      const metric = document.createElement('span');
      metric.className = 'io-summary-card-value ' + ioSummaryStateClass(state);
      metric.textContent = String(value);
      head.appendChild(copy);
      head.appendChild(metric);
      card.appendChild(head);
      ioSummaryCards.appendChild(card);
    }

    function appendIoSummarySkeletonCard(title, summary) {
      if (!ioSummaryCards) return;
      const card = document.createElement('div');
      card.className = 'status-card io-summary-card status-card-skeleton';

      const head = document.createElement('div');
      head.className = 'status-card-head';
      const copy = document.createElement('div');
      const h3 = document.createElement('h3');
      h3.textContent = title;
      const p = document.createElement('p');
      p.className = 'status-card-summary';
      p.textContent = summary;
      copy.appendChild(h3);
      copy.appendChild(p);

      const metric = document.createElement('span');
      metric.className = 'io-summary-card-value is-sleeping';
      metric.appendChild(createSkeletonLine('io-summary-card-value-skeleton', 100));
      head.appendChild(copy);
      head.appendChild(metric);
      card.appendChild(head);
      ioSummaryCards.appendChild(card);
    }

    function createIoStateBadge(state) {
      const badge = document.createElement('span');
      badge.className = 'io-state-badge ' + ioSummaryStateClass(state);
      badge.textContent = ioSummaryStateLabel(state);
      return badge;
    }

    function createIoCompactTable(title, columns, rows) {
      const section = document.createElement('section');
      section.className = 'io-table-section';

      const heading = document.createElement('div');
      heading.className = 'control-section-title ui-heading-inline';
      const icon = document.createElement('span');
      icon.className = 'ui-msr ui-msr-sm';
      icon.setAttribute('aria-hidden', 'true');
      icon.textContent = 'table_chart';
      const text = document.createElement('span');
      text.textContent = title;
      heading.appendChild(icon);
      heading.appendChild(text);
      section.appendChild(heading);

      const shell = document.createElement('div');
      shell.className = 'io-table-shell';
      const table = document.createElement('table');
      table.className = 'io-compact-table';

      const thead = document.createElement('thead');
      const headRow = document.createElement('tr');
      columns.forEach((column) => {
        const th = document.createElement('th');
        th.scope = 'col';
        th.textContent = column.label;
        headRow.appendChild(th);
      });
      thead.appendChild(headRow);
      table.appendChild(thead);

      const tbody = document.createElement('tbody');
      (rows || []).forEach((row) => {
        const trEl = document.createElement('tr');
        columns.forEach((column) => {
          const td = document.createElement('td');
          const rendered = typeof column.render === 'function' ? column.render(row) : row[column.key];
          if (rendered instanceof Node) {
            td.appendChild(rendered);
          } else {
            td.textContent = ioSummaryText(rendered, '-');
          }
          trEl.appendChild(td);
        });
        tbody.appendChild(trEl);
      });
      if (!rows || !rows.length) {
        const trEl = document.createElement('tr');
        const td = document.createElement('td');
        td.colSpan = columns.length;
        td.className = 'io-empty-cell';
        td.textContent = tr('io.empty', 'Aucune donnée.');
        trEl.appendChild(td);
        tbody.appendChild(trEl);
      }
      table.appendChild(tbody);
      shell.appendChild(table);
      section.appendChild(shell);
      return section;
    }

    function createIoSummaryTableSkeleton(title, labels, rowCount) {
      const widths = [72, 54, 48, 42, 64, 58, 46];
      const columns = (labels || []).map((label, index) => ({
        key: 'c' + index,
        label,
        render: () => createSkeletonLine('io-table-skeleton-line', widths[index % widths.length])
      }));
      const rows = Array.from({ length: Math.max(1, rowCount || 1) }, () => ({}));
      return createIoCompactTable(title, columns, rows);
    }

    function renderIoSummarySkeleton() {
      if (ioSummaryCards) {
        ioSummaryCards.innerHTML = '';
        appendIoSummarySkeletonCard(
          tr('io.cards.bindingPorts', 'BindingPorts'),
          tr('io.cards.bindingPorts.summary', 'ports physiques actifs')
        );
        appendIoSummarySkeletonCard(
          tr('io.cards.ioslots', 'IOSlots'),
          tr('io.cards.ioslots.summary', 'slots logiques actifs')
        );
        appendIoSummarySkeletonCard(
          tr('io.cards.domainSlots', 'DomainSlots'),
          tr('io.cards.domainSlots.summary', 'slots domaine actifs')
        );
        appendIoSummarySkeletonCard(
          tr('io.cards.manuallyDisabled', 'Désactivés manuellement'),
          tr('io.status.loading', 'Chargement...')
        );
        appendIoSummarySkeletonCard(
          tr('io.cards.errors', 'Slots en erreur'),
          tr('io.status.loading', 'Chargement...')
        );
      }
      if (ioSummaryTables) {
        ioSummaryTables.innerHTML = '';
        ioSummaryTables.appendChild(createIoSummaryTableSkeleton(
          tr('io.table.drivers', 'Affectations par driver'),
          [
            tr('io.col.driver', 'Driver'),
            tr('io.col.active', 'Actifs'),
            tr('io.col.manuallyDisabled', 'Désactivés'),
            tr('io.col.errors', 'Erreurs')
          ],
          3
        ));
        ioSummaryTables.appendChild(createIoSummaryTableSkeleton(
          tr('io.table.bindingPorts', 'BindingPorts'),
          [
            tr('io.col.port', 'Port'),
            tr('io.col.boardPort', 'Port carte'),
            tr('io.col.direction', 'Sens'),
            tr('io.col.driver', 'Driver'),
            tr('io.col.channel', 'Canal interne'),
            tr('io.col.state', 'Etat'),
            tr('io.col.lastValue', 'Dernière valeur'),
            tr('io.col.ioId', 'IoId')
          ],
          4
        ));
        ioSummaryTables.appendChild(createIoSummaryTableSkeleton(
          tr('io.table.ioSlots', 'IOSlots'),
          [
            tr('io.col.slot', 'Slot'),
            tr('io.col.configName', 'Nom config'),
            tr('io.col.kind', 'Type'),
            tr('io.col.driver', 'Driver'),
            tr('io.col.state', 'Etat'),
            tr('io.col.lastValue', 'Dernière valeur'),
            tr('io.col.error', 'Erreur')
          ],
          4
        ));
        ioSummaryTables.appendChild(createIoSummaryTableSkeleton(
          tr('io.table.domainSlots', 'DomainSlots'),
          [
            tr('io.col.domainSlot', 'Domaine'),
            tr('io.col.ioName', 'IONAME'),
            tr('io.col.ioSlot', 'IOSlot'),
            tr('io.col.state', 'Etat'),
            tr('io.col.lastValue', 'Dernière valeur')
          ],
          4
        ));
      }
    }

    function renderIoSummary(data) {
      const summary = data && typeof data.summary === 'object' ? data.summary : {};
      const drivers = Array.isArray(data && data.drivers) ? data.drivers : [];
      const bindingPorts = Array.isArray(data && data.binding_ports) ? data.binding_ports : [];
      const ioSlots = Array.isArray(data && data.io_slots) ? data.io_slots : [];
      const domainSlots = Array.isArray(data && data.domain_slots) ? data.domain_slots : [];
      const errors = Array.isArray(data && data.error_slots) ? data.error_slots : [];

      if (ioSummaryCards) {
        ioSummaryCards.innerHTML = '';
        appendIoSummaryCard(
          tr('io.cards.bindingPorts', 'BindingPorts'),
          ioSummaryNumber(summary.binding_ports_active) + '/' + ioSummaryNumber(summary.binding_ports_total),
          tr('io.cards.bindingPorts.summary', 'ports physiques actifs'),
          ioSummaryNumber(summary.binding_ports_error) ? 'error' : 'active'
        );
        appendIoSummaryCard(
          tr('io.cards.ioslots', 'IOSlots'),
          ioSummaryNumber(summary.io_slots_active) + '/' + ioSummaryNumber(summary.io_slots_total),
          tr('io.cards.ioslots.summary', 'slots logiques actifs'),
          ioSummaryNumber(summary.io_slots_error) ? 'error' : 'active'
        );
        appendIoSummaryCard(
          tr('io.cards.domainSlots', 'DomainSlots'),
          ioSummaryNumber(summary.domain_slots_active) + '/' + ioSummaryNumber(summary.domain_slots_total),
          tr('io.cards.domainSlots.summary', 'slots domaine actifs'),
          ioSummaryNumber(summary.domain_slots_error) ? 'error' : 'active'
        );
        appendIoSummaryCard(
          tr('io.cards.manuallyDisabled', 'Désactivés manuellement'),
          ioSummaryNumber(summary.io_slots_manually_disabled),
          tr('io.cards.manuallyDisabled.summary', 'slots désactivés par configuration'),
          ioSummaryNumber(summary.io_slots_manually_disabled) ? 'manually_disabled' : 'active'
        );
        appendIoSummaryCard(
          tr('io.cards.errors', 'Slots en erreur'),
          ioSummaryNumber(summary.error_slots),
          errors.length ? errors.map((slot) => ioSummaryText(slot.label, slot.io_slot)).slice(0, 3).join(', ') : tr('io.cards.errors.none', 'aucune erreur active'),
          errors.length ? 'error' : 'active'
        );
      }

      if (ioSummaryTables) {
        ioSummaryTables.innerHTML = '';
        ioSummaryTables.appendChild(createIoCompactTable(
          tr('io.table.drivers', 'Affectations par driver'),
          [
            { key: 'driver', label: tr('io.col.driver', 'Driver') },
            { key: 'active_slots', label: tr('io.col.active', 'Actifs') },
            { key: 'manually_disabled_slots', label: tr('io.col.manuallyDisabled', 'Désactivés') },
            { key: 'error_slots', label: tr('io.col.errors', 'Erreurs') }
          ],
          drivers
        ));
        ioSummaryTables.appendChild(createIoCompactTable(
          tr('io.table.bindingPorts', 'BindingPorts'),
          [
            { key: 'port_id', label: tr('io.col.port', 'Port') },
            { key: 'board_port', label: tr('io.col.boardPort', 'Port carte'), render: (row) => ioSummaryText(row.board_port, '-') },
            { key: 'direction', label: tr('io.col.direction', 'Sens'), render: (row) => createIoDirectionBadge(row.direction) },
            { key: 'driver', label: tr('io.col.driver', 'Driver') },
            { key: 'channel', label: tr('io.col.channel', 'Canal interne') },
            { key: 'state', label: tr('io.col.state', 'Etat'), render: (row) => createIoStateBadge(row.state) },
            { key: 'last_value', label: tr('io.col.lastValue', 'Dernière valeur') },
            { key: 'io_id', label: tr('io.col.ioId', 'IoId'), render: (row) => ioSummaryIoIdLabel(row) }
          ],
          bindingPorts
        ));
        ioSummaryTables.appendChild(createIoCompactTable(
          tr('io.table.ioSlots', 'IOSlots'),
          [
            { key: 'io_slot', label: tr('io.col.slot', 'Slot'), render: (row) => ioSummarySlotLabel(row) },
            { key: 'config_name', label: tr('io.col.configName', 'Nom config'), render: (row) => ioSummaryText(row.config_name, '-') },
            { key: 'kind', label: tr('io.col.kind', 'Type') },
            { key: 'driver', label: tr('io.col.driver', 'Driver'), render: (row) => createIoOptionalCell(row.driver) },
            { key: 'channel', label: tr('io.col.channel', 'Canal interne'), render: (row) => createIoOptionalCell(row.channel) },
            { key: 'state', label: tr('io.col.state', 'Etat'), render: (row) => createIoStateBadge(row.state) },
            { key: 'last_value', label: tr('io.col.lastValue', 'Dernière valeur') },
            { key: 'error', label: tr('io.col.error', 'Erreur') }
          ],
          ioSlots
        ));
        ioSummaryTables.appendChild(createIoCompactTable(
          tr('io.table.domainSlots', 'DomainSlots'),
          [
            { key: 'display_name', label: tr('io.col.domainSlot', 'Domaine') },
            { key: 'io_name', label: tr('io.col.ioName', 'IONAME'), render: (row) => ioSummaryText(row.io_name, '-') },
            { key: 'io_slot', label: tr('io.col.ioSlot', 'IOSlot'), render: (row) => ioSummarySlotLabel(row) },
            { key: 'state', label: tr('io.col.state', 'Etat'), render: (row) => createIoStateBadge(row.state) },
            { key: 'last_value', label: tr('io.col.lastValue', 'Dernière valeur') }
          ],
          domainSlots
        ));
      }
    }

    function mergeIoTopologyAndRuntime(topology, runtime) {
      const bindingRuntime = new Map(
        (Array.isArray(runtime && runtime.binding_ports) ? runtime.binding_ports : [])
          .map((row) => [Number(row && row.port_id), row])
      );
      const ioRuntime = new Map(
        (Array.isArray(runtime && runtime.io_slots) ? runtime.io_slots : [])
          .map((row) => [Number(row && row.domain_slot_id), row])
      );
      const domainRuntime = new Map(
        (Array.isArray(runtime && runtime.domain_slots) ? runtime.domain_slots : [])
          .map((row) => [Number(row && row.domain_slot_id), row])
      );

      return {
        ok: true,
        summary: runtime && runtime.summary,
        drivers: Array.isArray(runtime && runtime.drivers) ? runtime.drivers : [],
        binding_ports: (Array.isArray(topology && topology.binding_ports) ? topology.binding_ports : [])
          .map((row) => Object.assign({}, row, bindingRuntime.get(Number(row && row.port_id)) || {})),
        io_slots: (Array.isArray(topology && topology.io_slots) ? topology.io_slots : [])
          .map((row) => Object.assign({}, row, ioRuntime.get(Number(row && row.domain_slot_id)) || {})),
        domain_slots: (Array.isArray(topology && topology.domain_slots) ? topology.domain_slots : [])
          .map((row) => Object.assign({}, row, domainRuntime.get(Number(row && row.domain_slot_id)) || {})),
        error_slots: Array.isArray(runtime && runtime.error_slots) ? runtime.error_slots : []
      };
    }

    async function fetchIoTopology(forceRefresh) {
      if (!forceRefresh && ioTopologyCache) return ioTopologyCache;
      const data = await fetchOkJson(
        '/api/io/topology',
        { cache: 'no-store' },
        'topologie entrées/sorties indisponible'
      );
      ioTopologyCache = data;
      return data;
    }

    async function fetchIoRuntime() {
      return fetchOkJson(
        '/api/io/runtime',
        { cache: 'no-store' },
        'état entrées/sorties indisponible'
      );
    }

    async function fetchIoSummary(forceTopologyRefresh) {
      let results = await Promise.all([
        fetchIoTopology(!!forceTopologyRefresh),
        fetchIoRuntime()
      ]);
      let topology = results[0];
      let runtime = results[1];
      const topologyRevision = Number(topology && topology.revision);
      const runtimeRevision = Number(runtime && runtime.topology_revision);
      if (Number.isFinite(runtimeRevision) && runtimeRevision !== topologyRevision) {
        results = await Promise.all([fetchIoTopology(true), fetchIoRuntime()]);
        topology = results[0];
        runtime = results[1];
      }
      return mergeIoTopologyAndRuntime(topology, runtime);
    }

    async function refreshIoSummary(forceRefresh) {
      const reqSeq = ++ioSummaryReqSeq;
      if (forceRefresh || !ioSummaryLoadedOnce) renderIoSummarySkeleton();
      try {
        const data = await fetchIoSummary(forceRefresh || !ioTopologyCache);
        if (reqSeq !== ioSummaryReqSeq) return;
        ioSummaryLoadedOnce = true;
        renderIoSummary(data);
      } catch (err) {
        if (reqSeq !== ioSummaryReqSeq) return;
        if (ioSummaryTables) ioSummaryTables.innerHTML = '';
      }
    }

    async function onIoSummaryPageShown() {
      startIoSummaryTimer();
      await refreshIoSummary(!ioSummaryLoadedOnce);
    }

    function stopPoolMeasuresTimer() {
      poolMeasuresPoller.stop();
    }

    function showPoolMeasuresError(err) {
      if (!poolMeasuresStatus) return;
      poolMeasuresStatus.textContent = 'Chargement mesures echoue: ' + err;
    }

    function startPoolMeasuresTimer() {
      poolMeasuresPoller.start();
    }

    function normalizeRuntimeMeasureDomainKey(domain) {
      const key = String(domain || '').trim().toLowerCase();
      return runtimeMeasureDomainKeys.includes(key) ? key : '';
    }

    function runtimeManifestDomainKeys() {
      const keys = runtimeMeasureDomainKeys.slice();
      infoFlowDomainKeys.forEach((domainKey) => {
        if (!keys.includes(domainKey)) keys.push(domainKey);
      });
      return keys;
    }

    function normalizeRuntimeManifestDomainKey(domain) {
      const key = String(domain || '').trim().toLowerCase();
      return runtimeManifestDomainKeys().includes(key) ? key : '';
    }

    function createEmptyRuntimeManifestDomainCache() {
      const cache = {};
      runtimeManifestDomainKeys().forEach((domainKey) => {
        cache[domainKey] = [];
      });
      return cache;
    }

    function activePoolMeasureDomainKeys() {
      return runtimeMeasureDomainKeys.filter((domainKey) => poolMeasureDomainState[domainKey].active);
    }

    function registerRuntimeManifestEntry(cache, entry) {
      if (!entry || !Number.isFinite(Number(entry.id))) return;
      const domainKey = normalizeRuntimeManifestDomainKey(entry.domain);
      if (!domainKey || !Array.isArray(cache[domainKey])) return;
      cache[domainKey].push(entry);
    }

    async function parseRuntimeManifestStreamIntoCache(response, cache) {
      const data = await response.json().catch(() => null);
      if (!data || typeof data !== 'object') {
        throw new Error('manifeste runtime invalide');
      }
      const values = Array.isArray(data.values) ? data.values : [];
      values.forEach((entry) => registerRuntimeManifestEntry(cache, entry));
    }

    async function loadRuntimeManifestDomains(forceRefresh) {
      if (!forceRefresh && runtimeManifestDomainCache) {
        return runtimeManifestDomainCache;
      }
      if (!forceRefresh && runtimeManifestDomainLoadPromise) {
        return runtimeManifestDomainLoadPromise;
      }

      runtimeManifestDomainLoadPromise = (async () => {
        const response = await fetchWithBusyRetry(assetUrl('/api/runtime/manifest'), {
          cache: forceRefresh ? 'no-store' : 'default'
        });
        if (!response.ok) throw new Error('manifeste runtime indisponible');
        const nextCache = createEmptyRuntimeManifestDomainCache();
        await parseRuntimeManifestStreamIntoCache(response, nextCache);
        runtimeManifestDomainCache = nextCache;
        return runtimeManifestDomainCache;
      })();

      try {
        return await runtimeManifestDomainLoadPromise;
      } finally {
        runtimeManifestDomainLoadPromise = null;
      }
    }

    async function runtimeMeasureEntriesForDomain(domainKey, forceRefresh) {
      const cleanDomain = normalizeRuntimeManifestDomainKey(domainKey);
      if (!cleanDomain) return [];
      const cache = await loadRuntimeManifestDomains(!!forceRefresh);
      return Array.isArray(cache[cleanDomain]) ? cache[cleanDomain] : [];
    }

    function formatRuntimeDomainLabel(domain) {
      const key = String(domain || '').trim().toLowerCase();
      if (key === 'mode') return 'Mode';
      if (key === 'equipements') return tr('dashboard.domain.equipements', 'Equipements');
      if (key === 'sondes') return 'Sondes';
      if (key === 'micronova') return 'Micronova';
      if (key === 'mqtt') return 'MQTT';
      if (key === 'wifi') return tr('header.wifi', 'Réseau');
      if (key === 'i2c') return 'I2C';
      if (key === 'system') return 'Système';
      if (key === 'alarm') return 'Alarmes';
      if (!key) return 'Runtime';
      return key.charAt(0).toUpperCase() + key.slice(1);
    }

    function formatRuntimeGroupCardTitle(domain, group) {
      const domainLabel = formatRuntimeDomainLabel(domain);
      const groupLabel = String(group || '').trim();
      if (!groupLabel) return domainLabel;
      const comparableTitle = (value) => {
        const normalized = String(value || '').trim().toLowerCase();
        const ascii = typeof normalized.normalize === 'function'
          ? normalized.normalize('NFD').replace(/[\u0300-\u036f]/g, '')
          : normalized;
        return ascii.replace(/[^a-z0-9]+/g, '').replace(/s$/, '');
      };
      if (comparableTitle(groupLabel) === comparableTitle(domainLabel)) return domainLabel;
      return domainLabel + ' · ' + groupLabel;
    }

    function runtimeMeasureCssSlug(value) {
      const source = String(value || '').trim().toLowerCase();
      if (!source) return 'default';
      const normalized = typeof source.normalize === 'function'
        ? source.normalize('NFD').replace(/[\u0300-\u036f]/g, '')
        : source;
      const slug = normalized.replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '');
      return slug || 'default';
    }

    function formatRuntimeDurationMs(ms) {
      const totalMs = Number(ms);
      if (!Number.isFinite(totalMs) || totalMs < 0) return '-';
      if (totalMs < 1000) return Math.round(totalMs) + ' ms';

      const totalSec = Math.floor(totalMs / 1000);
      const days = Math.floor(totalSec / 86400);
      const hours = Math.floor((totalSec % 86400) / 3600);
      const minutes = Math.floor((totalSec % 3600) / 60);
      const seconds = totalSec % 60;
      const parts = [];

      if (days > 0) parts.push(days + ' j');
      if (hours > 0) parts.push(hours + ' h');
      if (minutes > 0) parts.push(minutes + ' min');
      if (seconds > 0 || parts.length === 0) parts.push(seconds + ' s');

      return parts.slice(0, 2).join(' ');
    }

    async function fetchRuntimeValues(ids) {
      const cleanIds = (ids || [])
        .map((id) => Number(id))
        .filter((id) => Number.isFinite(id) && id > 0);
      if (!cleanIds.length) return [];
      const query = cleanIds.join(',');
      const data = await fetchOkJson(
        '/api/runtime/values?ids=' + encodeURIComponent(query),
        { cache: 'no-store' },
        'lecture runtime indisponible',
        fetchFlowRemoteQueued
      );
      if (!Array.isArray(data.values)) throw new Error('lecture runtime indisponible');
      return data.values;
    }

    async function fetchPoolDashboardSlots() {
      const data = await fetchOkJson(
        '/api/runtime/dashboard_slots',
        { cache: 'no-store' },
        'lecture slots tableau de bord indisponible'
      );
      return data && typeof data === 'object' ? data : {};
    }

    async function fetchPoolSondeSlots() {
      const data = await fetchPoolDashboardSlots();
      const slots = Array.isArray(data && data.slots) ? data.slots : [];
      return slots
        .map((slot) => {
          const idx = Number(slot && slot.slot);
          return {
            slot: Number.isFinite(idx) ? idx : 999,
            label: String(slot && slot.label ? slot.label : '').trim(),
            value: String(slot && slot.value ? slot.value : '').trim(),
            unit: String(slot && slot.unit ? slot.unit : '').trim(),
            bgColor: String(slot && slot.bg_color ? slot.bg_color : '').trim(),
            enabled: slot && slot.enabled !== false,
            available: !!(slot && slot.available)
          };
        })
        .sort((a, b) => a.slot - b.slot)
        .slice(0, 8);
    }

    async function fetchPoolAlarmSlots() {
      const data = await fetchPoolDashboardSlots();
      const slots = Array.isArray(data && data.alarm_slots) ? data.alarm_slots : [];
      return slots
        .map((slot) => {
          const idx = Number(slot && slot.slot);
          return {
            slot: Number.isFinite(idx) ? idx : 999,
            label: String(slot && slot.label ? slot.label : '').trim(),
            bgColor: String(slot && slot.bg_color ? slot.bg_color : '').trim(),
            enabled: !!(slot && slot.enabled),
            available: !!(slot && slot.available),
            latched: !!(slot && slot.latched),
            conditionKnown: !!(slot && slot.condition_known),
            conditionTrue: !!(slot && slot.condition_true)
          };
        })
        .sort((a, b) => a.slot - b.slot)
        .slice(0, 8);
    }

    function runtimeMeasureDisplayLabel(entry) {
      return entry.label || entry.key || String(entry.id);
    }

    function isPoolDashboardGroupEntry(entry) {
      if (!entry || String(entry.domain || '').trim().toLowerCase() !== 'sondes') return false;
      return String(entry.group || '').trim().localeCompare('Dashboard', 'fr', { sensitivity: 'base' }) === 0;
    }

    function isPoolSondesGroupKey(domainKey, groupKey) {
      return String(domainKey || '').trim().toLowerCase() === 'sondes'
        && String(groupKey || '').trim().localeCompare('Sondes', 'fr', { sensitivity: 'base' }) === 0;
    }

    function isValidHexColor(color) {
      return /^#[0-9A-Fa-f]{6}$/.test(String(color || '').trim());
    }

    function splitPoolSondeValue(slot) {
      const unit = String(slot && slot.unit ? slot.unit : '').trim();
      const valueRaw = String(slot && slot.value ? slot.value : '').trim();
      const available = !!(slot && slot.available);
      if (!available) return { value: '--', unit: '' };
      if (!valueRaw) return { value: '-', unit: '' };
      if (!unit) return { value: valueRaw, unit: '' };

      const suffix = ' ' + unit;
      if (valueRaw.length > suffix.length && valueRaw.endsWith(suffix)) {
        return {
          value: valueRaw.slice(0, valueRaw.length - suffix.length).trim(),
          unit: unit
        };
      }
      return { value: valueRaw, unit: unit };
    }

    function buildPoolSondeSlotsGrid(slots) {
      const cleanSlots = Array(8).fill(null);
      if (Array.isArray(slots)) {
        slots.forEach((slot) => {
          const idx = Number(slot && slot.slot);
          if (Number.isInteger(idx) && idx >= 0 && idx < 8) cleanSlots[idx] = slot;
        });
      }
      const grid = document.createElement('div');
      grid.className = 'status-sonde-slot-grid';

      for (let i = 0; i < 8; i += 1) {
        const slot = cleanSlots[i] || null;
        const tile = document.createElement('div');
        tile.className = 'status-sonde-slot';
        const available = !!(slot && slot.enabled !== false && slot.available);
        if (!available) tile.classList.add('is-empty');

        const bgColor = slot && isValidHexColor(slot.bgColor) ? slot.bgColor : '';
        if (bgColor) tile.style.background = bgColor;

        const title = document.createElement('div');
        title.className = 'status-sonde-slot-title';
        title.textContent = slot && slot.label ? slot.label : 'Mesure';
        tile.appendChild(title);

        const metric = document.createElement('div');
        metric.className = 'status-sonde-slot-metric';
        const display = splitPoolSondeValue(slot);

        const value = document.createElement('span');
        value.className = 'status-sonde-slot-value';
        value.textContent = display.value || '-';
        metric.appendChild(value);

        if (display.unit) {
          const unit = document.createElement('span');
          unit.className = 'status-sonde-slot-unit';
          unit.textContent = display.unit;
          metric.appendChild(unit);
        }

        tile.appendChild(metric);
        grid.appendChild(tile);
      }

      return grid;
    }

    function runtimeMeasureResolvedLabel(entry, options) {
      const opts = options && typeof options === 'object' ? options : {};
      if (typeof opts.displayLabelResolver === 'function') {
        const resolved = String(opts.displayLabelResolver(entry) || '').trim();
        if (resolved) return resolved;
      }
      return runtimeMeasureDisplayLabel(entry);
    }

    function formatRuntimeFloatValue(value, decimals) {
      const n = Number(value);
      if (!Number.isFinite(n)) return '-';
      if (decimals !== null) {
        return n.toFixed(decimals);
      }
      const rounded = Math.round(n * 1000) / 1000;
      const text = rounded.toFixed(3).replace(/(?:\.0+|(\.\d*?)0+)$/, '$1');
      return text === '-0' ? '0' : text;
    }

    function formatRuntimeMeasureValue(entry, runtimeValue) {
      if (!runtimeValue || runtimeValue.status === 'not_found' || runtimeValue.status === 'unavailable') {
        return 'Indisponible';
      }

      const display = runtimeMeasureDisplayKind(entry);
      const type = String(entry.type || runtimeValue.type || '');
      const unit = entry.unit ? String(entry.unit) : '';
      const decimals = Number.isFinite(Number(entry.decimals)) ? Number(entry.decimals) : null;
      const rawValue = runtimeValue.value;

      if (display === 'time' && Number.isFinite(Number(rawValue))) {
        return formatRuntimeDurationMs(Number(rawValue));
      }
      if (type === 'bool') {
        return rawValue ? 'Actif' : 'Arret';
      }
      if (type === 'float' && Number.isFinite(Number(rawValue))) {
        const value = Number(rawValue);
        const text = formatRuntimeFloatValue(value, decimals);
        return unit ? (text + ' ' + unit) : text;
      }
      if ((type === 'int32' || type === 'uint32' || type === 'enum') && Number.isFinite(Number(rawValue))) {
        if (type === 'enum' && entry.enum && typeof entry.enum === 'object') {
          const enumKey = String(Number(rawValue));
          if (Object.prototype.hasOwnProperty.call(entry.enum, enumKey)) {
            return String(entry.enum[enumKey]);
          }
        }
        const text = String(Number(rawValue));
        return unit ? (text + ' ' + unit) : text;
      }
      if (type === 'string') {
        const text = (rawValue === null || rawValue === undefined || rawValue === '') ? '-' : String(rawValue);
        return unit ? (text + ' ' + unit) : text;
      }
      if (rawValue === null || rawValue === undefined || rawValue === '') return '-';
      return unit ? (String(rawValue) + ' ' + unit) : String(rawValue);
    }

    function runtimeMeasureDisplayKind(entry) {
      const explicit = String(entry && entry.display ? entry.display : '').trim();
      if (explicit === 'gauge' || explicit === 'circ-gauge') return 'threshold';
      if (explicit === 'threshold' || explicit === 'horiz-gauge' || explicit === 'badge' || explicit === 'boolean' || explicit === 'time' || explicit === 'value' || explicit === 'flags') {
        return explicit;
      }
      return String(entry && entry.type ? entry.type : '') === 'bool' ? 'boolean' : 'value';
    }

    function runtimeMeasureDisplayConfig(entry) {
      return (entry && entry.displayConfig && typeof entry.displayConfig === 'object' && !Array.isArray(entry.displayConfig))
        ? entry.displayConfig
        : {};
    }

    function buildRuntimeMeasureThresholdNode(entry, runtimeValue) {
      const displayConfig = runtimeMeasureDisplayConfig(entry);
      let bands = [];
      let min = Number(displayConfig.min);
      let max = Number(displayConfig.max);
      if (displayConfig.bands && typeof displayConfig.bands === 'object' && !Array.isArray(displayConfig.bands)) {
        bands = createFlowFiveZoneBands(displayConfig.bands);
        if (!Number.isFinite(min)) min = Number(displayConfig.bands.min);
        if (!Number.isFinite(max)) max = Number(displayConfig.bands.max);
      } else if (Array.isArray(displayConfig.bands)) {
        bands = displayConfig.bands;
      }
      if (!Number.isFinite(min) || !Number.isFinite(max) || max <= min) {
        return null;
      }

      const value = (!runtimeValue || runtimeValue.status === 'not_found' || runtimeValue.status === 'unavailable')
        ? null
        : runtimeValue.value;

      return buildFlowThresholdValueNode({
        value: value,
        min: min,
        max: max,
        unit: entry.unit ? String(entry.unit) : '',
        decimals: Number.isFinite(Number(entry.decimals)) ? Number(entry.decimals) : 0,
        bands: bands
      });
    }

    function buildRuntimeMeasureHorizGaugeNode(entry, runtimeValue) {
      const hasValue = !!runtimeValue && runtimeValue.status !== 'not_found' && runtimeValue.status !== 'unavailable' &&
        Number.isFinite(Number(runtimeValue.value));
      return buildFlowRssiGauge(hasValue ? Number(runtimeValue.value) : null, hasValue);
    }

    function buildRuntimeMeasureBooleanNode(entry, runtimeValue, options) {
      const displayConfig = runtimeMeasureDisplayConfig(entry);
      const opts = options && typeof options === 'object' ? options : {};
      const value = (!runtimeValue || runtimeValue.status === 'not_found' || runtimeValue.status === 'unavailable')
        ? null
        : (typeof runtimeValue.value === 'boolean' ? runtimeValue.value : null);
      const booleanTexts = (opts.booleanTexts && typeof opts.booleanTexts === 'object') ? opts.booleanTexts : {};

      return buildFlowReadonlyStateTile(
        String(runtimeMeasureResolvedLabel(entry, opts) || 'Etat'),
        value,
        {
          activeText: booleanTexts.activeText || displayConfig.activeText,
          inactiveText: booleanTexts.inactiveText || displayConfig.inactiveText,
          unknownText: booleanTexts.unknownText || displayConfig.unknownText
        }
      );
    }

    function buildRuntimeMeasureBadgeNode(entry, runtimeValue) {
      const displayConfig = runtimeMeasureDisplayConfig(entry);
      let text = formatRuntimeMeasureValue(entry, runtimeValue);
      if (String(entry.type || '') === 'bool') {
        if (!runtimeValue || runtimeValue.status === 'not_found' || runtimeValue.status === 'unavailable') {
          text = String(displayConfig.unknownText || 'Indisponible');
        } else {
          text = runtimeValue.value
            ? String(displayConfig.activeText || 'Actif')
            : String(displayConfig.inactiveText || 'Arret');
        }
      }
      const badge = document.createElement('span');
      badge.className = 'status-chip';
      const badgeLabel = String(displayConfig.badgeLabel || '').trim();
      badge.textContent = badgeLabel ? (badgeLabel + ' : ' + text) : text;
      return badge;
    }

    function runtimeMeasureFlagRole(entry) {
      const displayConfig = runtimeMeasureDisplayConfig(entry);
      const role = String(displayConfig.flagRole || '').trim().toLowerCase();
      if (role === 'active' || role === 'resettable' || role === 'condition') return role;
      return '';
    }

    function runtimeMeasureFlagColumnLabel(entry) {
      const displayConfig = runtimeMeasureDisplayConfig(entry);
      const explicit = String(displayConfig.columnLabel || '').trim();
      if (explicit) return explicit;
      const role = runtimeMeasureFlagRole(entry);
      if (role === 'active') return 'Act.';
      if (role === 'resettable') return 'Reset';
      if (role === 'condition') return 'Cond.';
      return runtimeMeasureDisplayLabel(entry);
    }

    function normalizeRuntimeMeasureFlags(entry) {
      const rawFlags = Array.isArray(entry && entry.flags) ? entry.flags : [];
      return rawFlags
        .map((flag, index) => {
          const mask = Number(flag && flag.mask);
          const label = String(flag && flag.label ? flag.label : '').trim();
          if (!Number.isFinite(mask) || mask <= 0 || !label) return null;
          return {
            mask: Math.trunc(mask),
            label,
            order: Number.isFinite(Number(flag && flag.order)) ? Number(flag.order) : index
          };
        })
        .filter((flag) => !!flag)
        .sort((left, right) => left.order - right.order);
    }

    function runtimeMeasureMaskValue(runtimeValue) {
      if (!runtimeValue || runtimeValue.status === 'not_found' || runtimeValue.status === 'unavailable') {
        return null;
      }
      const value = Number(runtimeValue.value);
      if (!Number.isFinite(value)) return null;
      return Math.trunc(value);
    }

    function mergeRuntimeAlarmFlags(entries) {
      const byMask = new Map();

      (entries || []).forEach((entry) => {
        normalizeRuntimeMeasureFlags(entry).forEach((flag) => {
          if (!flag || !Number.isFinite(Number(flag.mask))) return;
          const mask = Math.trunc(Number(flag.mask));
          if (mask <= 0) return;
          const existing = byMask.get(mask);
          if (!existing) {
            byMask.set(mask, flag);
            return;
          }
          if ((!existing.label || !existing.label.trim()) && flag.label && flag.label.trim()) {
            byMask.set(mask, flag);
          }
        });
      });

      return Array.from(byMask.values())
        .sort((left, right) => {
          const leftOrder = Number.isFinite(Number(left && left.order)) ? Number(left.order) : 9999;
          const rightOrder = Number.isFinite(Number(right && right.order)) ? Number(right.order) : 9999;
          if (leftOrder !== rightOrder) return leftOrder - rightOrder;
          return Number(left.mask) - Number(right.mask);
        });
    }

    function buildRuntimeMeasureFlagCell(value, label, columnLabel) {
      const known = typeof value === 'boolean';
      const state = known ? (value ? 'is-true' : 'is-false') : 'is-empty';
      const marker = document.createElement('span');
      marker.className = 'status-flag-check ' + state;
      marker.textContent = known ? (value ? iconCheckText() : '') : '?';
      marker.setAttribute(
        'aria-label',
        label + ' / ' + columnLabel + ' : ' + (known ? (value ? 'oui' : 'non') : 'indisponible')
      );
      return marker;
    }

    function isRuntimeAlarmGroup(group) {
      if (!group) return false;
      if (String(group.domainKey || '').trim().toLowerCase() !== 'alarm') return false;
      return String(group.groupKey || '').trim().localeCompare('Alarmes', 'fr', { sensitivity: 'base' }) === 0;
    }

    function buildRuntimeAlarmStateNode(value) {
      const known = typeof value === 'boolean';
      const node = document.createElement('div');
      node.className = 'status-alarm-slot-state ' + (known ? (value ? 'is-true' : 'is-false') : 'is-empty');

      const dot = document.createElement('span');
      dot.className = 'status-state-dot';
      node.appendChild(dot);

      const text = document.createElement('span');
      text.className = 'status-alarm-slot-state-text';
      text.textContent = known ? (value ? 'Déclenchée' : 'OK') : 'Indispo';
      node.appendChild(text);
      return node;
    }

    function buildRuntimeAlarmConditionNode(value) {
      const known = typeof value === 'boolean';
      const node = document.createElement('div');
      node.className = 'status-alarm-slot-condition ' + (known ? (value ? 'is-true' : 'is-false') : 'is-empty');
      node.textContent = known ? ('Statut: ' + (value ? 'KO' : 'OK')) : 'Statut: ?';
      return node;
    }

    function buildRuntimeAlarmGrid(entries, valueById) {
      const columnsByRole = new Map();
      const flagDefs = mergeRuntimeAlarmFlags(entries);

      (entries || []).forEach((entry) => {
        const role = runtimeMeasureFlagRole(entry);
        if (!role || columnsByRole.has(role)) return;
        columnsByRole.set(role, entry);
      });

      const activeEntry = columnsByRole.get('active') || null;
      const conditionEntry = columnsByRole.get('condition') || null;
      if (!flagDefs.length || (!activeEntry && !conditionEntry)) return null;

      const activeMaskValue = activeEntry ? runtimeMeasureMaskValue(valueById.get(Number(activeEntry.id))) : null;
      const conditionMaskValue = conditionEntry ? runtimeMeasureMaskValue(valueById.get(Number(conditionEntry.id))) : null;
      const maxSlots = 8;

      const grid = document.createElement('div');
      grid.className = 'status-alarm-slot-grid';

      for (let index = 0; index < maxSlots; index += 1) {
        const flag = flagDefs[index] || null;
        const tile = document.createElement('div');
        tile.className = 'status-alarm-slot';

        if (!flag) {
          tile.classList.add('is-empty');
          grid.appendChild(tile);
          continue;
        }

        const title = document.createElement('div');
        title.className = 'status-alarm-slot-title';
        title.textContent = flag.label;
        tile.appendChild(title);

        const footer = document.createElement('div');
        footer.className = 'status-alarm-slot-row';

        const activeValue = activeMaskValue === null ? null : ((activeMaskValue & flag.mask) !== 0);
        const conditionValue = conditionMaskValue === null ? null : ((conditionMaskValue & flag.mask) !== 0);
        footer.appendChild(buildRuntimeAlarmStateNode(activeValue));
        footer.appendChild(buildRuntimeAlarmConditionNode(conditionValue));

        tile.appendChild(footer);
        grid.appendChild(tile);
      }

      return grid;
    }

    function buildPoolAlarmSlotsGrid(slots) {
      const cleanSlots = Array(8).fill(null);
      if (Array.isArray(slots)) {
        slots.forEach((slot) => {
          const idx = Number(slot && slot.slot);
          if (Number.isInteger(idx) && idx >= 0 && idx < 8) cleanSlots[idx] = slot;
        });
      }

      const grid = document.createElement('div');
      grid.className = 'status-alarm-slot-grid';

      for (let i = 0; i < 8; i += 1) {
        const slot = cleanSlots[i] || null;
        const tile = document.createElement('div');
        tile.className = 'status-alarm-slot';
        const enabled = !!(slot && slot.enabled);
        if (!enabled) {
          tile.classList.add('is-empty');
          grid.appendChild(tile);
          continue;
        }

        const bgColor = slot && isValidHexColor(slot.bgColor) ? slot.bgColor : '';
        if (bgColor) tile.style.background = bgColor;

        const title = document.createElement('div');
        title.className = 'status-alarm-slot-title';
        title.textContent = slot && slot.label ? slot.label : 'Alarme';
        tile.appendChild(title);

        const footer = document.createElement('div');
        footer.className = 'status-alarm-slot-row';
        footer.appendChild(buildRuntimeAlarmStateNode(slot && slot.available ? !!slot.latched : null));
        footer.appendChild(buildRuntimeAlarmConditionNode(slot && slot.available && slot.conditionKnown ? !!slot.conditionTrue : null));
        tile.appendChild(footer);
        grid.appendChild(tile);
      }

      return grid;
    }

    function buildRuntimeMeasureFlagsTable(entries, valueById) {
      const columnsByRole = new Map();
      let flagDefs = [];

      (entries || []).forEach((entry) => {
        const role = runtimeMeasureFlagRole(entry);
        if (!role || columnsByRole.has(role)) return;
        columnsByRole.set(role, entry);
        if (!flagDefs.length) {
          flagDefs = normalizeRuntimeMeasureFlags(entry);
        }
      });

      if (!flagDefs.length || columnsByRole.size === 0) return null;

      const orderedColumns = ['active', 'condition']
        .map((role) => ({ role, entry: columnsByRole.get(role) || null }))
        .filter((column) => !!column.entry);
      if (!orderedColumns.length) return null;

      const table = document.createElement('table');
      table.className = 'status-flag-table';

      const thead = document.createElement('thead');
      const headRow = document.createElement('tr');
      const nameHead = document.createElement('th');
      nameHead.scope = 'col';
      nameHead.textContent = 'Alarme';
      headRow.appendChild(nameHead);
      orderedColumns.forEach((column) => {
        const th = document.createElement('th');
        th.scope = 'col';
        th.textContent = runtimeMeasureFlagColumnLabel(column.entry);
        headRow.appendChild(th);
      });
      thead.appendChild(headRow);
      table.appendChild(thead);

      const tbody = document.createElement('tbody');
      flagDefs.forEach((flag) => {
        const row = document.createElement('tr');
        const labelCell = document.createElement('th');
        labelCell.scope = 'row';
        labelCell.textContent = flag.label;
        row.appendChild(labelCell);

        orderedColumns.forEach((column) => {
          const runtimeValue = valueById.get(Number(column.entry.id));
          const maskValue = runtimeMeasureMaskValue(runtimeValue);
          const cell = document.createElement('td');
          const enabled = maskValue === null ? null : ((maskValue & flag.mask) !== 0);
          cell.appendChild(buildRuntimeMeasureFlagCell(enabled, flag.label, runtimeMeasureFlagColumnLabel(column.entry)));
          row.appendChild(cell);
        });

        tbody.appendChild(row);
      });
      table.appendChild(tbody);
      return table;
    }

    function buildPoolMeasureCards(entries, values, options) {
      const fragment = document.createDocumentFragment();
      const opts = options && typeof options === 'object' ? options : {};
      const sondeSlots = Array.isArray(opts.sondeSlots) ? opts.sondeSlots : [];
      const alarmSlots = Array.isArray(opts.alarmSlots) ? opts.alarmSlots : [];
      const valueById = new Map();
      (values || []).forEach((item) => {
        const id = Number(item && item.id);
        if (Number.isFinite(id)) valueById.set(id, item);
      });

      const groups = [];
      const groupsByName = new Map();
      (entries || []).forEach((entry) => {
        const domainKey = String(entry.domain || 'runtime');
        const groupKey = String(entry.group || '').trim();
        const cardKey = domainKey + '::' + groupKey;
        const cardTitle = formatRuntimeGroupCardTitle(domainKey, groupKey);
        let group = groupsByName.get(cardKey);
        if (!group) {
          group = { name: cardTitle, domainKey, groupKey, entries: [] };
          groupsByName.set(cardKey, group);
          groups.push(group);
        }
        group.entries.push(entry);
      });

      groups.forEach((group) => {
        const card = document.createElement('div');
        card.className =
          'status-card status-card-runtime'
          + ' status-card-runtime-domain-' + runtimeMeasureCssSlug(group.domainKey)
          + ' status-card-runtime-group-' + runtimeMeasureCssSlug(group.groupKey);
        const isPoolModeGroup =
          String(group.domainKey || '').trim().toLowerCase() === 'mode' &&
          String(group.groupKey || '').trim().localeCompare('Mode', 'fr', { sensitivity: 'base' }) === 0;
        const isPoolSondesGroup = isPoolSondesGroupKey(group.domainKey, group.groupKey);
        const groupDisplayOptions = {
          displayLabelResolver: (entry) => runtimeMeasureDisplayLabel(entry),
          booleanTexts: isPoolModeGroup
            ? {
              activeText: 'Marche',
              inactiveText: 'Arrêt'
            }
            : null
        };

        const heading = document.createElement('h3');
        heading.textContent = group.name;
        card.appendChild(heading);

        if (isPoolSondesGroup) {
          card.appendChild(buildPoolSondeSlotsGrid(sondeSlots));
          fragment.appendChild(card);
          return;
        }

        const badgeNodes = [];
        const horizGaugeRows = [];
        const booleanNodes = [];
        const flagEntries = [];
        const valueRows = [];

        group.entries.forEach((entry) => {
          const runtimeValue = valueById.get(Number(entry.id));
          const display = runtimeMeasureDisplayKind(entry);
          if (display === 'badge') {
            badgeNodes.push(buildRuntimeMeasureBadgeNode(entry, runtimeValue));
            return;
          }
          if (display === 'threshold') {
            const thresholdNode = buildRuntimeMeasureThresholdNode(entry, runtimeValue);
            if (thresholdNode) {
              valueRows.push([
                runtimeMeasureResolvedLabel(entry, groupDisplayOptions),
                thresholdNode
              ]);
              return;
            }
          }
          if (display === 'horiz-gauge') {
            horizGaugeRows.push([
              runtimeMeasureResolvedLabel(entry, groupDisplayOptions),
              buildRuntimeMeasureHorizGaugeNode(entry, runtimeValue)
            ]);
            return;
          }
          if (display === 'boolean') {
            booleanNodes.push(buildRuntimeMeasureBooleanNode(entry, runtimeValue, groupDisplayOptions));
            return;
          }
          if (display === 'flags') {
            flagEntries.push(entry);
            return;
          }
          const thresholdNode = buildRuntimeMeasureThresholdNode(entry, runtimeValue);
          valueRows.push([
            runtimeMeasureResolvedLabel(entry, groupDisplayOptions),
            thresholdNode || formatRuntimeMeasureValue(entry, runtimeValue)
          ]);
        });

        if (badgeNodes.length) {
          const badgeRow = document.createElement('div');
          badgeRow.className = 'status-chip-row';
          badgeNodes.forEach((node) => badgeRow.appendChild(node));
          card.appendChild(badgeRow);
        }

        if (booleanNodes.length) {
          const stateGrid = buildFlowReadonlyStateGrid(booleanNodes);
          if (stateGrid) card.appendChild(stateGrid);
        }

        if (flagEntries.length) {
          if (isRuntimeAlarmGroup(group)) {
            const alarmGrid = alarmSlots.length
              ? buildPoolAlarmSlotsGrid(alarmSlots)
              : buildRuntimeAlarmGrid(flagEntries, valueById);
            if (alarmGrid) {
              card.appendChild(alarmGrid);
            } else {
              const flagTable = buildRuntimeMeasureFlagsTable(flagEntries, valueById);
              if (flagTable) card.appendChild(flagTable);
            }
          } else {
            const flagTable = buildRuntimeMeasureFlagsTable(flagEntries, valueById);
            if (flagTable) card.appendChild(flagTable);
          }
        }

        if (horizGaugeRows.length || valueRows.length) {
          const kv = document.createElement('div');
          kv.className = 'status-kv';
          horizGaugeRows.forEach((row) => appendFlowStatusRow(kv, row[0], row[1]));
          valueRows.forEach((row) => appendFlowStatusRow(kv, row[0], row[1]));
          card.appendChild(kv);
        }

        fragment.appendChild(card);
      });
      return fragment;
    }

    function renderPoolMeasureDomainButtons() {
      if (!poolMeasuresDomains) return;
      poolMeasuresDomains.innerHTML = '';
      runtimeMeasureDomainKeys.forEach((domainKey) => {
        const state = poolMeasureDomainState[domainKey];
        const animation = takePoolMeasureDomainAnimation(domainKey);
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'measure-domain-chip'
          + (state.active ? ' active' : '')
          + (state.loading ? ' is-loading' : '')
          + (animation ? ' is-pulsing' : '')
          + (animation && animation.activating ? ' is-activating' : '');
        button.setAttribute('aria-pressed', state.active ? 'true' : 'false');
        button.setAttribute('aria-label', (state.active ? 'Masquer ' : 'Afficher ') + formatRuntimeDomainLabel(domainKey));
        if (animation) {
          button.style.setProperty('--measure-ripple-x', animation.x);
          button.style.setProperty('--measure-ripple-y', animation.y);
        }

        const check = document.createElement('span');
        check.className = 'measure-domain-chip-check';
        check.setAttribute('aria-hidden', 'true');
        check.textContent = iconCheckText();
        button.appendChild(check);

        const label = document.createElement('span');
        label.className = 'measure-domain-chip-label';
        label.textContent = formatRuntimeDomainLabel(domainKey);
        button.appendChild(label);

        button.addEventListener('pointerdown', () => {
          button.classList.add('is-pressing');
        });
        ['pointerup', 'pointerleave', 'pointercancel', 'blur'].forEach((eventName) => {
          button.addEventListener(eventName, () => {
            button.classList.remove('is-pressing');
          });
        });
        button.addEventListener('click', async (event) => {
          primePoolMeasureDomainAnimation(domainKey, event, !state.active);
          await togglePoolMeasureDomain(domainKey);
        });
        poolMeasuresDomains.appendChild(button);
      });
    }

    function primePoolMeasureDomainAnimation(domainKey, event, activating) {
      const cleanDomain = normalizeRuntimeMeasureDomainKey(domainKey);
      if (!cleanDomain) return;
      let x = '50%';
      let y = '50%';
      const target = event && event.currentTarget instanceof HTMLElement ? event.currentTarget : null;
      if (target) {
        const rect = target.getBoundingClientRect();
        const clientX = typeof event.clientX === 'number' ? event.clientX : rect.left + (rect.width / 2);
        const clientY = typeof event.clientY === 'number' ? event.clientY : rect.top + (rect.height / 2);
        const ratioX = Math.max(0, Math.min(100, ((clientX - rect.left) / Math.max(rect.width, 1)) * 100));
        const ratioY = Math.max(0, Math.min(100, ((clientY - rect.top) / Math.max(rect.height, 1)) * 100));
        x = ratioX.toFixed(1) + '%';
        y = ratioY.toFixed(1) + '%';
      }
      poolMeasureDomainAnimations[cleanDomain] = {
        until: Date.now() + 720,
        activating: !!activating,
        rendered: false,
        x,
        y
      };
    }

    function takePoolMeasureDomainAnimation(domainKey) {
      const cleanDomain = normalizeRuntimeMeasureDomainKey(domainKey);
      if (!cleanDomain) return null;
      const animation = poolMeasureDomainAnimations[cleanDomain];
      if (!animation) return null;
      if (animation.until <= Date.now()) {
        delete poolMeasureDomainAnimations[cleanDomain];
        return null;
      }
      if (animation.rendered) return null;
      animation.rendered = true;
      return animation;
    }

    function poolMeasureDomainHasRenderableData(domainKey, state) {
      const cleanDomain = normalizeRuntimeMeasureDomainKey(domainKey);
      const domainState = state && typeof state === 'object' ? state : null;
      if (!cleanDomain || !domainState) return false;
      if (cleanDomain === 'sondes' && Array.isArray(domainState.sondeSlots) && domainState.sondeSlots.length > 0) return true;
      if (cleanDomain === 'alarm' && Array.isArray(domainState.alarmSlots) && domainState.alarmSlots.length > 0) return true;
      return Array.isArray(domainState.entries) && domainState.entries.length > 0;
    }

    function renderPoolMeasuresGrid() {
      if (!poolMeasuresGrid) return;
      poolMeasuresGrid.innerHTML = '';

      const activeDomains = activePoolMeasureDomainKeys();
      if (!activeDomains.length) {
        const empty = document.createElement('div');
        empty.className = 'measure-domain-empty';
        empty.textContent = tr('dashboard.empty.activateBadge', 'Activez un badge pour charger un domaine.');
        poolMeasuresGrid.appendChild(empty);
        return;
      }

      let renderedCardCount = 0;
      activeDomains.forEach((domainKey) => {
        const state = poolMeasureDomainState[domainKey];
        const hasRenderableData = poolMeasureDomainHasRenderableData(domainKey, state);
        if (state.loading && !hasRenderableData) {
          const card = document.createElement('div');
          card.className = 'status-card';
          const heading = document.createElement('h3');
          heading.textContent = formatRuntimeDomainLabel(domainKey);
          const summary = document.createElement('p');
          summary.className = 'status-card-summary';
          summary.textContent = tr('dashboard.loading', 'Chargement en cours...');
          card.appendChild(heading);
          card.appendChild(summary);
          poolMeasuresGrid.appendChild(card);
          renderedCardCount += 1;
          return;
        }
        if (state.error && !hasRenderableData) {
          const card = document.createElement('div');
          card.className = 'status-card';
          const heading = document.createElement('h3');
          heading.textContent = formatRuntimeDomainLabel(domainKey);
          const summary = document.createElement('p');
          summary.className = 'status-card-summary';
          summary.textContent = state.error;
          card.appendChild(heading);
          card.appendChild(summary);
          poolMeasuresGrid.appendChild(card);
          renderedCardCount += 1;
          return;
        }
        if (!state.entries.length) {
          const card = document.createElement('div');
          card.className = 'status-card';
          const heading = document.createElement('h3');
          heading.textContent = formatRuntimeDomainLabel(domainKey);
          const summary = document.createElement('p');
          summary.className = 'status-card-summary';
          summary.textContent = tr('dashboard.empty.domainNoRuntime', 'Aucune valeur runtime exposee pour ce domaine.');
          card.appendChild(heading);
          card.appendChild(summary);
          poolMeasuresGrid.appendChild(card);
          renderedCardCount += 1;
          return;
        }
        const cards = buildPoolMeasureCards(state.entries, state.values, {
          sondeSlots: state.sondeSlots,
          alarmSlots: state.alarmSlots
        });
        renderedCardCount += cards.childNodes.length;
        poolMeasuresGrid.appendChild(cards);
      });

      if (renderedCardCount === 0) {
        const empty = document.createElement('div');
        empty.className = 'measure-domain-empty';
        empty.textContent = tr('dashboard.empty.activeDomainsNoRuntime', 'Aucune valeur runtime disponible pour les domaines actifs.');
        poolMeasuresGrid.appendChild(empty);
      }
    }

    function refreshPoolMeasuresStatus() {
      if (!poolMeasuresStatus) return;
      const activeDomains = activePoolMeasureDomainKeys();
      const domainLabel = (count) => count > 1
        ? tr('dashboard.status.domains.plural', 'Domaines')
        : tr('dashboard.status.domains.singular', 'Domaine');
      const valueLabel = (count) => count > 1
        ? tr('dashboard.status.values.plural', 'Valeurs')
        : tr('dashboard.status.values.singular', 'Valeur');
      if (!activeDomains.length) {
        poolMeasuresStatus.textContent =
          tr('dashboard.status.domains.singular', 'Domaine') + ': 0 | ' +
          tr('dashboard.status.values.singular', 'Valeur') + ': 0';
        return;
      }

      let loadingCount = 0;
      let errorCount = 0;
      let valueCount = 0;
      activeDomains.forEach((domainKey) => {
        const state = poolMeasureDomainState[domainKey];
        if (state.loading) loadingCount += 1;
        if (state.error) errorCount += 1;
        valueCount += state.entries.length;
        if (domainKey === 'sondes') {
          valueCount += Array.isArray(state.sondeSlots) ? state.sondeSlots.length : 0;
          valueCount += Array.isArray(state.alarmSlots) ? state.alarmSlots.length : 0;
        }
      });

      if (loadingCount > 0) {
        poolMeasuresStatus.textContent =
          tr('dashboard.status.loading', 'Chargement') + ': ' +
          loadingCount + ' ' +
          tr(loadingCount > 1 ? 'dashboard.status.domainWord.plural' : 'dashboard.status.domainWord.singular', loadingCount > 1 ? 'domaines' : 'domaine');
        return;
      }
      if (errorCount > 0) {
        poolMeasuresStatus.textContent =
          tr('dashboard.status.errors', 'Erreur(s)') + ': ' +
          errorCount + ' ' +
          tr(errorCount > 1 ? 'dashboard.status.domainWord.plural' : 'dashboard.status.domainWord.singular', errorCount > 1 ? 'domaines' : 'domaine');
        return;
      }
      poolMeasuresStatus.textContent =
        domainLabel(activeDomains.length) + ': ' + activeDomains.length + ' | ' +
        valueLabel(valueCount) + ': ' + valueCount;
    }

    function refreshPoolMeasuresView() {
      renderPoolMeasureDomainButtons();
      renderPoolMeasuresGrid();
      refreshPoolMeasuresStatus();
    }

    async function loadPoolMeasureDomain(domainKey, forceRefresh) {
      const cleanDomain = normalizeRuntimeMeasureDomainKey(domainKey);
      if (!cleanDomain) return;
      const state = poolMeasureDomainState[cleanDomain];
      if (!state.active) return;
      const hadRenderableData = poolMeasureDomainHasRenderableData(cleanDomain, state);
      const requestSeq = state.requestSeq + 1;
      state.requestSeq = requestSeq;
      state.loading = true;
      state.error = '';
      if (hadRenderableData) {
        renderPoolMeasureDomainButtons();
        refreshPoolMeasuresStatus();
      } else {
        refreshPoolMeasuresView();
      }

      try {
        const allEntries = await runtimeMeasureEntriesForDomain(cleanDomain, !!forceRefresh);
        const entries = cleanDomain === 'sondes'
          ? allEntries.filter((entry) => !isPoolDashboardGroupEntry(entry))
          : allEntries;
        const ids = entries.map((entry) => Number(entry.id)).filter((id) => Number.isFinite(id));
        const values = ids.length ? await fetchRuntimeValues(ids) : [];
        const sondeSlots = cleanDomain === 'sondes'
          ? await fetchPoolSondeSlots().catch(() => [])
          : [];
        const alarmSlots = cleanDomain === 'alarm'
          ? await fetchPoolAlarmSlots().catch(() => [])
          : [];
        if (state.requestSeq !== requestSeq) return;
        state.entries = entries;
        state.values = values;
        state.sondeSlots = sondeSlots;
        state.alarmSlots = alarmSlots;
        state.error = '';
      } catch (err) {
        if (state.requestSeq !== requestSeq) return;
        if (!hadRenderableData) {
          state.entries = [];
          state.values = [];
          state.sondeSlots = [];
          state.alarmSlots = [];
        }
        state.error = 'Chargement ' + formatRuntimeDomainLabel(cleanDomain) + ' echoue: ' + err;
      } finally {
        if (state.requestSeq === requestSeq) {
          state.loading = false;
        }
        refreshPoolMeasuresView();
      }
    }

    async function refreshActivePoolMeasureDomains(forceRefresh) {
      const activeDomains = activePoolMeasureDomainKeys();
      if (!activeDomains.length) {
        refreshPoolMeasuresView();
        return;
      }
      for (const domainKey of activeDomains) {
        if (!poolMeasureDomainState[domainKey].active) continue;
        await loadPoolMeasureDomain(domainKey, !!forceRefresh);
      }
    }

    async function togglePoolMeasureDomain(domainKey) {
      const cleanDomain = normalizeRuntimeMeasureDomainKey(domainKey);
      if (!cleanDomain) return;
      const state = poolMeasureDomainState[cleanDomain];
      if (state.active) {
        state.active = false;
        state.loading = false;
        state.error = '';
        state.sondeSlots = [];
        state.requestSeq += 1;
        refreshPoolMeasuresView();
        return;
      }
      state.active = true;
      await loadPoolMeasureDomain(cleanDomain, false);
    }

    async function refreshPoolMeasures(forceRefresh) {
      await refreshActivePoolMeasureDomains(!!forceRefresh);
    }

    async function onPoolMeasuresPageShown() {
      refreshPoolMeasuresView();
      startPoolMeasuresTimer();
      if (activePoolMeasureDomainKeys().length) {
        try {
          await refreshActivePoolMeasureDomains(false);
        } catch (err) {
          showPoolMeasuresError(err);
        }
      }
    }

    function poolConfigDisinfectionLabel(value) {
      const n = Number(value);
      if (n === 0) return tr('pool.disinfection.chlorine.title', 'Chlore / Brome');
      if (n === 1) return tr('pool.disinfection.swg.title', 'Électrolyse');
      if (n === 2) return tr('pool.disinfection.o2.title', 'Oxygène actif');
      if (n === 3) return tr('pool.disinfection.disabled', 'Désactivé');
      return tr('pool.state.unknown', 'Inconnu');
    }

    function poolConfigBoolLabel(value, activeText, inactiveText) {
      return toBool(value) ? (activeText || tr('pool.state.active', 'Actif')) : (inactiveText || tr('pool.state.stopped', 'Arrêt'));
    }

    function poolConfigFormatHour(value) {
      const n = Number(value);
      if (!Number.isFinite(n)) return String(value ?? '-');
      return String(Math.max(0, Math.min(23, Math.trunc(n)))).padStart(2, '0') + ':00';
    }

    function poolConfigHourToMinutes(value) {
      const n = Number(value);
      if (!Number.isFinite(n)) return null;
      return Math.max(0, Math.min(23, Math.trunc(n))) * 60;
    }

    function poolConfigDayProgress(startValue, stopValue) {
      const start = poolConfigHourToMinutes(startValue);
      const stopRaw = poolConfigHourToMinutes(stopValue);
      if (start === null || stopRaw === null) return 0;
      let stop = stopRaw;
      const now = new Date();
      let current = now.getHours() * 60 + now.getMinutes();
      if (stop <= start) {
        stop += 24 * 60;
        if (current < start) current += 24 * 60;
      }
      if (current <= start) return 0;
      if (current >= stop) return 100;
      return Math.max(0, Math.min(100, ((current - start) / (stop - start)) * 100));
    }

    function poolConfigFormatDurationMs(value) {
      const n = Number(value);
      if (!Number.isFinite(n)) return String(value ?? '-');
      if (Math.abs(n) >= 3600000 && n % 3600000 === 0) return String(Math.round(n / 3600000)) + ' h';
      if (Math.abs(n) >= 60000 && n % 60000 === 0) return String(Math.round(n / 60000)) + ' min';
      if (Math.abs(n) >= 1000 && n % 1000 === 0) return String(Math.round(n / 1000)) + ' s';
      return String(Math.round(n)) + ' ms';
    }

    function poolConfigFormatNumber(value) {
      const n = Number(value);
      if (!Number.isFinite(n)) return String(value ?? '-');
      const rounded = Math.round(n * 1000) / 1000;
      const normalized = rounded.toFixed(3).replace(/(?:\.0+|(\.\d*?)0+)$/, '$1');
      return webUiLocale === 'en' ? normalized : normalized.replace('.', ',');
    }

    function poolConfigDoc(moduleName, key) {
      try {
        return configDocFor(moduleName, key, []);
      } catch (err) {
        return null;
      }
    }

    function poolConfigFieldLabel(moduleName, key) {
      const doc = poolConfigDoc(moduleName, key);
      return doc && typeof doc.label === 'string' && doc.label.trim()
        ? doc.label.trim()
        : String(key || '');
    }

    function poolConfigEnumLabel(doc, value) {
      const options = doc && Array.isArray(doc._enumOptions) ? doc._enumOptions : [];
      const current = String(value ?? '');
      for (const opt of options) {
        if (!opt || typeof opt !== 'object') continue;
        if (String(opt.value) === current) {
          return typeof opt.label === 'string' && opt.label.trim() ? opt.label.trim() : current;
        }
      }
      return '';
    }

    function poolConfigFormatValue(moduleName, key, value) {
      if (value === null || typeof value === 'undefined' || value === '') return '-';
      const doc = poolConfigDoc(moduleName, key);
      const enumLabel = poolConfigEnumLabel(doc, value);
      if (enumLabel) return enumLabel;
      if (typeof value === 'boolean') return poolConfigBoolLabel(value);
      const cleanKey = String(key || '').toLowerCase();
      const unit = String(doc && doc.unit ? doc.unit : '').trim();
      if (cleanKey.endsWith('_ms') || unit === 'ms') return poolConfigFormatDurationMs(value);
      if (cleanKey.includes('hour') || /^filtr_(?:start|stop)_/.test(cleanKey)) return poolConfigFormatHour(value);
      if (Number.isFinite(Number(value))) {
        const base = poolConfigFormatNumber(value);
        if (unit === 'C') return base + ' °C';
        if (unit === 'm3') return base + ' m³';
        return unit ? (base + ' ' + unit) : base;
      }
      return String(value);
    }

    function poolConfigAppendMetric(parent, label, value, options) {
      if (!parent) return;
      const opts = options || {};
      const item = document.createElement('div');
      item.className = 'pool-metric' + (opts.featured ? ' is-featured' : '');
      const labelEl = document.createElement('span');
      labelEl.className = 'pool-metric-label';
      labelEl.textContent = String(label || '');
      const valueEl = document.createElement('b');
      valueEl.className = 'pool-metric-value';
      valueEl.textContent = String(value ?? '-');
      item.appendChild(labelEl);
      item.appendChild(valueEl);
      parent.appendChild(item);
    }

    function poolConfigFields(moduleName, data, options) {
      const opts = options || {};
      const keys = Object.keys((data && typeof data === 'object') ? data : {})
        .filter((key) => !opts.exclude || opts.exclude.indexOf(key) < 0)
        .sort();
      const picked = Number.isFinite(Number(opts.limit)) ? keys.slice(0, Number(opts.limit)) : keys;
      return picked.map((key) => ({
        key,
        label: poolConfigFieldLabel(moduleName, key),
        value: poolConfigFormatValue(moduleName, key, data[key])
      }));
    }

    function poolConfigBuildFieldList(moduleName, data, options) {
      const list = document.createElement('div');
      list.className = 'pool-field-list';
      poolConfigFields(moduleName, data, options).forEach((field) => {
        const row = document.createElement('div');
        row.className = 'pool-field-row';
        const label = document.createElement('span');
        label.className = 'pool-field-label';
        label.textContent = field.label;
        const value = document.createElement('b');
        const activeText = tr('pool.state.active', 'Actif').trim().toLowerCase();
        const cleanValue = String(field.value || '').trim().toLowerCase();
        const isActiveValue = cleanValue === activeText || cleanValue === 'active' || cleanValue === 'actif';
        value.className = 'pool-field-value' + (isActiveValue ? ' has-active-dot' : '');
        const valueText = document.createElement('span');
        valueText.textContent = field.value;
        value.appendChild(valueText);
        if (isActiveValue) {
          const dot = document.createElement('span');
          dot.className = 'pool-field-active-dot';
          dot.setAttribute('aria-hidden', 'true');
          value.appendChild(dot);
        }
        row.appendChild(label);
        row.appendChild(value);
        list.appendChild(row);
      });
      if (!list.childNodes.length) {
        const empty = document.createElement('div');
        empty.className = 'pool-field-empty';
        empty.textContent = tr('pool.empty.settings', 'Aucun réglage disponible.');
        list.appendChild(empty);
      }
      return list;
    }

    async function poolConfigFetchModule(moduleName) {
      const cleanModule = nettoyerNomFlowCfg(moduleName);
      const data = await fetchOkJson(
        '/api/flowcfg/module?name=' + encodeURIComponent(cleanModule),
        { cache: 'no-store' },
        tr('pool.error.moduleRead', 'lecture {module} impossible').replace('{module}', cleanModule),
        fetchFlowRemoteQueued
      );
      return {
        module: cleanModule,
        data: (data && data.data && typeof data.data === 'object') ? data.data : {},
        truncated: !!(data && data.truncated)
      };
    }

    async function poolConfigEnsureDocs() {
      const modules = poolConfigModuleDefs.map((def) => def.module)
        .concat(poolDisinfectionModeDefs.map((def) => def.module));
      await ensureCfgDocsForModule('');
      for (const moduleName of modules) {
        await ensureCfgDocsForModule(moduleName).catch(() => {});
      }
    }

    function poolConfigRenderModeBadges(modules) {
      if (!poolModeBadges) return;
      poolModeBadges.innerHTML = '';
      const modes = modules['poollogic/modes'] || {};
      const items = [
        [tr('pool.badge.poollogic', 'PoolLogic'), poolConfigBoolLabel(modes.enabled, tr('pool.state.active', 'Actif'), tr('pool.state.disabled', 'Désactivé')), toBool(modes.enabled), 'bolt'],
        [tr('pool.badge.auto', 'Auto'), poolConfigBoolLabel(modes.auto_mode, tr('pool.state.automatic', 'Automatique'), tr('pool.state.manual', 'Manuel')), toBool(modes.auto_mode), 'settings'],
        [tr('pool.badge.winter', 'Hiver'), poolConfigBoolLabel(modes.winter_mode, tr('pool.state.forced', 'Forcé'), tr('pool.state.normal', 'Normal')), toBool(modes.winter_mode), 'ac_unit']
      ];
      items.forEach((item) => {
        const badge = document.createElement('span');
        badge.className = 'pool-mode-badge' + (item[2] ? ' is-on' : ' is-off');
        const icon = document.createElement('span');
        icon.className = 'ui-msr pool-mode-badge-icon';
        icon.setAttribute('aria-hidden', 'true');
        icon.textContent = item[3];
        const label = document.createElement('span');
        label.textContent = item[0] + ' · ' + item[1];
        badge.appendChild(icon);
        badge.appendChild(label);
        poolModeBadges.appendChild(badge);
      });
    }

    function poolConfigHeroSummary(modules, start, stop) {
      const source = modules && typeof modules === 'object' ? modules : {};
      const modes = source['poollogic/modes'] || {};
      const heater = source['poollogic/heater'] || {};
      const poolLogicEnabled = toBool(modes.enabled);
      const autoMode = toBool(modes.auto_mode);
      const winterMode = toBool(modes.winter_mode);
      const treatment = poolConfigDisinfectionLabel(modes.disinfection_type);
      const heaterState = poolConfigBoolLabel(heater.heater_auto_mode, tr('pool.state.autoShort', 'auto'), tr('pool.state.off', 'arrêt'));

      if (!poolLogicEnabled) {
        return tr('pool.summary.poollogicOff', 'PoolLogic désactivé : le système reste en manuel et ne pilote pas la piscine.');
      }
      if (!autoMode) {
        return tr('pool.summary.manual', 'Mode manuel : PoolLogic est actif, mais les automatismes sont suspendus. Traitement configuré : {treatment}.')
          .replace('{treatment}', treatment);
      }
      if (winterMode) {
        return tr('pool.summary.autoWinter', 'Mode automatique hiver : filtration {start}-{stop}, traitement {treatment}, chauffage {heater}.')
          .replace('{start}', start)
          .replace('{stop}', stop)
          .replace('{treatment}', treatment)
          .replace('{heater}', heaterState);
      }
      return tr('pool.summary.auto', 'Mode automatique : filtration {start}-{stop}, traitement {treatment}, chauffage {heater}.')
        .replace('{start}', start)
        .replace('{stop}', stop)
        .replace('{treatment}', treatment)
        .replace('{heater}', heaterState);
    }

    function poolConfigRenderHero(modules, alarmSlots) {
      const modes = modules['poollogic/modes'] || {};
      const filtration = modules['poollogic/filtration'] || {};
      const alarms = poolConfigActiveAlarms(alarmSlots);
      const startValue = filtration.filtr_start_clc ?? filtration.filtr_start_min;
      const stopValue = filtration.filtr_stop_clc ?? filtration.filtr_stop_max;
      const start = poolConfigFormatHour(startValue);
      const stop = poolConfigFormatHour(stopValue);
      if (poolConfigTitle) {
        poolConfigTitle.textContent = tr('pool.overview.title', 'État Général');
      }
      if (poolHeroState) {
        poolHeroState.className = 'pool-hero-state ' + (alarms.length ? 'is-alert' : 'is-ok');
        poolHeroState.innerHTML = '';
        const icon = document.createElement('span');
        icon.className = 'ui-msr pool-hero-state-icon';
        icon.setAttribute('aria-hidden', 'true');
        icon.textContent = alarms.length ? 'warning' : 'check_circle';
        const label = document.createElement('span');
        label.textContent = alarms.length
          ? tr('pool.state.alarmNamed', 'Alarme - {alarm}').replace('{alarm}', alarms[0].label)
          : tr('pool.state.normalStatus', 'État normal');
        poolHeroState.appendChild(icon);
        poolHeroState.appendChild(label);
      }
      if (poolConfigSummary) {
        poolConfigSummary.textContent = poolConfigHeroSummary(modules, start, stop);
      }
      if (poolFiltrationStart) poolFiltrationStart.textContent = start;
      if (poolFiltrationStop) poolFiltrationStop.textContent = stop;
      if (poolFiltrationFill) {
        poolFiltrationFill.style.width = poolConfigDayProgress(startValue, stopValue).toFixed(1) + '%';
      }
      poolConfigRenderModeBadges(modules);
    }

    function poolConfigRenderDisinfection(modules) {
      if (!poolDisinfectionModes) return;
      poolDisinfectionModes.innerHTML = '';
      const modes = modules['poollogic/modes'] || {};
      const selectedType = Number(modes.disinfection_type);
      const selectedDef = poolDisinfectionModeDefs.find((def) => selectedType === def.typeValue) || poolDisinfectionModeDefs[0];
      const selected = selectedType === selectedDef.typeValue;
      const data = selected ? (modules[selectedDef.module] || {}) : {};

      const selector = document.createElement('div');
      selector.className = 'pool-treatment-selector';
      const selectorHead = document.createElement('div');
      selectorHead.className = 'pool-treatment-title';
      const selectorIcon = document.createElement('span');
      selectorIcon.className = 'ui-msr pool-treatment-title-icon';
      selectorIcon.setAttribute('aria-hidden', 'true');
      selectorIcon.textContent = 'water_drop';
      const selectorTitle = document.createElement('h3');
      selectorTitle.textContent = tr('pool.treatment.title', 'Traitement de l’eau');
      selectorHead.appendChild(selectorIcon);
      selectorHead.appendChild(selectorTitle);
      selector.appendChild(selectorHead);

      const choiceGroup = document.createElement('div');
      choiceGroup.className = 'pool-treatment-choice-group';
      poolDisinfectionModeDefs.forEach((def) => {
        const choice = document.createElement('button');
        choice.type = 'button';
        choice.disabled = true;
        choice.className = 'pool-treatment-choice' + (selectedType === def.typeValue ? ' is-selected' : '');
        const choiceIcon = document.createElement('span');
        choiceIcon.className = 'ui-msr pool-treatment-choice-icon';
        choiceIcon.setAttribute('aria-hidden', 'true');
        choiceIcon.textContent = def.icon;
        const choiceLabel = document.createElement('span');
        choiceLabel.textContent = tr(def.titleKey, def.title);
        choice.appendChild(choiceIcon);
        choice.appendChild(choiceLabel);
        choiceGroup.appendChild(choice);
      });
      selector.appendChild(choiceGroup);

      const detail = document.createElement('article');
      detail.className = 'pool-treatment-detail is-' + selectedDef.accent;
      const head = document.createElement('div');
      head.className = 'pool-card-head';
      const icon = document.createElement('span');
      icon.className = 'ui-msr pool-card-icon';
      icon.setAttribute('aria-hidden', 'true');
      icon.textContent = selectedDef.icon;
      const copy = document.createElement('div');
      copy.className = 'pool-card-title-wrap';
      const title = document.createElement('h3');
      title.textContent = selected ? tr(selectedDef.titleKey, selectedDef.title) : poolConfigDisinfectionLabel(selectedType);
      const note = document.createElement('p');
      note.textContent = selected
        ? tr(selectedDef.noteKey, selectedDef.note)
        : tr('pool.disinfection.disabled.note', 'Aucun traitement de désinfection n’est sélectionné.');
      copy.appendChild(title);
      copy.appendChild(note);
      head.appendChild(icon);
      head.appendChild(copy);
      detail.appendChild(head);

      const metrics = document.createElement('div');
      metrics.className = 'pool-metric-grid';
      if (selected) {
        if (selectedDef.key === 'chlorine') {
          poolConfigAppendMetric(metrics, tr('pool.metric.autoOrp', 'Auto ORP'), poolConfigBoolLabel(data.dis_auto_mode));
          poolConfigAppendMetric(metrics, tr('pool.metric.setpoint', 'Consigne'), poolConfigFormatValue(selectedDef.module, 'dis_setpoint', data.dis_setpoint), { featured: true });
          poolConfigAppendMetric(metrics, tr('pool.metric.window', 'Fenêtre'), poolConfigFormatValue(selectedDef.module, 'dis_window_ms', data.dis_window_ms));
        } else if (selectedDef.key === 'swg') {
          poolConfigAppendMetric(metrics, tr('pool.metric.control', 'Contrôle'), poolConfigFormatValue(selectedDef.module, 'swg_control_mode', data.swg_control_mode), { featured: true });
          poolConfigAppendMetric(metrics, tr('pool.metric.delay', 'Délai'), poolConfigFormatValue(selectedDef.module, 'dly_electro_min', data.dly_electro_min));
          poolConfigAppendMetric(metrics, tr('pool.metric.waterSafety', 'Sécurité eau'), poolConfigFormatValue(selectedDef.module, 'secure_elec_t', data.secure_elec_t));
        } else if (selectedDef.key === 'o2') {
          poolConfigAppendMetric(metrics, tr('pool.metric.poolVolume', 'Volume bassin'), poolConfigFormatValue(selectedDef.module, 'pool_volume_m3', data.pool_volume_m3), { featured: true });
          poolConfigAppendMetric(metrics, tr('pool.metric.weeklyDose', 'Dose hebdo'), poolConfigFormatValue(selectedDef.module, 'dose_ml_10m3_week', data.dose_ml_10m3_week));
          poolConfigAppendMetric(metrics, tr('pool.metric.injections', 'Injections'), poolConfigFormatValue(selectedDef.module, 'split_count', data.split_count));
          poolConfigAppendMetric(metrics, tr('pool.metric.pending', 'En attente'), poolConfigFormatValue(selectedDef.module, 'pending_ml', data.pending_ml));
        }
      }
      detail.appendChild(metrics);

      poolDisinfectionModes.appendChild(selector);
      poolDisinfectionModes.appendChild(detail);
    }

    function poolConfigActiveAlarms(alarmSlots) {
      return (Array.isArray(alarmSlots) ? alarmSlots : [])
        .filter((slot) => {
          if (!slot || slot.enabled === false) return false;
          return slot.conditionTrue === true || slot.latched === true;
        })
        .map((slot) => {
          const label = String(slot.label || '').trim() || tr('pool.alarm.defaultLabel', 'Alarme piscine');
          const state = slot.conditionTrue === true
            ? tr('pool.alarm.state.activeCondition', 'condition active')
            : tr('pool.alarm.state.latched', 'alarme mémorisée');
          return { label, state };
        });
    }

    function poolConfigRenderAlarms(alarmSlots) {
      if (!poolAlarmCard) return;
      const alarms = poolConfigActiveAlarms(alarmSlots);
      poolAlarmCard.innerHTML = '';
      poolAlarmCard.hidden = alarms.length === 0;
      if (alarms.length === 0) return;

      const head = document.createElement('div');
      head.className = 'pool-alarm-head';
      const icon = document.createElement('span');
      icon.className = 'ui-msr pool-alarm-icon';
      icon.setAttribute('aria-hidden', 'true');
      icon.textContent = 'warning';
      const titleWrap = document.createElement('div');
      titleWrap.className = 'pool-alarm-title-wrap';
      const title = document.createElement('h3');
      title.textContent = alarms.length > 1 ? tr('pool.alarm.title.plural', 'Alarmes piscine en cours') : tr('pool.alarm.title.singular', 'Alarme piscine en cours');
      const intro = document.createElement('p');
      intro.textContent = tr('pool.alarm.intro', 'PoolLogic signale une attention requise avant de laisser les automatismes fonctionner sans surveillance.');
      titleWrap.appendChild(title);
      titleWrap.appendChild(intro);
      head.appendChild(icon);
      head.appendChild(titleWrap);
      poolAlarmCard.appendChild(head);

      const list = document.createElement('div');
      list.className = 'pool-alarm-list';
      alarms.forEach((alarm) => {
        const row = document.createElement('div');
        row.className = 'pool-alarm-row';
        const label = document.createElement('b');
        label.textContent = alarm.label;
        const state = document.createElement('span');
        state.textContent = alarm.state;
        row.appendChild(label);
        row.appendChild(state);
        list.appendChild(row);
      });
      poolAlarmCard.appendChild(list);
    }

    function poolConfigRenderFiltrationCard(def, data) {
      const card = document.createElement('article');
      card.className = 'pool-config-card pool-filtration-card';

      const head = document.createElement('div');
      head.className = 'pool-card-head';
      const icon = document.createElement('span');
      icon.className = 'ui-msr pool-card-icon';
      icon.setAttribute('aria-hidden', 'true');
      icon.textContent = def.icon;
      const copy = document.createElement('div');
      copy.className = 'pool-card-title-wrap';
      const title = document.createElement('h3');
      title.textContent = tr(def.titleKey, def.title);
      copy.appendChild(title);
      head.appendChild(icon);
      head.appendChild(copy);
      card.appendChild(head);

      const start = poolConfigFormatHour(data.filtr_start_clc ?? data.filtr_start_min);
      const stop = poolConfigFormatHour(data.filtr_stop_clc ?? data.filtr_stop_max);
      const times = document.createElement('div');
      times.className = 'pool-filtration-times';
      const startEl = document.createElement('b');
      startEl.textContent = start;
      const arrow = document.createElement('span');
      arrow.className = 'ui-msr pool-filtration-arrow';
      arrow.setAttribute('aria-hidden', 'true');
      arrow.textContent = 'arrow_forward';
      const stopEl = document.createElement('b');
      stopEl.textContent = stop;
      times.appendChild(startEl);
      times.appendChild(arrow);
      times.appendChild(stopEl);
      card.appendChild(times);
      return card;
    }

    function poolConfigRenderGeneralCards(modules) {
      if (!poolConfigGrid) return;
      poolConfigGrid.innerHTML = '';
      const order = ['poollogic/heater', 'poollogic/safety', 'poollogic/regulation', 'poollogic/robot'];
      const orderedDefs = poolConfigModuleDefs.slice().sort((a, b) => {
        const ai = order.indexOf(a.module);
        const bi = order.indexOf(b.module);
        return (ai < 0 ? 999 : ai) - (bi < 0 ? 999 : bi);
      });
      orderedDefs.forEach((def) => {
        if (def.module === 'poollogic/modes' || def.module === 'poollogic/filtration' || def.module === 'poollogic/refill') return;
        const data = modules[def.module] || {};
        if (def.module === 'poollogic/filtration') {
          poolConfigGrid.appendChild(poolConfigRenderFiltrationCard(def, data));
          return;
        }
        const card = document.createElement('article');
        card.className = 'pool-config-card pool-config-card-' + runtimeMeasureCssSlug(def.module);

        const head = document.createElement('div');
        head.className = 'pool-card-head';
        const icon = document.createElement('span');
        icon.className = 'ui-msr pool-card-icon';
        icon.setAttribute('aria-hidden', 'true');
        icon.textContent = def.icon;
        const copy = document.createElement('div');
        copy.className = 'pool-card-title-wrap';
        const title = document.createElement('h3');
        title.textContent = tr(def.titleKey, def.title);
        const note = document.createElement('p');
        note.textContent = tr(def.noteKey, def.note);
        copy.appendChild(title);
        copy.appendChild(note);
        head.appendChild(icon);
        head.appendChild(copy);
        card.appendChild(head);
        card.appendChild(poolConfigBuildFieldList(def.module, data));
        poolConfigGrid.appendChild(card);
      });
    }

    function poolConfigRender(modules, alarmSlots) {
      const source = modules && typeof modules === 'object' ? modules : {};
      poolConfigRenderHero(source, alarmSlots);
      poolConfigRenderDisinfection(source);
      poolConfigRenderAlarms([]);
      poolConfigRenderGeneralCards(source);
    }

    function poolConfigRenderSkeleton() {
      if (poolDisinfectionModes) {
        poolDisinfectionModes.innerHTML = '';
        for (let i = 0; i < 2; i += 1) {
          const card = document.createElement('article');
          card.className = (i === 0 ? 'pool-treatment-selector' : 'pool-treatment-detail') + ' pool-config-skeleton';
          card.appendChild(createSkeletonLine('', 58));
          card.appendChild(createSkeletonLine('', 86));
          card.appendChild(createSkeletonLine('', 42));
          poolDisinfectionModes.appendChild(card);
        }
      }
      if (poolConfigGrid) {
        poolConfigGrid.innerHTML = '';
        for (let i = 0; i < 4; i += 1) {
          const card = document.createElement('article');
          card.className = 'pool-config-card pool-config-skeleton';
          card.appendChild(createSkeletonLine('', i % 2 === 0 ? 52 : 66));
          card.appendChild(createSkeletonLine('', 88));
          card.appendChild(createSkeletonLine('', 74));
          poolConfigGrid.appendChild(card);
        }
      }
      if (poolAlarmCard) {
        poolAlarmCard.hidden = true;
        poolAlarmCard.innerHTML = '';
      }
    }

    function poolConfigRenderError(err) {
      if (poolDisinfectionModes) poolDisinfectionModes.innerHTML = '';
      if (poolAlarmCard) {
        poolAlarmCard.hidden = true;
        poolAlarmCard.innerHTML = '';
      }
      if (!poolConfigGrid) return;
      poolConfigGrid.innerHTML = '';
      const card = document.createElement('article');
      card.className = 'pool-config-card pool-config-error-card';
      const head = document.createElement('div');
      head.className = 'pool-card-head';
      const icon = document.createElement('span');
      icon.className = 'ui-msr pool-card-icon';
      icon.setAttribute('aria-hidden', 'true');
      icon.textContent = 'error';
      const copy = document.createElement('div');
      copy.className = 'pool-card-title-wrap';
      const title = document.createElement('h3');
      title.textContent = tr('pool.error.title', 'Configuration piscine indisponible');
      const detail = document.createElement('p');
      detail.textContent = String(err || tr('pool.error.readFailed', 'Lecture de la configuration impossible.'));
      copy.appendChild(title);
      copy.appendChild(detail);
      head.appendChild(icon);
      head.appendChild(copy);
      card.appendChild(head);
      poolConfigGrid.appendChild(card);
    }

    async function loadPoolConfig(forceRefresh) {
      const reqSeq = ++poolConfigReqSeq;
      if (!poolConfigLoadedOnce || forceRefresh) poolConfigRenderSkeleton();
      if (poolConfigRefreshBtn) poolConfigRefreshBtn.disabled = true;
      try {
        await poolConfigEnsureDocs().catch(() => {});
        const modules = {};
        const allDefs = poolConfigModuleDefs.concat(poolDisinfectionModeDefs);
        for (const def of allDefs) {
          const payload = await poolConfigFetchModule(def.module);
          if (reqSeq !== poolConfigReqSeq) return;
          modules[payload.module] = payload.data;
        }
        const alarmSlots = await fetchPoolAlarmSlots().catch(() => []);
        if (reqSeq !== poolConfigReqSeq) return;
        poolConfigRender(modules, alarmSlots);
        poolConfigLoadedOnce = true;
      } catch (err) {
        if (reqSeq !== poolConfigReqSeq) return;
        poolConfigRenderError(err);
      } finally {
        if (reqSeq === poolConfigReqSeq && poolConfigRefreshBtn) {
          poolConfigRefreshBtn.disabled = false;
        }
      }
    }

    async function onPoolConfigPageShown(forceRefresh) {
      await loadPoolConfig(!!forceRefresh || !poolConfigLoadedOnce);
    }

    async function onUpgradePageShown() {
      renderUpgradeJourney(readUpgradeUiSession() || { phase: 'idle', target: '', detail: tr('updates.none', 'Aucune opération en cours.') });
      renderUpgradeCatalog();
      resumeUpgradeReconnectFlow();
      await loadWebMeta().catch(() => {});
      await refreshUpgradeStatus();
      startUpgradeStatusPolling();
    }

    function calibrationNormalizeSensorKey(rawKey) {
      const key = String(rawKey || '').trim();
      if (Object.prototype.hasOwnProperty.call(calibrationSensorDefs, key)) return key;
      return 'ph';
    }

    function calibrationSensorDef(sensorKey) {
      return calibrationSensorDefs[calibrationNormalizeSensorKey(sensorKey)];
    }

    function calibrationCurrentSensorDef() {
      const selected = calibrationSensorSelect ? calibrationSensorSelect.value : 'ph';
      return calibrationSensorDef(selected);
    }

    function calibrationParseNumberLoose(rawValue) {
      if (rawValue === null || typeof rawValue === 'undefined') return NaN;
      const normalized = String(rawValue).trim().replace(',', '.');
      if (!normalized) return NaN;
      const value = Number(normalized);
      return Number.isFinite(value) ? value : NaN;
    }

    function calibrationReadInputNumber(inputEl, label) {
      const value = calibrationParseNumberLoose(inputEl ? inputEl.value : '');
      if (!Number.isFinite(value)) {
        throw new Error(label + ' invalide');
      }
      return value;
    }

    function calibrationFormatNumber(value, maxDecimals) {
      const n = Number(value);
      if (!Number.isFinite(n)) return '-';
      const decimals = Math.max(0, Math.min(8, Number(maxDecimals)));
      const fixed = n.toFixed(Number.isFinite(decimals) ? decimals : 4);
      const trimmed = fixed.replace(/(\.\d*?[1-9])0+$/g, '$1').replace(/\.0+$/g, '');
      return trimmed.replace('.', ',');
    }

    function calibrationPatchNumber(value) {
      const n = Number(value);
      if (!Number.isFinite(n)) throw new Error(tr('calibration.err.invalidCoefficient', 'coefficient invalide'));
      return Number(n.toFixed(9));
    }

    function calibrationSetStatus(message, tone) {
      const text = String(message || '').trim() || tr('calibration.ready', 'Étalonnage prêt.');
      if (calibrationStatus) {
        calibrationStatus.textContent = text;
        calibrationStatus.classList.remove('is-ok', 'is-error', 'is-busy');
        if (tone === 'ok') calibrationStatus.classList.add('is-ok');
        else if (tone === 'error') calibrationStatus.classList.add('is-error');
        else if (tone === 'busy') calibrationStatus.classList.add('is-busy');
      }
      if (calibrationStatusChip) {
        if (tone === 'busy') {
          calibrationStatusChip.textContent = tr('calibration.chip.loading', 'Chargement');
        } else if (tone === 'error') {
          calibrationStatusChip.textContent = tr('calibration.chip.error', 'Erreur');
        } else if (tone === 'ok') {
          calibrationStatusChip.textContent = tr('calibration.chip.ok', 'OK');
        } else {
          calibrationStatusChip.textContent = tr('calibration.chip.ready', 'Prêt');
        }
      }
    }

    function calibrationSetSummary(moduleName, c0, c1) {
      if (calibrationIoModule) {
        calibrationIoModule.textContent = moduleName && String(moduleName).trim() ? String(moduleName).trim() : '-';
      }
      if (calibrationC0Current) {
        calibrationC0Current.textContent = Number.isFinite(c0) ? calibrationFormatNumber(c0, 6) : '-';
      }
      if (calibrationC1Current) {
        calibrationC1Current.textContent = Number.isFinite(c1) ? calibrationFormatNumber(c1, 6) : '-';
      }
    }

    function calibrationSetModeUi(mode) {
      const twoPoint = mode === 'two';
      if (calibrationTwoPointFields) calibrationTwoPointFields.hidden = !twoPoint;
      if (calibrationOnePointFields) calibrationOnePointFields.hidden = twoPoint;
      if (calibrationModeHint) {
        calibrationModeHint.textContent = twoPoint
          ? tr('calibration.mode.two.hint', 'Mode 2 points actif: recalcul de C0 et C1.')
          : tr('calibration.mode.one.hint', 'Mode 1 point actif: C0 conservé, ajustement de C1 (offset).');
      }
    }

    function calibrationResetComputedUi() {
      calibrationComputed = null;
      if (calibrationPreview) {
        calibrationPreview.hidden = true;
        calibrationPreview.innerHTML = '';
      }
      if (calibrationChecks) {
        calibrationChecks.hidden = true;
        calibrationChecks.innerHTML = '';
      }
      if (calibrationApplyBtn) calibrationApplyBtn.disabled = true;
    }

    function calibrationIoModuleFromId(ioIdRaw) {
      const ioId = Number(ioIdRaw);
      if (!Number.isFinite(ioId)) return '';
      const idx = Math.round(ioId) - 192;
      if (idx >= 0 && idx <= 15) {
        return 'io/input/a' + String(idx).padStart(2, '0');
      }
      return '';
    }

    function calibrationIoIdFromAnalogSlot(slotRaw) {
      const slot = Number(slotRaw);
      if (!Number.isFinite(slot)) return NaN;
      const idx = Math.trunc(slot);
      if (idx < 0 || idx > 15) return NaN;
      return 192 + idx;
    }

    async function calibrationEnsureDocSourcesLoaded() {
      if (cfgDocSources.length > 0) return;
      if (!flowCfgDocsLoaded) {
        await chargerFlowCfgDocs();
        return;
      }
      await ensureCfgDocsForModule('');
    }

    async function calibrationFetchFlowModule(moduleName) {
      const cleanModule = nettoyerNomFlowCfg(moduleName);
      if (!cleanModule) throw new Error(tr('calibration.err.invalidModule', 'module invalide'));
      const data = await fetchOkJson(
        '/api/flowcfg/module?name=' + encodeURIComponent(cleanModule),
        { cache: 'no-store' },
        'lecture module ' + cleanModule + ' impossible',
        fetchFlowRemoteQueued
      );
      if (!data || typeof data.data !== 'object' || Array.isArray(data.data)) {
        throw new Error(tr('calibration.err.invalidModuleNamed', 'module {module} invalide').replace('{module}', cleanModule));
      }
      return data.data;
    }

    function calibrationExtractCoeffKeys(moduleData, moduleName) {
      const source = (moduleData && typeof moduleData === 'object' && !Array.isArray(moduleData)) ? moduleData : {};
      const keys = Object.keys(source);
      const c0Key = keys.find((key) => /_c0$/i.test(String(key || ''))) || '';
      const c1Key = keys.find((key) => /_c1$/i.test(String(key || ''))) || '';
      if (!c0Key || !c1Key) {
        throw new Error(tr('calibration.err.coeffNotFound', 'coefficients C0/C1 introuvables dans {module}').replace('{module}', moduleName));
      }
      return { c0Key, c1Key };
    }

    function calibrationSyncSelectionUi() {
      const def = calibrationCurrentSensorDef();
      calibrationSetModeUi(def.mode);
      calibrationResetComputedUi();
      if (!calibrationContext || calibrationContext.sensorKey !== def.key) {
        calibrationContext = null;
        calibrationSetSummary('-', NaN, NaN);
      }
      calibrationSetLiveFillButtonsDisabled(!calibrationContext);
    }

    function calibrationSetLiveFillButtonsDisabled(disabled) {
      if (calibrationPoint1LiveBtn) calibrationPoint1LiveBtn.disabled = disabled;
      if (calibrationPoint2LiveBtn) calibrationPoint2LiveBtn.disabled = disabled;
      if (calibrationSingleLiveBtn) calibrationSingleLiveBtn.disabled = disabled;
    }

    async function loadCalibrationSensorConfig(prefillLive) {
      const def = calibrationCurrentSensorDef();
      if (calibrationSensorSelect && calibrationSensorSelect.value !== def.key) {
        calibrationSensorSelect.value = def.key;
      }
      calibrationSetModeUi(def.mode);
      calibrationResetComputedUi();
      calibrationSetStatus(tr('calibration.loadingSensorCfg', 'Chargement de la configuration sonde...'), 'busy');
      if (calibrationLoadBtn) calibrationLoadBtn.disabled = true;
      calibrationSetLiveFillButtonsDisabled(true);

      try {
        await calibrationEnsureDocSourcesLoaded();
        const poolSensorCfg = await calibrationFetchFlowModule('poollogic/sensors');
        let ioId = Number(poolSensorCfg[def.poollogicKey]);
        if (!Number.isFinite(ioId) || !calibrationIoModuleFromId(ioId)) {
          ioId = calibrationIoIdFromAnalogSlot(def.ioSlot);
        }
        if (!Number.isFinite(ioId) || ioId <= 0) {
          throw new Error(tr('calibration.err.ioNotConfigured', 'IO non configurée pour {sensor}').replace('{sensor}', def.label));
        }
        const ioModule = calibrationIoModuleFromId(ioId);
        if (!ioModule) {
          throw new Error(tr('calibration.err.unknownAnalogIo', 'IO analogique inconnue (id={id})').replace('{id}', String(ioId)));
        }
        const ioCfg = await calibrationFetchFlowModule(ioModule);
        const coeffKeys = calibrationExtractCoeffKeys(ioCfg, ioModule);
        const c0 = calibrationParseNumberLoose(ioCfg[coeffKeys.c0Key]);
        const c1 = calibrationParseNumberLoose(ioCfg[coeffKeys.c1Key]);
        if (!Number.isFinite(c0) || !Number.isFinite(c1)) {
          throw new Error(tr('calibration.err.invalidCoeffForModule', 'C0/C1 invalides pour {module}').replace('{module}', ioModule));
        }

        calibrationContext = {
          sensorKey: def.key,
          sensorLabel: def.label,
          mode: def.mode,
          runtimeUiId: def.runtimeUiId,
          recommendedSpan: Number(def.recommendedSpan) || 0,
          warningOffset: Number(def.warningOffset) || 0,
          ioId: ioId,
          ioModule: ioModule,
          c0Key: coeffKeys.c0Key,
          c1Key: coeffKeys.c1Key,
          c0: c0,
          c1: c1
        };

        calibrationSetSummary(ioModule, c0, c1);
        calibrationSetStatus(tr('calibration.sensorLoaded', 'Sonde {sensor} chargée.').replace('{sensor}', def.label), 'ok');
        calibrationSetLiveFillButtonsDisabled(false);

        if (prefillLive) {
          await calibrationPrefillLiveValue({ silent: true });
        }
      } catch (err) {
        calibrationContext = null;
        calibrationSetSummary('-', NaN, NaN);
        calibrationSetStatus(tr('calibration.loadFailed', 'Chargement étalonnage échoué: {err}').replace('{err}', String(err)), 'error');
      } finally {
        if (calibrationLoadBtn) calibrationLoadBtn.disabled = false;
      }
    }

    async function calibrationPrefillLiveValue(options) {
      const opts = options || {};
      if (!calibrationContext || !Number.isFinite(calibrationContext.runtimeUiId)) {
        throw new Error(tr('calibration.err.loadSensorFirst', 'chargez d\'abord une sonde'));
      }
      if (!opts.silent) {
        calibrationSetStatus(tr('calibration.readingLive', 'Lecture de la mesure live...'), 'busy');
      }

      const values = await fetchRuntimeValues([calibrationContext.runtimeUiId]);
      const runtimeValue = values.find((item) => Number(item && (item.id ?? item.runtimeId)) === calibrationContext.runtimeUiId);
      if (!runtimeValue || runtimeValue.status === 'not_found' || runtimeValue.status === 'unavailable') {
        throw new Error(tr('calibration.err.liveUnavailable', 'mesure live indisponible'));
      }
      const measured = calibrationParseNumberLoose(runtimeValue.value);
      if (!Number.isFinite(measured)) {
        throw new Error(tr('calibration.err.liveInvalid', 'mesure live invalide'));
      }

      if (opts.targetInput && typeof opts.targetInput.value === 'string') {
        opts.targetInput.value = String(measured);
      } else if (calibrationContext.mode === 'two') {
        const p1Empty = !String(calibrationPoint1Measured && calibrationPoint1Measured.value || '').trim();
        const p2Empty = !String(calibrationPoint2Measured && calibrationPoint2Measured.value || '').trim();
        if (calibrationPoint1Measured && (p1Empty || !p2Empty)) {
          calibrationPoint1Measured.value = String(measured);
        }
        if (calibrationPoint2Measured && p2Empty && !p1Empty) {
          calibrationPoint2Measured.value = String(measured);
        }
      } else if (calibrationSingleMeasured) {
        calibrationSingleMeasured.value = String(measured);
      }

      if (!opts.silent) {
        calibrationSetStatus(
          tr('calibration.liveValueFetched', 'Mesure live récupérée: {value}').replace('{value}', calibrationFormatNumber(measured, 4)),
          'ok'
        );
      }
    }

    function calibrationComputeModel() {
      if (!calibrationContext || !calibrationContext.ioModule) {
        throw new Error(tr('calibration.err.loadSensorFirst', 'chargez d\'abord une sonde'));
      }

      const oldC0 = Number(calibrationContext.c0);
      const oldC1 = Number(calibrationContext.c1);
      if (!Number.isFinite(oldC0) || !Number.isFinite(oldC1)) {
        throw new Error(tr('calibration.err.currentCoeffInvalid', 'coefficients actuels invalides'));
      }
      if (Math.abs(oldC0) < 1e-12) {
        throw new Error(tr('calibration.err.c0TooCloseZero', 'C0 actuel trop proche de 0'));
      }

      if (calibrationContext.mode === 'two') {
        const measured1 = calibrationReadInputNumber(calibrationPoint1Measured, 'Point 1 mesure affichée');
        const reference1 = calibrationReadInputNumber(calibrationPoint1Reference, 'Point 1 référence');
        const measured2 = calibrationReadInputNumber(calibrationPoint2Measured, 'Point 2 mesure affichée');
        const reference2 = calibrationReadInputNumber(calibrationPoint2Reference, 'Point 2 référence');
        if (Math.abs(measured2 - measured1) < 1e-9) {
          throw new Error(tr('calibration.err.twoMeasuredSame', 'les deux mesures affichées doivent être différentes'));
        }

        const raw1 = (measured1 - oldC1) / oldC0;
        const raw2 = (measured2 - oldC1) / oldC0;
        if (!Number.isFinite(raw1) || !Number.isFinite(raw2)) {
          throw new Error(tr('calibration.err.rawConversionImpossible', 'conversion brute impossible'));
        }
        if (Math.abs(raw2 - raw1) < 1e-12) {
          throw new Error(tr('calibration.err.rawPointsTooClose', 'les points bruts sont trop proches'));
        }

        const newC0 = (reference2 - reference1) / (raw2 - raw1);
        const newC1 = reference1 - (newC0 * raw1);
        if (!Number.isFinite(newC0) || !Number.isFinite(newC1)) {
          throw new Error(tr('calibration.err.coeffCalcImpossible', 'calcul des coefficients impossible'));
        }

        return {
          mode: 'two',
          sensorLabel: calibrationContext.sensorLabel,
          moduleName: calibrationContext.ioModule,
          oldC0,
          oldC1,
          newC0,
          newC1,
          measured1,
          measured2,
          reference1,
          reference2,
          raw1,
          raw2,
          spanMeasured: Math.abs(measured2 - measured1),
          spanReference: Math.abs(reference2 - reference1),
          warningOffset: calibrationContext.warningOffset,
          recommendedSpan: calibrationContext.recommendedSpan
        };
      }

      const measured = calibrationReadInputNumber(calibrationSingleMeasured, 'Mesure affichée');
      const reference = calibrationReadInputNumber(calibrationSingleReference, 'Référence');
      const raw = (measured - oldC1) / oldC0;
      if (!Number.isFinite(raw)) {
        throw new Error(tr('calibration.err.rawConversionImpossible', 'conversion brute impossible'));
      }
      const newC0 = oldC0;
      const newC1 = reference - (newC0 * raw);
      if (!Number.isFinite(newC1)) {
        throw new Error(tr('calibration.err.c1CalcImpossible', 'calcul C1 impossible'));
      }

      return {
        mode: 'one',
        sensorLabel: calibrationContext.sensorLabel,
        moduleName: calibrationContext.ioModule,
        oldC0,
        oldC1,
        newC0,
        newC1,
        measured,
        reference,
        raw,
        warningOffset: calibrationContext.warningOffset,
        recommendedSpan: 0
      };
    }

    function calibrationBuildChecks(model) {
      const checks = [];
      if (!model || typeof model !== 'object') return checks;

      if (model.mode === 'two') {
        if (model.recommendedSpan > 0) {
          if (model.spanReference < model.recommendedSpan) {
            checks.push({
              tone: 'warn',
              label: tr('calibration.check.warn', 'Alerte'),
              text: tr('calibration.check.spanWeak', 'L\'écart entre références est faible ({value}). Élargissez les points pour une meilleure précision.')
                .replace('{value}', calibrationFormatNumber(model.spanReference, 3))
            });
          } else {
            checks.push({
              tone: 'ok',
              label: tr('calibration.check.ok', 'OK'),
              text: tr('calibration.check.spanOk', 'Écart entre références correct ({value}).')
                .replace('{value}', calibrationFormatNumber(model.spanReference, 3))
            });
          }
        }
        if (model.newC0 <= 0) {
          checks.push({
            tone: 'warn',
            label: tr('calibration.check.warn', 'Alerte'),
            text: tr('calibration.check.c0Invalid', 'La pente C0 calculée est négative ou nulle. Vérifiez l\'ordre des points et les valeurs saisies.')
          });
        } else {
          checks.push({
            tone: 'ok',
            label: tr('calibration.check.ok', 'OK'),
            text: tr('calibration.check.c0Ok', 'La pente C0 calculée est cohérente.')
          });
        }
      } else {
        const offsetDelta = Math.abs(model.reference - model.measured);
        if (offsetDelta > model.warningOffset && model.warningOffset > 0) {
          checks.push({
            tone: 'warn',
            label: tr('calibration.check.warn', 'Alerte'),
            text: tr('calibration.check.offsetHigh', 'Décalage important ({value}). Vérifiez la référence.')
              .replace('{value}', calibrationFormatNumber(offsetDelta, 3))
          });
        } else {
          checks.push({
            tone: 'ok',
            label: tr('calibration.check.ok', 'OK'),
            text: tr('calibration.check.offsetOk', 'Décalage mesuré compatible avec un étalonnage 1 point.')
          });
        }
      }

      checks.push({
        tone: 'info',
        label: tr('menu.info', 'infos/about'),
        text: tr('calibration.check.moduleTargeted', 'Module ciblé: {module}').replace('{module}', model.moduleName)
      });
      return checks;
    }

    function calibrationRenderPreview(model) {
      if (!calibrationPreview) return;
      calibrationPreview.hidden = false;
      const modeLabel = model.mode === 'two'
        ? tr('calibration.mode.two.title', 'Étalonnage 2 points')
        : tr('calibration.mode.one.title', 'Étalonnage 1 point');
      const rows = [];
      if (model.mode === 'two') {
        rows.push({
          label: 'C0',
          current: calibrationFormatNumber(model.oldC0, 6),
          next: calibrationFormatNumber(model.newC0, 6)
        });
      }
      rows.push({
        label: 'C1',
        current: calibrationFormatNumber(model.oldC1, 6),
        next: calibrationFormatNumber(model.newC1, 6)
      });
      const rowsHtml = rows.map((row) =>
        '<span class="calibration-preview-row-label">' + row.label + '</span>' +
        '<span class="calibration-preview-value calibration-preview-value-current">' + row.current + '</span>' +
        '<b class="calibration-preview-value calibration-preview-value-new">' + row.next + '</b>'
      ).join('');
      const noteHtml = model.mode === 'two'
        ? ''
        : '<div class="calibration-preview-note">' + tr('calibration.preview.onePointNote', 'C0 conservé en mode 1 point (offset).') + '</div>';
      calibrationPreview.innerHTML =
        '<div class="calibration-preview-head">'
          + tr('calibration.preview.head', '{mode} prête pour {sensor}')
            .replace('{mode}', modeLabel)
            .replace('{sensor}', model.sensorLabel)
          + '</div>' +
        '<div class="calibration-preview-grid">' +
          '<span class="calibration-preview-col-head">' + tr('calibration.preview.coefficient', 'Coefficient') + '</span>' +
          '<span class="calibration-preview-col-head">' + tr('calibration.preview.current', 'Actuel') + '</span>' +
          '<span class="calibration-preview-col-head">' + tr('calibration.preview.new', 'Nouveau') + '</span>' +
          rowsHtml +
        '</div>' +
        noteHtml;
    }

    function calibrationRenderChecks(checks) {
      if (!calibrationChecks) return;
      const entries = Array.isArray(checks) ? checks : [];
      if (entries.length === 0) {
        calibrationChecks.hidden = true;
        calibrationChecks.innerHTML = '';
        return;
      }
      calibrationChecks.hidden = false;
      calibrationChecks.innerHTML = '';
      entries.forEach((entry) => {
        const row = document.createElement('div');
        row.className = 'calibration-check-line is-' + (entry && entry.tone ? entry.tone : 'info');

        const pill = document.createElement('span');
        pill.className = 'calibration-check-pill';
        pill.textContent = String(entry && entry.label ? entry.label : tr('menu.info', 'infos/about'));
        row.appendChild(pill);

        const text = document.createElement('span');
        text.className = 'calibration-check-text';
        text.textContent = String(entry && entry.text ? entry.text : '');
        row.appendChild(text);

        calibrationChecks.appendChild(row);
      });
    }

    function runCalibrationCompute() {
      try {
        const model = calibrationComputeModel();
        calibrationComputed = model;
        calibrationRenderPreview(model);
        calibrationRenderChecks(calibrationBuildChecks(model));
        if (calibrationApplyBtn) calibrationApplyBtn.disabled = false;
        calibrationSetStatus(tr('calibration.computeDone', 'Nouveaux coefficients calculés. Vous pouvez appliquer.'), 'ok');
      } catch (err) {
        calibrationResetComputedUi();
        calibrationSetStatus(tr('calibration.computeFailed', 'Calcul étalonnage échoué: {err}').replace('{err}', String(err)), 'error');
      }
    }

    async function applyCalibrationResult() {
      if (!calibrationContext || !calibrationComputed) return;
      if (calibrationApplyBtn) calibrationApplyBtn.disabled = true;
      calibrationSetStatus(tr('calibration.applyInProgress', 'Application des coefficients sur flow.io...'), 'busy');

      try {
        const patch = {};
        patch[calibrationContext.ioModule] = {
          [calibrationContext.c0Key]: calibrationPatchNumber(calibrationComputed.newC0),
          [calibrationContext.c1Key]: calibrationPatchNumber(calibrationComputed.newC1)
        };

        const response = await fetchJsonResponse(
          '/api/flowcfg/apply',
          createFormPostOptions({ patch: JSON.stringify(patch) }),
          fetchFlowRemoteQueued
        );
        if (!response.res.ok || !response.data || response.data.ok !== true) {
          throw new Error(formatFlowCfgApplyError(response.data));
        }

        await loadCalibrationSensorConfig(false);
        calibrationSetStatus(tr('calibration.applySuccess', 'Étalonnage appliqué avec succès.'), 'ok');
      } catch (err) {
        calibrationSetStatus(tr('calibration.applyFailed', 'Application étalonnage échouée: {err}').replace('{err}', String(err)), 'error');
        if (calibrationApplyBtn) calibrationApplyBtn.disabled = !calibrationComputed;
      }
    }

    function stopWifiScanPolling() {
      wifiScanPoller.stop();
    }

    function scheduleWifiScanPolling() {
      wifiScanPoller.schedule(1200);
    }

    function renderWifiScanList(data) {
      const networks = (data && Array.isArray(data.networks)) ? data.networks : [];
      const currentSsid = (wifiSsid.value || '').trim();
      const prevSelected = wifiSsidList.value || '';
      const maxWifiOptionLabelLen = 56;

      wifiSsidList.innerHTML = '';
      const manualOpt = document.createElement('option');
      manualOpt.value = '';
      manualOpt.textContent = 'Saisie manuelle';
      wifiSsidList.appendChild(manualOpt);

      for (const net of networks) {
        if (!net || typeof net.ssid !== 'string' || net.ssid.length === 0) continue;
        if (net.hidden) continue;
        const opt = document.createElement('option');
        const secureSuffix = net.secure ? ' (securise)' : ' (ouvert)';
        const rssiSuffix = Number.isFinite(net.rssi) ? (' ' + net.rssi + ' dBm') : '';
        const fullLabel = net.ssid + secureSuffix + rssiSuffix;
        opt.value = net.ssid;
        opt.textContent = fullLabel.length > maxWifiOptionLabelLen
            ? (fullLabel.slice(0, maxWifiOptionLabelLen - 1) + '…')
            : fullLabel;
        wifiSsidList.appendChild(opt);
      }

      const values = Array.from(wifiSsidList.options).map((o) => o.value);
      if (currentSsid && values.includes(currentSsid)) {
        wifiSsidList.value = currentSsid;
      } else if (prevSelected && values.includes(prevSelected)) {
        wifiSsidList.value = prevSelected;
      } else {
        wifiSsidList.value = '';
      }
    }

    function updateWifiScanStatusText(data, reqError) {
      if (reqError) {
        wifiConfigStatus.textContent = 'Scan réseau indisponible: ' + reqError;
        return;
      }
      if (!data || data.ok !== true) {
        wifiConfigStatus.textContent = 'Scan réseau : réponse invalide.';
        return;
      }

      const running = !!data.running;
      const requested = !!data.requested;
      const count = Number.isFinite(data.count) ? data.count : 0;
      const totalFound = Number.isFinite(data.total_found) ? data.total_found : count;
      if (running || requested) {
        wifiConfigStatus.textContent = 'Scan réseau en cours...';
        return;
      }
      if (count > 0) {
        wifiConfigStatus.textContent = 'Scan réseau terminé : ' + count + ' réseaux affichés (' + totalFound + ' détectés).';
        return;
      }
      wifiConfigStatus.textContent = 'Aucun réseau visible détecté.';
    }

    function toBool(v) {
      if (typeof v === 'boolean') return v;
      if (typeof v === 'number') return v !== 0;
      if (typeof v === 'string') {
        const s = v.trim().toLowerCase();
        return s === '1' || s === 'true' || s === 'on' || s === 'yes';
      }
      return false;
    }

    async function requestWifiScan(force) {
      return fetchOkJson('/api/wifi/scan', createFormPostOptions({
        force: force ? '1' : '0'
      }), 'échec démarrage scan');
    }

    async function refreshWifiScanStatus(triggerScan) {
      try {
        if (triggerScan) {
          await requestWifiScan(true);
        }
        const data = await fetchOkJson('/api/wifi/scan', { cache: 'no-store' }, 'échec lecture état');

        renderWifiScanList(data);
        updateWifiScanStatusText(data, null);

        if (data.running || data.requested) {
          scheduleWifiScanPolling();
        } else {
          stopWifiScanPolling();
        }
      } catch (err) {
        stopWifiScanPolling();
        updateWifiScanStatusText(null, err);
      }
    }

    async function loadWifiConfig() {
      try {
        const data = await fetchOkJson('/api/wifi/config', { cache: 'no-store' }, 'chargement réseau indisponible');
        wifiEnabled.checked = toBool(data.enabled);
        wifiSsid.value = data.ssid || '';
        wifiPass.value = data.pass || '';
        wifiConfigStatus.textContent = 'Configuration réseau chargée.';
      } catch (err) {
        wifiConfigStatus.textContent = 'Chargement réseau échoué: ' + err;
      }
    }

    async function saveWifiConfig() {
      await fetchOkJson('/api/wifi/config', createFormPostOptions({
        enabled: wifiEnabled.checked ? '1' : '0',
        ssid: wifiSsid.value.trim(),
        pass: wifiPass.value
      }), 'échec application');
      wifiConfigStatus.textContent = 'Configuration réseau appliquée (reconnexion en cours).';
    }

    function nettoyerNomFlowCfg(moduleName) {
      return String(moduleName || '').trim().replace(/^\/+|\/+$/g, '');
    }

    function cheminFlowCfgCourant() {
      return flowCfgPath.length > 0 ? flowCfgPath.join('/') : '';
    }

    function cfgPathHasPrefix(pathValue, prefix) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      const cleanPrefix = nettoyerNomFlowCfg(prefix);
      if (!cleanPrefix) return true;
      return cleanPath === cleanPrefix || cleanPath.startsWith(cleanPrefix + '/');
    }

    function cfgDocPathCandidates(pathValue) {
      const cleanDisplay = nettoyerNomFlowCfg(pathValue);
      const candidates = [];
      const pushCandidate = (candidate) => {
        const cleanCandidate = nettoyerNomFlowCfg(candidate);
        if (!cleanCandidate) return;
        if (candidates.indexOf(cleanCandidate) >= 0) return;
        candidates.push(cleanCandidate);
      };

      let mappedStore = null;
      for (const alias of cfgTreeAliases) {
        if (!cfgPathHasPrefix(cleanDisplay, alias.display)) continue;
        if (!mappedStore || alias.display.length > mappedStore.display.length) {
          mappedStore = alias;
        }
      }

      if (mappedStore) {
        pushCandidate(mappedStore.store + cleanDisplay.slice(mappedStore.display.length));
      }

      pushCandidate(cleanDisplay);
      return candidates;
    }

    function cfgStorePathFromDisplayPath(pathValue) {
      const cleanDisplay = nettoyerNomFlowCfg(pathValue);
      if (!cleanDisplay) return '';
      for (const branch of cfgTreeVirtualBranches) {
        if (branch.display === cleanDisplay) return null;
      }
      const candidates = cfgDocPathCandidates(cleanDisplay);
      return candidates.length > 0 ? candidates[0] : cleanDisplay;
    }

    function cfgDisplayPathFromStorePath(pathValue) {
      const cleanStore = nettoyerNomFlowCfg(pathValue);
      if (!cleanStore) return '';
      let bestAlias = null;
      for (const alias of cfgTreeAliases) {
        if (!cfgPathHasPrefix(cleanStore, alias.store)) continue;
        if (!bestAlias || alias.store.length > bestAlias.store.length) {
          bestAlias = alias;
        }
      }
      if (!bestAlias) return cleanStore;
      return bestAlias.display + cleanStore.slice(bestAlias.store.length);
    }

    function cfgVirtualChildrenForDisplayPath(pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      for (const branch of cfgTreeVirtualBranches) {
        if (branch.display === cleanPath) {
          return branch.children.slice().sort((a, b) => a.localeCompare(b));
        }
      }
      return null;
    }

    function cfgIsAliasStoreShadowPath(pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      if (!cleanPath) return false;
      for (const alias of cfgTreeAliases) {
        const inStoreBranch = cfgPathHasPrefix(cleanPath, alias.store) || cfgPathHasPrefix(alias.store, cleanPath);
        if (!inStoreBranch) continue;
        const inDisplayBranch = cfgPathHasPrefix(cleanPath, alias.display) || cfgPathHasPrefix(alias.display, cleanPath);
        if (!inDisplayBranch) return true;
      }
      return false;
    }

    function cfgChildTokenForDisplayPath(parentPath, childPath) {
      const cleanParent = nettoyerNomFlowCfg(parentPath);
      const cleanChild = nettoyerNomFlowCfg(childPath);
      if (!cleanChild) return '';
      if (!cleanParent) {
        const rootSegments = cleanChild.split('/');
        return rootSegments.length > 0 ? rootSegments[0] : '';
      }
      if (!cfgPathHasPrefix(cleanChild, cleanParent) || cleanChild === cleanParent) {
        return '';
      }
      const suffix = cleanChild.slice(cleanParent.length + 1);
      const slashIndex = suffix.indexOf('/');
      return slashIndex >= 0 ? suffix.slice(0, slashIndex) : suffix;
    }

    function cfgPathLabel(pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      if (!cleanPath) return 'Racine';
      const meta = configPathMeta(cleanPath);
      if (meta && typeof meta.label === 'string' && meta.label.length > 0) {
        return meta.label;
      }
      const segs = cleanPath.split('/');
      return segs[segs.length - 1] || cleanPath;
    }

    function cfgTreeNodeRefInfo(pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      if (!cleanPath) return null;
      const matchIo = cleanPath.match(/^io\/input\/(?:analog\/)?(a\d{2})$/i)
        || cleanPath.match(/^io\/input\/(?:digital\/)?(i\d{2})$/i)
        || cleanPath.match(/^io\/output\/(d\d{2})$/i);
      if (matchIo) {
        const ref = String(matchIo[1] || '').toLowerCase();
        if (!ref) return null;
        return {
          type: 'io',
          ref: ref,
          modulePath: cleanPath,
          nameKey: ref + '_name'
        };
      }
      return null;
    }

    async function fetchCfgTreeNodeTextName(source, pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      const info = cfgTreeNodeRefInfo(cleanPath);
      if (!info || info.type !== 'io') return;
      if (!cfgTreeNodeTextNames[source]) cfgTreeNodeTextNames[source] = {};
      if (!cfgTreeNodeTextNamePending[source]) cfgTreeNodeTextNamePending[source] = new Set();

      const existing = cfgTreeNodeTextNames[source][cleanPath];
      if (typeof existing !== 'undefined') return;
      if (cfgTreeNodeTextNamePending[source].has(cleanPath)) return;

      cfgTreeNodeTextNamePending[source].add(cleanPath);
      try {
        const storePath = cfgStorePathFromDisplayPath(cleanPath) || info.modulePath;
        if (!storePath) return;
        const url = source === 'supervisor'
          ? ('/api/supervisorcfg/module?name=' + encodeURIComponent(storePath))
          : ('/api/flowcfg/module?name=' + encodeURIComponent(storePath));
        const fetchFn = source === 'supervisor' ? fetch : fetchFlowRemoteQueued;
        const res = await fetchFn(url, { cache: 'no-store' });
        const data = await res.json().catch(() => null);
        if (!res.ok || !data || data.ok !== true || typeof data.data !== 'object') {
          cfgTreeNodeTextNames[source][cleanPath] = '';
          return;
        }
        const raw = data.data[info.nameKey];
        const textName = (typeof raw === 'string') ? raw.trim() : '';
        cfgTreeNodeTextNames[source][cleanPath] = textName;
      } catch (err) {
        cfgTreeNodeTextNames[source][cleanPath] = '';
      } finally {
        cfgTreeNodeTextNamePending[source].delete(cleanPath);
        renderFlowCfgTree();
      }
    }

    function cfgTreeDecoratedNodeLabel(source, pathValue, baseLabel) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      const info = cfgTreeNodeRefInfo(cleanPath);
      if (!info) return baseLabel;

      const ref = info.ref || String(baseLabel || '').trim();
      if (!ref) return baseLabel;

      const sourceCache = cfgTreeNodeTextNames[source] || {};
      const cached = sourceCache[cleanPath];
      if (typeof cached !== 'undefined') {
        return (typeof cached === 'string' && cached.length > 0) ? (ref + ' [' + cached + ']') : ref;
      }
      fetchCfgTreeNodeTextName(source, cleanPath).catch(() => {});
      return ref;
    }

    function clearCfgTreeNodeTextNameCache(source) {
      if (source !== 'flow' && source !== 'supervisor') return;
      cfgTreeNodeTextNames[source] = {};
      cfgTreeNodeTextNamePending[source] = new Set();
    }

    function flowCfgTitreDepuisChemin(pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      if (!cleanPath) return 'Racine';
      const segs = cleanPath.split('/');
      let prefix = '';
      return segs.map((seg) => {
        prefix = prefix ? (prefix + '/' + seg) : seg;
        return cfgPathLabel(prefix);
      }).join(' / ');
    }

    function cfgCacheKey(prefix) {
      const p = nettoyerNomFlowCfg(prefix);
      return p.length > 0 ? p : '__root__';
    }

    function cfgChildrenCacheForSource(source) {
      return source === 'supervisor' ? supCfgChildrenCache : flowCfgChildrenCache;
    }

    function cfgFilteredChildren(source, prefix) {
      const p = nettoyerNomFlowCfg(prefix);
      const node = cfgChildrenCacheForSource(source)[cfgCacheKey(p)];
      if (!node || !Array.isArray(node.children)) return [];
      return node.children
        .filter((name) => {
          const childPath = p ? (p + '/' + name) : name;
          return !isConfigPathHidden(childPath) && !cfgIsAliasStoreShadowPath(childPath);
        })
        .slice();
    }

    function cfgExpandAncestors(source, pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      if (!cleanPath) return;
      const expandedSet = source === 'supervisor' ? supCfgExpandedNodes : flowCfgExpandedNodes;
      const segs = cleanPath.split('/');
      let prefix = '';
      for (let i = 0; i < segs.length; ++i) {
        prefix = prefix ? (prefix + '/' + segs[i]) : segs[i];
        expandedSet.add(prefix);
      }
    }

    function cfgNodeForPath(source, pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      return cfgChildrenCacheForSource(source)[cfgCacheKey(cleanPath)] || null;
    }

    async function chargerCfgChildren(source, prefix, forceReload) {
      const p = nettoyerNomFlowCfg(prefix);
      const cache = cfgChildrenCacheForSource(source);
      const key = cfgCacheKey(p);
      if (!forceReload && cache[key]) {
        return cache[key];
      }

      const virtualChildren = cfgVirtualChildrenForDisplayPath(p);
      if (virtualChildren) {
        const node = {
          prefix: p,
          hasExact: false,
          children: virtualChildren.filter((name) => {
            const childPath = p ? (p + '/' + name) : name;
            return !isConfigPathHidden(childPath) && !cfgIsAliasStoreShadowPath(childPath);
          })
        };
        cache[key] = node;
        return node;
      }

      const storePrefix = cfgStorePathFromDisplayPath(p);
      const baseUrl = source === 'supervisor' ? '/api/supervisorcfg/children' : '/api/flowcfg/children';
      const url = storePrefix && storePrefix.length > 0
        ? (baseUrl + '?prefix=' + encodeURIComponent(storePrefix))
        : baseUrl;
      const fetchFn = source === 'supervisor' ? fetch : fetchFlowRemoteQueued;
      const res = await fetchFn(url, { cache: 'no-store' });
      const data = await res.json().catch(() => null);
      if (!res.ok || !data || data.ok !== true || !Array.isArray(data.children)) {
        const fallback = source === 'supervisor'
          ? 'liste enfants supervisor indisponible'
          : 'liste enfants indisponible';
        throw new Error(extractApiErrorMessage(data, fallback));
      }

      const children = data.children
        .filter((name) => typeof name === 'string' && name.length > 0)
        .map((name) => nettoyerNomFlowCfg(name))
        .filter((name) => name.length > 0)
        .map((child) => {
          const storeChildPath = storePrefix ? (storePrefix + '/' + child) : child;
          return cfgDisplayPathFromStorePath(storeChildPath);
        })
        .map((displayChildPath) => cfgChildTokenForDisplayPath(p, displayChildPath))
        .filter((name) => name.length > 0)
        .filter((name) => {
          const childPath = p ? (p + '/' + name) : name;
          return !isConfigPathHidden(childPath) && !cfgIsAliasStoreShadowPath(childPath);
        });

      const node = {
        prefix: p,
        hasExact: !!data.has_exact,
        children: Array.from(new Set(children)).sort((a, b) => a.localeCompare(b))
      };
      cache[key] = node;
      return node;
    }

    async function ensureCfgPathLoaded(source, pathValue, forceReload) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      await chargerCfgChildren(source, '', !!forceReload);
      if (!cleanPath) return cfgNodeForPath(source, '');

      const segs = cleanPath.split('/');
      let prefix = '';
      let node = null;
      for (let i = 0; i < segs.length; ++i) {
        prefix = prefix ? (prefix + '/' + segs[i]) : segs[i];
        node = await chargerCfgChildren(source, prefix, !!forceReload);
      }
      return node;
    }

    function currentCfgTreePath(source) {
      return source === 'supervisor' ? nettoyerNomFlowCfg(supCfgTreePath) : cheminFlowCfgCourant();
    }

    function cfgSourceLabel(source) {
      if (source !== 'supervisor') {
        return tr('cfg.remote.flow', 'Config Store flow.io');
      }
      if (webLocalConfigLabel) return webLocalConfigLabel;
      return isMicronovaProfile()
        ? tr('cfg.local.micronova', 'Config Store Micronova')
        : tr('cfg.local.supervisor', 'Config Store Supervisor');
    }

    function renderFlowCfgCurrentPath(source, pathValue, node) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      const childCount = cfgFilteredChildren(source, cleanPath).length;
      const level = cleanPath ? cleanPath.split('/').length : 0;
      const hasExact = !!(node && node.hasExact);
      const sourceLabel = cfgSourceLabel(source);

      flowCfgPathLabel.textContent = cleanPath ? (sourceLabel + ' / ' + flowCfgTitreDepuisChemin(cleanPath)) : sourceLabel;
      flowCfgPathLabel.setAttribute('aria-label', cleanPath ? (tr('config.branch', 'Branche') + ' ' + cleanPath) : sourceLabel);
      flowCfgApplyBtn.textContent = source === 'supervisor' ? tr('cfg.apply.local', 'Appliquer localement') : tr('config.apply', 'Appliquer');

      if (!cleanPath) {
        flowCfgPathMeta.textContent = childCount > 0
          ? (childCount + ' branche(s) disponible(s) dans ' + sourceLabel + '.')
          : ('Aucune branche disponible dans ' + sourceLabel + '.');
        return;
      }

      const details = [];
      details.push(tr('config.level', 'Niveau') + ' ' + level);
      if (hasExact) {
        details.push(tr('config.variablesConfigurable', 'variables configurables'));
      }
      if (childCount > 0) {
        details.push(childCount + ' ' + tr('config.subBranches', 'sous-branche(s)'));
      }
      if (details.length === 0) {
        details.push(tr('config.branchEmpty', 'branche vide'));
      }
      flowCfgPathMeta.textContent = details.join(' | ');
    }

    function buildFlowCfgTreeItem(source, pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      const label = cfgTreeDecoratedNodeLabel(source, cleanPath, cfgPathLabel(cleanPath));
      const cachedNode = cfgNodeForPath(source, cleanPath);
      const children = cfgFilteredChildren(source, cleanPath);
      const isExpanded = source === 'supervisor' ? supCfgExpandedNodes.has(cleanPath) : flowCfgExpandedNodes.has(cleanPath);
      const isSelected = source === cfgTreeSelectedSource && cleanPath === currentCfgTreePath(source);
      const hasKnownChildren = children.length > 0;
      const canExpand = !cachedNode || hasKnownChildren;

      const item = document.createElement('li');
      item.className = 'cfg-tree-item';
      item.setAttribute('role', 'treeitem');
      item.setAttribute('aria-expanded', canExpand ? String(isExpanded) : 'false');

      const row = document.createElement('div');
      row.className = 'cfg-tree-row' + (canExpand ? '' : ' is-leaf');

      const toggle = document.createElement('button');
      toggle.type = 'button';
      toggle.className = 'cfg-tree-toggle' + (isExpanded ? ' is-expanded' : '') + (canExpand ? '' : ' is-leaf');
      toggle.setAttribute('aria-label', canExpand ? ('Afficher ' + label) : (label + ' sans sous-branche'));
      if (canExpand) {
        const glyph = document.createElement('span');
        glyph.className = 'cfg-tree-toggle-glyph' + (isExpanded ? ' is-minus' : ' is-plus');
        glyph.textContent = isExpanded ? '-' : '+';
        toggle.appendChild(glyph);
      }
      toggle.disabled = !canExpand;
      if (canExpand) {
        toggle.addEventListener('click', async (event) => {
          event.stopPropagation();
          await toggleFlowCfgBranch(source, cleanPath);
        });
      }
      row.appendChild(toggle);

      const nodeBtn = document.createElement('button');
      nodeBtn.type = 'button';
      nodeBtn.className = 'cfg-tree-node'
        + (isSelected ? ' is-selected' : '')
        + (cachedNode && cachedNode.hasExact ? ' is-exact' : '');
      nodeBtn.setAttribute('aria-current', isSelected ? 'true' : 'false');
      nodeBtn.addEventListener('click', async () => {
        if (canExpand && isExpanded) {
          await toggleFlowCfgBranch(source, cleanPath);
          return;
        }
        await selectFlowCfgPath(source, cleanPath, false);
      });

      const nodeLabel = document.createElement('span');
      nodeLabel.className = 'cfg-tree-node-label';
      nodeLabel.textContent = label;
      nodeBtn.appendChild(nodeLabel);
      row.appendChild(nodeBtn);
      item.appendChild(row);

      if (isExpanded && hasKnownChildren) {
        const group = document.createElement('ul');
        group.className = 'cfg-tree-group is-nested';
        group.setAttribute('role', 'group');
        children.forEach((child) => {
          const childPath = cleanPath ? (cleanPath + '/' + child) : child;
          group.appendChild(buildFlowCfgTreeItem(source, childPath));
        });
        item.appendChild(group);
      }

      return item;
    }

    function buildCfgTreeRootItem(source, label, expanded, children) {
      const item = document.createElement('li');
      item.className = 'cfg-tree-item cfg-tree-item-root';
      item.setAttribute('role', 'treeitem');
      item.setAttribute('aria-expanded', children.length > 0 ? String(expanded) : 'false');

      const row = document.createElement('div');
      row.className = 'cfg-tree-row cfg-tree-root-row';

      const toggle = document.createElement('button');
      toggle.type = 'button';
      toggle.className = 'cfg-tree-toggle' + (expanded ? ' is-expanded' : '') + (children.length > 0 ? '' : ' is-leaf');
      if (children.length > 0) {
        const glyph = document.createElement('span');
        glyph.className = 'cfg-tree-toggle-glyph' + (expanded ? ' is-minus' : ' is-plus');
        glyph.textContent = expanded ? '-' : '+';
        toggle.appendChild(glyph);
      }
      toggle.disabled = children.length === 0;
      toggle.setAttribute('aria-label', expanded ? ('Replier ' + label) : ('Afficher ' + label));
      toggle.addEventListener('click', async (event) => {
        event.stopPropagation();
        if (source === 'flow') flowCfgRootExpanded = !flowCfgRootExpanded;
        else supCfgRootExpanded = !supCfgRootExpanded;
        renderFlowCfgTree();
      });
      row.appendChild(toggle);

      const labelBtn = document.createElement('button');
      labelBtn.type = 'button';
      labelBtn.className = 'cfg-tree-root-label' + ((cfgTreeSelectedSource === source && !currentCfgTreePath(source)) ? ' is-selected' : '');
      labelBtn.textContent = label;
      labelBtn.addEventListener('click', async () => {
        await selectFlowCfgPath(source, '', false);
      });
      row.appendChild(labelBtn);
      item.appendChild(row);

      if (expanded && children.length > 0) {
        const group = document.createElement('ul');
        group.className = 'cfg-tree-group cfg-tree-group-root';
        group.setAttribute('role', 'group');
        children.forEach((child) => {
          group.appendChild(buildFlowCfgTreeItem(source, child));
        });
        item.appendChild(group);
      }

      return item;
    }

    function renderFlowCfgTree() {
      const savedScrollTop = flowCfgTree.scrollTop;
      flowCfgTree.innerHTML = '';
      const flowChildren = cfgFilteredChildren('flow', '');
      const supervisorChildren = cfgFilteredChildren('supervisor', '');

      const roots = document.createElement('ul');
      roots.className = 'cfg-tree-group';
      roots.setAttribute('role', 'tree');
      if (webRemoteConfigEnabled && flowChildren.length > 0) {
        roots.appendChild(buildCfgTreeRootItem('flow', tr('cfg.remote.flow', 'Config Store flow.io'), flowCfgRootExpanded, flowChildren));
      }
      roots.appendChild(buildCfgTreeRootItem('supervisor', cfgSourceLabel('supervisor'), supCfgRootExpanded, supervisorChildren));
      flowCfgTree.appendChild(roots);
      flowCfgTree.scrollTop = savedScrollTop;
    }

    function renderFlowCfgTreeSkeleton() {
      const savedScrollTop = flowCfgTree.scrollTop;
      flowCfgTree.innerHTML = '';
      const skeleton = document.createElement('div');
      skeleton.className = 'cfg-tree-skeleton';
      [100, 88, 92, 78, 84].forEach((width, index) => {
        const line = document.createElement('div');
        line.className = 'skeleton-line cfg-tree-skeleton-line' + (index > 1 ? ' is-indented' : '');
        line.style.width = width + '%';
        skeleton.appendChild(line);
      });
      flowCfgTree.appendChild(skeleton);
      flowCfgTree.scrollTop = savedScrollTop;
    }

    function restoreFlowCfgTreeScroll(scrollTop) {
      if (!flowCfgTree || !Number.isFinite(scrollTop)) return;
      flowCfgTree.scrollTop = scrollTop;
      requestAnimationFrame(() => {
        if (!flowCfgTree) return;
        flowCfgTree.scrollTop = scrollTop;
      });
    }

    async function toggleFlowCfgBranch(source, pathValue) {
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      const expandedSet = source === 'supervisor' ? supCfgExpandedNodes : flowCfgExpandedNodes;
      if (!cleanPath) return;
      if (expandedSet.has(cleanPath)) {
        expandedSet.delete(cleanPath);
        renderFlowCfgTree();
        return;
      }
      try {
        flowCfgStatus.textContent = 'Chargement des sous-branches...';
        await chargerCfgChildren(source, cleanPath, false);
        expandedSet.add(cleanPath);
        renderFlowCfgTree();
        flowCfgStatus.textContent = 'Sous-branches chargees.';
      } catch (err) {
        flowCfgStatus.textContent = 'Chargement des sous-branches echoue: ' + err;
      }
    }

    async function selectFlowCfgPath(source, pathValue, forceReload) {
      const preservedTreeScrollTop = flowCfgTree ? flowCfgTree.scrollTop : 0;
      beginFlowCfgLoading('Chargement de la configuration distante...', { tree: false, detail: true });
      const cleanPath = nettoyerNomFlowCfg(pathValue);
      try {
        let node = null;
        const storePath = cfgStorePathFromDisplayPath(cleanPath);
        cfgTreeSelectedSource = source === 'supervisor' ? 'supervisor' : 'flow';
        if (cfgTreeSelectedSource === 'supervisor') {
          node = await ensureCfgPathLoaded('supervisor', cleanPath, !!forceReload);
          supCfgTreePath = cleanPath;
          supCfgRootExpanded = true;
          cfgExpandAncestors('supervisor', cleanPath);
          if (cleanPath && cfgFilteredChildren('supervisor', cleanPath).length > 0) {
            supCfgExpandedNodes.add(cleanPath);
          }
        } else {
          node = await ensureCfgPathLoaded('flow', cleanPath, !!forceReload);
          flowCfgPath = cleanPath ? cleanPath.split('/') : [];
          flowCfgRootExpanded = true;
          cfgExpandAncestors('flow', cleanPath);
          if (cleanPath && cfgFilteredChildren('flow', cleanPath).length > 0) {
            flowCfgExpandedNodes.add(cleanPath);
          }
        }

        renderFlowCfgCurrentPath(cfgTreeSelectedSource, cleanPath, node);
        renderFlowCfgTree();
        restoreFlowCfgTreeScroll(preservedTreeScrollTop);

        if (!cleanPath) {
          resetPrimaryCfgEditor(cfgFilteredChildren(cfgTreeSelectedSource, '').length > 0
            ? 'Sélectionnez une branche dans l\'arborescence.'
            : 'Aucune branche disponible.');
          return;
        }

        if (node && node.hasExact) {
          if (cfgTreeSelectedSource === 'supervisor') {
            await chargerPrimarySupervisorCfgModule(storePath || cleanPath);
          } else {
            await chargerFlowCfgModule(storePath || cleanPath);
          }
          return;
        }

        const childCount = cfgFilteredChildren(cfgTreeSelectedSource, cleanPath).length;
        if (childCount > 0) {
          resetPrimaryCfgEditor('Branche ouverte. Sélectionnez une sous-branche ou un noeud configurable.');
        } else {
          resetPrimaryCfgEditor('Aucune variable configurable dans cette branche.');
        }
      } catch (err) {
        renderFlowCfgCurrentPath(cfgTreeSelectedSource, cleanPath, null);
        renderFlowCfgTree();
        restoreFlowCfgTreeScroll(preservedTreeScrollTop);
        resetPrimaryCfgEditor('Chargement branche échoué: ' + err);
      } finally {
        endFlowCfgLoading({ tree: false, detail: true });
      }
    }

    function renderFlowCfgFieldsSkeleton() {
      flowCfgFields.innerHTML = '';
      for (let i = 0; i < 5; ++i) {
        const row = document.createElement('div');
        row.className = 'control-row control-row-skeleton';

        const labelWrap = document.createElement('div');
        labelWrap.className = 'control-label-wrap';
        labelWrap.appendChild(createSkeletonLine('', i % 2 === 0 ? 38 : 44));
        if (i % 2 === 0) {
          labelWrap.appendChild(createSkeletonLine('', 62));
        }
        row.appendChild(labelWrap);

        const inputSkel = document.createElement('div');
        if (i === 1) {
          inputSkel.className = 'control-switch-skeleton';
        } else {
          inputSkel.className = 'control-input-skeleton';
        }
        row.appendChild(inputSkel);
        flowCfgFields.appendChild(row);
      }
    }

    function beginFlowCfgLoading(statusText, options) {
      const opts = options || {};
      const loadTree = opts.tree !== false;
      const loadDetail = opts.detail !== false;

      if (loadTree) {
        flowCfgTreeLoadingDepth += 1;
        if (flowCfgTreeLoadingDepth === 1) {
          if (flowCfgTreePane) flowCfgTreePane.classList.add('is-loading');
          renderFlowCfgTreeSkeleton();
        }
      }
      if (loadDetail) {
        flowCfgDetailLoadingDepth += 1;
        if (flowCfgDetailLoadingDepth === 1) {
          if (flowCfgDetailPane) flowCfgDetailPane.classList.add('is-loading');
          renderFlowCfgFieldsSkeleton();
          flowCfgApplyBtn.disabled = true;
        }
      }
      if (loadTree || loadDetail) {
        flowCfgRefreshBtn.disabled = true;
      }
      if (statusText) {
        flowCfgStatus.textContent = statusText;
      }
    }

    function endFlowCfgLoading(options) {
      const opts = options || {};
      const loadTree = opts.tree !== false;
      const loadDetail = opts.detail !== false;

      if (loadTree && flowCfgTreeLoadingDepth > 0) {
        flowCfgTreeLoadingDepth -= 1;
        if (flowCfgTreeLoadingDepth === 0) {
          if (flowCfgTreePane) flowCfgTreePane.classList.remove('is-loading');
        }
      }
      if (loadDetail && flowCfgDetailLoadingDepth > 0) {
        flowCfgDetailLoadingDepth -= 1;
        if (flowCfgDetailLoadingDepth === 0) {
          if (flowCfgDetailPane) flowCfgDetailPane.classList.remove('is-loading');
        }
      }
      if (flowCfgTreeLoadingDepth === 0 && flowCfgDetailLoadingDepth === 0) {
        flowCfgRefreshBtn.disabled = false;
      }
    }

    function stopFlowCfgRetry() {
      if (!flowCfgRetryTimer) return;
      clearTimeout(flowCfgRetryTimer);
      flowCfgRetryTimer = null;
    }

    function scheduleFlowCfgRetry(delayMs) {
      stopFlowCfgRetry();
      flowCfgRetryTimer = setTimeout(() => {
        flowCfgRetryTimer = null;
        if (!isPageActive('page-control')) return;
        ensureFlowCfgLoaded(true).catch(() => {});
      }, delayMs);
    }

    function cfgDocKeyFromModuleName(moduleName) {
      const clean = nettoyerNomFlowCfg(moduleName);
      if (!clean) return '';
      return clean.toLowerCase().replace(/[^a-z0-9/_-]+/g, '').replace(/\/+/g, '/').replace(/^\/|\/$/g, '');
    }

    function cfgDocLocaleAssetUrl(locale) {
      const cleanLocale = normalizeWebUiLocale(locale);
      const base = '/api/cfgdoc/i18n?locale=' + encodeURIComponent(cleanLocale);
      return assetUrl(base);
    }

    async function loadCfgDocI18nBundle(locale, forceReload) {
      const cleanLocale = normalizeWebUiLocale(locale);
      if (!forceReload && flowCfgDocI18nLocale === cleanLocale && flowCfgDocI18nMap && Object.keys(flowCfgDocI18nMap).length > 0) {
        cfgI18nDebugLog('cfgdoc i18n cache hit', { locale: cleanLocale, entries: Object.keys(flowCfgDocI18nMap).length });
        return true;
      }
      if (!forceReload && flowCfgDocI18nPromise) {
        return flowCfgDocI18nPromise;
      }

      flowCfgDocI18nPromise = (async () => {
        try {
          const payload = await fetchOkJson(
            cfgDocLocaleAssetUrl(cleanLocale),
            { cache: 'no-store' },
            'traductions cfgdoc indisponibles'
          );
          const source = payload && payload.translations && typeof payload.translations === 'object'
            ? payload.translations
            : payload;
          if (!source || typeof source !== 'object') return false;
          const mapped = {};
          Object.keys(source).forEach((rawKey) => {
            if (typeof source[rawKey] !== 'string') return;
            const key = String(rawKey || '').trim();
            if (!key) return;
            mapped[key] = source[rawKey];
          });
          flowCfgDocI18nLocale = cleanLocale;
          flowCfgDocI18nMap = mapped;
          cfgI18nDebugLog('cfgdoc i18n loaded', {
            locale: cleanLocale,
            entries: Object.keys(mapped).length,
            sampleWifiLabel: mapped['cfgdocs.wifi.enabled.label'] || null,
            sampleCfgmodLabel: mapped['cfgmods.network.wifi.label'] || null
          });
          return true;
        } catch (err) {
          cfgI18nDebugLog('cfgdoc i18n load failed', { locale: cleanLocale, error: String(err) });
          return false;
        } finally {
          flowCfgDocI18nPromise = null;
        }
      })();

      return flowCfgDocI18nPromise;
    }

    function cfgDocResolveLocalizedText(docLike, field) {
      if (!docLike || typeof docLike !== 'object') return '';
      const tokenField = field === 'label' ? 'label_i18n' : 'help_i18n';
      const legacyTokenField = field === 'label' ? 'label_t' : 'help_t';
      const fallback = typeof docLike[field] === 'string' ? docLike[field] : '';
      const token = typeof docLike[tokenField] === 'string' && docLike[tokenField].trim()
        ? docLike[tokenField]
        : (typeof docLike[legacyTokenField] === 'string' && docLike[legacyTokenField].trim()
          ? docLike[legacyTokenField]
          : '');
      if (!token) return fallback;
      return cfgDocTr(token, fallback);
    }

    function cfgDocApplyLocalizedText(docLike) {
      if (!docLike || typeof docLike !== 'object') return docLike;
      const out = Object.assign({}, docLike);
      const nextLabel = cfgDocResolveLocalizedText(out, 'label');
      const nextHelp = cfgDocResolveLocalizedText(out, 'help');
      if (nextLabel) out.label = nextLabel;
      if (nextHelp) out.help = nextHelp;
      return out;
    }

    function cfgDocApplyLocalizedEnumOptions(options) {
      if (!Array.isArray(options)) return options;
      return options.map((entry) => cfgDocApplyLocalizedText(entry));
    }

    async function refreshCfgDocLocaleRuntime(forceReload) {
      if (!flowCfgDocsLoaded) return;
      cfgI18nDebugLog('refreshCfgDocLocaleRuntime start', {
        locale: webUiLocale,
        forceReload: !!forceReload,
        activePage: getActivePageId()
      });
      const loaded = await loadCfgDocI18nBundle(webUiLocale, !!forceReload);
      if (!loaded) return;
      if (!isPageActive('page-control')) return;
      try {
        await ensureCfgDocsForModule(cfgTreeSelectedSource === 'supervisor' ? supCfgCurrentModule : flowCfgCurrentModule);
        if (cfgTreeSelectedSource === 'supervisor' && supCfgCurrentPdmExtension && supCfgCurrentPdmExtension.module) {
          await ensureCfgDocsForModule(supCfgCurrentPdmExtension.module);
        } else if (cfgTreeSelectedSource !== 'supervisor' && flowCfgCurrentPdmExtension && flowCfgCurrentPdmExtension.module) {
          await ensureCfgDocsForModule(flowCfgCurrentPdmExtension.module);
        }
      } catch (err) {
      }
      renderFlowCfgTree();
      renderFlowCfgCurrentPath(cfgTreeSelectedSource, currentCfgTreePath(cfgTreeSelectedSource), cfgNodeForPath(cfgTreeSelectedSource, currentCfgTreePath(cfgTreeSelectedSource)));
      if (cfgTreeSelectedSource === 'supervisor') {
        if (supCfgCurrentModule && supCfgCurrentData && typeof supCfgCurrentData === 'object') {
          renderPrimarySupervisorCfgFieldsWithExtensions(supCfgCurrentData);
        }
      } else if (flowCfgCurrentModule && flowCfgCurrentData && typeof flowCfgCurrentData === 'object') {
        renderFlowCfgFieldsWithExtensions(flowCfgCurrentData);
      }
      cfgI18nDebugLog('refreshCfgDocLocaleRuntime done', {
        locale: webUiLocale,
        source: cfgTreeSelectedSource,
        supervisorModule: supCfgCurrentModule,
        flowModule: flowCfgCurrentModule
      });
    }

    async function loadCfgDocIndex() {
      if (flowCfgDocIndex) return flowCfgDocIndex;
      if (flowCfgDocIndexUnavailable) throw new Error('cfgdoc_index_unavailable');
      if (flowCfgDocIndexPromise) return flowCfgDocIndexPromise;

      flowCfgDocIndexPromise = (async () => {
        try {
          const data = await fetchOkJson(
            '/api/cfgdoc/index',
            { cache: 'no-store' },
            'index de documentation indisponible'
          );
          const docs = (data && data.docs && typeof data.docs === 'object') ? data.docs : {};
          const meta = (data && data.meta && typeof data.meta === 'object')
            ? data.meta
            : ((data && data._meta && typeof data._meta === 'object') ? data._meta : {});
          const modules = (data && data.modules && typeof data.modules === 'object') ? data.modules : {};
          flowCfgDocIndex = { docs: docs, meta: meta, modules: modules };
          flowCfgDocIndexUnavailable = false;
          return flowCfgDocIndex;
        } catch (err) {
          flowCfgDocIndexUnavailable = true;
          throw err;
        }
      })().finally(() => {
        flowCfgDocIndexPromise = null;
      });

      return flowCfgDocIndexPromise;
    }

    async function getCfgDocForModule(moduleName) {
      const moduleKey = cfgDocKeyFromModuleName(moduleName);
      const cacheKey = moduleKey || '__root';
      if (flowCfgDocModuleCache.has(cacheKey)) {
        return flowCfgDocModuleCache.get(cacheKey);
      }
      if (flowCfgDocModuleLoadPromises.has(cacheKey)) {
        return flowCfgDocModuleLoadPromises.get(cacheKey);
      }

      const loadPromise = (async () => {
        try {
          const index = await loadCfgDocIndex();
          const relativePath = (index && index.modules && typeof index.modules[cacheKey] === 'string')
            ? String(index.modules[cacheKey]).trim()
            : '';
          if (!relativePath) return null;
          const payload = await fetchOkJson(
            '/api/cfgdoc/module?name=' + encodeURIComponent(cacheKey),
            { cache: 'no-store' },
            'documentation indisponible pour ' + cacheKey
          );
          const docs = (payload && payload.docs && typeof payload.docs === 'object') ? payload.docs : {};
          const meta = (payload && payload.meta && typeof payload.meta === 'object')
            ? payload.meta
            : ((payload && payload._meta && typeof payload._meta === 'object') ? payload._meta : {});
          const normalized = normalizeDocSource({ docs: docs, meta: meta });
          flowCfgDocModuleCache.set(cacheKey, normalized);
          return normalized;
        } catch (err) {
          return null;
        }
      })().finally(() => {
        flowCfgDocModuleLoadPromises.delete(cacheKey);
      });

      flowCfgDocModuleLoadPromises.set(cacheKey, loadPromise);
      return loadPromise;
    }

    async function ensureCfgDocsForModule(moduleName) {
      await loadCfgDocI18nBundle(webUiLocale, false);
      await getCfgDocForModule(moduleName);
      const baseSources = [];
      if (flowCfgDocIndex) {
        const idxSource = normalizeDocSource({ docs: flowCfgDocIndex.docs || {}, meta: flowCfgDocIndex.meta || {} });
        if (idxSource) baseSources.push(idxSource);
      }
      for (const source of flowCfgDocModuleCache.values()) {
        const normalized = normalizeDocSource(source);
        if (normalized) baseSources.push(normalized);
      }
      cfgDocSources = baseSources;
      chargerCfgTreeMetaDepuisDocs();
    }

    async function chargerFlowCfgDocs() {
      flowCfgDocsLoaded = true;
      try {
        await ensureCfgDocsForModule('');
      } catch (err) {
        cfgDocSources = [];
        cfgTreeAliases = [];
        cfgTreeVirtualBranches = [];
      }
    }

    function normalizeDocSource(source) {
      if (!source || typeof source !== 'object') return null;
      const docs = (source.docs && typeof source.docs === 'object') ? source.docs : {};
      const meta = (source.meta && typeof source.meta === 'object')
        ? source.meta
        : ((source._meta && typeof source._meta === 'object') ? source._meta : {});
      return { docs: docs, meta: meta };
    }

    function chargerCfgTreeMetaDepuisDocs() {
      const aliases = [];
      const virtualBranches = [];
      const seenAliasKeys = new Set();
      const seenBranchKeys = new Set();

      for (const src of cfgDocSources) {
        const normalized = normalizeDocSource(src);
        if (!normalized || !normalized.meta) continue;

        const aliasEntries = Array.isArray(normalized.meta.cfg_tree_aliases)
          ? normalized.meta.cfg_tree_aliases
          : [];
        aliasEntries.forEach((entry) => {
          const display = nettoyerNomFlowCfg(entry && entry.display);
          const store = nettoyerNomFlowCfg(entry && entry.store);
          if (!display || !store) return;
          const key = display + '->' + store;
          if (seenAliasKeys.has(key)) return;
          seenAliasKeys.add(key);
          aliases.push({ display: display, store: store });
        });

        const branchEntries = Array.isArray(normalized.meta.cfg_tree_virtual_branches)
          ? normalized.meta.cfg_tree_virtual_branches
          : [];
        branchEntries.forEach((entry) => {
          const display = nettoyerNomFlowCfg(entry && entry.display);
          if (!display || seenBranchKeys.has(display)) return;
          const children = Array.isArray(entry && entry.children)
            ? entry.children.map((child) => nettoyerNomFlowCfg(child)).filter((child) => child.length > 0)
            : [];
          seenBranchKeys.add(display);
          virtualBranches.push({ display: display, children: children });
        });
      }

      cfgTreeAliases = aliases;
      cfgTreeVirtualBranches = virtualBranches;
    }

    function resolveEnumOptions(enumSetName, sources) {
      const setName = String(enumSetName || '').trim();
      if (!setName) return null;
      for (const src of sources) {
        const normalized = normalizeDocSource(src);
        if (!normalized) continue;
        const enumSets = (normalized.meta && normalized.meta.enum_sets &&
          typeof normalized.meta.enum_sets === 'object')
          ? normalized.meta.enum_sets
          : null;
        if (enumSets && Array.isArray(enumSets[setName])) {
          return enumSets[setName];
        }
      }
      return null;
    }

    function enrichResolvedDoc(doc, sources) {
      if (!doc || typeof doc !== 'object') return null;
      const resolved = cfgDocApplyLocalizedText(doc);
      const enumSetName = (typeof resolved.enum_set === 'string') ? resolved.enum_set.trim() : '';
      const enumOptions = resolveEnumOptions(enumSetName, sources);
      if (enumSetName && Array.isArray(enumOptions)) {
        resolved._enumOptions = cfgDocApplyLocalizedEnumOptions(enumOptions);
      }
      return resolved;
    }

    function poolLogicDeviceSlotSource(source) {
      return source === 'supervisor' ? 'supervisor' : 'flow';
    }

    function poolLogicDeviceSlotRef(slot) {
      const n = Number.parseInt(slot, 10);
      if (!Number.isFinite(n) || n < 0 || n > 15) return '';
      return 'd' + String(n).padStart(2, '0');
    }

    function poolLogicDeviceIoOutputModule(slot) {
      const ref = poolLogicDeviceSlotRef(slot);
      return ref ? ('io/output/' + ref) : '';
    }

    function poolLogicDeviceIoOutputNameKey(slot) {
      const ref = poolLogicDeviceSlotRef(slot);
      return ref ? (ref + '_name') : '';
    }

    function isPoolLogicDeviceSlotField(moduleName, key, doc) {
      const cleanModule = nettoyerNomFlowCfg(moduleName).toLowerCase();
      const cleanKey = String(key || '').trim().toLowerCase();
      if (cleanModule !== 'poollogic/devices') return false;
      if (!cleanKey.endsWith('_slot')) return false;
      return !doc || String(doc.enum_set || '').trim() === 'poollogic_device_slot';
    }

    function poolLogicDeviceSlotLabel(source, slot, fallback) {
      const n = Number.parseInt(slot, 10);
      const ref = poolLogicDeviceSlotRef(n);
      if (!ref) return String(fallback || slot);
      const src = poolLogicDeviceSlotSource(source);
      const cache = poolLogicDeviceIoOutputNames[src] || {};
      const ioName = typeof cache[n] === 'string' ? cache[n].trim() : '';
      const baseName = ioName || String(ioOutputPdmLabels[n] || '').trim();
      const suffix = baseName ? (' [' + baseName + ']') : '';
      return 'pd' + String(n) + ' - ' + ref + suffix;
    }

    function dynamicPoolLogicDeviceSlotOptions(source, enumOptions) {
      const byValue = {};
      if (Array.isArray(enumOptions)) {
        enumOptions.forEach((opt) => {
          if (!opt || typeof opt !== 'object') return;
          const value = Number.parseInt(opt.value, 10);
          if (Number.isFinite(value)) byValue[value] = opt;
        });
      }
      const out = [];
      for (let slot = 0; slot <= 15; slot += 1) {
        const base = byValue[slot] ? Object.assign({}, byValue[slot]) : { value: slot };
        base.value = slot;
        base.label = poolLogicDeviceSlotLabel(source, slot, base.label);
        out.push(base);
      }
      return out;
    }

    function configEnumOptionsForField(source, moduleName, key, doc) {
      const options = (doc && Array.isArray(doc._enumOptions)) ? doc._enumOptions : null;
      if (!options) return null;
      if (isWaveshareProfile() && isPoolLogicDeviceSlotField(moduleName, key, doc)) {
        return dynamicPoolLogicDeviceSlotOptions(source, options);
      }
      return options;
    }

    async function loadPoolLogicDeviceSlotLabels(source, forceReload) {
      const src = poolLogicDeviceSlotSource(source);
      if (!poolLogicDeviceIoOutputNames[src]) poolLogicDeviceIoOutputNames[src] = {};
      const cache = poolLogicDeviceIoOutputNames[src];
      const fetchOne = async (slot) => {
        if (!forceReload && Object.prototype.hasOwnProperty.call(cache, slot)) return;
        const moduleName = poolLogicDeviceIoOutputModule(slot);
        const nameKey = poolLogicDeviceIoOutputNameKey(slot);
        if (!moduleName || !nameKey) return;
        try {
          const url = src === 'supervisor'
            ? ('/api/supervisorcfg/module?name=' + encodeURIComponent(moduleName))
            : ('/api/flowcfg/module?name=' + encodeURIComponent(moduleName));
          const res = src === 'supervisor'
            ? await fetchWithBusyRetry(url, { cache: 'no-store' })
            : await fetchFlowRemoteQueued(url, { cache: 'no-store' });
          const payload = await res.json().catch(() => null);
          if (!res.ok || !payload || payload.ok !== true || !payload.data || typeof payload.data !== 'object') {
            cache[slot] = '';
            return;
          }
          const raw = payload.data[nameKey];
          cache[slot] = (typeof raw === 'string') ? raw.trim() : '';
        } catch (err) {
          cache[slot] = '';
        }
      };
      const jobs = [];
      for (let slot = 0; slot <= 15; slot += 1) {
        jobs.push(fetchOne(slot));
      }
      await Promise.all(jobs);
    }

    function closeColorPickerPopover() {
      if (!activeColorPickerPopover) return;
      const state = activeColorPickerPopover;
      activeColorPickerPopover = null;
      if (state.outsideHandler) {
        document.removeEventListener('mousedown', state.outsideHandler, true);
      }
      if (state.keyHandler) {
        document.removeEventListener('keydown', state.keyHandler, true);
      }
      if (state.repositionHandler) {
        window.removeEventListener('resize', state.repositionHandler, true);
        window.removeEventListener('scroll', state.repositionHandler, true);
      }
      if (state.popover && state.popover.parentNode) {
        state.popover.parentNode.removeChild(state.popover);
      }
    }

    function enumOptionColor(enumOptions, value) {
      if (!Array.isArray(enumOptions)) return '';
      const currentValue = String(value ?? '');
      for (const opt of enumOptions) {
        if (!opt || typeof opt !== 'object') continue;
        if (String(opt.value) !== currentValue) continue;
        return (typeof opt.color === 'string') ? opt.color.trim() : '';
      }
      return '';
    }

    function positionColorPickerPopover(popover, anchorEl) {
      if (!popover || !anchorEl) return;
      const anchorRect = anchorEl.getBoundingClientRect();
      const popRect = popover.getBoundingClientRect();
      const margin = 12;
      let left = anchorRect.left + (anchorRect.width / 2) - (popRect.width / 2);
      let top = anchorRect.bottom + 10;
      left = Math.max(margin, Math.min(left, window.innerWidth - popRect.width - margin));
      if (top + popRect.height > window.innerHeight - margin) {
        top = Math.max(margin, anchorRect.top - popRect.height - 10);
      }
      popover.style.left = Math.round(left) + 'px';
      popover.style.top = Math.round(top) + 'px';
    }

    function updateColorTriggerVisual(trigger, enumOptions, value) {
      if (!trigger) return;
      const color = enumOptionColor(enumOptions, value) || '#FFFFFF';
      trigger.style.background = color;
      trigger.dataset.color = color;
      trigger.setAttribute('aria-label', 'Couleur ' + color);
      trigger.title = color;
    }

    function openColorPickerPopover(trigger, inputEl, enumOptions) {
      if (!trigger || !inputEl || !Array.isArray(enumOptions) || !enumOptions.length) return;
      closeColorPickerPopover();

      const popover = document.createElement('div');
      popover.className = 'color-picker-popover';
      popover.setAttribute('role', 'dialog');
      popover.setAttribute('aria-modal', 'false');

      const grid = document.createElement('div');
      grid.className = 'color-picker-grid';
      const currentValue = String(inputEl.value ?? '');
      enumOptions.forEach((opt) => {
        if (!opt || typeof opt !== 'object') return;
        const color = (typeof opt.color === 'string') ? opt.color.trim() : '';
        if (!color) return;
        const swatch = document.createElement('button');
        swatch.type = 'button';
        swatch.className = 'color-picker-swatch';
        swatch.style.background = color;
        swatch.dataset.color = color;
        swatch.setAttribute('aria-label', color);
        swatch.title = color;
        if (String(opt.value) === currentValue) {
          swatch.classList.add('is-selected');
        }
        swatch.addEventListener('click', () => {
          inputEl.value = String(opt.value);
          updateColorTriggerVisual(trigger, enumOptions, inputEl.value);
          inputEl.dispatchEvent(new Event('input', { bubbles: true }));
          inputEl.dispatchEvent(new Event('change', { bubbles: true }));
          closeColorPickerPopover();
        });
        grid.appendChild(swatch);
      });
      popover.appendChild(grid);
      document.body.appendChild(popover);
      positionColorPickerPopover(popover, trigger);

      const outsideHandler = (event) => {
        const target = event && event.target;
        if (popover.contains(target) || trigger.contains(target)) return;
        closeColorPickerPopover();
      };
      const keyHandler = (event) => {
        if (event && event.key === 'Escape') {
          closeColorPickerPopover();
        }
      };
      const repositionHandler = () => {
        if (!activeColorPickerPopover || activeColorPickerPopover.popover !== popover) return;
        positionColorPickerPopover(popover, trigger);
      };

      document.addEventListener('mousedown', outsideHandler, true);
      document.addEventListener('keydown', keyHandler, true);
      window.addEventListener('resize', repositionHandler, true);
      window.addEventListener('scroll', repositionHandler, true);
      activeColorPickerPopover = { popover, trigger, outsideHandler, keyHandler, repositionHandler };
    }

    function createColorPickerControl(doc, key, value, enumOptions) {
      const input = document.createElement('input');
      input.type = 'hidden';
      input.className = 'control-color-input';
      input.dataset.key = key;
      input.dataset.kind = configNumericKind(doc, value);
      input.dataset.label = (doc && typeof doc.label === 'string' && doc.label.length > 0) ? doc.label : key;
      input.value = String(value ?? '');
      storeConfigFieldInitialValue(input, value);

      const trigger = document.createElement('button');
      trigger.type = 'button';
      trigger.className = 'control-color-trigger';
      trigger.setAttribute('aria-haspopup', 'dialog');
      updateColorTriggerVisual(trigger, enumOptions, input.value);
      trigger.addEventListener('click', (event) => {
        event.preventDefault();
        event.stopPropagation();
        if (activeColorPickerPopover && activeColorPickerPopover.trigger === trigger) {
          closeColorPickerPopover();
          return;
        }
        openColorPickerPopover(trigger, input, enumOptions);
      });

      return { input, trigger };
    }

    function formatConfigValueForDisplay(value, displayFormat) {
      if (displayFormat === 'hex' && typeof value === 'number' && Number.isFinite(value)) {
        const raw = Math.max(0, Math.trunc(value));
        const width = raw <= 0xFF ? 2 : 0;
        const hex = raw.toString(16).toUpperCase();
        return '0x' + (width > 0 ? hex.padStart(width, '0') : hex);
      }
      return String(value ?? '');
    }

    function parseConfigNumericValueDetailed(rawValue, kind, displayFormat) {
      if (displayFormat === 'hex') {
        if (typeof rawValue === 'number' && Number.isFinite(rawValue)) {
          return { ok: true, value: Math.max(0, Math.trunc(rawValue)) };
        }
        const raw = String(rawValue ?? '').trim();
        const normalized = raw.length > 0 ? raw : '0';
        if (!/^(?:0[xX])?[0-9A-Fa-f]+$/.test(normalized)) {
          return {
            ok: false,
            value: 0,
            error: 'utilisez une valeur hexadécimale valide, par exemple 0x77'
          };
        }
        const parsed = Number.parseInt(normalized, 16);
        if (!Number.isFinite(parsed)) {
          return {
            ok: false,
            value: 0,
            error: 'utilisez une valeur hexadécimale valide, par exemple 0x77'
          };
        }
        return { ok: true, value: parsed };
      }
      if (typeof rawValue === 'number' && Number.isFinite(rawValue)) {
        return { ok: true, value: kind === 'float' ? rawValue : Math.trunc(rawValue) };
      }
      const raw = String(rawValue ?? '').trim();
      if (kind === 'float') {
        const parsed = Number.parseFloat(raw);
        return { ok: true, value: Number.isFinite(parsed) ? parsed : 0 };
      }
      const parsed = Number.parseInt(raw, 10);
      return { ok: true, value: Number.isFinite(parsed) ? parsed : 0 };
    }

    function parseConfigNumericValue(rawValue, kind, displayFormat) {
      const parsed = parseConfigNumericValueDetailed(rawValue, kind, displayFormat);
      return parsed.value;
    }

    function setConfigFieldValidationState(inputEl, ok, message) {
      if (!inputEl) return ok;
      const row = inputEl.closest('.control-row');
      const validationMessage = ok ? '' : String(message || 'Valeur invalide');
      if (typeof inputEl.setCustomValidity === 'function') {
        inputEl.setCustomValidity(validationMessage);
      }
      if (ok) {
        inputEl.removeAttribute('aria-invalid');
        inputEl.removeAttribute('title');
      } else {
        inputEl.setAttribute('aria-invalid', 'true');
        inputEl.setAttribute('title', validationMessage);
      }
      if (row) {
        row.classList.toggle('is-invalid', !ok);
      }
      return ok;
    }

    function validateConfigFieldValue(inputEl, options) {
      const opts = options || {};
      if (!inputEl) return true;
      const kind = String(inputEl.dataset.kind || '').trim();
      const displayFormat = String(inputEl.dataset.format || '').trim();
      if ((kind !== 'int' && kind !== 'float') || displayFormat !== 'hex') {
        return setConfigFieldValidationState(inputEl, true, '');
      }
      const parsed = parseConfigNumericValueDetailed(inputEl.value, kind, displayFormat);
      const ok = setConfigFieldValidationState(inputEl, !!parsed.ok, parsed.error || '');
      if (!ok && !opts.silent && typeof inputEl.reportValidity === 'function') {
        inputEl.reportValidity();
      }
      return ok;
    }

    function readConfigFieldValueStrict(inputEl) {
      if (!inputEl) return null;
      const kind = String(inputEl.dataset.kind || '').trim();
      const displayFormat = String(inputEl.dataset.format || '').trim();
      if ((kind === 'int' || kind === 'float') && displayFormat === 'hex') {
        const parsed = parseConfigNumericValueDetailed(inputEl.value, kind, displayFormat);
        if (!parsed.ok) {
          validateConfigFieldValue(inputEl);
          const label = String(inputEl.dataset.label || inputEl.dataset.key || 'champ').trim();
          throw new Error(label + ' : ' + parsed.error);
        }
        return parsed.value;
      }
      return readConfigFieldValue(inputEl);
    }

    function updatePrimaryCfgApplyState() {
      if (!flowCfgApplyBtn) return;
      if (flowCfgApplyBtn.hidden) {
        flowCfgApplyBtn.disabled = true;
        flowCfgApplyBtn.removeAttribute('title');
        return;
      }
      const fields = flowCfgFields ? Array.from(flowCfgFields.querySelectorAll('[data-key]')) : [];
      let hasDirty = false;
      let hasInvalid = false;
      fields.forEach((el) => {
        if (!el || typeof el !== 'object') return;
        if (el.dataset.runtimeHidden === '1') return;
        if (!validateConfigFieldValue(el, { silent: true })) {
          hasInvalid = true;
        }
        if (configFieldIsDirty(el)) {
          hasDirty = true;
        }
      });
      flowCfgApplyBtn.disabled = !hasDirty || hasInvalid;
      if (hasInvalid) {
        flowCfgApplyBtn.title = 'Corrigez les champs invalides avant application';
      } else if (!hasDirty) {
        flowCfgApplyBtn.title = 'Aucun changement a appliquer';
      } else {
        flowCfgApplyBtn.title = 'Appliquer les changements';
      }
    }

    function configNumericKind(doc, value) {
      const typeName = String((doc && doc.type) || '').trim().toLowerCase();
      if (typeName === 'float' || typeName === 'double') {
        return 'float';
      }
      if (typeName === 'int32' || typeName === 'uint16' || typeName === 'uint8') {
        return 'int';
      }
      if (typeName === 'bool' || typeName === 'boolean') {
        return 'bool';
      }
      if (typeof value === 'number') {
        return Number.isInteger(value) ? 'int' : 'float';
      }
      if (typeof value === 'boolean') {
        return 'bool';
      }
      return 'string';
    }

    function configFieldNormalizedInitialValue(doc, value) {
      const numericKind = configNumericKind(doc, value);
      if (numericKind === 'int' || numericKind === 'float') {
        const displayFormat = (doc && typeof doc.display_format === 'string') ? doc.display_format : '';
        return parseConfigNumericValue(value, numericKind, displayFormat);
      }
      return value;
    }

    function configIsBindingPortField(moduleName, key) {
      if (String(key || '').trim() !== 'binding_port') return false;
      const modulePath = String(moduleName || '').trim().toLowerCase();
      return /^io\/(?:input\/(?:a\d{2}|i\d{2})|output\/d\d{2})$/.test(modulePath);
    }

    function configNormalizeBindingPortSelectValue(value) {
      const current = String(value ?? '').trim();
      return (current.length === 0 || current === '65535') ? '0' : current;
    }

    function configDocFor(moduleName, key, extraSources) {
      const k = String(key || '').trim();
      const candidates = cfgDocPathCandidates(moduleName);
      if (candidates.length === 0 || !k) return null;

      const sources = [];
      if (Array.isArray(extraSources)) {
        extraSources.forEach((src) => {
          const normalized = normalizeDocSource(src);
          if (normalized) sources.push(normalized);
        });
      }
      cfgDocSources.forEach((src) => {
        const normalized = normalizeDocSource(src);
        if (normalized) sources.push(normalized);
      });

      let merged = null;
      for (const source of sources) {
        const docs = source.docs;
        const wildcard = docs['*/' + k];
        if (wildcard && typeof wildcard === 'object') {
          merged = Object.assign(merged || {}, wildcard);
        }
        candidates.forEach((candidate) => {
          const exact = docs[candidate + '/' + k];
          if (exact && typeof exact === 'object') {
            merged = Object.assign(merged || {}, exact);
          }
        });
      }
      return enrichResolvedDoc(merged, sources);
    }

    function configPathMeta(pathValue) {
      const candidates = cfgDocPathCandidates(pathValue);
      if (candidates.length === 0) return null;
      const sources = [];
      for (const src of cfgDocSources) {
        const normalized = normalizeDocSource(src);
        if (!normalized) continue;
        sources.push(normalized);
      }
      let merged = null;
      for (const normalized of sources) {
        candidates.forEach((candidate) => {
          const exact = normalized.docs[candidate];
          if (exact && typeof exact === 'object') {
            merged = Object.assign(merged || {}, exact);
          }
        });
      }
      return enrichResolvedDoc(merged, sources);
    }

    function isConfigPathHidden(pathValue) {
      const meta = configPathMeta(pathValue);
      return !!(meta && meta.hidden === true);
    }

    function flowCfgApplyPerFieldEnabled(moduleName) {
      const meta = configPathMeta(moduleName);
      return !!(meta && meta.apply_per_field === true);
    }

    function isDigitalInputConfigModule(moduleName) {
      const modulePath = String(moduleName || '').trim().toLowerCase().replace(/\/+$/, '');
      return /^io\/input\/i\d{2}$/.test(modulePath);
    }

    function normalizeDigitalInputConfigKey(moduleName, key) {
      const rawKey = String(key || '').trim().toLowerCase();
      if (!rawKey) return '';
      const modulePath = String(moduleName || '').trim().toLowerCase().replace(/\/+$/, '');
      if (modulePath && rawKey.startsWith(modulePath + '/')) {
        return rawKey.slice(modulePath.length + 1);
      }
      const slashIdx = rawKey.lastIndexOf('/');
      return slashIdx >= 0 ? rawKey.slice(slashIdx + 1) : rawKey;
    }

    function parseDigitalInputModeValue(rawValue) {
      if (typeof rawValue === 'number' && Number.isFinite(rawValue)) return rawValue;
      const txt = String(rawValue ?? '').trim().toLowerCase();
      if (!txt) return NaN;
      if (txt === '0') return 0;
      if (txt === '1') return 1;
      if (txt.includes('etat')) return 0;
      if (txt.includes('compteur')) return 1;
      return NaN;
    }

    function isCounterModeOnlyConfigField(moduleName, key, doc) {
      if (!isDigitalInputConfigModule(moduleName)) return false;
      const cleanKey = normalizeDigitalInputConfigKey(moduleName, key);
      if (!cleanKey || cleanKey === 'mode') return false;
      if (cleanKey === 'counter_total' || cleanKey === 'edge_mode') return true;
      if (/^i\d{2}_(?:c0|prec)$/.test(cleanKey)) return true;
      const helpTxt = String((doc && doc.help) || '').toLowerCase();
      if (!helpTxt) return false;
      return helpTxt.includes('mode compteur') || helpTxt.includes('compteur d\'impulsion');
    }

    function resetPrimaryCfgEditor(message) {
      supCfgCurrentPdmExtension = null;
      flowCfgFields.innerHTML = '';
      flowCfgApplyBtn.hidden = false;
      flowCfgApplyBtn.disabled = true;
      if (message) {
        flowCfgStatus.textContent = message;
      }
    }

    function resetFlowCfgEditor(message) {
      flowCfgCurrentModule = '';
      flowCfgCurrentData = {};
      flowCfgCurrentPdmExtension = null;
      resetPrimaryCfgEditor(message);
    }

    function storeConfigFieldInitialValue(el, value) {
      if (!el) return;
      el.dataset.initialValue = JSON.stringify(value);
    }

    function readConfigFieldValue(el) {
      if (!el) return null;
      const kind = String(el.dataset.kind || '').trim();
      const displayFormat = String(el.dataset.format || '').trim();
      if (kind === 'bool') {
        if (el.tagName === 'SELECT') {
          const raw = String(el.value ?? '').trim().toLowerCase();
          return raw === 'true' || raw === '1' || raw === 'on';
        }
        return !!el.checked;
      }
      if (kind === 'int' || kind === 'float') {
        return parseConfigNumericValue(el.value, kind, displayFormat);
      }
      const masked = el.dataset.masked === '1';
      const raw = String(el.value ?? '');
      if (masked && raw.length === 0) {
        try {
          return JSON.parse(el.dataset.initialValue || 'null');
        } catch (err) {
          return '';
        }
      }
      return raw;
    }

    function configFieldIsDirty(el) {
      if (!el) return false;
      let initialValue = null;
      try {
        initialValue = JSON.parse(el.dataset.initialValue || 'null');
      } catch (err) {
        initialValue = null;
      }
      return JSON.stringify(readConfigFieldValue(el)) !== JSON.stringify(initialValue);
    }

    function flowCfgLocalApplyMessage(message) {
      return String(message || '').trim() || tr('cfg.apply.busy', 'Application de la configuration en cours...');
    }

    function flowCfgSetControlsLocked(locked) {
      const page = document.getElementById('page-control');
      if (!page) return;
      const nodes = page.querySelectorAll('.cfg-tree button,.control-fields input,.control-fields select,.control-fields textarea,.control-field-apply,#flowCfgApply,#flowCfgRefresh');
      nodes.forEach((node) => {
        if (!node) return;
        if (locked) {
          if (!Object.prototype.hasOwnProperty.call(node.dataset, 'cfgApplyPrevDisabled')) {
            node.dataset.cfgApplyPrevDisabled = node.disabled ? '1' : '0';
          }
          node.disabled = true;
          return;
        }
        if (!Object.prototype.hasOwnProperty.call(node.dataset, 'cfgApplyPrevDisabled')) return;
        node.disabled = node.dataset.cfgApplyPrevDisabled === '1';
        delete node.dataset.cfgApplyPrevDisabled;
      });
    }

    function setFlowCfgLocalApplyBusy(active, message) {
      flowCfgLocalApplyBusyDepth = Math.max(0, flowCfgLocalApplyBusyDepth + (active ? 1 : -1));
      const busy = flowCfgLocalApplyBusyDepth > 0;
      const text = flowCfgLocalApplyMessage(message);
      const page = document.getElementById('page-control');
      if (page) {
        page.classList.toggle('is-config-applying', busy);
        page.setAttribute('aria-busy', busy ? 'true' : 'false');
      }
      if (flowCfgApplyBusy) {
        flowCfgApplyBusy.hidden = !busy;
        const label = flowCfgApplyBusy.querySelector('span:last-child');
        if (label) label.textContent = text;
      }
      if (flowCfgStatus && busy) {
        flowCfgStatus.textContent = text;
      }
      if (flowCfgApplyBtn) {
        if (busy) {
          if (!flowCfgApplyBtnSavedText) flowCfgApplyBtnSavedText = flowCfgApplyBtn.textContent || '';
          flowCfgApplyBtn.classList.add('is-pending');
          flowCfgApplyBtn.textContent = tr('cfg.apply.busyShort', 'Application...');
        } else {
          flowCfgApplyBtn.classList.remove('is-pending');
          if (flowCfgApplyBtnSavedText) {
            flowCfgApplyBtn.textContent = flowCfgApplyBtnSavedText;
            flowCfgApplyBtnSavedText = '';
          }
        }
      }
      flowCfgSetControlsLocked(busy);
      if (!busy) {
        updatePrimaryCfgApplyState();
        document.querySelectorAll('.control-field-apply').forEach((button) => {
          const row = button && button.closest('.control-row');
          const input = row && row.querySelector('input.control-input,select.control-input,textarea.control-input');
          if (input) updateControlFieldApplyState(input, button);
        });
      }
    }

    function updateControlFieldApplyState(inputEl, applyBtn) {
      if (!inputEl || !applyBtn) return;
      const valid = validateConfigFieldValue(inputEl, { silent: true });
      const dirty = configFieldIsDirty(inputEl);
      const busy = flowCfgLocalApplyBusyDepth > 0;
      applyBtn.disabled = busy || !dirty || !valid;
      applyBtn.classList.toggle('is-dirty', dirty && valid);
      if (!busy) applyBtn.classList.remove('is-pending');
      applyBtn.title = !valid
        ? 'Corrigez ce champ avant application'
        : (busy ? tr('cfg.apply.busy', 'Application de la configuration en cours...') : (dirty ? 'Appliquer ce changement' : 'Aucun changement a appliquer'));
      applyBtn.setAttribute('aria-label', applyBtn.title);
      const row = inputEl.closest('.control-row');
      if (row) row.classList.toggle('is-dirty', dirty);
    }

    function buildFlowCfgSingleFieldPatchJson(moduleName, inputEl) {
      if (!moduleName || !inputEl) throw new Error('champ non disponible');
      const key = String(inputEl.dataset.key || '').trim();
      if (!key) throw new Error('cle de configuration absente');
      const targetModule = nettoyerNomFlowCfg(inputEl.dataset.module || moduleName);
      if (!targetModule) throw new Error('branche de configuration absente');
      const patch = {};
      patch[targetModule] = {
        [key]: readConfigFieldValueStrict(inputEl)
      };
      return JSON.stringify(patch);
    }

    function renderConfigFields(containerEl, moduleName, dataObj, options) {
      const opts = options || {};
      const appendMode = !!opts.append;
      if (!appendMode) {
        closeColorPickerPopover();
        containerEl.innerHTML = '';
      }
      const data = (dataObj && typeof dataObj === 'object') ? dataObj : {};
      const perFieldApply = !!opts.perFieldApply;
      const controlsPrimaryPane = !!opts.controlsPrimaryPane;
      const onApplyField = typeof opts.onApplyField === 'function' ? opts.onApplyField : null;
      const sectionTitle = String(opts.sectionTitle || '').trim();
      let modeFieldInputEl = null;
      const visibilityEntries = [];
      if (controlsPrimaryPane) {
        flowCfgApplyBtn.hidden = perFieldApply;
      }
      const keys = Object.keys(data).sort();
      if (sectionTitle && keys.length > 0) {
        const sectionEl = document.createElement('div');
        sectionEl.className = 'control-section-title';
        sectionEl.textContent = sectionTitle;
        containerEl.appendChild(sectionEl);

        const dividerEl = document.createElement('div');
        dividerEl.className = 'control-divider';
        containerEl.appendChild(dividerEl);
      }
      if (keys.length === 0) {
        if (appendMode) return;
        const row = document.createElement('div');
        row.className = 'control-row';
        const label = document.createElement('span');
        label.className = 'control-label';
        label.textContent = 'Aucun champ configurable dans cette branche.';
        row.appendChild(label);
        containerEl.appendChild(row);
        if (controlsPrimaryPane && !perFieldApply) {
          updatePrimaryCfgApplyState();
        }
        return;
      }

      for (const key of keys) {
        const value = data[key];
        const row = document.createElement('div');
        row.className = 'control-row';

        const doc = configDocFor(moduleName, key, []);
        const labelWrap = document.createElement('div');
        labelWrap.className = 'control-label-wrap';
        const label = document.createElement('span');
        label.className = 'control-label';
        label.textContent = (doc && typeof doc.label === 'string' && doc.label.length > 0) ? doc.label : key;
        labelWrap.appendChild(label);

        const helpTxt = (doc && typeof doc.help === 'string') ? doc.help : '';
        if (helpTxt.length > 0) {
          const help = document.createElement('span');
          help.className = 'control-help';
          help.textContent = helpTxt;
          labelWrap.appendChild(help);
        }
        row.appendChild(labelWrap);

        const enumOptions = configEnumOptionsForField(opts.source || cfgTreeSelectedSource, moduleName, key, doc);
        let inputEl = null;
        const valueWrap = document.createElement('div');
        valueWrap.className = 'control-value-wrap';

        if (enumOptions && enumOptions.length > 0 && enumOptions.some((opt) => opt && typeof opt.color === 'string' && opt.color.trim().length > 0)) {
          const colorControl = createColorPickerControl(doc, key, value, enumOptions);
          inputEl = colorControl.input;
          inputEl.dataset.module = moduleName;
          valueWrap.classList.add('control-value-wrap-color');
          valueWrap.appendChild(colorControl.input);
          valueWrap.appendChild(colorControl.trigger);
        } else if (enumOptions && enumOptions.length > 0) {
          const select = document.createElement('select');
          select.className = 'control-input';
          select.dataset.key = key;
          select.dataset.kind = configNumericKind(doc, value);
          if (doc && typeof doc.display_format === 'string') {
            select.dataset.format = doc.display_format;
          }
          const isBindingPortField = configIsBindingPortField(moduleName, key);
          const currentValue = isBindingPortField ? configNormalizeBindingPortSelectValue(value) : String(value);
          let hasSelectedOption = false;
          enumOptions.forEach((opt) => {
            if (!opt || typeof opt !== 'object') return;
            const optionEl = document.createElement('option');
            optionEl.value = String(opt.value);
            optionEl.textContent = (typeof opt.label === 'string' && opt.label.length > 0)
              ? opt.label
              : String(opt.value);
            if (typeof opt.color === 'string' && opt.color.trim().length > 0) {
              optionEl.dataset.color = opt.color.trim();
            }
            if (optionEl.value === currentValue) {
              optionEl.selected = true;
              hasSelectedOption = true;
            }
            select.appendChild(optionEl);
          });
          if (!hasSelectedOption && currentValue.length > 0) {
            const placeholder = document.createElement('option');
            placeholder.value = currentValue;
            placeholder.textContent = isBindingPortField
              ? ('Port inconnu (' + currentValue + ')')
              : 'Valeur inconnue';
            placeholder.selected = true;
            select.insertBefore(placeholder, select.firstChild);
          }
          storeConfigFieldInitialValue(select, isBindingPortField ? parseConfigNumericValue(currentValue, 'int', '') : value);
          inputEl = select;
          inputEl.dataset.module = moduleName;
          valueWrap.appendChild(select);
        } else if (typeof value === 'boolean') {
          row.classList.add('control-row-bool');
          const sw = document.createElement('label');
          sw.className = 'md3-switch';
          const input = document.createElement('input');
          input.type = 'checkbox';
          input.checked = value;
          input.dataset.key = key;
          input.dataset.kind = 'bool';
          input.setAttribute('aria-label', label.textContent || key);
          const track = document.createElement('span');
          track.className = 'md3-track';
          const thumb = document.createElement('span');
          thumb.className = 'md3-thumb';
          storeConfigFieldInitialValue(input, value);
          sw.appendChild(input);
          sw.appendChild(track);
          sw.appendChild(thumb);
          inputEl = input;
          inputEl.dataset.module = moduleName;
          valueWrap.classList.add('control-value-wrap-bool');
          valueWrap.appendChild(sw);
        } else if (configNumericKind(doc, value) !== 'string') {
          const input = document.createElement('input');
          input.className = 'control-input';
          const displayFormat = (doc && typeof doc.display_format === 'string') ? doc.display_format : '';
          const numericKind = configNumericKind(doc, value);
          input.type = displayFormat === 'hex' ? 'text' : 'number';
          input.value = formatConfigValueForDisplay(
            configFieldNormalizedInitialValue(doc, value),
            displayFormat
          );
          if (displayFormat !== 'hex') {
            input.step = (numericKind === 'float') ? '0.001' : '1';
          }
          input.dataset.key = key;
          input.dataset.kind = numericKind;
          input.dataset.label = label.textContent || key;
          if (displayFormat) {
            input.dataset.format = displayFormat;
          }
          storeConfigFieldInitialValue(input, configFieldNormalizedInitialValue(doc, value));
          inputEl = input;
          inputEl.dataset.module = moduleName;
          valueWrap.appendChild(input);
        } else {
          const isSecret = /pass|token|secret/i.test(key);
          const textValue = String(value ?? '');
          const input = document.createElement('input');
          input.className = 'control-input';
          input.type = isSecret ? 'password' : 'text';
          if (isSecret && textValue === '***') {
            input.value = '';
            input.placeholder = 'Conserver (masqué)';
            input.dataset.masked = '1';
          } else {
            input.value = textValue;
            input.dataset.masked = '0';
          }
          input.dataset.key = key;
          input.dataset.kind = 'string';
          input.dataset.label = label.textContent || key;
          storeConfigFieldInitialValue(input, value);
          inputEl = input;
          inputEl.dataset.module = moduleName;
          valueWrap.appendChild(input);
        }

        if (normalizeDigitalInputConfigKey(moduleName, key) === 'mode') {
          modeFieldInputEl = inputEl;
        }

        if (perFieldApply && inputEl) {
          const applyBtn = document.createElement('button');
          applyBtn.type = 'button';
          applyBtn.className = 'control-field-apply';
          applyBtn.innerHTML = fieldApplyCheckIcon;
          applyBtn.disabled = true;
          applyBtn.title = 'Aucun changement a appliquer';
          applyBtn.setAttribute('aria-label', applyBtn.title);
          applyBtn.addEventListener('click', async () => {
            if (onApplyField) {
              await onApplyField(inputEl, applyBtn);
            }
          });

          const syncApplyState = () => updateControlFieldApplyState(inputEl, applyBtn);
          inputEl.addEventListener('input', syncApplyState);
          inputEl.addEventListener('change', syncApplyState);
          updateControlFieldApplyState(inputEl, applyBtn);
          valueWrap.appendChild(applyBtn);
        } else if (controlsPrimaryPane && inputEl) {
          const syncPrimaryState = () => {
            validateConfigFieldValue(inputEl, { silent: true });
            updatePrimaryCfgApplyState();
          };
          inputEl.addEventListener('input', syncPrimaryState);
          inputEl.addEventListener('change', syncPrimaryState);
          validateConfigFieldValue(inputEl, { silent: true });
        }

        row.appendChild(valueWrap);
        containerEl.appendChild(row);
        visibilityEntries.push({ row, inputEl, key, doc });
      }

      if (isDigitalInputConfigModule(moduleName) && visibilityEntries.length > 0) {
        const readModeValue = () => {
          if (modeFieldInputEl) {
            const parsedInputMode = parseDigitalInputModeValue(readConfigFieldValue(modeFieldInputEl));
            if (Number.isFinite(parsedInputMode)) return parsedInputMode;
            if (modeFieldInputEl.tagName === 'SELECT' && modeFieldInputEl.selectedIndex >= 0) {
              const selectedOption = modeFieldInputEl.options[modeFieldInputEl.selectedIndex];
              const parsedLabelMode = parseDigitalInputModeValue(selectedOption ? selectedOption.textContent : '');
              if (Number.isFinite(parsedLabelMode)) return parsedLabelMode;
            }
          }
          const directMode = parseDigitalInputModeValue(data.mode);
          if (Number.isFinite(directMode)) return directMode;
          const modeCandidateKey = Object.keys(data).find((candidateKey) =>
            normalizeDigitalInputConfigKey(moduleName, candidateKey) === 'mode'
          );
          if (modeCandidateKey) {
            return parseDigitalInputModeValue(data[modeCandidateKey]);
          }
          return NaN;
        };
        const applyConditionalVisibility = () => {
          const modeValue = readModeValue();
          const hideCounterOnly = Number.isFinite(modeValue) && modeValue === 0;
          visibilityEntries.forEach((entry) => {
            const shouldHide = hideCounterOnly && isCounterModeOnlyConfigField(moduleName, entry.key, entry.doc);
            entry.row.hidden = shouldHide;
            if (entry.inputEl) {
              entry.inputEl.dataset.runtimeHidden = shouldHide ? '1' : '0';
              entry.inputEl.disabled = !!shouldHide;
            }
          });
          if (controlsPrimaryPane && !perFieldApply) {
            updatePrimaryCfgApplyState();
          }
        };
        applyConditionalVisibility();
        if (modeFieldInputEl) {
          modeFieldInputEl.addEventListener('input', applyConditionalVisibility);
          modeFieldInputEl.addEventListener('change', applyConditionalVisibility);
        }
      }

      if (controlsPrimaryPane && !perFieldApply) {
        updatePrimaryCfgApplyState();
      }
    }

    function renderFlowCfgFields(dataObj) {
      renderConfigFields(flowCfgFields, flowCfgCurrentModule, dataObj, {
        source: 'flow',
        controlsPrimaryPane: true,
        perFieldApply: flowCfgApplyPerFieldEnabled(flowCfgCurrentModule),
        onApplyField: appliquerFlowCfgField
      });
    }

    function renderFlowCfgFieldsWithExtensions(dataObj) {
      renderFlowCfgFields(dataObj);
      if (flowCfgCurrentPdmExtension &&
          flowCfgCurrentPdmExtension.data &&
          Object.keys(flowCfgCurrentPdmExtension.data).length > 0) {
        renderConfigFields(flowCfgFields, flowCfgCurrentPdmExtension.module, flowCfgCurrentPdmExtension.data, {
          append: true,
          source: 'flow',
          sectionTitle: flowCfgPdmSectionTitle(flowCfgCurrentModule, dataObj),
          controlsPrimaryPane: true,
          perFieldApply: flowCfgApplyPerFieldEnabled(flowCfgCurrentModule),
          onApplyField: appliquerFlowCfgField
        });
      }
      updatePrimaryCfgApplyState();
    }

    function flowCfgIoOutputSlotIndex(moduleName, dataObj) {
      const cleanModule = nettoyerNomFlowCfg(moduleName).toLowerCase();
      const match = cleanModule.match(/^(?:io\/output\/)?d(\d{1,2})$/);
      if (match) {
        const slot = Number.parseInt(match[1], 10);
        if (Number.isFinite(slot) && slot >= 0 && slot <= 15) return slot;
      }
      const data = (dataObj && typeof dataObj === 'object') ? dataObj : null;
      if (data) {
        const key = Object.keys(data).find((candidate) => /^d\d{2}_name$/i.test(String(candidate || '').trim()));
        if (key) {
          const keyMatch = String(key).match(/^d(\d{2})_name$/i);
          const slot = keyMatch ? Number.parseInt(keyMatch[1], 10) : -1;
          if (Number.isFinite(slot) && slot >= 0 && slot <= 15) return slot;
        }
      }
      return -1;
    }

    function flowCfgPdmModuleForIoOutput(moduleName, dataObj) {
      const slot = flowCfgIoOutputSlotIndex(moduleName, dataObj);
      if (slot < 0) return '';
      return 'pdm/pd' + String(slot);
    }

    function flowCfgPdmSectionTitle(moduleName, dataObj) {
      const slot = flowCfgIoOutputSlotIndex(moduleName, dataObj);
      if (slot < 0) return 'Extension PoolDevice';
      const label = String(ioOutputPdmLabels[slot] || '').trim();
      if (!label) return 'Extension PoolDevice (pd' + String(slot) + ')';
      return 'Extension PoolDevice - ' + label + ' (pd' + String(slot) + ')';
    }

    async function loadFlowCfgPdmExtensionData(moduleName, dataObj) {
      const pdmModule = flowCfgPdmModuleForIoOutput(moduleName, dataObj);
      if (!pdmModule) return null;
      try {
        const res = await fetchFlowRemoteQueued(
          '/api/flowcfg/module?name=' + encodeURIComponent(pdmModule),
          { cache: 'no-store' }
        );
        const data = await res.json().catch(() => null);
        if (!res.ok || !data || data.ok !== true || typeof data.data !== 'object') {
          return null;
        }
        return {
          module: pdmModule,
          data: data.data
        };
      } catch (err) {
        return null;
      }
    }

    async function loadPrimarySupervisorPdmExtensionData(moduleName, dataObj) {
      if (!isWaveshareProfile()) return null;
      const pdmModule = flowCfgPdmModuleForIoOutput(moduleName, dataObj);
      if (!pdmModule) return null;
      try {
        const res = await fetchWithBusyRetry(
          '/api/supervisorcfg/module?name=' + encodeURIComponent(pdmModule),
          { cache: 'no-store' }
        );
        const data = await res.json().catch(() => null);
        if (!res.ok || !data || data.ok !== true || typeof data.data !== 'object') {
          return null;
        }
        return {
          module: pdmModule,
          data: data.data
        };
      } catch (err) {
        return null;
      }
    }

    function renderPrimarySupervisorCfgFields(dataObj) {
      renderConfigFields(flowCfgFields, supCfgCurrentModule, dataObj, {
        source: 'supervisor',
        controlsPrimaryPane: true,
        perFieldApply: flowCfgApplyPerFieldEnabled(supCfgCurrentModule),
        onApplyField: appliquerPrimaryCfgField
      });
    }

    function renderPrimarySupervisorCfgFieldsWithExtensions(dataObj) {
      renderPrimarySupervisorCfgFields(dataObj);
      if (supCfgCurrentPdmExtension &&
          supCfgCurrentPdmExtension.data &&
          Object.keys(supCfgCurrentPdmExtension.data).length > 0) {
        renderConfigFields(flowCfgFields, supCfgCurrentPdmExtension.module, supCfgCurrentPdmExtension.data, {
          append: true,
          source: 'supervisor',
          sectionTitle: flowCfgPdmSectionTitle(supCfgCurrentModule, dataObj),
          controlsPrimaryPane: true,
          perFieldApply: flowCfgApplyPerFieldEnabled(supCfgCurrentModule),
          onApplyField: appliquerPrimaryCfgField
        });
      }
      updatePrimaryCfgApplyState();
    }

    function buildPatchJsonFromFields(fieldsContainer, moduleName) {
      if (!moduleName) throw new Error('branche non sélectionnée');
      const patch = {};
      const fields = fieldsContainer.querySelectorAll('[data-key]');
      fields.forEach((el) => {
        if (el.dataset.runtimeHidden === '1') return;
        const targetModule = nettoyerNomFlowCfg(el.dataset.module || moduleName);
        if (!targetModule) return;
        const key = el.dataset.key;
        const kind = el.dataset.kind;
        if (!key || !kind) return;
        if (!patch[targetModule]) patch[targetModule] = {};
        const modulePatch = patch[targetModule];
        if (kind === 'bool') {
          modulePatch[key] = !!el.checked;
          return;
        }
        if (kind === 'int') {
          modulePatch[key] = readConfigFieldValueStrict(el);
          return;
        }
        if (kind === 'float') {
          modulePatch[key] = readConfigFieldValueStrict(el);
          return;
        }
        const masked = el.dataset.masked === '1';
        const raw = String(el.value ?? '');
        if (masked && raw.length === 0) return;
        modulePatch[key] = raw;
      });
      return JSON.stringify(patch);
    }

    function buildFlowCfgPatchJson() {
      return buildPatchJsonFromFields(flowCfgFields, flowCfgCurrentModule);
    }

    function buildPrimaryCfgPatchJson() {
      if (cfgTreeSelectedSource === 'supervisor') {
        return buildPatchJsonFromFields(flowCfgFields, supCfgCurrentModule);
      }
      return buildPatchJsonFromFields(flowCfgFields, flowCfgCurrentModule);
    }

    async function chargerFlowCfgModule(moduleName) {
      beginFlowCfgLoading('Chargement de la branche distante...', { tree: false, detail: true });
      const m = nettoyerNomFlowCfg(moduleName);
      try {
        if (!m) {
          resetFlowCfgEditor('Aucune branche sélectionnée.');
          return;
        }
        const res = await fetchFlowRemoteQueued(
          '/api/flowcfg/module?name=' + encodeURIComponent(m),
          { cache: 'no-store' }
        );
        const data = await res.json();
        if (!res.ok || !data || data.ok !== true || typeof data.data !== 'object') {
          throw new Error('lecture module impossible');
        }
        await ensureCfgDocsForModule(m);
        const pdmModule = flowCfgPdmModuleForIoOutput(m, data.data);
        if (pdmModule) {
          await ensureCfgDocsForModule(pdmModule);
        }
        if (isWaveshareProfile() && m === 'poollogic/devices') {
          await loadPoolLogicDeviceSlotLabels('flow', true);
        }
        flowCfgCurrentModule = m;
        flowCfgCurrentData = data.data;
        flowCfgCurrentPdmExtension = await loadFlowCfgPdmExtensionData(m, flowCfgCurrentData);
        renderFlowCfgFieldsWithExtensions(flowCfgCurrentData);
        flowCfgStatus.textContent = data.truncated
          ? tr('config.branchLoadedTruncated', 'Branche chargée (tronquée, buffer distant atteint).')
          : tr('config.branchLoaded', 'Branche chargée.');
      } catch (err) {
        flowCfgCurrentPdmExtension = null;
        resetFlowCfgEditor('Chargement branche échoué: ' + err);
      } finally {
        endFlowCfgLoading({ tree: false, detail: true });
      }
    }

    async function chargerPrimarySupervisorCfgModule(moduleName) {
      beginFlowCfgLoading('Chargement de la branche locale...', { tree: false, detail: true });
      const m = nettoyerNomFlowCfg(moduleName);
      try {
        if (!m) {
          supCfgCurrentModule = '';
          supCfgCurrentData = {};
          supCfgCurrentPdmExtension = null;
          resetPrimaryCfgEditor('Aucune branche locale sélectionnée.');
          return;
        }
        const res = await fetchWithBusyRetry('/api/supervisorcfg/module?name=' + encodeURIComponent(m), { cache: 'no-store' });
        const data = await res.json();
        if (!res.ok || !data || data.ok !== true || typeof data.data !== 'object') {
          throw new Error('lecture module supervisor impossible');
        }
        await ensureCfgDocsForModule(m);
        const pdmModule = isWaveshareProfile() ? flowCfgPdmModuleForIoOutput(m, data.data) : '';
        if (pdmModule) {
          await ensureCfgDocsForModule(pdmModule);
        }
        if (isWaveshareProfile() && m === 'poollogic/devices') {
          await loadPoolLogicDeviceSlotLabels('supervisor', true);
        }
        supCfgCurrentModule = m;
        supCfgCurrentData = data.data;
        supCfgCurrentPdmExtension = await loadPrimarySupervisorPdmExtensionData(m, supCfgCurrentData);
        renderPrimarySupervisorCfgFieldsWithExtensions(supCfgCurrentData);
        flowCfgStatus.textContent = data.truncated
          ? 'Branche locale chargée (tronquée, buffer atteint).'
          : 'Branche locale chargée.';
      } catch (err) {
        supCfgCurrentModule = '';
        supCfgCurrentData = {};
        supCfgCurrentPdmExtension = null;
        resetPrimaryCfgEditor('Chargement branche locale échoué: ' + err);
      } finally {
        endFlowCfgLoading({ tree: false, detail: true });
      }
    }

    function markCfgSourceUnavailable(source) {
      const emptyRootNode = {
        prefix: '',
        hasExact: false,
        children: []
      };
      if (source === 'supervisor') {
        supCfgChildrenCache = { [cfgCacheKey('')]: emptyRootNode };
        supCfgExpandedNodes = new Set();
        supCfgTreePath = '';
        supCfgCurrentModule = '';
        supCfgCurrentData = {};
        return;
      }
      flowCfgChildrenCache = { [cfgCacheKey('')]: emptyRootNode };
      flowCfgExpandedNodes = new Set();
      flowCfgPath = [];
      flowCfgCurrentModule = '';
      flowCfgCurrentData = {};
    }

    function formatCfgLoadStatus(result, finalMessage) {
      if (result && !result.flowLoaded && result.supervisorLoaded) {
        return finalMessage
          ? 'flow.io indisponible pour le moment. Configuration Supervisor disponible. Nouvelle tentative automatique...'
          : 'Configuration Supervisor disponible. Nouvelle tentative pour flow.io.';
      }
      if (result && result.flowLoaded && !result.supervisorLoaded) {
        return finalMessage
          ? 'Configuration flow.io disponible. Configuration Supervisor indisponible. Nouvelle tentative automatique...'
          : 'Configuration flow.io disponible. Nouvelle tentative pour Supervisor.';
      }
      return finalMessage
        ? 'flow.io indisponible pour le moment. Nouvelle tentative automatique...'
        : 'flow.io se prépare... nouvelle tentative.';
    }

    async function chargerFlowCfgModules(forceReload) {
      const force = !!forceReload;
      if (force) {
        flowCfgChildrenCache = {};
        flowCfgExpandedNodes = new Set();
        supCfgChildrenCache = {};
        supCfgExpandedNodes = new Set();
      }

      if (!webRemoteConfigEnabled) {
        if (force) {
          flowCfgChildrenCache = {};
          flowCfgExpandedNodes = new Set();
        }
        markCfgSourceUnavailable('flow');
        let supervisorLoaded = false;
        try {
          await ensureCfgPathLoaded('supervisor', '', force);
          supervisorLoaded = true;
        } catch (err) {
          markCfgSourceUnavailable('supervisor');
        }
        if (supervisorLoaded) {
          await selectFlowCfgPath('supervisor', currentCfgTreePath('supervisor'), force);
        } else {
          cfgTreeSelectedSource = 'supervisor';
          renderFlowCfgCurrentPath('supervisor', '', null);
          renderFlowCfgTree();
          resetPrimaryCfgEditor('Aucune branche disponible.');
        }
        return {
          ok: supervisorLoaded,
          flowLoaded: false,
          supervisorLoaded
        };
      }

      const rootLoads = await Promise.allSettled([
        ensureCfgPathLoaded('flow', '', force),
        ensureCfgPathLoaded('supervisor', '', force)
      ]);
      const flowLoaded = rootLoads[0].status === 'fulfilled';
      const supervisorLoaded = rootLoads[1].status === 'fulfilled';

      if (!flowLoaded) {
        markCfgSourceUnavailable('flow');
      }
      if (!supervisorLoaded) {
        markCfgSourceUnavailable('supervisor');
      }

      const currentSource = cfgTreeSelectedSource === 'supervisor' ? 'supervisor' : 'flow';
      let nextSource = currentSource;
      if (currentSource === 'flow' && !flowLoaded && supervisorLoaded) {
        nextSource = 'supervisor';
      } else if (currentSource === 'supervisor' && !supervisorLoaded && flowLoaded) {
        nextSource = 'flow';
      }

      const nextPath = nextSource === currentSource
        ? nettoyerNomFlowCfg(currentCfgTreePath(currentSource))
        : '';
      const selectableSource = nextSource === 'flow'
        ? (flowLoaded ? 'flow' : (supervisorLoaded ? 'supervisor' : ''))
        : (supervisorLoaded ? 'supervisor' : (flowLoaded ? 'flow' : ''));

      if (selectableSource) {
        await selectFlowCfgPath(selectableSource, selectableSource === nextSource ? nextPath : '', force);
      } else {
        cfgTreeSelectedSource = 'flow';
        renderFlowCfgCurrentPath('flow', '', null);
        renderFlowCfgTree();
        resetPrimaryCfgEditor('Aucune branche disponible.');
      }

      return {
        ok: flowLoaded && supervisorLoaded,
        flowLoaded,
        supervisorLoaded
      };
    }

    async function ensureFlowCfgLoaded(forceReload) {
      const force = !!forceReload;
      if (force) {
        flowCfgFlowOnlyFailureStreak = 0;
      }
      if (flowCfgLoadPromise) {
        await flowCfgLoadPromise;
        if (!force) {
          return;
        }
      }

      flowCfgLoadPromise = (async () => {
        if (!flowCfgDocsLoaded) {
          await chargerFlowCfgDocs();
        }

        const wasLoaded = flowCfgLoadedOnce;
        const retryDelaysMs = (wasLoaded && !force) ? [0] : [0, 900, 2200, 3600];
        let loadResult = { ok: false, flowLoaded: false, supervisorLoaded: false };
        for (let attempt = 0; attempt < retryDelaysMs.length; ++attempt) {
          if (retryDelaysMs[attempt] > 0) {
            await waitMs(retryDelaysMs[attempt]);
          }
          loadResult = await chargerFlowCfgModules(force || attempt > 0);
          if (loadResult.ok) {
            flowCfgLoadedOnce = true;
            flowCfgFlowOnlyFailureStreak = 0;
            stopFlowCfgRetry();
            return;
          }
          if (loadResult.supervisorLoaded && !loadResult.flowLoaded) {
            break;
          }
          if (attempt + 1 < retryDelaysMs.length) {
            flowCfgStatus.textContent = formatCfgLoadStatus(loadResult, false);
          }
        }

        if (isPageActive('page-control')) {
          if (loadResult.supervisorLoaded && !loadResult.flowLoaded) {
            flowCfgFlowOnlyFailureStreak += 1;
            const retryDelayMs = Math.min(60000, 7000 + ((flowCfgFlowOnlyFailureStreak - 1) * 5000));
            if (flowCfgFlowOnlyFailureStreak >= 6) {
              stopFlowCfgRetry();
              flowCfgStatus.textContent =
                'flow.io indisponible (lien I2C). Configuration Supervisor disponible. ' +
                'Auto-retry en pause, utilisez Rafraîchir.';
            } else {
              flowCfgStatus.textContent =
                'flow.io indisponible pour le moment. Configuration Supervisor disponible. ' +
                'Nouvelle tentative dans ' + Math.max(1, Math.round(retryDelayMs / 1000)) + ' s.';
              scheduleFlowCfgRetry(retryDelayMs);
            }
            return;
          }
          flowCfgFlowOnlyFailureStreak = 0;
          flowCfgStatus.textContent = formatCfgLoadStatus(loadResult, true);
          scheduleFlowCfgRetry(2500);
        }
      })();

      try {
        await flowCfgLoadPromise;
      } finally {
        flowCfgLoadPromise = null;
      }
    }

    function formatFlowCfgApplyError(data) {
      const err = (data && typeof data === 'object' && data.err && typeof data.err === 'object') ? data.err : {};
      const code = typeof err.code === 'string' ? err.code : '';
      const where = typeof err.where === 'string' ? err.where : '';

      if (code === 'ArgsTooLarge' || code === 'CfgTruncated') {
        return 'Trop de changements en une seule fois pour le lien I2C (' + (where || 'flowcfg') + ').';
      }
      if (code === 'NotReady') {
        return 'Lien I2C temporairement indisponible (' + (where || 'flowcfg') + ').';
      }
      if (code === 'IoError') {
        return 'Erreur de communication I2C (' + (where || 'flowcfg') + ').';
      }
      if (code === 'BadCfgJson') {
        return 'Patch de configuration invalide (' + (where || 'flowcfg') + ').';
      }
      if (code === 'CfgApplyFailed') {
        return 'flow.io a refusé la configuration (' + (where || 'flowcfg') + ').';
      }
      if (code) {
        return code + (where ? ' (' + where + ')' : '');
      }
      return 'apply refusé';
    }

    async function appliquerFlowCfgField(inputEl, applyBtn) {
      if (!inputEl || !applyBtn || !flowCfgCurrentModule) return;
      const key = String(inputEl.dataset.key || '').trim();
      if (!key) return;
      if (!configFieldIsDirty(inputEl)) {
        updateControlFieldApplyState(inputEl, applyBtn);
        return;
      }

      try {
        applyBtn.disabled = true;
        applyBtn.classList.add('is-pending');
        flowCfgStatus.textContent = 'Application du champ "' + key + '"...';

        const patch = buildFlowCfgSingleFieldPatchJson(flowCfgCurrentModule, inputEl);
        const body = new URLSearchParams();
        body.set('patch', patch);
        const res = await fetchFlowRemoteQueued('/api/flowcfg/apply', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
          body: body.toString()
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok || !data || data.ok !== true) {
          throw new Error(formatFlowCfgApplyError(data));
        }

        await chargerFlowCfgModule(flowCfgCurrentModule);
        await refreshWebUiLocale(true);
        flowCfgStatus.textContent = 'Champ "' + key + '" applique.';
      } catch (err) {
        flowCfgStatus.textContent = 'Application du champ echouee: ' + err;
        updateControlFieldApplyState(inputEl, applyBtn);
      }
    }

    async function appliquerPrimaryCfgField(inputEl, applyBtn) {
      if (!inputEl || !applyBtn || !supCfgCurrentModule) return;
      const key = String(inputEl.dataset.key || '').trim();
      if (!key) return;
      if (!configFieldIsDirty(inputEl)) {
        updateControlFieldApplyState(inputEl, applyBtn);
        return;
      }

      setFlowCfgLocalApplyBusy(true, tr('cfg.apply.busyField', 'Application locale du champ « {field} »...').replace('{field}', key));
      try {
        applyBtn.disabled = true;
        applyBtn.classList.add('is-pending');
        flowCfgStatus.textContent = 'Application locale du champ "' + key + '"...';

        const patch = buildFlowCfgSingleFieldPatchJson(supCfgCurrentModule, inputEl);
        const body = new URLSearchParams();
        body.set('patch', patch);
        const res = await fetchWithBusyRetry('/api/supervisorcfg/apply', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
          body: body.toString()
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok || !data || data.ok !== true) {
          throw new Error(extractApiErrorMessage(data, 'apply refusé'));
        }

        clearCfgTreeNodeTextNameCache('supervisor');
        await chargerPrimarySupervisorCfgModule(supCfgCurrentModule);
        renderFlowCfgTree();
        await refreshWebUiLocale(true);
        flowCfgStatus.textContent = 'Champ local "' + key + '" applique.';
      } catch (err) {
        flowCfgStatus.textContent = 'Application locale du champ echouee: ' + err;
        updateControlFieldApplyState(inputEl, applyBtn);
      } finally {
        applyBtn.classList.remove('is-pending');
        setFlowCfgLocalApplyBusy(false);
      }
    }

    async function appliquerFlowCfg() {
      try {
        const patch = buildFlowCfgPatchJson();
        const body = new URLSearchParams();
        body.set('patch', patch);
        const res = await fetchFlowRemoteQueued('/api/flowcfg/apply', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
          body: body.toString()
        });
        const data = await res.json().catch(() => ({}));
        if (!res.ok || !data || data.ok !== true) {
          throw new Error(formatFlowCfgApplyError(data));
        }
        flowCfgStatus.textContent = 'Configuration appliquée sur flow.io.';
        await chargerFlowCfgModule(flowCfgCurrentModule);
        await refreshWebUiLocale(true);
      } catch (err) {
        flowCfgStatus.textContent = 'Application cfg échouée: ' + err;
      }
    }

    async function appliquerPrimaryCfg() {
      if (cfgTreeSelectedSource === 'supervisor') {
        setFlowCfgLocalApplyBusy(true, tr('cfg.apply.busy', 'Application de la configuration en cours...'));
        try {
          const patch = buildPrimaryCfgPatchJson();
          const body = new URLSearchParams();
          body.set('patch', patch);
          const res = await fetchWithBusyRetry('/api/supervisorcfg/apply', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
            body: body.toString()
          });
          const data = await res.json().catch(() => ({}));
          if (!res.ok || !data || data.ok !== true) {
            throw new Error('apply refusé');
          }
          flowCfgStatus.textContent = 'Configuration locale appliquée.';
          clearCfgTreeNodeTextNameCache('supervisor');
          await chargerPrimarySupervisorCfgModule(supCfgCurrentModule);
          renderFlowCfgTree();
          await refreshWebUiLocale(true);
        } catch (err) {
          flowCfgStatus.textContent = 'Application cfg locale échouée: ' + err;
        } finally {
          setFlowCfgLocalApplyBusy(false);
        }
        return;
      }
      await appliquerFlowCfg();
    }

    function setFlowCfgBackupStatus(message, tone) {
      if (!flowCfgBackupStatus) return;
      flowCfgBackupStatus.textContent = String(message || '').trim() || 'Sauvegarde configuration prête.';
      flowCfgBackupStatus.classList.remove('is-ok', 'is-error', 'is-busy');
      if (tone === 'ok') flowCfgBackupStatus.classList.add('is-ok');
      if (tone === 'error') flowCfgBackupStatus.classList.add('is-error');
      if (tone === 'busy') flowCfgBackupStatus.classList.add('is-busy');
    }

    function setFlowCfgBackupProgress(percent, visible, label) {
      const show = !!visible;
      if (flowCfgBackupProgress) flowCfgBackupProgress.hidden = !show;
      if (flowCfgBackupProgressLabel && typeof label === 'string' && label.trim().length > 0) {
        flowCfgBackupProgressLabel.textContent = label.trim();
      }

      if (!show) {
        if (flowCfgBackupPct) flowCfgBackupPct.textContent = '0%';
        if (flowCfgBackupProgressBar) {
          flowCfgBackupProgressBar.style.width = '0%';
          flowCfgBackupProgressBar.classList.remove('is-complete');
        }
        if (flowCfgBackupProgressDot) flowCfgBackupProgressDot.hidden = false;
        return;
      }

      const safePercent = Math.max(0, Math.min(100, Math.round(Number(percent) || 0)));
      if (flowCfgBackupPct) flowCfgBackupPct.textContent = safePercent + '%';
      if (flowCfgBackupProgressBar) {
        flowCfgBackupProgressBar.style.width = safePercent + '%';
        flowCfgBackupProgressBar.classList.toggle('is-complete', safePercent >= 100);
      }
      if (flowCfgBackupProgressDot) flowCfgBackupProgressDot.hidden = safePercent >= 100;
    }

    function setFlowCfgBackupBusy(busy) {
      flowCfgBackupBusy = !!busy;
      if (flowCfgExportBtn) flowCfgExportBtn.disabled = flowCfgBackupBusy;
      if (flowCfgImportBtn) flowCfgImportBtn.disabled = flowCfgBackupBusy;
      if (flowCfgImportFileInput) flowCfgImportFileInput.disabled = flowCfgBackupBusy;
    }

    function flowCfgBackupStoreLabel(storeName) {
      if (isWaveshareProfile()) return webProfileName || 'Waveshare';
      return storeName === 'supervisor' ? webProfileName : 'flow.io';
    }

    function flowCfgBackupStoreFetchImpl(storeName) {
      return storeName === 'supervisor' ? fetch : fetchFlowCfgEndpoint;
    }

    function flowCfgBackupStoreBasePath(storeName) {
      return storeName === 'supervisor' ? '/api/supervisorcfg' : '/api/flowcfg';
    }

    function flowCfgBackupStoreNames() {
      return isWaveshareProfile() ? ['flow'] : ['supervisor', 'flow'];
    }

    function flowCfgBackupIsoDateForFile(dateLike) {
      const d = dateLike instanceof Date ? dateLike : new Date();
      const pad = (value) => String(value).padStart(2, '0');
      return d.getUTCFullYear()
        + pad(d.getUTCMonth() + 1)
        + pad(d.getUTCDate())
        + '-'
        + pad(d.getUTCHours())
        + pad(d.getUTCMinutes())
        + pad(d.getUTCSeconds());
    }

    function flowCfgBackupDownloadText(filename, textContent) {
      const blob = new Blob([textContent], { type: 'application/json;charset=utf-8' });
      const url = URL.createObjectURL(blob);
      const anchor = document.createElement('a');
      anchor.href = url;
      anchor.download = filename;
      anchor.rel = 'noopener';
      document.body.appendChild(anchor);
      anchor.click();
      setTimeout(() => {
        document.body.removeChild(anchor);
        URL.revokeObjectURL(url);
      }, 0);
    }

    function flowCfgBackupShouldRedactField(key, value) {
      const normalizedKey = String(key || '').trim().toLowerCase();
      if (!normalizedKey) return false;
      if (/pass|token|secret|api[_-]?key/.test(normalizedKey)) return true;
      if (typeof value === 'string' && value.trim() === '***') return true;
      return false;
    }

    function flowCfgBackupRedactModuleData(storeName, moduleName, moduleData) {
      const result = {};
      const redactedFields = [];
      const source = (moduleData && typeof moduleData === 'object' && !Array.isArray(moduleData)) ? moduleData : {};
      Object.keys(source).sort().forEach((key) => {
        const value = source[key];
        if (flowCfgBackupShouldRedactField(key, value)) {
          result[key] = flowCfgBackupRedactedToken;
          redactedFields.push({
            module: moduleName,
            key: key,
            reason: 'secret',
            store: storeName
          });
          return;
        }
        result[key] = value;
      });
      return { data: result, redactedFields };
    }

    async function flowCfgBackupFetchModules(storeName) {
      const basePath = flowCfgBackupStoreBasePath(storeName);
      const fetchImpl = flowCfgBackupStoreFetchImpl(storeName);
      const normalizeModules = (data) => {
        if (!Array.isArray(data && data.modules)) {
          throw new Error('liste modules ' + flowCfgBackupStoreLabel(storeName) + ' invalide');
        }
        return data.modules
          .filter((moduleName) => typeof moduleName === 'string' && moduleName.trim().length > 0)
          .map((moduleName) => moduleName.trim())
          .sort((left, right) => left.localeCompare(right));
      };
      const sameModuleList = (left, right) => {
        if (!Array.isArray(left) || !Array.isArray(right)) return false;
        if (left.length !== right.length) return false;
        for (let i = 0; i < left.length; i += 1) {
          if (left[i] !== right[i]) return false;
        }
        return true;
      };

      if (storeName !== 'flow') {
        const data = await fetchOkJson(
          basePath + '/modules',
          { cache: 'no-store' },
          'liste modules ' + flowCfgBackupStoreLabel(storeName) + ' indisponible',
          fetchImpl
        );
        return normalizeModules(data);
      }

      let previous = null;
      let stableCount = 0;
      let attempt = 0;
      while (stableCount < 1) {
        attempt += 1;
        const data = await fetchOkJson(
          basePath + '/modules',
          { cache: 'no-store' },
          'liste modules ' + flowCfgBackupStoreLabel(storeName) + ' indisponible',
          fetchImpl
        );
        const modules = normalizeModules(data);
        if (previous && sameModuleList(previous, modules)) {
          stableCount += 1;
          return modules;
        }
        previous = modules;
        stableCount = 0;
        const retryDelayMs = attempt <= 3
          ? (100 * attempt)
          : Math.min(1200, 300 + ((attempt - 3) * 120));
        await waitMs(retryDelayMs);
      }
      return previous || [];
    }

    async function flowCfgBackupFetchModule(storeName, moduleName) {
      const basePath = flowCfgBackupStoreBasePath(storeName);
      const fetchImpl = flowCfgBackupStoreFetchImpl(storeName);
      const maxAttempts = storeName === 'flow' ? Number.POSITIVE_INFINITY : 1;
      let lastError = null;
      for (let attempt = 1; attempt <= maxAttempts; attempt += 1) {
        try {
          const data = await fetchOkJson(
            basePath + '/module?name=' + encodeURIComponent(moduleName),
            { cache: 'no-store' },
            'lecture module ' + moduleName + ' (' + flowCfgBackupStoreLabel(storeName) + ') impossible',
            fetchImpl
          );
          if (!data || typeof data.data !== 'object' || Array.isArray(data.data)) {
            throw new Error('module ' + moduleName + ' invalide (' + flowCfgBackupStoreLabel(storeName) + ')');
          }
          return {
            data: data.data,
            truncated: !!data.truncated
          };
        } catch (err) {
          lastError = err;
          if (attempt >= maxAttempts) break;
          const retryDelayMs = attempt <= 3
            ? (120 * attempt)
            : Math.min(5000, 500 + ((attempt - 3) * 250));
          await waitMs(retryDelayMs);
        }
      }
      throw (lastError || new Error('lecture module ' + moduleName + ' impossible'));
    }

    function flowCfgBackupValidatePrimitiveValue(value, moduleName, key) {
      if (typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean') return;
      throw new Error('Valeur invalide pour ' + moduleName + '.' + key + ' (type non supporté).');
    }

    function flowCfgBackupNormalizeStoreSection(rawStore, storeName) {
      const store = rawStore && typeof rawStore === 'object' ? rawStore : {};
      const rawModules = (store.modules && typeof store.modules === 'object' && !Array.isArray(store.modules))
        ? store.modules
        : {};
      const modules = {};
      Object.keys(rawModules).forEach((moduleName) => {
        const cleanModuleName = String(moduleName || '').trim();
        if (!cleanModuleName) return;
        const rawModuleData = rawModules[moduleName];
        if (!rawModuleData || typeof rawModuleData !== 'object' || Array.isArray(rawModuleData)) {
          throw new Error('Module invalide dans backup: ' + cleanModuleName + ' (' + flowCfgBackupStoreLabel(storeName) + ').');
        }
        const normalizedData = {};
        Object.keys(rawModuleData).forEach((key) => {
          const cleanKey = String(key || '').trim();
          if (!cleanKey) return;
          const value = rawModuleData[key];
          flowCfgBackupValidatePrimitiveValue(value, cleanModuleName, cleanKey);
          normalizedData[cleanKey] = value;
        });
        modules[cleanModuleName] = normalizedData;
      });

      const truncatedModules = Array.isArray(store.truncated_modules)
        ? store.truncated_modules
            .filter((moduleName) => typeof moduleName === 'string' && moduleName.trim().length > 0)
            .map((moduleName) => moduleName.trim())
        : [];

      const redactedFields = Array.isArray(store.redacted_fields)
        ? store.redacted_fields
            .filter((entry) => entry && typeof entry === 'object')
            .map((entry) => ({
              module: String(entry.module || '').trim(),
              key: String(entry.key || '').trim()
            }))
            .filter((entry) => entry.module.length > 0 && entry.key.length > 0)
        : [];

      const failedModules = Array.isArray(store.failed_modules)
        ? store.failed_modules
            .filter((entry) => entry && typeof entry === 'object')
            .map((entry) => ({
              module: String(entry.module || '').trim(),
              reason: String(entry.reason || '').trim()
            }))
            .filter((entry) => entry.module.length > 0)
        : [];

      return {
        modules,
        truncated_modules: truncatedModules,
        redacted_fields: redactedFields,
        failed_modules: failedModules
      };
    }

    function flowCfgBackupValidateDocument(parsedDoc) {
      if (!parsedDoc || typeof parsedDoc !== 'object') {
        throw new Error('Backup invalide (objet JSON attendu).');
      }
      if (String(parsedDoc.format || '').trim() !== flowCfgBackupFormat) {
        throw new Error('Backup invalide (format non reconnu).');
      }
      if (Number(parsedDoc.version) !== flowCfgBackupVersion) {
        throw new Error('Backup invalide (version non supportée).');
      }
      const stores = (parsedDoc.stores && typeof parsedDoc.stores === 'object') ? parsedDoc.stores : null;
      if (!stores) {
        throw new Error('Backup invalide (stores absent).');
      }
      return {
        format: flowCfgBackupFormat,
        version: flowCfgBackupVersion,
        stores: {
          supervisor: flowCfgBackupNormalizeStoreSection(stores.supervisor, 'supervisor'),
          flow: flowCfgBackupNormalizeStoreSection(stores.flow, 'flow')
        }
      };
    }

    function flowCfgBackupBuildRedactedFieldSet(redactedFields) {
      const set = new Set();
      (Array.isArray(redactedFields) ? redactedFields : []).forEach((entry) => {
        const moduleName = String(entry && entry.module ? entry.module : '').trim();
        const key = String(entry && entry.key ? entry.key : '').trim();
        if (!moduleName || !key) return;
        set.add(moduleName + '\u0000' + key);
      });
      return set;
    }

    function flowCfgBackupBuildModulePatch(moduleName, moduleData, redactedFieldSet) {
      const patch = {};
      const source = (moduleData && typeof moduleData === 'object' && !Array.isArray(moduleData)) ? moduleData : {};
      Object.keys(source).sort().forEach((key) => {
        const value = source[key];
        if (value === flowCfgBackupRedactedToken) return;
        if (redactedFieldSet && redactedFieldSet.has(moduleName + '\u0000' + key)) return;
        patch[key] = value;
      });
      return patch;
    }

    function flowCfgBackupSplitModulePatch(moduleName, modulePatch, maxBytes) {
      const keys = Object.keys(modulePatch || {}).sort();
      if (!keys.length) return [];

      const chunks = [];
      let current = {};
      keys.forEach((key) => {
        const next = Object.assign({}, current, { [key]: modulePatch[key] });
        const nextPatch = { [moduleName]: next };
        if (utf8ByteLength(JSON.stringify(nextPatch)) <= maxBytes) {
          current = next;
          return;
        }

        if (Object.keys(current).length === 0) {
          throw new Error('Champ trop volumineux pour import: ' + moduleName + '.' + key);
        }

        chunks.push({ [moduleName]: current });
        current = { [key]: modulePatch[key] };
        const singlePatch = { [moduleName]: current };
        if (utf8ByteLength(JSON.stringify(singlePatch)) > maxBytes) {
          throw new Error('Champ trop volumineux pour import: ' + moduleName + '.' + key);
        }
      });

      if (Object.keys(current).length > 0) {
        chunks.push({ [moduleName]: current });
      }
      return chunks;
    }

    async function flowCfgBackupApplyPatch(storeName, patchJson) {
      const basePath = flowCfgBackupStoreBasePath(storeName);
      const fetchImpl = flowCfgBackupStoreFetchImpl(storeName);
      const response = await fetchJsonResponse(
        basePath + '/apply',
        createFormPostOptions({ patch: patchJson }),
        fetchImpl
      );
      if (!response.res.ok || !response.data || response.data.ok !== true) {
        if (storeName === 'flow') {
          throw new Error(formatFlowCfgApplyError(response.data));
        }
        throw new Error(extractApiErrorMessage(response.data, 'apply refusé'));
      }
    }

    async function exportFlowCfgBackup() {
      if (flowCfgBackupBusy) return;
      setFlowCfgBackupBusy(true);
      const startedAt = Date.now();
      try {
        setFlowCfgBackupStatus('Préparation de l\'export ConfigStore...', 'busy');
        setFlowCfgBackupProgress(0, true, 'Export ConfigStore');
        const createdAt = new Date();
        const backupDoc = {
          format: flowCfgBackupFormat,
          version: flowCfgBackupVersion,
          created_at_utc: createdAt.toISOString(),
          meta: {
            supervisor_fw: supervisorFirmwareVersion || '-',
            flow_reachable: false
          },
          stores: {
            supervisor: {
              modules: {},
              truncated_modules: [],
              redacted_fields: [],
              failed_modules: []
            },
            flow: {
              modules: {},
              truncated_modules: [],
              redacted_fields: [],
              failed_modules: []
            }
          }
        };

        const stores = flowCfgBackupStoreNames();
        const modulesByStore = {};
        let totalModuleCount = 0;
        for (const storeName of stores) {
          const storeLabel = flowCfgBackupStoreLabel(storeName);
          setFlowCfgBackupStatus('Lecture des modules ' + storeLabel + '...', 'busy');
          const modules = await flowCfgBackupFetchModules(storeName);
          modulesByStore[storeName] = modules;
          totalModuleCount += modules.length;
          backupDoc.stores[storeName].module_count = modules.length;
        }

        let exportedModuleCount = 0;
        if (totalModuleCount === 0) {
          setFlowCfgBackupProgress(100, true, 'Export ConfigStore');
        }

        for (const storeName of stores) {
          const storeLabel = flowCfgBackupStoreLabel(storeName);
          const modules = modulesByStore[storeName] || [];
          for (let i = 0; i < modules.length; i += 1) {
            const moduleName = modules[i];
            const moduleOrder = exportedModuleCount + 1;
            setFlowCfgBackupStatus(
              'Export ' + storeLabel + ' : ' + moduleOrder + '/' + totalModuleCount + ' ' + moduleName + '...',
              'busy'
            );
            let modulePayload = null;
            try {
              modulePayload = await flowCfgBackupFetchModule(storeName, moduleName);
            } catch (err) {
              backupDoc.stores[storeName].failed_modules.push({
                module: moduleName,
                reason: String(err || '').trim() || 'lecture module impossible'
              });
              exportedModuleCount += 1;
              setFlowCfgBackupProgress((exportedModuleCount / totalModuleCount) * 100, true, 'Export ConfigStore');
              continue;
            }
            if (modulePayload.truncated) {
              backupDoc.stores[storeName].truncated_modules.push(moduleName);
            }
            const redacted = flowCfgBackupRedactModuleData(storeName, moduleName, modulePayload.data);
            backupDoc.stores[storeName].modules[moduleName] = redacted.data;
            redacted.redactedFields.forEach((entry) => {
              backupDoc.stores[storeName].redacted_fields.push({
                module: entry.module,
                key: entry.key
              });
            });
            exportedModuleCount += 1;
            setFlowCfgBackupProgress((exportedModuleCount / totalModuleCount) * 100, true, 'Export ConfigStore');
          }
          if (storeName === 'flow') {
            backupDoc.meta.flow_reachable = true;
          }
        }

        const truncatedErrors = []
          .concat((backupDoc.stores.supervisor.truncated_modules || []).map((moduleName) => 'Supervisor/' + moduleName))
          .concat((backupDoc.stores.flow.truncated_modules || []).map((moduleName) => 'flow.io/' + moduleName));
        if (truncatedErrors.length > 0) {
          throw new Error(
            'export interrompu: modules tronqués (' + truncatedErrors.join(', ') + ').'
          );
        }

        const failedModuleErrors = []
          .concat((backupDoc.stores.supervisor.failed_modules || []).map((entry) => 'Supervisor/' + entry.module))
          .concat((backupDoc.stores.flow.failed_modules || []).map((entry) => 'flow.io/' + entry.module));

        const serialized = JSON.stringify(backupDoc, null, 2);
        const fileName = 'flowio-configstore-backup-' + flowCfgBackupIsoDateForFile(createdAt) + '.json';
        flowCfgBackupDownloadText(fileName, serialized);
        const durationMs = Date.now() - startedAt;
        setFlowCfgBackupProgress(100, true, 'Export ConfigStore');
        const failedSummary = failedModuleErrors.length > 0
          ? ' Modules ignorés: ' + failedModuleErrors.length + ' (' + failedModuleErrors.join(', ') + ').'
          : '';
        setFlowCfgBackupStatus(
          'Export terminé (' + fileName + ', ' + Math.max(1, Math.round(durationMs / 1000)) + ' s).' + failedSummary,
          'ok'
        );
      } catch (err) {
        setFlowCfgBackupStatus('Export échoué: ' + err, 'error');
      } finally {
        setFlowCfgBackupBusy(false);
      }
    }

    async function importFlowCfgBackupFromText(rawText, fileName) {
      if (flowCfgBackupBusy) return;
      setFlowCfgBackupBusy(true);
      const startedAt = Date.now();
      try {
        setFlowCfgBackupProgress(0, true, 'Import ConfigStore');
        let parsedDoc = null;
        try {
          parsedDoc = JSON.parse(String(rawText || ''));
        } catch (err) {
          throw new Error('JSON invalide.');
        }
        const backupDoc = flowCfgBackupValidateDocument(parsedDoc);
        if (!confirm('Confirmer l\'import du backup "' + (fileName || 'inconnu') + '" ?')) {
          setFlowCfgBackupStatus('Import annulé.', '');
          return;
        }

        const report = {
          supervisor: { modules_applied: 0, modules_skipped: 0, patches_applied: 0 },
          flow: { modules_applied: 0, modules_skipped: 0, patches_applied: 0 }
        };

        const stores = flowCfgBackupStoreNames();
        const importPlan = {};
        let totalPatchCount = 0;
        stores.forEach((storeName) => {
          const storeData = backupDoc.stores[storeName];
          const moduleNames = Object.keys(storeData.modules || {}).sort((left, right) => left.localeCompare(right));
          const truncatedSet = new Set(storeData.truncated_modules || []);
          const redactedSet = flowCfgBackupBuildRedactedFieldSet(storeData.redacted_fields);
          importPlan[storeName] = {};
          moduleNames.forEach((moduleName) => {
            if (truncatedSet.has(moduleName)) return;
            const modulePatch = flowCfgBackupBuildModulePatch(
              moduleName,
              storeData.modules[moduleName],
              redactedSet
            );
            if (Object.keys(modulePatch).length === 0) return;
            const chunkPatches = flowCfgBackupSplitModulePatch(
              moduleName,
              modulePatch,
              flowCfgBackupPatchTargetBytes
            );
            importPlan[storeName][moduleName] = chunkPatches;
            totalPatchCount += chunkPatches.length;
          });
        });
        let appliedPatchCount = 0;
        if (totalPatchCount === 0) {
          setFlowCfgBackupProgress(100, true, 'Import ConfigStore');
        }
        for (const storeName of stores) {
          const storeData = backupDoc.stores[storeName];
          const storeLabel = flowCfgBackupStoreLabel(storeName);
          const moduleNames = Object.keys(storeData.modules || {}).sort((left, right) => left.localeCompare(right));
          const truncatedSet = new Set(storeData.truncated_modules || []);

          for (let moduleIndex = 0; moduleIndex < moduleNames.length; moduleIndex += 1) {
            const moduleName = moduleNames[moduleIndex];
            if (truncatedSet.has(moduleName)) {
              report[storeName].modules_skipped += 1;
              continue;
            }

            const chunkPatches = (importPlan[storeName] && importPlan[storeName][moduleName])
              ? importPlan[storeName][moduleName]
              : [];
            if (chunkPatches.length === 0) {
              report[storeName].modules_skipped += 1;
              continue;
            }

            for (let chunkIndex = 0; chunkIndex < chunkPatches.length; chunkIndex += 1) {
              setFlowCfgBackupStatus(
                'Import ' + storeLabel + ' : ' + moduleName
                + ' (' + (moduleIndex + 1) + '/' + moduleNames.length + ', patch ' + (chunkIndex + 1)
                + '/' + chunkPatches.length + ')...',
                'busy'
              );
              await flowCfgBackupApplyPatch(storeName, JSON.stringify(chunkPatches[chunkIndex]));
              report[storeName].patches_applied += 1;
              appliedPatchCount += 1;
              if (totalPatchCount > 0) {
                setFlowCfgBackupProgress((appliedPatchCount / totalPatchCount) * 100, true, 'Import ConfigStore');
              }
            }

            report[storeName].modules_applied += 1;
          }
        }

        await ensureFlowCfgLoaded(true).catch(() => {});
        const durationMs = Date.now() - startedAt;
        setFlowCfgBackupStatus(
          'Import terminé (' + Math.max(1, Math.round(durationMs / 1000)) + ' s). '
            + 'Supervisor: ' + report.supervisor.modules_applied + ' module(s), '
            + report.supervisor.patches_applied + ' patch(s). '
            + 'flow.io: ' + report.flow.modules_applied + ' module(s), '
            + report.flow.patches_applied + ' patch(s).',
          'ok'
        );
        setFlowCfgBackupProgress(100, true, 'Import ConfigStore');
      } catch (err) {
        setFlowCfgBackupStatus('Import échoué: ' + err, 'error');
      } finally {
        setFlowCfgBackupBusy(false);
        if (flowCfgImportFileInput) {
          flowCfgImportFileInput.value = '';
        }
      }
    }

    async function importFlowCfgBackupFromFile(file) {
      if (!file) return;
      const text = await file.text();
      await importFlowCfgBackupFromText(text, file.name || 'backup.json');
    }

    async function callSystemAction(target, action) {
      const flowLocalProfile = isWaveshareProfile();
      let endpoint = '/api/system/reboot';
      if (target === 'flow' && action === 'reboot') {
        endpoint = flowLocalProfile ? '/api/system/reboot' : '/api/flow/system/reboot';
      }
      else if (target === 'flow' && action === 'hardware_reboot') endpoint = '/api/flow/system/hardware-reboot';
      else if (target === 'nextion' && action === 'reboot') endpoint = '/api/system/nextion/reboot';
      else if (target === 'flow' && action === 'factory_reset') endpoint = flowLocalProfile ? '/api/system/factory-reset' : '/api/flow/system/factory-reset';
      else if (target === 'supervisor' && action === 'factory_reset') endpoint = '/api/system/factory-reset';
      const flowUsesRemote = target === 'flow' && !flowLocalProfile;
      await fetchOkJson(endpoint, { method: 'POST' }, 'échec action', flowUsesRemote ? fetchFlowRemoteQueued : fetch);
      if (target === 'flow' && action === 'factory_reset') {
        if (systemStatusText) systemStatusText.textContent = 'Reset flow.io en cours';
      } else if (target === 'flow' && action === 'hardware_reboot') {
        if (systemStatusText) systemStatusText.textContent = 'Reset matériel flow.io';
      } else if (target === 'flow' && action === 'reboot') {
        if (systemStatusText) systemStatusText.textContent = 'Redémarrage flow.io';
      } else if (target === 'nextion' && action === 'reboot') {
        if (systemStatusText) systemStatusText.textContent = 'Redémarrage Nextion';
      } else if (target === 'supervisor' && action === 'factory_reset') {
        if (systemStatusText) systemStatusText.textContent = 'Reset superviseur en cours';
      } else {
        if (systemStatusText) systemStatusText.textContent = 'Redémarrage superviseur';
      }
    }

    function clearPendingSystemAction(button) {
      if (!button) return;
      const pending = pendingSystemActionCountdowns.get(button);
      if (pending && pending.timer) {
        clearTimeout(pending.timer);
      }
      pendingSystemActionCountdowns.delete(button);
      button.disabled = false;
      button.classList.remove('is-counting-down');
      if (button.dataset.defaultHtml) {
        button.innerHTML = button.dataset.defaultHtml;
      } else {
        button.textContent = button.dataset.defaultLabel || 'Redémarrer';
      }
    }

    function startDelayedSystemAction(button, countdownLabel, actionRunner, failurePrefix) {
      if (!button || typeof actionRunner !== 'function') return;
      if (!button.dataset.defaultHtml) {
        button.dataset.defaultHtml = button.innerHTML || '';
      }
      if (!button.dataset.defaultLabel) {
        const labelNode = button.querySelector('[data-i18n]');
        const label = labelNode && labelNode.textContent
          ? String(labelNode.textContent).trim()
          : String(button.textContent || '').trim();
        button.dataset.defaultLabel = label || 'Redémarrer';
      }
      clearPendingSystemAction(button);

      let remaining = rebootActionDelaySeconds;
      button.disabled = true;
      button.classList.add('is-counting-down');
      const countdownNode = document.createElement('span');
      countdownNode.className = 'system-action-countdown';
      button.replaceChildren(countdownNode);

      const tick = () => {
        countdownNode.textContent = remaining + ' s';
        if (systemStatusText) systemStatusText.textContent = countdownLabel + ' dans ' + remaining + ' s';

        if (remaining <= 1) {
          pendingSystemActionCountdowns.delete(button);
          runAsyncTaskSafely(async () => {
            countdownNode.textContent = '…';
            try {
              await actionRunner();
            } catch (err) {
              clearPendingSystemAction(button);
              if (systemStatusText) systemStatusText.textContent = failurePrefix;
              return;
            }
            clearPendingSystemAction(button);
          });
          return;
        }

        remaining -= 1;
        const timer = setTimeout(tick, 1000);
        pendingSystemActionCountdowns.set(button, { timer });
      };

      tick();
    }

    function initUpgradeBindings() {
      bindClickAction(checkUpdatesBtn, () => checkFirmwareUpdates());
      bindClickAction(cancelUpgradeUiBtn, () => cancelUpgradeUiSession());
    }

    function initStatusBindings() {
      bindClickAction(flowStatusRefreshBtn, async () => {
        try {
          await refreshFlowStatus(true);
        } catch (err) {
          flowStatusChip.textContent = 'erreur lecture statut';
        }
      });
      bindClickAction(poolMeasuresRefreshBtn, async () => {
        try {
          await refreshPoolMeasures(true);
        } catch (err) {
          showPoolMeasuresError(err);
        }
      });
      bindClickAction(poolConfigRefreshBtn, () => loadPoolConfig(true));
    }

    function initInfoBindings() {
      bindClickAction(infoSystemLoader, () => refreshInfoFlowDomain('system', true));
      bindClickAction(infoWifiLoader, () => refreshInfoFlowDomain('wifi', true));
      bindClickAction(infoMqttLoader, () => refreshInfoFlowDomain('mqtt', true));
      updateInfoLoadButtonsText();
    }

    function initCalibrationBindings() {
      if (!calibrationSensorSelect) return;

      calibrationSensorSelect.addEventListener('change', () => {
        calibrationSyncSelectionUi();
        calibrationSetStatus(tr('calibration.sensorSelected', 'Sonde sélectionnée. Chargez la configuration pour continuer.'));
      });

      bindClickAction(calibrationLoadBtn, () => loadCalibrationSensorConfig(true));

      const bindLiveFill = (button, targetInput, label) => bindClickAction(button, async () => {
        try {
          await calibrationPrefillLiveValue({
            silent: false,
            targetInput: targetInput
          });
        } catch (err) {
          calibrationSetStatus(
            tr('calibration.liveFailedFor', 'Mesure live échouée ({label}): {err}')
              .replace('{label}', label)
              .replace('{err}', String(err)),
            'error'
          );
        }
      });
      bindLiveFill(calibrationPoint1LiveBtn, calibrationPoint1Measured, 'Point 1');
      bindLiveFill(calibrationPoint2LiveBtn, calibrationPoint2Measured, 'Point 2');
      bindLiveFill(calibrationSingleLiveBtn, calibrationSingleMeasured, 'Mesure');
      bindClickAction(calibrationComputeBtn, () => runCalibrationCompute());
      bindClickAction(calibrationApplyBtn, () => applyCalibrationResult());

      calibrationSyncSelectionUi();
      calibrationSetStatus(tr('calibration.ready', 'Étalonnage prêt.'));
    }

    function initWifiBindings() {
      if (toggleWifiPassBtn && wifiPass) {
        mettreAJourEtatVisibiliteMotDePasse(
          wifiPass,
          toggleWifiPassBtn,
          tr('wifi.password.show', 'Afficher le mot de passe réseau'),
          tr('wifi.password.hide', 'Masquer le mot de passe réseau')
        );
        toggleWifiPassBtn.addEventListener('click', () => {
          basculerVisibiliteMotDePasse(
            wifiPass,
            toggleWifiPassBtn,
            tr('wifi.password.show', 'Afficher le mot de passe réseau'),
            tr('wifi.password.hide', 'Masquer le mot de passe réseau')
          );
        });
      }
      wifiSsidList.addEventListener('change', () => {
        const picked = (wifiSsidList.value || '').trim();
        if (picked.length > 0) {
          wifiSsid.value = picked;
        }
      });
      bindClickAction(scanWifiBtn, () => refreshWifiScanStatus(true));
      bindClickAction(applyWifiCfgBtn, async () => {
        try {
          await saveWifiConfig();
        } catch (err) {
          wifiConfigStatus.textContent = tr('system.action.wifiApplyFailed', 'Application réseau échouée') + ': ' + err;
        }
      });
    }

    function initSystemBindings() {
      bindClickAction(rebootDeviceActionBtn, () => {
        if (!rebootDeviceTargetSelect || !rebootDeviceActionBtn) return;
        const selected = String(rebootDeviceTargetSelect.value || 'supervisor');
        if (!confirmRebootLaunch(selected)) return;
        const actionMap = {
          supervisor: {
            countdown: isMicronovaProfile() ? 'Reboot Micronova' : 'Reboot Supervisor',
            failure: isMicronovaProfile() ? 'Reboot Micronova échoué' : 'Reboot Supervisor échoué',
            runner: () => callSystemAction('supervisor', 'reboot')
          },
          flow_soft: {
            countdown: 'Reboot flow.io',
            failure: 'Reboot flow.io échoué',
            runner: () => callSystemAction('flow', 'reboot')
          },
          flow_hard: {
            countdown: 'Reset matériel flow.io',
            failure: 'Reset matériel flow.io échoué',
            runner: () => callSystemAction('flow', 'hardware_reboot')
          },
          nextion: {
            countdown: 'Reboot Nextion',
            failure: 'Reboot Nextion échoué',
            runner: () => callSystemAction('nextion', 'reboot')
          },
          factory_reset: {
            countdown: 'Init usine flow.io',
            failure: 'Init usine flow.io échouée',
            runner: () => callSystemAction('flow', 'factory_reset')
          }
        };
        const chosen = actionMap[selected] || actionMap.supervisor;
        startDelayedSystemAction(
          rebootDeviceActionBtn,
          chosen.countdown,
          chosen.runner,
          chosen.failure
        );
      });
      bindClickAction(factoryResetDeviceActionBtn, () => {
        if (!factoryResetDeviceActionBtn) return;
        if (!confirmRebootLaunch('factory_reset')) return;
        startDelayedSystemAction(
          factoryResetDeviceActionBtn,
          'Init usine flow.io',
          () => callSystemAction('flow', 'factory_reset'),
          'Init usine flow.io échouée'
        );
      });
    }

    function initConfigBindings() {
      bindClickAction(flowCfgRefreshBtn, () => ensureFlowCfgLoaded(true));
      bindClickAction(flowCfgApplyBtn, () => appliquerPrimaryCfg());
      bindClickAction(flowCfgExportBtn, () => exportFlowCfgBackup());
      bindClickAction(flowCfgImportBtn, () => {
        if (!flowCfgImportFileInput || flowCfgBackupBusy) return;
        flowCfgImportFileInput.value = '';
        flowCfgImportFileInput.click();
      });
      if (flowCfgImportFileInput) {
        flowCfgImportFileInput.addEventListener('change', () => {
          const file = flowCfgImportFileInput.files && flowCfgImportFileInput.files[0]
            ? flowCfgImportFileInput.files[0]
            : null;
          if (!file) return;
          runAsyncTaskSafely(() => importFlowCfgBackupFromFile(file));
        });
      }
    }

    function initGlobalUiBindings() {
      if (themeToggle) {
        themeToggle.addEventListener('change', () => {
          applyThemePreference(themeToggle.checked ? 'dark' : 'light', true);
        });
      }
      document.addEventListener('visibilitychange', () => {
        const activePageId = getActivePageId();
        const onUpgradePage = activePageId === 'page-system';
        if (document.hidden || !onUpgradePage) {
          stopUpgradeStatusPolling();
        } else {
          startUpgradeStatusPolling();
        }
        if (document.hidden || activePageId !== 'page-pool-measures') {
          stopPoolMeasuresTimer();
        } else {
          startPoolMeasuresTimer();
        }
        if (document.hidden || activePageId !== 'page-io-summary') {
          stopIoSummaryTimer();
        } else {
          startIoSummaryTimer();
          refreshIoSummary(false).catch(() => {});
        }
        if (document.hidden || !logsOverlayOpen) {
          closeLogSocket();
          if (!logsOverlayOpen) setWsStatusText(tr('terminal.inactive', 'inactif'));
        } else if (logsOverlayOpen) {
          connectLogSocket();
        }
        if (!document.hidden && activePageId === 'page-activity-log') {
          refreshActivityLog(false).catch(() => {});
        }
        if (document.hidden || activePageId !== 'page-info') {
          stopInfoPolling();
        } else {
          startInfoPolling();
        }
        if (!document.hidden) {
          refreshWebUiLocale(true).catch(() => {});
          refreshAppHeaderWifi(true).catch(() => {});
          refreshAppHeaderTime(true).catch(() => {});
          probeHeaderReachability().catch(() => {});
        }
      });
    }

    initUpgradeBindings();
    initStatusBindings();
    initInfoBindings();
    initCalibrationBindings();
    initWifiBindings();
    initSystemBindings();
    initConfigBindings();
    initGlobalUiBindings();

    applyThemePreference(currentThemePreference(), false);
    applyWebUiLocale(webUiLocale);
    syncMenuIconFallbacks();
    renderUpgradeJourney(readUpgradeUiSession() || { phase: 'idle', target: '', detail: tr('updates.none', 'Aucune opération en cours.') });
    refreshWebUiLocale(true).catch(() => {});
    resumeUpgradeReconnectFlow();
    startAppHeaderClock();
    startHeaderReachabilityProbe();
    refreshAppHeader(resolveInitialPageId());
    const initialPageId = resolveInitialPageId();
    const startInitialUi = async () => {
      await loadWebMeta().catch(() => {});
      refreshAppHeaderWifi(true).catch(() => {});
      refreshAppHeaderTime(true).catch(() => {});
      showPage(initialPageId, { deferHeavyMs: 260 });
    };
    if (typeof window.requestAnimationFrame === 'function') {
      window.requestAnimationFrame(() => {
        startInitialUi().catch(() => {});
      });
    } else {
      setTimeout(() => {
        startInitialUi().catch(() => {});
      }, 16);
    }
