(function () {
  const App = window.ConfiguratorApp;
  if (App && App.initDom) {
    App.initDom();
  }

  const dataUrl =
    (App && App.dataUrl) ||
    (window.GCRegions && window.GCRegions.dataUrl);

  let CITIES = [];
  fetch(dataUrl("gc-locations.json"))
    .then(function (r) {
      if (!r.ok) return [];
      return r.json().catch(function () {
        return [];
      });
    })
    .then(function (data) {
      CITIES = Array.isArray(data) ? data : [];
      if (App && App.state) App.state.CITIES = CITIES;
      var inp = document.getElementById("city-search");
      if (inp)
        inp.placeholder =
          "Search here; i.e. Mobile, New Orleans, Pensacola, etc.";
      applyDefaultLocation();
    })
    .catch(function () {
      CITIES = [];
      if (App && App.state) App.state.CITIES = CITIES;
      var inp = document.getElementById("city-search");
      if (inp) inp.placeholder = "Could not load locations — try again later.";
    });

  const REGIONS = window.GCRegions || {};
  const ROOT_CODE = REGIONS.ROOT_CODE || "gc";
  const ROOT_LABEL = REGIONS.ROOT_LABEL || "Gulf Coast";
  const STATE_NAMES = REGIONS.STATE_NAMES || {};
  const STATE_ADJACENCY = REGIONS.STATE_ADJACENCY || {};
  const WIDER_SCOPES = REGIONS.WIDER_SCOPES || [];
  /** Repeater-name prefix built from the location, e.g. GC-MOB-. */
  const NAME_PREFIX_ROOT = ROOT_CODE.toUpperCase();

  const input = document.getElementById("city-search");
  const dropdown = document.getElementById("city-dropdown");
  const resultCard = document.getElementById("result-card");
  const resultGrid = document.getElementById("result-grid");
  const commandsCard = document.getElementById("commands-card");
  const commandsBlock = document.getElementById("commands-block");
  const copyBtn = document.getElementById("copy-btn");
  const cliShowDefaultsEl = document.getElementById("cli-show-defaults");
  const serialUsbBtn = document.getElementById("serial-usb-btn");
  const serialReadBtn = document.getElementById("serial-read-btn");
  const serialApplyBtn = document.getElementById("serial-apply-btn");
  const serialApplyBtn2 = document.getElementById("serial-apply-btn-2");
  const serialAdvertZerohopBtn = document.getElementById(
    "serial-advert-zerohop-btn",
  );
  const serialAdvertZerohopBtn2 = document.getElementById(
    "serial-advert-zerohop-btn-2",
  );
  const serialAdvertFloodBtn = document.getElementById(
    "serial-advert-flood-btn",
  );
  const serialAdvertFloodBtn2 = document.getElementById(
    "serial-advert-flood-btn-2",
  );
  const serialConsoleForm = document.getElementById("serial-console-form");
  const serialConsoleInput = document.getElementById("serial-console-input");
  const serialConsoleSendBtn = document.getElementById("serial-console-send-btn");
  const serialConsoleClearBtn = document.getElementById(
    "serial-console-clear-btn",
  );
  const serialStatusEl = document.getElementById("serial-status");
  const serialApplyLogEl = document.getElementById("serial-apply-log");
  const serialShowCommandLogEl = document.getElementById("serial-show-command-log");
  const serialUnsupportedEl = document.getElementById("serial-unsupported");
  const policyCard = document.getElementById("policy-card");
  const policyGridsContainer = document.getElementById(
    "policy-grids-container",
  );
  const namePrefixPreviewEl = document.getElementById("name-prefix-preview");
  const nameLocationModeWrapEl = document.getElementById(
    "name-location-mode-wrap",
  );
  const nameLocationModeEl = document.getElementById("name-location-mode");
  const nameSuffixEl = document.getElementById("name-suffix");
  const namePowerEmojiEl = document.getElementById("name-power-emoji");
  const namePreviewEl = document.getElementById("name-preview");
  const namePreviewMetaEl = document.getElementById("name-preview-meta");
  const namePreviewNoteEl = document.getElementById("name-preview-note");
  const settingDutycycleEl = document.getElementById("setting-dutycycle");
  const settingPathHashModeEl = document.getElementById(
    "setting-path-hash-mode",
  );
  const settingLoopDetectEl = document.getElementById("setting-loop-detect");
  const settingRepeatEl = document.getElementById("setting-repeat");
  const settingOwnerInfoEl = document.getElementById("setting-owner-info");
  const settingAdminPasswordEl = document.getElementById(
    "setting-admin-password",
  );
  const settingGuestPasswordEl = document.getElementById(
    "setting-guest-password",
  );
  const settingTxdelayEl = document.getElementById("setting-txdelay");
  const settingDirectTxdelayEl = document.getElementById(
    "setting-direct-txdelay",
  );
  const settingFloodAdvertIntervalEl = document.getElementById(
    "setting-flood-advert-interval",
  );
  const settingAdvertIntervalEl = document.getElementById(
    "setting-advert-interval",
  );
  const settingFloodMaxUnscopedEl = document.getElementById(
    "setting-flood-max-unscoped",
  );
  const settingFloodMaxAdvertEl = document.getElementById(
    "setting-flood-max-advert",
  );
  const settingFloodMaxEl = document.getElementById("setting-flood-max");
  const settingRxdelayEl = document.getElementById("setting-rxdelay");
  const settingRadioRxgainEl = document.getElementById("setting-radio-rxgain");
  const settingIntThreshEl = document.getElementById("setting-int-thresh");
  const settingAgcResetEl = document.getElementById("setting-agc-reset");
  const modCard = document.getElementById("mod-card");
  const modInfoSlotEl = document.getElementById("mod-info-slot");
  const settingOtaFwUrlEl = document.getElementById("setting-ota-fw-url");
  const settingOtaWifiSsidEl = document.getElementById("setting-ota-wifi-ssid");
  const settingOtaWifiPassEl = document.getElementById("setting-ota-wifi-pass");
  const modWanJoinBtn = document.getElementById("mod-wan-join-btn");
  const modWanCheckBtn = document.getElementById("mod-wan-check-btn");
  const modWanLeaveBtn = document.getElementById("mod-wan-leave-btn");
  const modWanPwrBtn = document.getElementById("mod-wan-pwr-btn");
  const deviceWanOtaBtn = document.getElementById("device-wan-ota-btn");
  const settingMultiAcksEl = document.getElementById("setting-multi-acks");
  const settingRadioPresetEl = document.getElementById("setting-radio-preset");
  const settingRadioCustomWrapEl = document.getElementById(
    "setting-radio-custom-wrap",
  );
  const settingRadioFreqEl = document.getElementById("setting-radio-freq");
  const settingRadioSfEl = document.getElementById("setting-radio-sf");
  const settingRadioBwEl = document.getElementById("setting-radio-bw");
  const settingRadioCrEl = document.getElementById("setting-radio-cr");
  const settingRadioTxpowerEl = document.getElementById("setting-radio-txpower");
  const settingRadioErrorEl = document.getElementById("setting-radio-error");
  const deviceInfoVersionEl = document.getElementById("device-info-version");
  const deviceInfoRoleEl = document.getElementById("device-info-role");
  const deviceInfoPubkeyEl = document.getElementById("device-info-pubkey");
  const deviceInfoClockEl = document.getElementById("device-info-clock");
  const deviceSyncClockBtn = document.getElementById("device-sync-clock-btn");
  const deviceCopyPubkeyBtn = document.getElementById("device-copy-pubkey-btn");
  const devicePrvkeyEl = document.getElementById("device-prvkey");
  const devicePrvkeyRevealBtn = document.getElementById(
    "device-prvkey-reveal-btn",
  );
  const devicePrvkeyCopyBtn = document.getElementById("device-prvkey-copy-btn");
  const deviceVanityBtn = document.getElementById("device-vanity-btn");
  const deviceRebootBtn = document.getElementById("device-reboot-btn");
  const deviceOtaBtn = document.getElementById("device-ota-btn");
  const deviceFactoryResetBtn = document.getElementById(
    "device-factory-reset-btn",
  );
  const configExportBtn = document.getElementById("config-export-btn");
  const configImportBtn = document.getElementById("config-import-btn");
  const configImportFileEl = document.getElementById("config-import-file");
  const settingLatEl = document.getElementById("setting-lat");
  const settingLonEl = document.getElementById("setting-lon");
  const settingAdvertLocEl = document.getElementById("setting-advert-loc");

  function positionApi() {
    return App && App.position ? App.position : null;
  }

  function getAdvertLocPolicy() {
    const api = positionApi();
    if (api) return api.getAdvertLocPolicy();
    return settingAdvertLocEl ? settingAdvertLocEl.value : "prefs";
  }

  function advertIncludesLocation() {
    const api = positionApi();
    if (api) return api.advertIncludesLocation();
    return getAdvertLocPolicy() !== "none";
  }

  function getFormCoords() {
    const api = positionApi();
    if (api) return api.getCoords();
    return { valid: false, lat: null, lon: null };
  }

  function shouldEnforceDefaults() {
    if (cliShowDefaultsEl && cliShowDefaultsEl.checked) return true;
    return !deviceCliBaseline;
  }

  function coordsRequiredForApply() {
    return getAdvertLocPolicy() === "prefs" && !getFormCoords().valid;
  }

  function parseGpsAdvertReply(reply) {
    const r = String(reply || "")
      .trim()
      .replace(/^>\s*/, "");
    if (r === "none" || r === "share" || r === "prefs") return r;
    return null;
  }

  let selectionMode = "none";
  let selectedStateCode = null;
  let selectedCity = null;
  let activeIndex = -1;
  let lastMatches = [];
  let lastNeighbors = [];
  let lastHasCoords = false;
  let namePreviewState = {
    name: "",
    isValid: false,
    totalBytes: 0,
    message: "Pick a location first to build a name.",
  };
  let serialApplyAbort = null;
  let serialApplying = false;
  let serialReading = false;
  let serialConsoleSending = false;
  let serialConsoleHistory = [];
  let serialConsoleHistoryBrowse = -1;
  /** Named region codes last read from the device (for region remove on apply). */
  let deviceNamedRegionsFromRead = null;
  /** Home region name last read from the device (null / "" / "*"). */
  let deviceHomeRegionFromRead = null;
  /** Default flood scope last read from the device (null / "" / "<null>"). */
  let deviceDefaultRegionFromRead = null;
  const SERIAL_CONSOLE_HISTORY_MAX = 50;
  const SERIAL_LOG_VERBOSE_KEY = "configurator.serialShowCommandLog";

  function isSerialShowCommandLog() {
    return Boolean(serialShowCommandLogEl && serialShowCommandLogEl.checked);
  }

  function initSerialShowCommandLogToggle() {
    if (!serialShowCommandLogEl) return;
    try {
      serialShowCommandLogEl.checked =
        sessionStorage.getItem(SERIAL_LOG_VERBOSE_KEY) === "1";
    } catch (_e) {
      /* ignore */
    }
    serialShowCommandLogEl.addEventListener("change", function () {
      try {
        sessionStorage.setItem(
          SERIAL_LOG_VERBOSE_KEY,
          serialShowCommandLogEl.checked ? "1" : "0",
        );
      } catch (_e) {
        /* ignore */
      }
    });
  }

  function isSerialBusy() {
    return serialApplying || serialReading || serialConsoleSending;
  }

  /**
   * Commands only this project's firmware answers. Stock MeshCore replies
   * "??: <key>" to an unknown get, which is what the probe keys off.
   */
  const MOD_PROBE_COMMAND = "get ota.wan.pwr";
  const MOD_READ_COMMANDS = [MOD_PROBE_COMMAND, "get ota.fw.url", "get ota.slot"];

  /** True once a device read has populated the grid; keeps location picks off it. */
  let policyFromDevice = false;
  let deviceAllowedSet = null;
  let deviceCliBaseline = null;
  let deviceDeniedSet = null;
  let modFirmwareDetected = false;
  let modWanPower = null;
  let deviceOtaFwUrl = "";

  function isUnknownCommandReply(reply) {
    return /^\?\?:/.test(String(reply || "").trim());
  }

  const REPEATER_READ_COMMANDS = [
    "get name",
    "get radio",
    "get repeat",
    "get owner.info",
    "get guest.password",
    "get dutycycle",
    "get flood.advert.interval",
    "get advert.interval",
    "get flood.max.unscoped",
    "get flood.max.advert",
    "get flood.max",
    "get path.hash.mode",
    "get loop.detect",
    "get txdelay",
    "get direct.txdelay",
    "get rxdelay",
    "get radio.rxgain",
    "get tx",
    "get int.thresh",
    "get agc.reset.interval",
    "get multi.acks",
    "get lat",
    "get lon",
    "gps advert",
    "region home",
    "region default",
    "region list allowed",
    "region list denied",
    "ver",
    "get role",
    "get public.key",
    "clock",
  ].concat(MOD_READ_COMMANDS);

  const NAME_POWER_EMOJI_VALUES = ["🌞", "⚡", "🔋", "👀"];

  function takeReadReply(byCmd, cmd, failures) {
    const entry = byCmd[cmd];
    if (!entry) {
      return undefined;
    }
    if (!entry.ok) {
      failures.push(cmd + ": " + (entry.reply || "failed"));
      return undefined;
    }
    return stripCliReply(entry.reply);
  }

  function splitNameSuffixAndEmoji(nameBody) {
    let body = String(nameBody || "");
    let emoji = "";
    for (let i = 0; i < NAME_POWER_EMOJI_VALUES.length; i++) {
      const mark = NAME_POWER_EMOJI_VALUES[i];
      if (body.endsWith(mark)) {
        emoji = mark;
        body = body.slice(0, -mark.length);
        break;
      }
    }
    return { body: body, emoji: emoji };
  }

  function openSettingsTier(tierId) {
    const el = document.getElementById(tierId);
    if (el instanceof HTMLDetailsElement) {
      el.open = true;
    }
  }

  function expandSettingsTiersAfterRead(flags) {
    // Expert fields were merged into the Advanced group; open it for either.
    if (flags && (flags.advanced || flags.expert)) {
      openSettingsTier("settings-tier-advanced");
    }
  }

  function scrollConfiguratorSection(sectionId) {
    const el = document.getElementById(sectionId);
    if (el && el.scrollIntoView) {
      el.scrollIntoView({ behavior: "smooth", block: "nearest" });
    }
  }

  function getRepeaterSerial() {
    return typeof window !== "undefined" ? window.RepeaterSerial : null;
  }

  /**
   * Confirm the USB serial session is still live before a command or button
   * action. Clears stale "Connected" UI when the port/streams are gone
   * (e.g. after an unacknowledged reboot or unplug).
   * @param {string} [actionLabel]
   * @returns {Promise<object|null>} RepeaterSerial or null
   */
  async function ensureSerialReady(actionLabel) {
    const rs = getRepeaterSerial();
    if (!rs || !rs.isSupported()) {
      appendSerialLog("Web Serial is not available in this browser.", "is-error");
      return null;
    }
    if (isSerialBusy()) {
      appendSerialLog(
        "Busy — wait for the current action to finish.",
        "is-error",
      );
      return null;
    }
    let ok = false;
    try {
      ok = rs.ensureConnected
        ? await rs.ensureConnected()
        : Boolean(rs.isConnected && rs.isConnected());
    } catch (_e) {
      ok = false;
    }
    if (!ok) {
      appendSerialLog(
        (actionLabel ? actionLabel + ": " : "") +
          "Device is not connected. Connect over USB first.",
        "is-error",
      );
      serialApplying = false;
      serialReading = false;
      serialConsoleSending = false;
      setSerialStatus("disconnected", "Disconnected");
      updateUsbApplyUi(getAnchor());
      return null;
    }
    return rs;
  }

  function handleSerialConnectionLost(reason) {
    serialApplying = false;
    serialReading = false;
    serialConsoleSending = false;
    setModDetected(false);
    modWanPower = null;
    deviceCliBaseline = null;
    clearDeviceRegionReadSnapshot();
    if (serialApplyAbort) {
      try {
        serialApplyAbort.abort();
      } catch (_e) {
        /* ignore */
      }
      serialApplyAbort = null;
    }
    setSerialStatus("disconnected", "Disconnected");
    refreshConfiguratorOutputs();
    const why = String(reason || "");
    // Intentional post-command drops are logged by the action handler.
    if (why.indexOf("command:") === 0) {
      return;
    }
    if (why === "device-disconnect") {
      appendSerialLog(
        "USB device disconnected. Reconnect over USB to continue.",
        "is-error",
      );
    } else if (why === "stale") {
      appendSerialLog(
        "Serial session was stale (no longer connected). Reconnect over USB.",
        "is-error",
      );
    } else {
      appendSerialLog(
        "USB serial connection lost. Reconnect over USB to continue.",
        "is-error",
      );
    }
  }

  function initSerialDisconnectWatch() {
    const rs = getRepeaterSerial();
    if (rs && typeof rs.setOnDisconnect === "function") {
      rs.setOnDisconnect(handleSerialConnectionLost);
    }
  }

  function appendSerialLog(text, className) {
    if (!serialApplyLogEl) return;
    const line = document.createElement("div");
    line.className = "serial-apply-log-line" + (className ? " " + className : "");
    line.textContent = text;
    serialApplyLogEl.appendChild(line);
    serialApplyLogEl.scrollTop = serialApplyLogEl.scrollHeight;
  }

  function clearSerialLog() {
    if (serialApplyLogEl) {
      serialApplyLogEl.textContent = "";
    }
  }

  function setSerialStatus(state, label) {
    if (!serialStatusEl) return;
    serialStatusEl.textContent = label;
    serialStatusEl.dataset.state = state;
  }

  function validateCommandLinesForSerial(lines) {
    const rs = getRepeaterSerial();
    const maxLen = rs ? rs.MAX_LINE_LEN : 151;
    const tooLong = lines.filter(function (line) {
      return line.length > maxLen;
    });
    if (tooLong.length) {
      return {
        ok: false,
        message:
          tooLong.length +
          " command(s) exceed " +
          maxLen +
          " characters (room server limit). Shorten region names or split manually.",
        lines: tooLong,
      };
    }
    return { ok: true };
  }

  function syncSerialUsbToggleButton(connected, busy, supported) {
    if (!serialUsbBtn) return;
    const labelEl = serialUsbBtn.querySelector(".serial-usb-btn-label");
    const connectIcon = serialUsbBtn.querySelector(".serial-btn-icon--connect");
    const disconnectIcon = serialUsbBtn.querySelector(
      ".serial-btn-icon--disconnect",
    );
    if (connected) {
      serialUsbBtn.dataset.action = "disconnect";
      serialUsbBtn.classList.add("serial-btn-secondary");
      serialUsbBtn.classList.remove("serial-usb-toggle--connect");
      serialUsbBtn.classList.add("serial-usb-toggle--disconnect");
      serialUsbBtn.title = "Disconnect the USB serial session";
      serialUsbBtn.setAttribute("aria-label", "Disconnect USB");
      if (labelEl) labelEl.textContent = "Disconnect";
      if (connectIcon) connectIcon.hidden = true;
      if (disconnectIcon) disconnectIcon.hidden = false;
    } else {
      serialUsbBtn.dataset.action = "connect";
      serialUsbBtn.classList.remove("serial-btn-secondary");
      serialUsbBtn.classList.add("serial-usb-toggle--connect");
      serialUsbBtn.classList.remove("serial-usb-toggle--disconnect");
      serialUsbBtn.title =
        "Chrome or Edge on HTTPS or localhost; 115200 baud";
      serialUsbBtn.setAttribute("aria-label", "Connect USB");
      if (labelEl) labelEl.textContent = "Connect USB";
      if (connectIcon) connectIcon.hidden = false;
      if (disconnectIcon) disconnectIcon.hidden = true;
    }
    serialUsbBtn.disabled = !supported || busy;
  }

  function onSerialUsbToggleClick(event) {
    if (!serialUsbBtn || serialUsbBtn.disabled) return;
    if (serialUsbBtn.dataset.action === "disconnect") {
      disconnectSerialUsb();
    } else {
      // Shift-click reaches the browser picker when several boards are paired.
      connectSerialUsb({ forcePicker: !!(event && event.shiftKey) });
    }
  }

  function updateUsbApplyUi(anchor) {
    const rs = getRepeaterSerial();
    const supported = rs && rs.isSupported();
    const busy = isSerialBusy();
    const connected = Boolean(rs && rs.isConnected());
    const consoleEnabled = supported && connected && !busy;
    if (serialUnsupportedEl) {
      serialUnsupportedEl.hidden = supported;
    }
    syncSerialUsbToggleButton(connected, busy, supported);
    refreshModUi();
    if (serialReadBtn) {
      serialReadBtn.disabled = !supported || busy || !connected;
    }
    const advertEnabled = supported && !busy && rs && rs.isConnected();
    [serialAdvertZerohopBtn, serialAdvertZerohopBtn2].forEach(function (btn) {
      if (btn) btn.disabled = !advertEnabled;
    });
    [serialAdvertFloodBtn, serialAdvertFloodBtn2].forEach(function (btn) {
      if (btn) btn.disabled = !advertEnabled;
    });
    if (serialConsoleInput) {
      serialConsoleInput.disabled = !consoleEnabled;
    }
    if (serialConsoleSendBtn) {
      serialConsoleSendBtn.disabled = !consoleEnabled;
    }
    if (serialConsoleClearBtn) {
      serialConsoleClearBtn.disabled = !connected;
    }
    // Device-tools maintenance actions require an active connection.
    const deviceActionEnabled = supported && connected && !busy;
    [
      deviceSyncClockBtn,
      deviceRebootBtn,
      deviceOtaBtn,
      deviceFactoryResetBtn,
    ].forEach(function (btn) {
      if (btn) btn.disabled = !deviceActionEnabled;
    });
    if (serialApplyBtn) {
      const applyLines = buildConfiguratorCommandLines(anchor, {
        enforceFirmwareDefaults: shouldEnforceDefaults(),
      });
      const hasCommands = applyLines.length > 0;
      const needsLocation = !anchor;
      const needsCoords = coordsRequiredForApply();
      serialApplyBtn.disabled =
        !supported ||
        busy ||
        !(rs && rs.isConnected()) ||
        !hasCommands ||
        needsLocation ||
        needsCoords;
      if (needsCoords && rs && rs.isConnected()) {
        serialApplyBtn.title =
          "Set latitude and longitude — advert location uses stored prefs.";
      } else if (needsLocation && rs && rs.isConnected()) {
        serialApplyBtn.title = "Choose a location for region scopes.";
      } else if (!hasCommands && rs && rs.isConnected()) {
        serialApplyBtn.title = "Nothing to send — the repeater already matches this form.";
      } else {
        serialApplyBtn.title = "";
      }
    }
    // Mirror the primary Apply button's state onto the preview-area duplicate.
    if (serialApplyBtn2) {
      if (serialApplyBtn) {
        serialApplyBtn2.disabled = serialApplyBtn.disabled;
        serialApplyBtn2.title = serialApplyBtn.title;
      } else {
        serialApplyBtn2.disabled = true;
      }
    }
    if (serialReading) {
      setSerialStatus("reading", "Reading…");
    } else if (rs && rs.isConnected() && !serialApplying) {
      setSerialStatus("connected", "Connected");
    } else if (serialApplying) {
      setSerialStatus("applying", "Applying…");
    } else if (supported) {
      setSerialStatus("disconnected", "Disconnected");
    }
  }

  async function connectSerialUsb(opts) {
    const rs = getRepeaterSerial();
    if (!rs || !rs.isSupported()) return;
    const forcePicker = !!(opts && opts.forcePicker);
    try {
      clearSerialLog();
      const paired = forcePicker
        ? []
        : await (rs.getAuthorizedPorts ? rs.getAuthorizedPorts() : []);
      appendSerialLog(
        paired.length
          ? "Reconnecting to a previously paired device…"
          : "Requesting USB port…",
      );
      const result = await rs.connect({
        baudRate: rs.DEFAULT_BAUD,
        forcePicker: forcePicker,
      });
      appendSerialLog(
        "Connected at " +
          rs.DEFAULT_BAUD +
          " baud" +
          (result && result.viaReconnect
            ? " (reconnected without the picker)."
            : "."),
      );
      const probe = await rs.sendLine("ver");
      if (probe.reply) {
        appendSerialLog("Device: " + probe.reply);
      }
      if (await probeModFirmware()) {
        appendSerialLog(
          "Hotspot OTA firmware detected — extra commands available.",
          "is-ok",
        );
      }
      updateUsbApplyUi(getAnchor());
      if (serialConsoleInput) {
        serialConsoleInput.focus({ preventScroll: true });
      }
      promptReadFromRepeater({ afterConnect: true });
    } catch (err) {
      appendSerialLog(
        "Connect failed: " + (err && err.message ? err.message : String(err)),
        "is-error",
      );
      setSerialStatus("disconnected", "Disconnected");
      updateUsbApplyUi(getAnchor());
    }
  }

  async function disconnectSerialUsb() {
    const rs = getRepeaterSerial();
    if (!rs) return;
    if (serialApplyAbort) {
      serialApplyAbort.abort();
      serialApplyAbort = null;
    }
    try {
      await rs.disconnect();
      appendSerialLog("Disconnected.");
    } catch (err) {
      appendSerialLog(
        "Disconnect error: " + (err && err.message ? err.message : String(err)),
        "is-error",
      );
    }
    serialApplying = false;
    serialReading = false;
    serialConsoleSending = false;
    setModDetected(false);
    modWanPower = null;
    deviceCliBaseline = null;
    clearDeviceRegionReadSnapshot();
    setSerialStatus("disconnected", "Disconnected");
    refreshConfiguratorOutputs();
  }

  function logSerialCommandReply(line, result, err) {
    appendSerialLog("> " + line);
    if (err) {
      appendSerialLog(
        "  -> " + (err && err.message ? err.message : String(err)),
        "is-error",
      );
      return;
    }
    if (result && result.reply) {
      appendSerialLog(
        "  -> " + result.reply,
        result.ok ? "is-ok" : "is-error",
      );
    } else if (result && !result.ok) {
      appendSerialLog("  -> (no reply)", "is-error");
    } else if (result) {
      appendSerialLog("  -> (ok)", "is-ok");
    }
  }

  function pushSerialConsoleHistory(cmd) {
    if (!cmd) return;
    const last = serialConsoleHistory[serialConsoleHistory.length - 1];
    if (last === cmd) return;
    serialConsoleHistory.push(cmd);
    if (serialConsoleHistory.length > SERIAL_CONSOLE_HISTORY_MAX) {
      serialConsoleHistory.shift();
    }
    serialConsoleHistoryBrowse = -1;
  }

  async function sendSerialConsoleCommand(line) {
    const cmd = String(line || "").trim();
    if (!cmd) return;
    const rs = await ensureSerialReady("Console");
    if (!rs) return;

    const maxLen = rs.MAX_LINE_LEN || 151;
    if (cmd.length > maxLen) {
      appendSerialLog(
        "Command too long (" + cmd.length + " > " + maxLen + ").",
        "is-error",
      );
      return;
    }

    serialConsoleSending = true;
    updateUsbApplyUi(getAnchor());

    try {
      const result = await rs.sendLine(cmd);
      if (result && result.disconnected) {
        appendSerialLog("> " + cmd);
        appendSerialLog(
          "Command sent; device is disconnecting.",
          "is-ok",
        );
        pushSerialConsoleHistory(cmd);
        return;
      }
      logSerialCommandReply(cmd, result);
      pushSerialConsoleHistory(cmd);
    } catch (err) {
      logSerialCommandReply(cmd, null, err);
    } finally {
      serialConsoleSending = false;
      updateUsbApplyUi(getAnchor());
      if (serialConsoleInput && rs.isConnected()) {
        serialConsoleInput.focus({ preventScroll: true });
      }
    }
  }

  function onSerialConsoleSubmit(ev) {
    if (ev && ev.preventDefault) {
      ev.preventDefault();
    }
    const value = serialConsoleInput ? serialConsoleInput.value : "";
    if (serialConsoleInput) {
      serialConsoleInput.value = "";
    }
    sendSerialConsoleCommand(value);
  }

  function onSerialConsoleKeydown(ev) {
    if (!serialConsoleInput || !serialConsoleHistory.length) {
      return;
    }
    if (ev.key === "ArrowUp") {
      ev.preventDefault();
      if (serialConsoleHistoryBrowse < 0) {
        serialConsoleHistoryBrowse = serialConsoleHistory.length - 1;
      } else if (serialConsoleHistoryBrowse > 0) {
        serialConsoleHistoryBrowse--;
      }
      serialConsoleInput.value =
        serialConsoleHistory[serialConsoleHistoryBrowse] || "";
    } else if (ev.key === "ArrowDown") {
      ev.preventDefault();
      if (serialConsoleHistoryBrowse < 0) {
        return;
      }
      if (serialConsoleHistoryBrowse >= serialConsoleHistory.length - 1) {
        serialConsoleHistoryBrowse = -1;
        serialConsoleInput.value = "";
      } else {
        serialConsoleHistoryBrowse++;
        serialConsoleInput.value =
          serialConsoleHistory[serialConsoleHistoryBrowse] || "";
      }
    }
  }

  async function sendRepeaterAdvert(kind) {
    const rs = await ensureSerialReady(
      kind === "zerohop" ? "Zero-hop advert" : "Flood advert",
    );
    if (!rs) return;

    const cmd = kind === "zerohop" ? "advert.zerohop" : "advert";

    serialConsoleSending = true;
    updateUsbApplyUi(getAnchor());
    try {
      const result = await rs.sendLine(cmd);
      logSerialCommandReply(cmd, result);
      pushSerialConsoleHistory(cmd);
    } catch (err) {
      logSerialCommandReply(cmd, null, err);
    } finally {
      serialConsoleSending = false;
      updateUsbApplyUi(getAnchor());
    }
  }

  function stripCliReply(reply) {
    return String(reply || "")
      .replace(/^\s*>\s*/, "")
      .trim();
  }

  function indexResultsByCommand(results) {
    const map = Object.create(null);
    (results || []).forEach(function (entry) {
      if (entry && entry.line) {
        map[entry.line] = entry;
      }
    });
    return map;
  }

  function clearDeviceRegionReadSnapshot() {
    deviceNamedRegionsFromRead = null;
    deviceHomeRegionFromRead = null;
    deviceDefaultRegionFromRead = null;
  }

  function rememberDeviceRegionsFromRead(
    allowed,
    denied,
    homeRegion,
    defaultRegion,
  ) {
    const set = new Set();
    (allowed || []).forEach(function (c) {
      const code = String(c || "").trim();
      if (code && code !== "*") set.add(code);
    });
    (denied || []).forEach(function (c) {
      const code = String(c || "").trim();
      if (code && code !== "*") set.add(code);
    });
    const home = String(homeRegion || "").trim();
    if (home && home !== "*") set.add(home);
    const def = String(defaultRegion || "").trim();
    if (def && def !== "*" && def.toLowerCase() !== "<null>") set.add(def);
    deviceNamedRegionsFromRead = set;
    deviceHomeRegionFromRead = home || null;
    deviceDefaultRegionFromRead =
      !def || def.toLowerCase() === "<null>" ? null : def;
  }

  /**
   * MeshCore requires child regions removed before parents. Scope codes are
   * hierarchical by dash (gc-al-mob under gc-al under gc), so segment count
   * is the depth.
   */
  function regionHierarchyDepth(code) {
    if (!code || code === "*") return 0;
    return String(code).split("-").length * 10;
  }

  /** Nearest ancestor of a dashed scope code that is also in `needed`. */
  function parentRegionCode(code, needed) {
    const parts = String(code || "").split("-");
    for (let i = parts.length - 1; i > 0; i--) {
      const ancestor = parts.slice(0, i).join("-");
      if (!needed || needed.has(ancestor)) return ancestor;
    }
    return null;
  }

  function orderRegionRemovesDeepestFirst(codes) {
    return codes.slice().sort(function (a, b) {
      const d = regionHierarchyDepth(b) - regionHierarchyDepth(a);
      if (d !== 0) return d;
      if (b.length !== a.length) return b.length - a.length;
      return a.localeCompare(b);
    });
  }

  function parseRegionNameList(reply) {
    const s = stripCliReply(reply);
    if (!s || s === "-none-") return [];
    return s
      .split(",")
      .map(function (part) {
        return part.trim();
      })
      .filter(Boolean);
  }

  function parseRegionHomeName(reply) {
    const s = stripCliReply(reply);
    const m = s.match(/^home is\s+(.+)$/i);
    if (!m) return "";
    return m[1].trim();
  }

  function parseRegionDefaultName(reply) {
    const s = stripCliReply(reply);
    const m = s.match(/^default scope is\s+(.+)$/i);
    if (!m) return "";
    return m[1].trim();
  }

  function snapDutycycleSelect(value) {
    const options = [1, 10, 50, 100];
    const v = parseFloat(String(value).replace(/%$/, ""));
    if (!Number.isFinite(v)) return null;
    let best = options[0];
    let bestDiff = Math.abs(v - best);
    for (let i = 1; i < options.length; i++) {
      const d = Math.abs(v - options[i]);
      if (d < bestDiff) {
        best = options[i];
        bestDiff = d;
      }
    }
    return String(best);
  }

  function setSelectIfPresent(el, value) {
    if (!el || value == null || value === "") return false;
    const v = String(value);
    for (let i = 0; i < el.options.length; i++) {
      if (el.options[i].value === v) {
        el.value = v;
        return true;
      }
    }
    return false;
  }

  function applyReadRadioToForm(reply) {
    const raw = stripCliReply(reply);
    const parts = raw.split(",");
    if (parts.length < 4) return false;
    const params = {
      freq: parseFloat(parts[0]),
      bw: parseFloat(parts[1]),
      sf: parseInt(parts[2], 10),
      cr: parseInt(parts[3], 10),
    };
    if (
      !Number.isFinite(params.freq) ||
      !Number.isFinite(params.bw) ||
      !Number.isFinite(params.sf) ||
      !Number.isFinite(params.cr)
    ) {
      return false;
    }
    if (!settingRadioPresetEl) return false;
    let matched = -1;
    for (let i = 0; i < FREQUENCY_PRESETS.length; i++) {
      if (radioParamsMatch(params, FREQUENCY_PRESETS[i])) {
        matched = i;
        break;
      }
    }
    if (matched >= 0) {
      settingRadioPresetEl.value = String(matched);
      settingRadioPresetEl.dataset.lastPreset = String(matched);
    } else {
      settingRadioPresetEl.value = "custom";
      fillCustomRadioFields(params);
    }
    refreshRadioSettingsUi();
    return true;
  }

  function applyReadNameToForm(deviceName, anchor) {
    const name = String(deviceName || "").trim();
    if (!name || !nameSuffixEl) {
      return { applied: false };
    }

    let remainder = name;
    let prefixMismatch = false;
    const prefix = anchor ? getEffectivePrefix(anchor) : "";

    if (anchor && prefix) {
      const nameU = name.toUpperCase();
      const prefixU = prefix.toUpperCase();
      if (nameU.indexOf(prefixU) === 0) {
        remainder = name.slice(prefix.length);
      } else {
        prefixMismatch = true;
        remainder = name;
      }
    }

    const split = splitNameSuffixAndEmoji(remainder);
    nameSuffixEl.value = split.body;
    if (namePowerEmojiEl) {
      if (split.emoji) {
        setSelectIfPresent(namePowerEmojiEl, split.emoji);
      } else {
        namePowerEmojiEl.value = "";
      }
    }

    return { applied: true, prefixMismatch: prefixMismatch };
  }

  /**
   * Detect location from a repeater name prefix (GC- / GC-XXX-).
   * City segment preferred over state when both could match.
   */
  function detectLocationFromDeviceName(deviceName) {
    const name = String(deviceName || "").trim();
    if (!name) return null;
    const nameU = name.toUpperCase();
    if (nameU.indexOf(NAME_PREFIX_ROOT + "-") !== 0) return null;
    if (!CITIES || !CITIES.length) return null;

    for (let i = 0; i < CITIES.length; i++) {
      const city = CITIES[i];
      const prefix = buildNamePrefix(
        {
          mode: "city",
          state_code: city.state_code,
          row: city,
        },
        "city",
      );
      if (!prefix) continue;
      if (nameU.indexOf(prefix.toUpperCase()) === 0) {
        return {
          choice: {
            type: "city",
            city: city,
            label: city.name,
          },
          mode: "city",
          prefix: prefix,
          locationLabel: city.name + " (" + city.city_code + ")",
          source: "name",
        };
      }
    }

    const stateCodes = Object.keys(STATE_NAMES);
    for (let i = 0; i < stateCodes.length; i++) {
      const pc = stateCodes[i];
      const prefix = buildNamePrefix(
        { mode: "state", state_code: pc, row: null },
        "state",
      );
      if (!prefix) continue;
      if (nameU.indexOf(prefix.toUpperCase()) === 0) {
        return {
          choice: {
            type: "state",
            code: pc,
            label: STATE_NAMES[pc] || pc,
          },
          mode: "state",
          prefix: prefix,
          locationLabel: (STATE_NAMES[pc] || pc) + " (" + pc + ")",
          source: "name",
        };
      }
    }

    if (new RegExp("^" + NAME_PREFIX_ROOT + "-[A-Z0-9]{2,6}-", "i").test(name)) {
      return null;
    }

    const countryPrefix = buildNamePrefix(
      { mode: "country", state_code: null, row: null },
      "country",
    );
    if (countryPrefix && nameU.indexOf(countryPrefix.toUpperCase()) === 0) {
      return {
        choice: { type: "country", label: ROOT_LABEL + " (" + ROOT_CODE + ")" },
        mode: "country",
        prefix: countryPrefix,
        locationLabel: ROOT_LABEL + " (" + ROOT_CODE + ")",
        source: "name",
      };
    }
    return null;
  }

  /**
   * Fallback location when the name has no prefix: `region home` only.
   * The allow list is deliberately not consulted -- it says which regions a
   * node floods, not where it sits, so its order would pick a location at
   * random among the neighbours it allows.
   */
  function detectLocationFromRegionHints(homeRegion) {
    const ordered = [];
    const home = String(homeRegion || "").trim();
    if (home && home !== "*") ordered.push(home);
    if (!ordered.length) return null;

    for (let i = 0; i < ordered.length; i++) {
      const city = findCityByCode(ordered[i]);
      if (city) {
        return {
          choice: {
            type: "city",
            city: city,
            label: city.name,
          },
          mode: "city",
          prefix: buildNamePrefix(
            {
              mode: "city",
              state_code: city.state_code,
              row: city,
            },
            "city",
          ),
          locationLabel: city.name + " (" + city.city_code + ")",
          source: "regions",
        };
      }
    }

    for (let i = 0; i < ordered.length; i++) {
      const pc = ordered[i];
      if (!Object.prototype.hasOwnProperty.call(STATE_NAMES, pc)) continue;
      return {
        choice: {
          type: "state",
          code: pc,
          label: STATE_NAMES[pc] || pc,
        },
        mode: "state",
        prefix: buildNamePrefix(
          { mode: "state", state_code: pc, row: null },
          "state",
        ),
        locationLabel: (STATE_NAMES[pc] || pc) + " (" + pc + ")",
        source: "regions",
      };
    }

    if (ordered.indexOf(ROOT_CODE) >= 0) {
      return {
        choice: { type: "country", label: ROOT_LABEL + " (" + ROOT_CODE + ")" },
        mode: "country",
        prefix: buildNamePrefix(
          { mode: "country", state_code: null, row: null },
          "country",
        ),
        locationLabel: ROOT_LABEL + " (" + ROOT_CODE + ")",
        source: "regions",
      };
    }
    return null;
  }

  function locationDetectionMatchesAnchor(detection, anchor) {
    if (!detection || !anchor) return false;
    if (detection.choice.type === "city" && anchor.mode === "city" && anchor.row) {
      return anchor.row.city_code === detection.choice.city.city_code;
    }
    if (detection.choice.type === "state" && anchor.mode === "state") {
      return anchor.state_code === detection.choice.code;
    }
    if (detection.choice.type === "country" && anchor.mode === "country") {
      return true;
    }
    return false;
  }

  function applyDetectedLocationFromDevice(detection) {
    if (!detection || !detection.choice) return getAnchor();
    if (!locationDetectionMatchesAnchor(detection, getAnchor())) {
      commitLocationChoice(detection.choice, "keep");
    }
    if (detection.mode) {
      refreshLocationModeOptions(getAnchor());
      setCurrentLocationMode(detection.mode);
      syncPrefixField(getAnchor());
    }
    return getAnchor();
  }

  /** Ensure allow/deny rows exist for region codes present on the device but not in the default grids. */
  function ensureDeviceRegionScopeRows(codes) {
    if (!policyGridsContainer || !policyCard) return [];
    const wanted = [];
    const seen = new Set();
    (codes || []).forEach(function (code) {
      const c = String(code || "").trim();
      if (!c || c === "*" || seen.has(c)) return;
      seen.add(c);
      wanted.push(c);
    });
    if (!wanted.length) return [];

    const missing = wanted.filter(function (code) {
      return !policyCard.querySelector(
        'input.policy-allow[data-code="' + code.replace(/"/g, "") + '"]',
      );
    });
    if (!missing.length) return [];

    let scopesCol = policyGridsContainer.querySelector(
      ".policy-grids-col--scopes",
    );
    if (!scopesCol) {
      scopesCol = document.createElement("div");
      scopesCol.className = "policy-grids-col policy-grids-col--scopes";
      const layout = policyGridsContainer.querySelector(".policy-grids-layout");
      if (layout) layout.appendChild(scopesCol);
      else policyGridsContainer.appendChild(scopesCol);
    }

    let subsection = scopesCol.querySelector(
      '.policy-subsection[data-policy-scope="device"]',
    );
    if (!subsection) {
      subsection = document.createElement("div");
      subsection.className = "policy-subsection";
      subsection.setAttribute("data-policy-scope", "device");
      subsection.innerHTML =
        '<div class="policy-subhead"><h3 class="policy-subtitle">Scopes from device</h3></div>' +
        '<p class="policy-subsection-note">Region codes read from the repeater that are outside the usual neighbour lists for this location.</p>' +
        '<div class="policy-table-head" role="row">' +
        '<div class="policy-head-scope" role="columnheader">Scope</div>' +
        '<div class="policy-head-clear-wrap" role="columnheader"></div>' +
        '<div class="policy-head-col" role="columnheader"><span class="policy-head-label">Allow</span></div>' +
        '<div class="policy-head-col" role="columnheader"><span class="policy-head-label">Deny</span></div>' +
        "</div>";
      scopesCol.appendChild(subsection);
    }

    missing.forEach(function (code) {
      if (
        subsection.querySelector(
          'input.policy-allow[data-code="' + code.replace(/"/g, "") + '"]',
        )
      ) {
        return;
      }
      const name = expandedNameForRegionCode(code);
      const label =
        name !== code
          ? escapeHtml(name) + " (" + escapeHtml(code) + ")"
          : escapeHtml(code);
      subsection.insertAdjacentHTML(
        "beforeend",
        policyRow(label, code, { allow: false, deny: false }),
      );
    });

    syncScopeMasters(subsection);
    refreshHomeOverrideSelect();
    refreshDefaultScopeSelect();
    return missing;
  }

  function applyDeviceCheckState() {
    if (!policyCard || !deviceAllowedSet) return;
    policyCard.querySelectorAll("input.policy-allow").forEach(function (el) {
      const code = el.getAttribute("data-code");
      if (!code) return;
      el.checked = deviceAllowedSet.has(code);
    });
    policyCard.querySelectorAll("input.policy-deny").forEach(function (el) {
      const code = el.getAttribute("data-code");
      if (!code) return;
      el.checked = deviceDeniedSet.has(code);
    });
    const untagged = document.getElementById("policy-untagged-flood");
    if (untagged) untagged.checked = deviceAllowedSet.has("*");
  }

  const COORD_LOCATION_MAX_MI = 60;

  function resolveLocationFromCoords(lat, lon, mark) {
    const seed = { lat: lat, lon: lon, state_code: null, city_code: "__device__", name: "" };
    const near = findGeographicNeighbors(seed, {
      maxKm: COORD_LOCATION_MAX_MI * KM_PER_MI,
      maxCount: 1,
    });
    if (near.length) {
      const city = near[0].c;
      const miles = Math.round(kmToMi(near[0].km));
      commitLocationChoice(
        { type: "city", city: city, label: city.name },
        "keep",
      );
      if (input) input.value = city.name;
      if (typeof mark === "function") mark("Location", "general");
      appendSerialLog(
        "Detected location from the device's stored coordinates: " +
          city.name +
          " (" +
          city.city_code +
          "), ~" +
          miles +
          " mi away.",
        "is-ok",
      );
      return getAnchor();
    }
    clearLocationSelection();
    if (input) input.value = "";
    refreshConfiguratorOutputs();
    appendSerialLog(
      "Location left unset — the device's coordinates are more than " +
        COORD_LOCATION_MAX_MI +
        " mi from any mapped city, and neither its name nor region home says where it is.",
      "is-error",
    );
    return null;
  }

  function applyReadRegionsToPolicy(
    allowed,
    denied,
    homeRegion,
    anchor,
    defaultRegion,
  ) {
    rememberDeviceRegionsFromRead(allowed, denied, homeRegion, defaultRegion);
    if (!policyCard) {
      return { applied: false, reason: "no-card", missing: [] };
    }

    const allowedSet = new Set(allowed || []);
    const deniedSet = new Set(denied || []);
    deviceAllowedSet = allowedSet;
    deviceDeniedSet = deniedSet;
    policyFromDevice = true;

    if (anchor) {
      rebuildPolicyGridsForAnchor(anchor);
    } else {
      renderDeviceRegionGrid(allowed, denied, homeRegion, defaultRegion);
    }

    const allCodes = [];
    allowedSet.forEach(function (c) {
      allCodes.push(c);
    });
    deniedSet.forEach(function (c) {
      allCodes.push(c);
    });
    if (homeRegion) allCodes.push(homeRegion);
    if (
      defaultRegion &&
      String(defaultRegion).toLowerCase() !== "<null>"
    ) {
      allCodes.push(defaultRegion);
    }

    const missing = ensureDeviceRegionScopeRows(allCodes);

    applyDeviceCheckState();

    finalizePolicyUiChange();
    refreshHomeOverrideSelect();
    refreshDefaultScopeSelect();

    const ov = document.getElementById("policy-home-override");
    const sel = document.getElementById("policy-home-override-select");
    if (ov && sel && homeRegion) {
      const defaultHome = deepestAllowedHomeRegionCode(anchor);
      if (homeRegion === defaultHome) {
        ov.checked = false;
        sel.value = "";
      } else if (homeRegion === "*") {
        ov.checked = true;
        sel.value = HOME_OVERRIDE_OMIT;
      } else {
        ov.checked = true;
        let found = false;
        for (let i = 0; i < sel.options.length; i++) {
          if (sel.options[i].value === homeRegion) {
            sel.value = homeRegion;
            found = true;
            break;
          }
        }
        if (!found) {
          appendSerialLog(
            "Home region " +
              homeRegion +
              " is not in the policy list for this location.",
            "is-error",
          );
        }
      }
      sel.disabled = !ov.checked;
    }

    const defSel = document.getElementById("policy-default-scope-select");
    if (defSel) {
      const autoCode = recommendedDefaultScopeCode(anchor);
      const defNorm = String(defaultRegion || "")
        .trim()
        .toLowerCase();
      if (!defNorm || defNorm === "<null>") {
        if (!autoCode) {
          defSel.value = "";
        } else {
          defSel.value = DEFAULT_SCOPE_NONE;
        }
      } else if (defaultRegion === autoCode) {
        defSel.value = "";
      } else {
        let found = false;
        for (let i = 0; i < defSel.options.length; i++) {
          if (defSel.options[i].value === defaultRegion) {
            defSel.value = defaultRegion;
            found = true;
            break;
          }
        }
        if (!found) {
          const o = document.createElement("option");
          o.value = defaultRegion;
          o.textContent = homeOverrideOptionLabel(defaultRegion);
          defSel.appendChild(o);
          defSel.value = defaultRegion;
          appendSerialLog(
            "Default scope " +
              defaultRegion +
              " is not currently Allow-checked; kept as an explicit override.",
            "is-ok",
          );
        }
      }
    }

    return { applied: true, missing: missing };
  }

  function applyReadResultsToForm(byCmd, anchor) {
    let updated = 0;
    const failures = [];
    const labels = [];
    const tierFlags = { advanced: false, expert: false };
    let scrollTarget = null;
    let namePrefixMismatch = false;
    let workingAnchor = anchor || getAnchor();

    function mark(label, tier) {
      labels.push(label);
      updated++;
      if (tier === "advanced") {
        tierFlags.advanced = true;
      } else if (tier === "expert") {
        tierFlags.expert = true;
      }
    }

    const nameValue = takeReadReply(byCmd, "get name", failures);
    const homeValue = takeReadReply(byCmd, "region home", failures);
    const defaultScopeValue = takeReadReply(byCmd, "region default", failures);
    const allowedValue = takeReadReply(byCmd, "region list allowed", failures);
    const deniedValue = takeReadReply(byCmd, "region list denied", failures);
    const homeRegion =
      homeValue !== undefined ? parseRegionHomeName(homeValue) || homeValue : "";
    const defaultRegion =
      defaultScopeValue !== undefined
        ? parseRegionDefaultName(defaultScopeValue) || defaultScopeValue
        : "";
    const allowed =
      allowedValue !== undefined ? parseRegionNameList(allowedValue) : [];
    const denied =
      deniedValue !== undefined ? parseRegionNameList(deniedValue) : [];

    const detection =
      (nameValue !== undefined
        ? detectLocationFromDeviceName(nameValue)
        : null) ||
      (homeValue !== undefined
        ? detectLocationFromRegionHints(homeRegion)
        : null);

    if (detection) {
      workingAnchor = applyDetectedLocationFromDevice(detection);
      mark("Location", "general");
      scrollTarget = scrollTarget || "config-identity-block";
      appendSerialLog(
        "Detected location from " +
          (detection.source === "regions"
            ? "region home"
            : "name prefix " + (detection.prefix || "")) +
          ": " +
          detection.locationLabel +
          ".",
        "is-ok",
      );
    }

    if (nameValue !== undefined) {
      const nameResult = applyReadNameToForm(nameValue, workingAnchor);
      if (nameResult.applied) {
        mark("Name", "general");
        scrollTarget = scrollTarget || "config-identity-block";
        if (nameResult.prefixMismatch) {
          namePrefixMismatch = true;
        }
      }
    }

    const radioValue = takeReadReply(byCmd, "get radio", failures);
    if (radioValue !== undefined && applyReadRadioToForm(radioValue)) {
      mark("Radio", "general");
      if (isCustomRadioPreset()) {
        tierFlags.expert = true;
      }
      scrollTarget = scrollTarget || "general-card";
    }

    const repeatValue = takeReadReply(byCmd, "get repeat", failures);
    if (repeatValue !== undefined && settingRepeatEl) {
      if (repeatValue === "on" || repeatValue === "off") {
        settingRepeatEl.value = repeatValue;
        mark("Repeat mode", "general");
      }
    }

    const ownerValue = takeReadReply(byCmd, "get owner.info", failures);
    if (ownerValue !== undefined && settingOwnerInfoEl) {
      settingOwnerInfoEl.value = ownerValue.replace(/\|/g, " ");
      mark("Owner info", "general");
    }

    const guestValue = takeReadReply(byCmd, "get guest.password", failures);
    if (guestValue !== undefined && settingGuestPasswordEl) {
      settingGuestPasswordEl.value = guestValue;
      mark("Guest password", "general");
    }

    const dutycycleValue = takeReadReply(byCmd, "get dutycycle", failures);
    if (dutycycleValue !== undefined && settingDutycycleEl) {
      const snapped = snapDutycycleSelect(dutycycleValue);
      if (snapped && setSelectIfPresent(settingDutycycleEl, snapped)) {
        mark("Duty cycle", "general");
      }
    }

    const floodAdvertValue = takeReadReply(
      byCmd,
      "get flood.advert.interval",
      failures,
    );
    if (floodAdvertValue !== undefined && settingFloodAdvertIntervalEl) {
      const hours = parseInt(floodAdvertValue, 10);
      if (Number.isFinite(hours)) {
        settingFloodAdvertIntervalEl.value = String(hours);
        mark("Flood advert interval", "advanced");
      }
    }

    const advertValue = takeReadReply(byCmd, "get advert.interval", failures);
    if (advertValue !== undefined && settingAdvertIntervalEl) {
      const minutes = parseInt(advertValue, 10);
      if (Number.isFinite(minutes)) {
        const formMinutes = minutes > 0 ? minutes / 2 : 0;
        settingAdvertIntervalEl.value =
          formMinutes > 0 ? String(formMinutes) : "0";
        mark("Zero-hop advert interval", "advanced");
      }
    }

    const floodMaxUnscopedValue = takeReadReply(
      byCmd,
      "get flood.max.unscoped",
      failures,
    );
    if (floodMaxUnscopedValue !== undefined && settingFloodMaxUnscopedEl) {
      settingFloodMaxUnscopedEl.value = floodMaxUnscopedValue;
      mark("Flood max unscoped", "advanced");
    }

    const floodMaxAdvertValue = takeReadReply(
      byCmd,
      "get flood.max.advert",
      failures,
    );
    if (floodMaxAdvertValue !== undefined && settingFloodMaxAdvertEl) {
      settingFloodMaxAdvertEl.value = floodMaxAdvertValue;
      mark("Flood max advert", "advanced");
    }

    const floodMaxValue = takeReadReply(byCmd, "get flood.max", failures);
    if (floodMaxValue !== undefined && settingFloodMaxEl) {
      settingFloodMaxEl.value = floodMaxValue;
      mark("Flood max", "advanced");
    }

    const pathHashValue = takeReadReply(byCmd, "get path.hash.mode", failures);
    if (pathHashValue !== undefined && settingPathHashModeEl) {
      if (setSelectIfPresent(settingPathHashModeEl, pathHashValue)) {
        mark("Path hash mode", "advanced");
      }
    }

    const loopDetectValue = takeReadReply(byCmd, "get loop.detect", failures);
    if (loopDetectValue !== undefined && settingLoopDetectEl) {
      if (setSelectIfPresent(settingLoopDetectEl, loopDetectValue)) {
        mark("Loop detection", "advanced");
      }
    }

    const txdelayValue = takeReadReply(byCmd, "get txdelay", failures);
    if (txdelayValue !== undefined && settingTxdelayEl) {
      settingTxdelayEl.value = txdelayValue;
      mark("Tx delay", "expert");
    }

    const directTxdelayValue = takeReadReply(
      byCmd,
      "get direct.txdelay",
      failures,
    );
    if (directTxdelayValue !== undefined && settingDirectTxdelayEl) {
      settingDirectTxdelayEl.value = directTxdelayValue;
      mark("Direct tx delay", "expert");
    }

    const rxdelayValue = takeReadReply(byCmd, "get rxdelay", failures);
    if (rxdelayValue !== undefined && settingRxdelayEl) {
      settingRxdelayEl.value = rxdelayValue;
      mark("Rx delay", "expert");
    }

    const radioRxgainValue = takeReadReply(byCmd, "get radio.rxgain", failures);
    if (radioRxgainValue !== undefined && settingRadioRxgainEl) {
      if (
        (radioRxgainValue === "on" || radioRxgainValue === "off") &&
        setSelectIfPresent(settingRadioRxgainEl, radioRxgainValue)
      ) {
        mark("Radio RX gain", "expert");
      }
    }

    const txPowerValue = takeReadReply(byCmd, "get tx", failures);
    if (txPowerValue !== undefined && settingRadioTxpowerEl) {
      settingRadioTxpowerEl.value = txPowerValue;
      mark("TX power", "expert");
    }

    // Read-only device info (display only; not part of the config form).
    const verValue = takeReadReply(byCmd, "ver", failures);
    if (verValue !== undefined && deviceInfoVersionEl) {
      deviceInfoVersionEl.textContent = verValue || "—";
    }
    const roleValue = takeReadReply(byCmd, "get role", failures);
    if (roleValue !== undefined && deviceInfoRoleEl) {
      deviceInfoRoleEl.textContent = roleValue || "—";
    }
    const pubkeyValue = takeReadReply(byCmd, "get public.key", failures);
    if (pubkeyValue !== undefined && deviceInfoPubkeyEl) {
      deviceInfoPubkeyEl.textContent = pubkeyValue || "—";
    }
    const clockValue = takeReadReply(byCmd, "clock", failures);
    if (clockValue !== undefined && deviceInfoClockEl) {
      deviceInfoClockEl.textContent = clockValue || "—";
    }

    const intThreshValue = takeReadReply(byCmd, "get int.thresh", failures);
    if (intThreshValue !== undefined && settingIntThreshEl) {
      settingIntThreshEl.value = intThreshValue;
      mark("Interference threshold", "expert");
    }

    const agcResetValue = takeReadReply(
      byCmd,
      "get agc.reset.interval",
      failures,
    );
    if (agcResetValue !== undefined && settingAgcResetEl) {
      settingAgcResetEl.value = agcResetValue;
      mark("AGC reset", "expert");
    }

    const multiAcksValue = takeReadReply(byCmd, "get multi.acks", failures);
    if (multiAcksValue !== undefined && settingMultiAcksEl) {
      if (setSelectIfPresent(settingMultiAcksEl, multiAcksValue)) {
        mark("Multi-acks", "expert");
      }
    }

    const api = positionApi();
    const latValue = takeReadReply(byCmd, "get lat", failures);
    const lonValue = takeReadReply(byCmd, "get lon", failures);
    if (latValue !== undefined || lonValue !== undefined) {
      const lat = latValue !== undefined ? parseFloat(latValue) : NaN;
      const lon = lonValue !== undefined ? parseFloat(lonValue) : NaN;
      if (api) {
        if (api.hasValidCoords(lat, lon)) {
          api.setCoords(lat, lon, { source: "device" });
          mark("Coordinates", "general");
          scrollTarget = scrollTarget || "config-identity-block";
          if (!detection) {
            workingAnchor = resolveLocationFromCoords(lat, lon, mark);
          }
        } else if (latValue !== undefined && lonValue !== undefined) {
          api.setCoords(null, null, { source: null });
        }
      }
    }

    const advertLocValue = takeReadReply(byCmd, "gps advert", failures);
    if (advertLocValue !== undefined && settingAdvertLocEl) {
      const policy = parseGpsAdvertReply(advertLocValue);
      if (policy && setSelectIfPresent(settingAdvertLocEl, policy)) {
        mark("Advert location", "general");
        scrollTarget = scrollTarget || "config-identity-block";
      }
    }

    if (
      homeValue !== undefined ||
      defaultScopeValue !== undefined ||
      allowedValue !== undefined ||
      deniedValue !== undefined
    ) {
      if (allowed.length) {
        appendSerialLog("Allowed regions: " + allowed.join(", "), "is-ok");
      } else if (allowedValue !== undefined) {
        appendSerialLog("Allowed regions: (none)", "is-ok");
      }
      if (denied.length) {
        appendSerialLog("Denied regions: " + denied.join(", "), "is-ok");
      } else if (deniedValue !== undefined) {
        appendSerialLog("Denied regions: (none)", "is-ok");
      }
      if (homeValue !== undefined) {
        appendSerialLog(
          "Home region: " + (homeRegion || "(wildcard)"),
          "is-ok",
        );
      }
      if (defaultScopeValue !== undefined) {
        appendSerialLog(
          "Default flood scope: " +
            (defaultRegion && String(defaultRegion).toLowerCase() !== "<null>"
              ? defaultRegion
              : "(none)"),
          "is-ok",
        );
      }
      workingAnchor = getAnchor() || workingAnchor;
      const regionResult = applyReadRegionsToPolicy(
        allowed,
        denied,
        homeRegion,
        workingAnchor,
        defaultRegion,
      );
      if (regionResult.applied) {
        mark("Region policy", "advanced");
        scrollTarget = scrollTarget || "policy-card";
        if (regionResult.missing && regionResult.missing.length) {
          appendSerialLog(
            "Added device region scope(s) to the form: " +
              regionResult.missing.join(", ") +
              ".",
            "is-ok",
          );
        }
      } else if (regionResult.reason === "no-location") {
        appendSerialLog(
          "Location left unset — this repeater's region home is " +
            (homeRegion && homeRegion !== "*"
              ? homeRegion + ", which is not a scope this configurator knows"
              : "the wildcard *, and its name has no " +
                NAME_PREFIX_ROOT +
                "- prefix") +
            ". The allow list alone does not say where the node is, so region " +
            "policy was not applied. Pick a location above, or set region home " +
            "on the device, then read again.",
          "is-error",
        );
      }
    }

    applyModReadResults(byCmd, failures, mark);

    expandSettingsTiersAfterRead(tierFlags);
    refreshConfiguratorOutputs();
    captureDeviceCliBaseline(getAnchor());
    refreshConfiguratorOutputs();

    return {
      updated: updated,
      failures: failures,
      labels: labels,
      scrollTarget: scrollTarget,
      namePrefixMismatch: namePrefixMismatch,
      adminPasswordUnreadable: true,
    };
  }
  function closeSerialReadConfirmModal() {
    const modal = document.getElementById("serial-read-confirm-modal");
    if (modal) modal.hidden = true;
    document.body.classList.remove("config-confirm-modal-open");
  }

  function promptReadFromRepeater(options) {
    options = options || {};
    const rs = getRepeaterSerial();
    if (!rs || !rs.isConnected() || isSerialBusy()) {
      if (!rs || !rs.isConnected()) {
        appendSerialLog(
          "Device is not connected. Connect over USB first.",
          "is-error",
        );
        setSerialStatus("disconnected", "Disconnected");
        updateUsbApplyUi(getAnchor());
      }
      return;
    }

    const modal = document.getElementById("serial-read-confirm-modal");
    const hintEl = document.getElementById("serial-read-confirm-hint");
    const anchor = getAnchor();
    let hint =
      "This overwrites the form with values from the connected device. Admin password cannot be read from the device.";
    if (options.afterConnect) {
      hint =
        "USB connected. Read settings from the repeater now and overwrite the form? Admin password cannot be read from the device.";
    }
    hint +=
      " Region scopes will be replaced with what the device reports; picking a location afterward will not change them.";
    if (hintEl) hintEl.textContent = hint;
    if (!modal) {
      if (window.confirm(hint)) {
        performReadFromRepeater();
      }
      return;
    }
    modal.hidden = false;
    document.body.classList.add("config-confirm-modal-open");
    const confirmBtn = document.getElementById("serial-read-confirm-btn");
    if (confirmBtn) confirmBtn.focus({ preventScroll: true });
  }

  function initSerialReadConfirmModal() {
    const confirmBtn = document.getElementById("serial-read-confirm-btn");
    const modal = document.getElementById("serial-read-confirm-modal");
    if (confirmBtn) {
      confirmBtn.addEventListener("click", function () {
        closeSerialReadConfirmModal();
        performReadFromRepeater();
      });
    }
    if (modal) {
      modal
        .querySelectorAll("[data-serial-read-confirm-dismiss]")
        .forEach(function (el) {
          el.addEventListener("click", closeSerialReadConfirmModal);
        });
    }
    document.addEventListener("keydown", function (e) {
      if (e.key !== "Escape") return;
      const m = document.getElementById("serial-read-confirm-modal");
      if (m && !m.hidden) {
        closeSerialReadConfirmModal();
      }
    });
  }

  async function performReadFromRepeater() {
    const rs = await ensureSerialReady("Read settings");
    if (!rs || !rs.queryCommands) return;

    const anchor = getAnchor();

    serialReading = true;
    serialApplyAbort = new AbortController();
    updateUsbApplyUi(anchor);
    clearSerialLog();
    const verboseRead = isSerialShowCommandLog();
    if (verboseRead) {
      appendSerialLog(
        "Reading " + REPEATER_READ_COMMANDS.length + " setting(s)…",
      );
    }

    try {
      const results = await rs.queryCommands(REPEATER_READ_COMMANDS, {
        signal: serialApplyAbort.signal,
        onProgress: function (ev) {
          if (!verboseRead) return;
          if (ev.phase === "sending") {
            appendSerialLog("> " + ev.line);
          } else if (ev.phase === "done") {
            if (ev.reply) {
              appendSerialLog(
                "  -> " + ev.reply,
                ev.ok ? "is-ok" : "is-error",
              );
            } else if (!ev.ok) {
              appendSerialLog("  -> (no reply)", "is-error");
            }
          }
        },
      });
      const summary = applyReadResultsToForm(indexResultsByCommand(results), anchor);
      if (summary.labels.length) {
        appendSerialLog(
          "Updated " +
            summary.labels.length +
            " field group(s): " +
            summary.labels.join(", ") +
            ".",
          "is-ok",
        );
      } else {
        appendSerialLog(
          verboseRead
            ? "Read finished but no form fields were updated — check replies above."
            : "Read finished but no form fields were updated — enable Show command log for details.",
          "is-error",
        );
      }
      appendSerialLog(
        "Admin password was not read (not exposed by device firmware).",
        "is-muted",
      );
      if (summary.namePrefixMismatch) {
        appendSerialLog(
          "Device name does not match the location prefix — adjust the suffix, change location mode, or pick a matching location.",
          "is-error",
        );
      }
      if (summary.failures.length) {
        appendSerialLog(
          summary.failures.length +
            " command(s) failed or are unsupported on this firmware.",
          "is-error",
        );
      }
      if (summary.scrollTarget) {
        scrollConfiguratorSection(summary.scrollTarget);
      }
    } catch (err) {
      if (err && err.name === "AbortError") {
        appendSerialLog("Read cancelled.", "is-error");
      } else {
        appendSerialLog(
          "Read failed: " + (err && err.message ? err.message : String(err)),
          "is-error",
        );
      }
    } finally {
      serialReading = false;
      serialApplyAbort = null;
      updateUsbApplyUi(getAnchor());
    }
  }

  function readFromRepeater() {
    promptReadFromRepeater({ afterConnect: false });
  }

  async function offerRepeaterReboot(rs, appliedLines) {
    if (!rs || !(await rs.ensureConnected())) {
      appendSerialLog(
        "Cannot reboot: device is no longer connected.",
        "is-error",
      );
      updateUsbApplyUi(getAnchor());
      return;
    }

    const needsRebootHint =
      appliedLines &&
      appliedLines.some(function (line) {
        return (
          /^set radio /.test(line) ||
          /^set freq /.test(line) ||
          /^set radio\.rxgain /.test(line)
        );
      });
    const msg = needsRebootHint
      ? "Configuration applied.\n\nReboot the repeater now? Radio or frequency changes need a reboot to take effect."
      : "Configuration applied.\n\nReboot the repeater now?";

    if (!window.confirm(msg)) {
      return;
    }

    if (!(await rs.ensureConnected())) {
      appendSerialLog(
        "Cannot reboot: device is no longer connected.",
        "is-error",
      );
      updateUsbApplyUi(getAnchor());
      return;
    }

    appendSerialLog("> reboot");
    try {
      const result = await rs.sendLine("reboot", { timeoutMs: 3000 });
      if (result && result.disconnected) {
        appendSerialLog("Reboot sent; device is disconnecting.", "is-ok");
      } else if (result && result.reply) {
        appendSerialLog(
          "  -> " + result.reply,
          result.ok ? "is-ok" : "is-error",
        );
      } else {
        appendSerialLog("Reboot sent (device may disconnect).", "is-ok");
      }
    } catch (err) {
      appendSerialLog(
        "Reboot failed: " + (err && err.message ? err.message : String(err)),
        "is-error",
      );
    }
    updateUsbApplyUi(getAnchor());
  }

  async function applyToRepeater() {
    const rs = await ensureSerialReady("Apply configuration");
    if (!rs) return;

    const anchor = getAnchor();
    if (!anchor) {
      window.alert("Choose a location first — region scopes are required.");
      return;
    }
    if (!namePreviewState.isValid || !namePreviewState.name) {
      window.alert("Set a valid repeater name before applying.");
      return;
    }
    if (coordsRequiredForApply()) {
      window.alert(
        "Set latitude and longitude — advert location is set to prefs (stored coordinates).",
      );
      return;
    }

    const lines = buildConfiguratorCommandLines(anchor, {
      enforceFirmwareDefaults: shouldEnforceDefaults(),
    });
    if (!lines.length) {
      window.alert("Nothing to apply — the repeater already matches this form.");
      return;
    }

    const validation = validateCommandLinesForSerial(lines);
    if (!validation.ok) {
      window.alert(validation.message);
      return;
    }

    const msg =
      "Apply " +
      lines.length +
      " command(s) to the connected repeater?\n\n" +
      (deviceCliBaseline && !shouldEnforceDefaults()
        ? "These are the lines shown in the command preview — only what differs from the repeater as read."
        : "This overwrites repeater settings with everything in the command preview.") +
      "\n\nContinue?";
    if (!window.confirm(msg)) {
      return;
    }

    serialApplying = true;
    serialApplyAbort = new AbortController();
    updateUsbApplyUi(anchor);
    clearSerialLog();
    appendSerialLog("Applying " + lines.length + " command(s)…");

    let applySucceeded = false;
    try {
      await rs.applyCommands(lines, {
        signal: serialApplyAbort.signal,
        onProgress: function (ev) {
          if (ev.phase === "sending") {
            appendSerialLog("> " + ev.line);
          } else if (ev.phase === "done") {
            if (ev.reply) {
              appendSerialLog("  -> " + ev.reply, ev.ok ? "is-ok" : "is-error");
            }
          }
        },
      });
      applySucceeded = true;
      appendSerialLog("Apply complete.", "is-ok");
      syncDeviceRegionSnapshotFromForm(anchor);
    } catch (err) {
      if (err && err.name === "AbortError") {
        appendSerialLog("Apply cancelled.", "is-error");
      } else {
        const detail =
          err && err.line
            ? 'Failed on "' + err.line + '": '
            : "Apply failed: ";
        appendSerialLog(
          detail + (err && err.message ? err.message : String(err)),
          "is-error",
        );
      }
    } finally {
      serialApplying = false;
      serialApplyAbort = null;
      updateUsbApplyUi(getAnchor());
    }

    if (applySucceeded) {
      await offerRepeaterReboot(rs, lines);
    }
  }

  function stateCentroid(pc) {
    let sumLat = 0;
    let sumLon = 0;
    let n = 0;
    for (let i = 0; i < CITIES.length; i++) {
      const r = CITIES[i];
      if (r.state_code !== pc || isMeshMapperCode(r.city_code)) continue;
      if (
        r.lat == null ||
        r.lon == null ||
        !Number.isFinite(r.lat) ||
        !Number.isFinite(r.lon)
      )
        continue;
      sumLat += r.lat;
      sumLon += r.lon;
      n++;
    }
    if (!n) return REGIONS.stateCentroid ? REGIONS.stateCentroid(pc) : { lat: 30.4, lon: -88.5 };
    return { lat: sumLat / n, lon: sumLon / n };
  }

  function getAnchor() {
    if (selectionMode === "city" && selectedCity)
      return {
        mode: "city",
        state_code: selectedCity.state_code,
        row: selectedCity,
      };
    if (selectionMode === "state" && selectedStateCode)
      return {
        mode: "state",
        state_code: selectedStateCode,
        row: null,
      };
    if (selectionMode === "country")
      return { mode: "country", state_code: null, row: null };
    return null;
  }

  function neighborSeedRow(anchor) {
    if (!anchor) return null;
    if (anchor.mode === "city" && anchor.row) return anchor.row;
    if (anchor.mode === "state") {
      const c = stateCentroid(anchor.state_code);
      return {
        lat: c.lat,
        lon: c.lon,
        state_code: anchor.state_code,
        city_code: "__state_centroid__",
        name: "",
      };
    }
    const c = REGIONS.stateCentroid ? REGIONS.stateCentroid(null) : { lat: 30.4, lon: -88.5 };
    return {
      lat: c.lat,
      lon: c.lon,
      state_code: null,
      city_code: "__root_centroid__",
      name: "",
    };
  }

  function normalize(s) {
    return (s || "")
      .toLowerCase()
      .normalize("NFD")
      .replace(/[\u0300-\u036f]/g, "");
  }

  function cityMatchesQuery(c, q) {
    if (
      normalize(c.name).includes(q) ||
      normalize(c.city_code).includes(q)
    )
      return true;
    const al = c.aliases;
    if (!al || !Array.isArray(al) || !al.length) return false;
    for (let i = 0; i < al.length; i++) {
      if (normalize(al[i]).includes(q)) return true;
    }
    return false;
  }

  function filterLocationChoices(query) {
    if (!query || query.length < 2) return [];
    const q = normalize(query);
    const out = [];
    const seen = new Set();

    const rootMatch =
      q === ROOT_CODE ||
      normalize(ROOT_LABEL).includes(q) ||
      normalize("gulfcoastmesh").includes(q);
    if (rootMatch) {
      out.push({
        type: "country",
        code: ROOT_CODE,
        label: ROOT_LABEL + " (" + ROOT_CODE + ") · region",
      });
      seen.add("__root__");
    }

    const stateKeys = Object.keys(STATE_NAMES);
    for (let pi = 0; pi < stateKeys.length; pi++) {
      if (out.length >= 16) break;
      const pc = stateKeys[pi];
      const pn = STATE_NAMES[pc];
      if (normalize(pc).includes(q) || normalize(pn).includes(q)) {
        if (seen.has("p:" + pc)) continue;
        seen.add("p:" + pc);
        out.push({
          type: "state",
          code: pc,
          label: pn + " (" + pc + ") · state",
        });
      }
    }

    for (let ci = 0; ci < CITIES.length && out.length < 16; ci++) {
      const c = CITIES[ci];
      if (isMeshMapperCode(c.city_code)) continue;
      if (cityMatchesQuery(c, q)) {
        if (seen.has("c:" + c.city_code)) continue;
        seen.add("c:" + c.city_code);
        out.push({
          type: "place",
          code: c.city_code,
          row: c,
          label:
            c.name +
            " (" +
            c.city_code +
            ") · " +
            (STATE_NAMES[c.state_code] || c.state_code),
        });
      }
    }
    return out;
  }

  function renderDropdown(matches) {
    lastMatches = matches;
    if (!matches.length) {
      dropdown.style.display = "none";
      return;
    }
    dropdown.innerHTML = matches
      .map(function (item, i) {
        const safeLabel = escapeHtml(item.label || "");
        return (
          '<div class="search-dropdown-item" data-index="' +
          i +
          '" data-choice-type="' +
          escapeHtml(item.type) +
          '">' +
          '<span class="search-dropdown-place">' +
          safeLabel +
          "</span></div>"
        );
      })
      .join("");
    dropdown.style.display = "block";
    activeIndex = 0;
    dropdown
      .querySelectorAll(".search-dropdown-item")
      .forEach(function (el, i) {
        el.classList.toggle("active", i === 0);
        el.addEventListener("click", function () {
          const idx = parseInt(el.dataset.index, 10);
          const item = lastMatches[idx];
          if (!item) return;
          if (item.type === "country") selectCountryBe();
          else if (item.type === "state") selectState(item.code);
          else if (item.type === "place" && item.row) selectCity(item.row);
        });
      });
  }

  function escapeHtml(s) {
    const d = document.createElement("div");
    d.textContent = s || "";
    return d.innerHTML;
  }

  function haversineKm(lat1, lon1, lat2, lon2) {
    const R = 6371;
    const toR = Math.PI / 180;
    const dLat = (lat2 - lat1) * toR;
    const dLon = (lon2 - lon1) * toR;
    const a =
      Math.sin(dLat / 2) * Math.sin(dLat / 2) +
      Math.cos(lat1 * toR) *
        Math.cos(lat2 * toR) *
        Math.sin(dLon / 2) *
        Math.sin(dLon / 2);
    return 2 * R * Math.asin(Math.min(1, Math.sqrt(a)));
  }

  /** Max distance for neighbouring cities / state hints. */
  const NEIGHBOR_RADIUS_MI = 100;
  const KM_PER_MI = 1.60934;
  const NEIGHBOR_RADIUS_KM = Math.round(NEIGHBOR_RADIUS_MI * KM_PER_MI);

  function kmToMi(km) {
    return km / KM_PER_MI;
  }
  /** Max city rows in the policy scope (codes panel lists the full radius). */
  const NEIGHBOR_SCOPE_MAX_CITIES = 10;

  /**
   * Nearest other cities by haversine distance, deduped by city_code.
   * @param {object} city — selected row with lat/lon
   * @param {{ maxKm?: number, maxCount?: number }} [opts]
   * @returns {{ c: object, km: number }[]}
   */
  function findGeographicNeighbors(city, opts) {
    opts = opts || {};
    const maxKm =
      opts.maxKm != null && Number.isFinite(opts.maxKm) ? opts.maxKm : Infinity;
    const maxCount =
      opts.maxCount != null && Number.isFinite(opts.maxCount)
        ? opts.maxCount
        : Infinity;
    if (
      city.lat == null ||
      city.lon == null ||
      !Number.isFinite(city.lat) ||
      !Number.isFinite(city.lon) ||
      !CITIES.length
    )
      return [];
    const scored = [];
    for (let i = 0; i < CITIES.length; i++) {
      const o = CITIES[i];
      if (o.city_code === city.city_code) continue;
      if (isMeshMapperCode(o.city_code)) continue;
      if (
        o.lat == null ||
        o.lon == null ||
        !Number.isFinite(o.lat) ||
        !Number.isFinite(o.lon)
      )
        continue;
      const km = haversineKm(city.lat, city.lon, o.lat, o.lon);
      scored.push({ c: o, km });
    }
    scored.sort(function (a, b) {
      return a.km - b.km;
    });
    const out = [];
    const seen = new Set();
    for (let i = 0; i < scored.length; i++) {
      if (scored[i].km > maxKm) break;
      const code = scored[i].c.city_code;
      if (seen.has(code)) continue;
      seen.add(code);
      out.push(scored[i]);
      if (out.length >= maxCount) break;
    }
    return out;
  }

  function resultRow(label, name, code) {
    return (
      '<div class="result-item"><span class="result-item-label">' +
      escapeHtml(label) +
      '</span><span class="result-item-value">' +
      escapeHtml(name) +
      ' <span class="result-code-inline">(' +
      escapeHtml(code) +
      ")</span></span></div>"
    );
  }

  function chosenLocationRowsHTML(c) {
    const st = STATE_NAMES[c.state_code] || c.state_code;
    return (
      resultRow("State", st, c.state_code) +
      resultRow("City", c.name, c.city_code)
    );
  }

  function chosenLocationRowsFromAnchor(anchor) {
    if (!anchor) return "";
    if (anchor.mode === "country") return resultRow("Region", ROOT_LABEL, ROOT_CODE);
    if (anchor.mode === "state" && anchor.state_code) {
      const n = STATE_NAMES[anchor.state_code] || anchor.state_code;
      return resultRow("State", n, anchor.state_code);
    }
    if (anchor.mode === "city" && anchor.row)
      return chosenLocationRowsHTML(anchor.row);
    return "";
  }

  /** Wider scopes offered as CLI rows, from the parallel us-* branch. */
  const WIDER_SCOPE_LABELS = {};
  WIDER_SCOPES.forEach(function (w) {
    WIDER_SCOPE_LABELS[w.code] = w.label;
  });

  /** Root scopes first, then wider us-* rows; the rest sort by depth. */
  const CLI_ORDER_FIRST = [ROOT_CODE].concat(
    WIDER_SCOPES.map(function (w) {
      return w.code;
    }),
  );

  /** Flood advert with lat/lon: 32 − 1 − 8 = 23 UTF-8 bytes for the name. */
  const NAME_ADVERT_MAX_UTF8 = 23;
  /** Firmware stores node_name in char node_name[32] (31 chars + null). */
  const NAME_FIRMWARE_MAX_UTF8 = 31;
  /** Prefix segment in repeater names (e.g. GC-MOB-). */
  const NAME_PREFIX_MAX_UTF8 = 7;

  function nameByteLength(value) {
    const s = String(value || "");
    if (typeof TextEncoder !== "undefined") {
      return new TextEncoder().encode(s).length;
    }
    return unescape(encodeURIComponent(s)).length;
  }

  function citySegment(cityCode) {
    if (!cityCode) return "";
    const parts = String(cityCode).split("-");
    return parts[parts.length - 1].toUpperCase();
  }

  function stateSegment(stateCode) {
    if (!stateCode) return "";
    const parts = String(stateCode).split("-");
    return parts[parts.length - 1].toUpperCase();
  }

  function getCurrentLocationMode() {
    return nameLocationModeEl ? nameLocationModeEl.value : "";
  }

  function setCurrentLocationMode(mode) {
    if (nameLocationModeEl) {
      nameLocationModeEl.value = mode;
    }
  }

  /** Normalize an auto-generated prefix (city/state/country): trim and ensure trailing "-". */
  function normalizePrefix(raw) {
    let core = String(raw || "")
      .trim()
      .replace(/-+$/, "");
    if (!core) return "";
    core = trimUtf8ToMaxBytes(core, NAME_PREFIX_MAX_UTF8 - 1);
    if (!core) return "";
    const withDash = core + "-";
    if (nameByteLength(withDash) <= NAME_PREFIX_MAX_UTF8) {
      return withDash;
    }
    core = trimUtf8ToMaxBytes(core, NAME_PREFIX_MAX_UTF8 - 1);
    return core ? core + "-" : "";
  }

  function getEffectivePrefix(anchor) {
    if (!anchor) return "";
    const mode = getCurrentLocationMode() || defaultLocationMode(anchor);
    return buildNamePrefix(anchor, mode);
  }

  function syncPrefixField(anchor) {
    if (!namePrefixPreviewEl) return;
    namePrefixPreviewEl.readOnly = true;
    namePrefixPreviewEl.setAttribute("readonly", "");
    if (!anchor) {
      namePrefixPreviewEl.value = "";
      namePrefixPreviewEl.title = "Choose a location to generate a prefix.";
      return;
    }
    const mode = getCurrentLocationMode() || defaultLocationMode(anchor);
    namePrefixPreviewEl.value = buildNamePrefix(anchor, mode);
    namePrefixPreviewEl.title =
      "Max " + NAME_PREFIX_MAX_UTF8 + " UTF-8 bytes; trailing dash included.";
  }

  function resetNamingForLocation(anchor) {
    if (!anchor) {
      syncPrefixField(null);
      return;
    }
    const def = defaultLocationMode(anchor);
    setCurrentLocationMode(def);
    syncPrefixField(anchor);
  }

  function availableLocationModes(anchor) {
    if (!anchor) return [];
    if (anchor.mode === "city") {
      return ["city", "state", "country", "none"];
    }
    if (anchor.mode === "state") {
      return ["state", "country", "none"];
    }
    if (anchor.mode === "country") {
      return ["country", "none"];
    }
    return [];
  }

  function defaultLocationMode(anchor) {
    if (!anchor) return "";
    if (anchor.mode === "city") return "city";
    if (anchor.mode === "state") return "state";
    if (anchor.mode === "country") return "country";
    return "";
  }

  function refreshLocationModeOptions(anchor) {
    const available = new Set(availableLocationModes(anchor));
    if (nameLocationModeWrapEl) {
      nameLocationModeWrapEl.hidden = false;
    }
    if (nameLocationModeEl) {
      const options = nameLocationModeEl.options;
      for (let i = 0; i < options.length; i++) {
        const opt = options[i];
        opt.hidden = false;
        opt.disabled = anchor
          ? !available.has(opt.value)
          : opt.value !== "none";
      }
    }
    const current = getCurrentLocationMode();
    if (!anchor) {
      if (current !== "none") {
        setCurrentLocationMode("none");
      }
      syncPrefixField(null);
      return;
    }
    if (!available.has(current)) {
      setCurrentLocationMode(defaultLocationMode(anchor));
      syncPrefixField(anchor);
    }
  }

  function buildNamePrefix(anchor, mode) {
    if (!anchor) return "";
    if (mode === "none") return "";
    if (mode === "country") return normalizePrefix(NAME_PREFIX_ROOT + "-");
    if (mode === "state") {
      const pCode =
        anchor.mode === "state"
          ? anchor.state_code
          : anchor.row
            ? anchor.row.state_code
            : "";
      const pSeg = stateSegment(pCode);
      return pSeg ? normalizePrefix(NAME_PREFIX_ROOT + "-" + pSeg + "-") : "";
    }
    if (mode === "city") {
      const cSeg = anchor.row ? citySegment(anchor.row.city_code) : "";
      return cSeg ? normalizePrefix(NAME_PREFIX_ROOT + "-" + cSeg + "-") : "";
    }
    return "";
  }

  function trimUtf8ToMaxBytes(str, maxBytes) {
    const s = String(str || "");
    if (maxBytes <= 0) return "";
    if (nameByteLength(s) <= maxBytes) return s;
    for (let i = s.length - 1; i >= 0; i--) {
      const part = s.slice(0, i);
      if (nameByteLength(part) <= maxBytes) return part;
    }
    return "";
  }

  function clampNamingInput(anchor) {
    if (!nameSuffixEl) return;
    const prefix = getEffectivePrefix(anchor);
    const emoji = namePowerEmojiEl ? namePowerEmojiEl.value || "" : "";
    const maxSuffixBytes =
      NAME_FIRMWARE_MAX_UTF8 - nameByteLength(prefix) - nameByteLength(emoji);
    const trimmed = trimUtf8ToMaxBytes(nameSuffixEl.value, maxSuffixBytes);
    if (trimmed !== nameSuffixEl.value) {
      nameSuffixEl.value = trimmed;
    }
  }

  function buildRepeaterName(anchor) {
    const prefix = getEffectivePrefix(anchor);
    const suffix = nameSuffixEl ? String(nameSuffixEl.value || "").trim() : "";
    const emoji = namePowerEmojiEl ? namePowerEmojiEl.value || "" : "";
    const name = prefix + suffix + emoji;
    const prefixBytes = nameByteLength(prefix);
    const suffixBytes = nameByteLength(suffix);
    const emojiBytes = nameByteLength(emoji);
    const totalBytes = nameByteLength(name);
    const hasSuffix = suffixBytes > 0;
    const fitsAdvert = totalBytes <= NAME_ADVERT_MAX_UTF8;
    const isValid = hasSuffix && totalBytes <= NAME_FIRMWARE_MAX_UTF8;
    const advertName = trimUtf8ToMaxBytes(name, NAME_ADVERT_MAX_UTF8);
    return {
      prefix: prefix,
      suffix: suffix,
      emoji: emoji,
      name: name,
      advertName: advertName,
      prefixBytes: prefixBytes,
      suffixBytes: suffixBytes,
      emojiBytes: emojiBytes,
      totalBytes: totalBytes,
      hasSuffix: hasSuffix,
      fitsAdvert: fitsAdvert,
      isValid: isValid,
    };
  }

  /** MeshCore: set radio freq,bw,sf,cr (MHz, kHz; SF 5–12; CR 5–8; BW 7–500). */
  const FREQUENCY_PRESETS = [
    { name: "GulfCoastMesh (Narrow)", freq: 910.525, sf: 7, bw: 62.5, cr: 8 },
    { name: "USA/Canada (Recommended)", freq: 910.525, sf: 7, bw: 62.5, cr: 5 },
  ];

  const RADIO_FIELD_SPECS = {
    freq: { label: "Freq (MHz)", min: 150, max: 2500, integer: false },
    sf: { label: "SF", min: 5, max: 12, integer: true },
    bw: { label: "BW (kHz)", min: 7, max: 500, integer: false, maxDecimals: 3 },
    cr: { label: "CR", min: 5, max: 8, integer: true },
  };

  /**
   * Preferred radio preset on load: the plan the flasher writes
   * (pages/flasher/member-config-mobile.json, `set radio 910.525 62.5 7 8`).
   * Differs from MeshCore firmware `set radio` default (see FIRMWARE_DEFAULT_RADIO).
   */
  const DEFAULT_RADIO_PRESET_INDEX = Math.max(
    0,
    FREQUENCY_PRESETS.findIndex(function (p) {
      return p.name === "GulfCoastMesh (Narrow)";
    }),
  );

  /**
   * MeshCore firmware defaults for other settings (docs.meshcore.io/cli_commands).
   * Used only to omit unchanged lines from general-settings CLI output.
   */
  const FIRMWARE_DEFAULTS = {
    repeat: "on",
    txdelay: 0.5,
    directTxdelay: 0.2,
    floodAdvertHours: 47,
    advertIntervalMinutes: 0,
    floodMaxUnscoped: 64,
    floodMaxAdvert: 8,
    floodMax: 64,
    pathHashMode: "0",
    dutycycle: "50",
    loopDetect: "off",
    rxdelay: 0,
    radioRxgain: "on",
    intThresh: 0,
    agcResetInterval: 0,
    multiAcks: "0",
    adminPassword: "password",
  };

  const FIRMWARE_DEFAULT_RADIO = {
    freq: 869.618,
    bw: 62.5,
    sf: 8,
    cr: 5,
  };

  /** Configurator form default when a numeric field is left empty (not firmware). */
  const FLOOD_ADVERT_INTERVAL_FORM_DEFAULT = 23;
  const FLOOD_MAX_UNSCOPED_FORM_DEFAULT = 12;
  const FLOOD_MAX_ADVERT_FORM_DEFAULT = 8;
  const FLOOD_MAX_FORM_DEFAULT = 64;
  const LOOP_DETECT_FORM_DEFAULT = "minimal";
  const PATH_HASH_MODE_FORM_DEFAULT = "1";
  const DUTYCYCLE_FORM_DEFAULT = "50";

  /** Recommended settings applied via the confirmation dialog. */
  const RECOMMENDED_SETTINGS = {
    radioPresetName: "GulfCoastMesh (Narrow)",
    dutycycle: "50",
    floodAdvertHours: 23,
    advertIntervalMinutes: 240,
    floodMax: 64,
    pathHashMode: "1",
    loopDetect: "minimal",
    agcResetInterval: 4,
    multiAcks: "1",
  };

  /**
   * Flood hop allowance presets for the recommendation dialog.
   * Values are [flood.max.unscoped, flood.max.advert].
   */
  const RECOMMENDED_FLOOD_ALLOWANCE = {
    minimal: { label: "Minimal", floodMaxUnscoped: 4, floodMaxAdvert: 6 },
    average: { label: "Average", floodMaxUnscoped: 8, floodMaxAdvert: 12 },
    extended: { label: "Extended", floodMaxUnscoped: 16, floodMaxAdvert: 24 },
    full: { label: "Full", floodMaxUnscoped: 64, floodMaxAdvert: 64 },
  };
  const DEFAULT_RECOMMENDED_FLOOD_ALLOWANCE = "average";

  function getSelectedRecommendedFloodAllowance() {
    const checked = document.querySelector(
      'input[name="recommended-flood-allowance"]:checked',
    );
    const key =
      checked && RECOMMENDED_FLOOD_ALLOWANCE[checked.value]
        ? checked.value
        : DEFAULT_RECOMMENDED_FLOOD_ALLOWANCE;
    return RECOMMENDED_FLOOD_ALLOWANCE[key];
  }

  function openRecommendedSettingsModal() {
    const modal = document.getElementById("recommended-settings-modal");
    if (!modal) return;
    const defaultRadio = modal.querySelector(
      'input[name="recommended-flood-allowance"][value="' +
        DEFAULT_RECOMMENDED_FLOOD_ALLOWANCE +
        '"]',
    );
    if (defaultRadio) defaultRadio.checked = true;
    modal.hidden = false;
    document.body.classList.add("config-confirm-modal-open");
    const confirmBtn = document.getElementById(
      "recommended-settings-confirm-btn",
    );
    if (confirmBtn) confirmBtn.focus({ preventScroll: true });
  }

  function closeRecommendedSettingsModal() {
    const modal = document.getElementById("recommended-settings-modal");
    if (!modal) return;
    modal.hidden = true;
    document.body.classList.remove("config-confirm-modal-open");
  }

  function applyRecommendedSettings() {
    const rec = RECOMMENDED_SETTINGS;
    const flood = getSelectedRecommendedFloodAllowance();
    const presetIndex = FREQUENCY_PRESETS.findIndex(function (p) {
      return p.name === rec.radioPresetName;
    });
    if (settingRadioPresetEl && presetIndex >= 0) {
      settingRadioPresetEl.value = String(presetIndex);
      settingRadioPresetEl.dataset.lastPreset = String(presetIndex);
    }
    if (settingDutycycleEl) {
      settingDutycycleEl.value = rec.dutycycle;
    }
    if (settingFloodAdvertIntervalEl) {
      settingFloodAdvertIntervalEl.value = String(rec.floodAdvertHours);
    }
    if (settingAdvertIntervalEl) {
      settingAdvertIntervalEl.value = String(rec.advertIntervalMinutes);
    }
    if (settingFloodMaxEl) {
      settingFloodMaxEl.value = String(rec.floodMax);
    }
    if (settingFloodMaxUnscopedEl) {
      settingFloodMaxUnscopedEl.value = String(flood.floodMaxUnscoped);
    }
    if (settingFloodMaxAdvertEl) {
      settingFloodMaxAdvertEl.value = String(flood.floodMaxAdvert);
    }
    if (settingPathHashModeEl) {
      settingPathHashModeEl.value = rec.pathHashMode;
    }
    if (settingLoopDetectEl) {
      settingLoopDetectEl.value = rec.loopDetect;
    }
    if (settingAgcResetEl) {
      settingAgcResetEl.value = String(rec.agcResetInterval);
    }
    if (settingMultiAcksEl) {
      setSelectIfPresent(settingMultiAcksEl, rec.multiAcks);
    }
    openSettingsTier("settings-tier-general");
    openSettingsTier("settings-tier-expert");
    openSettingsTier("settings-tier-advanced");
    refreshRadioSettingsUi();
    refreshConfiguratorOutputs();
    closeRecommendedSettingsModal();
    scrollConfiguratorSection("settings-tier-general");
  }

  function initRecommendedSettingsUi() {
    const openBtns = [
      document.getElementById("recommended-settings-btn"),
      document.getElementById("recommended-settings-btn-2"),
    ];
    const confirmBtn = document.getElementById(
      "recommended-settings-confirm-btn",
    );
    const modal = document.getElementById("recommended-settings-modal");
    openBtns.forEach(function (openBtn) {
      if (openBtn) {
        openBtn.addEventListener("click", openRecommendedSettingsModal);
      }
    });
    if (confirmBtn) {
      confirmBtn.addEventListener("click", applyRecommendedSettings);
    }
    if (modal) {
      modal
        .querySelectorAll("[data-recommended-settings-dismiss]")
        .forEach(function (el) {
          el.addEventListener("click", closeRecommendedSettingsModal);
        });
    }
    document.addEventListener("keydown", function (e) {
      if (e.key !== "Escape") return;
      const m = document.getElementById("recommended-settings-modal");
      if (m && !m.hidden) {
        closeRecommendedSettingsModal();
      }
    });
  }

  function roundToMaxDecimals(value, maxDecimals) {
    const factor = Math.pow(10, maxDecimals);
    return Math.round(Number(value) * factor) / factor;
  }

  function formatDecimalMaxPlaces(value, maxDecimals) {
    const rounded = roundToMaxDecimals(value, maxDecimals);
    const fixed = rounded.toFixed(maxDecimals);
    return fixed.replace(/\.?0+$/, "");
  }

  function formatRadioCliNumber(value, maxDecimals) {
    const n = Number(value);
    if (maxDecimals != null) {
      return formatDecimalMaxPlaces(n, maxDecimals);
    }
    if (Number.isInteger(n) || Math.abs(n - Math.round(n)) < 1e-9) {
      const rounded = Math.round(n);
      return Math.abs(n - rounded) < 1e-9 ? String(rounded) : String(n);
    }
    return String(n);
  }

  function formatRadioCliLine(params) {
    return (
      "set radio " +
      formatRadioCliNumber(params.freq) +
      "," +
      formatRadioCliNumber(params.bw, 3) +
      "," +
      formatRadioCliNumber(params.sf) +
      "," +
      formatRadioCliNumber(params.cr)
    );
  }

  function clampRadioBwInput() {
    if (!settingRadioBwEl) return;
    const raw = String(settingRadioBwEl.value || "").trim();
    if (!raw) return;
    const parsed = parseFloat(raw);
    if (Number.isNaN(parsed)) return;
    const formatted = formatDecimalMaxPlaces(parsed, 3);
    if (formatted !== raw) {
      settingRadioBwEl.value = formatted;
    }
  }

  function parseRadioField(el, spec) {
    if (!el) {
      return { ok: false, error: spec.label + " is required." };
    }
    const raw = String(el.value || "").trim();
    if (!raw) {
      return { ok: false, error: spec.label + " is required." };
    }
    let value = spec.integer ? parseInt(raw, 10) : parseFloat(raw);
    if (Number.isNaN(value)) {
      return { ok: false, error: spec.label + " must be a number." };
    }
    if (spec.integer && !Number.isInteger(value)) {
      return { ok: false, error: spec.label + " must be a whole number." };
    }
    if (value < spec.min || value > spec.max) {
      return {
        ok: false,
        error:
          spec.label +
          " must be between " +
          spec.min +
          " and " +
          spec.max +
          ".",
      };
    }
    if (spec.maxDecimals != null && !spec.integer) {
      value = roundToMaxDecimals(value, spec.maxDecimals);
      const formatted = formatDecimalMaxPlaces(value, spec.maxDecimals);
      if (el.value !== formatted) {
        el.value = formatted;
      }
    }
    return { ok: true, value: value };
  }

  function isCustomRadioPreset() {
    return settingRadioPresetEl && settingRadioPresetEl.value === "custom";
  }

  function getPresetRadioByIndex(index) {
    const p = FREQUENCY_PRESETS[index];
    if (!p) return null;
    return { freq: p.freq, bw: p.bw, sf: p.sf, cr: p.cr };
  }

  function getFirmwareDefaultRadioParams() {
    return FIRMWARE_DEFAULT_RADIO;
  }

  function radioParamsMatch(a, b) {
    if (!a || !b) return false;
    return (
      roundToMaxDecimals(a.freq, 3) === roundToMaxDecimals(b.freq, 3) &&
      roundToMaxDecimals(a.bw, 3) === roundToMaxDecimals(b.bw, 3) &&
      a.sf === b.sf &&
      a.cr === b.cr
    );
  }

  function delayFactorsEqual(a, b) {
    return Math.abs(Number(a) - Number(b)) < 1e-9;
  }

  function fillCustomRadioFields(params) {
    if (!params) return;
    if (settingRadioFreqEl) settingRadioFreqEl.value = String(params.freq);
    if (settingRadioSfEl) settingRadioSfEl.value = String(params.sf);
    if (settingRadioBwEl) {
      settingRadioBwEl.value = formatDecimalMaxPlaces(params.bw, 3);
    }
    if (settingRadioCrEl) settingRadioCrEl.value = String(params.cr);
  }

  function getRadioSettings() {
    if (!settingRadioPresetEl) {
      return {
        valid: true,
        params: getPresetRadioByIndex(DEFAULT_RADIO_PRESET_INDEX),
      };
    }
    if (!isCustomRadioPreset()) {
      const idx = parseInt(settingRadioPresetEl.value, 10);
      const params = getPresetRadioByIndex(idx);
      if (!params) {
        return { valid: false, errors: ["Select a radio preset."] };
      }
      return { valid: true, params: params };
    }
    const errors = [];
    const freq = parseRadioField(settingRadioFreqEl, RADIO_FIELD_SPECS.freq);
    const bw = parseRadioField(settingRadioBwEl, RADIO_FIELD_SPECS.bw);
    const sf = parseRadioField(settingRadioSfEl, RADIO_FIELD_SPECS.sf);
    const cr = parseRadioField(settingRadioCrEl, RADIO_FIELD_SPECS.cr);
    [freq, bw, sf, cr].forEach(function (r) {
      if (!r.ok) errors.push(r.error);
    });
    if (errors.length) {
      return { valid: false, errors: errors };
    }
    return {
      valid: true,
      params: {
        freq: freq.value,
        bw: bw.value,
        sf: sf.value,
        cr: cr.value,
      },
    };
  }

  function refreshRadioSettingsUi() {
    const isCustom = isCustomRadioPreset();
    if (settingRadioCustomWrapEl) {
      settingRadioCustomWrapEl.hidden = !isCustom;
    }
    const radio = getRadioSettings();
    if (settingRadioErrorEl) {
      if (isCustom && !radio.valid) {
        settingRadioErrorEl.hidden = false;
        settingRadioErrorEl.textContent = radio.errors.join(" ");
      } else {
        settingRadioErrorEl.hidden = true;
        settingRadioErrorEl.textContent = "";
      }
    }
    const invalid = isCustom && !radio.valid;
    function onCustomRadioFieldInput() {
      if (settingRadioBwEl) clampRadioBwInput();
      refreshConfiguratorOutputs();
    }

    [
      settingRadioFreqEl,
      settingRadioSfEl,
      settingRadioBwEl,
      settingRadioCrEl,
    ].forEach(function (el) {
      if (el) el.classList.toggle("is-invalid", invalid);
    });
    return radio;
  }

  function initRadioPresetSelect() {
    if (!settingRadioPresetEl) return;
    settingRadioPresetEl.innerHTML = "";
    for (let i = 0; i < FREQUENCY_PRESETS.length; i++) {
      const p = FREQUENCY_PRESETS[i];
      const opt = document.createElement("option");
      opt.value = String(i);
      opt.textContent =
        p.name +
        " — " +
        p.freq +
        " MHz / SF" +
        p.sf +
        " / " +
        p.bw +
        " kHz / CR" +
        p.cr;
      opt.title =
        "LoRa " +
        p.freq +
        " MHz, SF" +
        p.sf +
        ", BW " +
        p.bw +
        " kHz, CR" +
        p.cr +
        " — applied via set radio (reboot required)";
      settingRadioPresetEl.appendChild(opt);
    }
    const customOpt = document.createElement("option");
    customOpt.value = "custom";
    customOpt.textContent = "Custom";
    customOpt.title =
      "Enter frequency, SF, BW, CR, and TX power yourself (set radio / set tx)";
    settingRadioPresetEl.appendChild(customOpt);
    settingRadioPresetEl.value = String(DEFAULT_RADIO_PRESET_INDEX);
    settingRadioPresetEl.dataset.lastPreset = String(
      DEFAULT_RADIO_PRESET_INDEX,
    );
    refreshRadioSettingsUi();
  }

  function parseTxDelayFactor(el, fallback) {
    if (!el) return fallback;
    const raw = String(el.value || "").trim();
    if (!raw) return fallback;
    const v = parseFloat(raw);
    if (!Number.isFinite(v)) return fallback;
    return Math.min(2, Math.max(0, v));
  }

  function parseFloodAdvertHours(el) {
    const fallback = FLOOD_ADVERT_INTERVAL_FORM_DEFAULT;
    if (!el) return fallback;
    const raw = String(el.value || "").trim();
    if (!raw) return fallback;
    const v = parseInt(raw, 10);
    if (!Number.isFinite(v)) return fallback;
    return Math.min(168, Math.max(3, v));
  }

  function parseZeroHopAdvertMinutes(el) {
    if (!el) return 0;
    const raw = String(el.value || "").trim();
    if (!raw) return 0;
    const v = parseInt(raw, 10);
    if (!Number.isFinite(v) || v <= 0) return 0;
    const clamped = Math.min(240, Math.max(60, v));
    return clamped - (clamped % 2);
  }

  function parseFloodMaxHops(el, fallback) {
    if (!el) return fallback;
    const raw = String(el.value || "").trim();
    if (!raw) return fallback;
    const v = parseInt(raw, 10);
    if (!Number.isFinite(v)) return fallback;
    return Math.min(64, Math.max(0, v));
  }

  function parseRxdelayBase(el, fallback) {
    if (!el) return fallback;
    const raw = String(el.value || "").trim();
    if (!raw) return fallback;
    const v = parseFloat(raw);
    if (!Number.isFinite(v)) return fallback;
    return Math.min(20, Math.max(0, roundToMaxDecimals(v, 1)));
  }

  function parseAgcResetSeconds(el, fallback) {
    if (!el) return fallback;
    const raw = String(el.value || "").trim();
    if (!raw) return fallback;
    const v = parseInt(raw, 10);
    if (!Number.isFinite(v) || v <= 0) return 0;
    const clamped = Math.min(255, Math.max(4, v));
    return clamped - (clamped % 4);
  }

  function parseIntThresh(el, fallback) {
    if (!el) return fallback;
    const raw = String(el.value || "").trim();
    if (!raw) return fallback;
    const v = parseInt(raw, 10);
    if (!Number.isFinite(v)) return fallback;
    return Math.min(255, Math.max(0, v));
  }

  function buildGeneralSettingsCli(showDefaults) {
    const lines = [];
    const fw = FIRMWARE_DEFAULTS;
    const firmwareRadio = getFirmwareDefaultRadioParams();
    const radio = getRadioSettings();

    const includeRadio =
      showDefaults ||
      Boolean(
        radio.valid &&
        radio.params &&
        !radioParamsMatch(radio.params, firmwareRadio),
      );
    if (includeRadio && radio.valid && radio.params) {
      lines.push(formatRadioCliLine(radio.params));
    }

    const repeat = settingRepeatEl ? settingRepeatEl.value : fw.repeat;
    if (showDefaults || repeat !== fw.repeat) {
      lines.push("set repeat " + (repeat === "off" ? "off" : "on"));
    }

    const ownerInfo = settingOwnerInfoEl
      ? String(settingOwnerInfoEl.value || "").trim()
      : "";
    if (ownerInfo) {
      lines.push("set owner.info " + ownerInfo);
    }

    const adminPassword = settingAdminPasswordEl
      ? String(settingAdminPasswordEl.value || "").trim()
      : "";
    if (showDefaults) {
      lines.push("password " + (adminPassword || fw.adminPassword));
    } else if (adminPassword && adminPassword !== fw.adminPassword) {
      lines.push("password " + adminPassword);
    }

    const guestPassword = settingGuestPasswordEl
      ? String(settingGuestPasswordEl.value || "").trim()
      : "";
    if (guestPassword) {
      lines.push("set guest.password " + guestPassword);
    }

    // Mod-only settings, emitted only for a device that answers to them.
    if (modFirmwareDetected) {
      const otaUrl = settingOtaFwUrlEl
        ? String(settingOtaFwUrlEl.value || "").trim()
        : "";
      if (otaUrl && (showDefaults || otaUrl !== deviceOtaFwUrl)) {
        lines.push("set ota.fw.url " + otaUrl);
      }
      const ssid = settingOtaWifiSsidEl
        ? String(settingOtaWifiSsidEl.value || "").trim()
        : "";
      const wifiPass = settingOtaWifiPassEl
        ? String(settingOtaWifiPassEl.value || "")
        : "";
      if (ssid) {
        lines.push("set ota.wan.wifi " + ssid + "," + wifiPass);
      }
    }

    const dutycycle = settingDutycycleEl
      ? settingDutycycleEl.value
      : DUTYCYCLE_FORM_DEFAULT;
    if (showDefaults || dutycycle !== fw.dutycycle) {
      lines.push("set dutycycle " + dutycycle);
    }

    const floodAdvertHours = parseFloodAdvertHours(
      settingFloodAdvertIntervalEl,
    );
    if (showDefaults || floodAdvertHours !== fw.floodAdvertHours) {
      lines.push("set flood.advert.interval " + floodAdvertHours);
    }

    const advertInterval = parseZeroHopAdvertMinutes(settingAdvertIntervalEl);
    if (showDefaults || advertInterval !== fw.advertIntervalMinutes) {
      lines.push("set advert.interval " + advertInterval);
    }

    const floodMaxUnscoped = parseFloodMaxHops(
      settingFloodMaxUnscopedEl,
      FLOOD_MAX_UNSCOPED_FORM_DEFAULT,
    );
    if (showDefaults || floodMaxUnscoped !== fw.floodMaxUnscoped) {
      lines.push("set flood.max.unscoped " + floodMaxUnscoped);
    }

    const floodMaxAdvert = parseFloodMaxHops(
      settingFloodMaxAdvertEl,
      FLOOD_MAX_ADVERT_FORM_DEFAULT,
    );
    if (showDefaults || floodMaxAdvert !== fw.floodMaxAdvert) {
      lines.push("set flood.max.advert " + floodMaxAdvert);
    }

    const floodMax = parseFloodMaxHops(
      settingFloodMaxEl,
      FLOOD_MAX_FORM_DEFAULT,
    );
    if (showDefaults || floodMax !== fw.floodMax) {
      lines.push("set flood.max " + floodMax);
    }

    const pathMode = settingPathHashModeEl
      ? settingPathHashModeEl.value
      : PATH_HASH_MODE_FORM_DEFAULT;
    if (showDefaults || pathMode !== fw.pathHashMode) {
      lines.push("set path.hash.mode " + pathMode);
    }

    const loopDetect = settingLoopDetectEl
      ? settingLoopDetectEl.value
      : LOOP_DETECT_FORM_DEFAULT;
    if (showDefaults || loopDetect !== fw.loopDetect) {
      lines.push("set loop.detect " + loopDetect);
    }

    const txdelay = parseTxDelayFactor(settingTxdelayEl, fw.txdelay);
    if (showDefaults || !delayFactorsEqual(txdelay, fw.txdelay)) {
      lines.push("set txdelay " + txdelay);
    }

    const directTxdelay = parseTxDelayFactor(
      settingDirectTxdelayEl,
      fw.directTxdelay,
    );
    if (showDefaults || !delayFactorsEqual(directTxdelay, fw.directTxdelay)) {
      lines.push("set direct.txdelay " + directTxdelay);
    }

    const rxdelay = parseRxdelayBase(settingRxdelayEl, fw.rxdelay);
    if (showDefaults || !delayFactorsEqual(rxdelay, fw.rxdelay)) {
      lines.push("set rxdelay " + formatDecimalMaxPlaces(rxdelay, 1));
    }

    const radioRxgain = settingRadioRxgainEl
      ? settingRadioRxgainEl.value
      : fw.radioRxgain;
    if (showDefaults || radioRxgain !== fw.radioRxgain) {
      lines.push("set radio.rxgain " + radioRxgain);
    }

    // TX power (dBm): CLI `set tx <n>`. Emit when the user entered a value.
    const txPowerRaw = settingRadioTxpowerEl
      ? String(settingRadioTxpowerEl.value).trim()
      : "";
    if (txPowerRaw !== "") {
      const txPowerNum = parseInt(txPowerRaw, 10);
      if (Number.isFinite(txPowerNum)) {
        lines.push("set tx " + txPowerNum);
      }
    }

    const intThresh = parseIntThresh(settingIntThreshEl, fw.intThresh);
    if (showDefaults || intThresh !== fw.intThresh) {
      lines.push("set int.thresh " + intThresh);
    }

    const agcReset = parseAgcResetSeconds(
      settingAgcResetEl,
      fw.agcResetInterval,
    );
    if (showDefaults || agcReset !== fw.agcResetInterval) {
      lines.push("set agc.reset.interval " + agcReset);
    }

    const multiAcks = settingMultiAcksEl
      ? settingMultiAcksEl.value
      : fw.multiAcks;
    if (showDefaults || multiAcks !== fw.multiAcks) {
      lines.push("set multi.acks " + multiAcks);
    }

    const coords = getFormCoords();
    if (coords.valid) {
      lines.push("set lat " + coords.lat);
      lines.push("set lon " + coords.lon);
    }

    const advertLoc = getAdvertLocPolicy();
    if (showDefaults) {
      if (advertLoc === "none") lines.push("gps advert none");
      else if (advertLoc === "share") lines.push("gps advert share");
      else lines.push("gps advert prefs");
    } else if (advertLoc === "none") {
      lines.push("gps advert none");
    } else if (advertLoc === "share") {
      lines.push("gps advert share");
    }

    return lines.join("\n");
  }

  function refreshNamingUi(anchor) {
    clampNamingInput(anchor);
    refreshLocationModeOptions(anchor);
    syncPrefixField(anchor);
    const state = buildRepeaterName(anchor);
    namePreviewState = state;

    if (nameSuffixEl) {
      nameSuffixEl.removeAttribute("maxlength");
      nameSuffixEl.title =
        "Up to " +
        NAME_FIRMWARE_MAX_UTF8 +
        " UTF-8 bytes total (firmware); " +
        NAME_ADVERT_MAX_UTF8 +
        " bytes if advertising with location";
    }

    const overAdvert =
      advertIncludesLocation() &&
      state.totalBytes > NAME_ADVERT_MAX_UTF8 &&
      state.totalBytes <= NAME_FIRMWARE_MAX_UTF8;
    const overFirmware = state.totalBytes > NAME_FIRMWARE_MAX_UTF8;

    if (namePreviewEl) {
      if (!state.name) {
        namePreviewEl.textContent = "—";
        namePreviewEl.classList.remove("is-warning");
      } else if (overAdvert) {
        namePreviewEl.textContent = state.name;
        namePreviewEl.classList.add("is-warning");
      } else {
        namePreviewEl.textContent = state.name;
        namePreviewEl.classList.remove("is-warning");
      }
    }
    if (namePreviewMetaEl) {
      let metaText = "";
      let isWarning = false;
      let isError = false;
      if (!state.hasSuffix) {
        metaText = "Add a custom name to generate the command.";
        isError = true;
      } else if (overFirmware) {
        metaText =
          state.totalBytes +
          " / " +
          NAME_FIRMWARE_MAX_UTF8 +
          " bytes. Too long for firmware; shorten the extra name.";
        isError = true;
      } else if (overAdvert) {
        metaText = state.totalBytes + " / " + NAME_FIRMWARE_MAX_UTF8 + " bytes";
        isWarning = true;
      } else if (state.hasSuffix) {
        const byteLimit = advertIncludesLocation()
          ? NAME_ADVERT_MAX_UTF8
          : NAME_FIRMWARE_MAX_UTF8;
        metaText = state.totalBytes + " / " + byteLimit + " bytes";
      }
      namePreviewMetaEl.textContent = metaText;
      namePreviewMetaEl.classList.toggle("is-error", isError);
      namePreviewMetaEl.classList.toggle("is-warning", isWarning);
    }
    if (namePreviewNoteEl) {
      const showNote =
        advertIncludesLocation() &&
        state.hasSuffix &&
        state.totalBytes > NAME_ADVERT_MAX_UTF8;
      if (showNote) {
        namePreviewNoteEl.hidden = false;
        namePreviewNoteEl.classList.add("is-warning");
        namePreviewNoteEl.innerHTML =
          "The firmware accepts up to " +
          NAME_FIRMWARE_MAX_UTF8 +
          " bytes in the firmware and for the <code>set name</code> command. " +
          "However, <b>flood adverts</b> with location are limited to " +
          NAME_ADVERT_MAX_UTF8 +
          " bytes: " +
          '<code class="config-naming-preview-advert">' +
          escapeHtml(state.advertName) +
          "</code>";
      } else {
        namePreviewNoteEl.hidden = true;
        namePreviewNoteEl.classList.remove("is-warning");
        namePreviewNoteEl.textContent = "";
      }
    }
  }

  function widerScopeLabel(code) {
    return WIDER_SCOPE_LABELS[code] || code;
  }

  function sortCodesForCli(codes) {
    const set = new Set(codes);
    const out = [];
    CLI_ORDER_FIRST.forEach(function (c) {
      if (set.has(c)) {
        out.push(c);
        set.delete(c);
      }
    });
    return out.concat(Array.from(set).sort());
  }

  function withoutStar(codes) {
    return codes.filter(function (c) {
      return c !== "*";
    });
  }

  function starFirst(sorted) {
    const idx = sorted.indexOf("*");
    if (idx <= 0) return sorted;
    return ["*"].concat(
      sorted.filter(function (c) {
        return c !== "*";
      }),
    );
  }

  function findCityByCode(code) {
    if (!code || !CITIES || !CITIES.length) return null;
    for (let i = 0; i < CITIES.length; i++) {
      if (CITIES[i].city_code === code) return CITIES[i];
    }
    return null;
  }

  /** Home-override select: omit `region home` from generated CLI entirely. */
  const HOME_OVERRIDE_OMIT = "__nohome__";
  /** Default-scope select: emit `region default <null>` to clear. */
  const DEFAULT_SCOPE_NONE = "__none__";

  function expandedNameForRegionCode(code) {
    if (code === "*") return "wildcard root";
    if (code === ROOT_CODE) return ROOT_LABEL;
    if (Object.prototype.hasOwnProperty.call(STATE_NAMES, code)) {
      return STATE_NAMES[code];
    }
    const cc = widerScopeLabel(code);
    if (cc !== code) return cc;
    const row = findCityByCode(code);
    if (row && row.name) return row.name;
    return code;
  }

  function homeOverrideOptionLabel(code) {
    const name = expandedNameForRegionCode(code);
    if (name !== code) {
      return code + " (" + name + ")";
    }
    return code;
  }

  function addRegionCode(needed, code) {
    if (!code || needed.has(code)) return false;
    needed.add(code);
    return true;
  }

  /**
   * Add state/be ancestors for cities and be for states when those
   * codes appear in policy selections. Skips auto-adding ancestors for the
   * selected home city and home state so region def lines follow home
   * Allow checkboxes only.
   */
  function expandRegionNeeded(needed, anchor) {
    const homeCity =
      anchor && anchor.mode === "city" && anchor.row
        ? anchor.row.city_code
        : null;
    const homeState = anchor
      ? anchor.mode === "state"
        ? anchor.state_code
        : anchor.row
          ? anchor.row.state_code
          : null
      : null;
    let changed = true;
    while (changed) {
      changed = false;
      const snap = Array.from(needed);
      for (let i = 0; i < snap.length; i++) {
        const c = snap[i];
        if (c === "*" || c === homeCity || c === homeState) continue;
        const parts = String(c).split("-");
        for (let j = parts.length - 1; j > 0; j--) {
          if (addRegionCode(needed, parts.slice(0, j).join("-"))) changed = true;
        }
      }
    }
  }

  /**
   * Build ordered { code, parent } entries (parent null = under *).
   * Shallowest first so every parent is defined before its children.
   */
  function buildOrderedRegionEntries(needed, homeCityRow) {
    const codes = [];
    needed.forEach(function (c) {
      if (c && c !== "*") codes.push(c);
    });
    if (homeCityRow && codes.indexOf(homeCityRow.city_code) < 0) {
      // keep the home city addressable even when it is not an Allow row
    }
    codes.sort(function (a, b) {
      const d = regionHierarchyDepth(a) - regionHierarchyDepth(b);
      return d !== 0 ? d : a.localeCompare(b);
    });
    return codes.map(function (code) {
      return { code: code, parent: parentRegionCode(code, needed) };
    });
  }

  /**
   * MeshCore `region def` tokens for a parent→children tree.
   * Uses name|jump to pop back when starting another branch.
   * @param {Array<{code:string,parent:string|null}>} entries
   * @returns {string[]}
   */
  function buildRegionDefTokens(entries) {
    const childrenOf = Object.create(null);
    childrenOf["*"] = [];
    entries.forEach(function (e) {
      const parent = e.parent || "*";
      if (!childrenOf[parent]) childrenOf[parent] = [];
      childrenOf[parent].push(e.code);
      if (!childrenOf[e.code]) childrenOf[e.code] = [];
    });

    const tokens = [];

    function emitUnder(parentKey) {
      const kids = childrenOf[parentKey] || [];
      for (let i = 0; i < kids.length; i++) {
        const child = kids[i];
        const moreSiblings = i < kids.length - 1;
        const grandkids = childrenOf[child] || [];
        if (grandkids.length > 0) {
          tokens.push(child);
          emitUnder(child);
          if (moreSiblings && tokens.length) {
            tokens[tokens.length - 1] =
              tokens[tokens.length - 1] + "|" + parentKey;
          }
        } else if (moreSiblings) {
          tokens.push(child + "|" + parentKey);
        } else {
          tokens.push(child);
        }
      }
    }

    emitUnder("*");
    return tokens;
  }

  /**
   * Pack region def tokens into CLI lines (≤ RepeaterSerial.MAX_LINE_LEN / 160).
   * One line per root child of * when possible. Within a long subtree, cut only
   * after a `name|jump` token; strip that jump (line ends) and start the next
   * line with path reposition `a|a b|b …` (cursor resets to * between commands).
   */
  function packRegionDefLines(tokens, entries) {
    const PREFIX = "region def ";
    const rs = getRepeaterSerial();
    const maxLen = Math.min((rs && rs.MAX_LINE_LEN) || 151, 160);
    if (!tokens.length) return [];

    function lineFor(toks) {
      return PREFIX + toks.join(" ");
    }

    function tokenJump(tok) {
      const i = tok.indexOf("|");
      return i < 0 ? null : tok.slice(i + 1);
    }

    function lastJumpIndex(arr) {
      for (let j = arr.length - 1; j >= 0; j--) {
        if (tokenJump(arr[j])) return j;
      }
      return -1;
    }

    function isRootToken(tok, root) {
      return tok === root || tok.indexOf(root + "|") === 0;
    }

    const parentOf = Object.create(null);
    entries.forEach(function (e) {
      parentOf[e.code] = e.parent || "*";
    });

    /** Walk * → code via name|name so putRegion keeps existing parents. */
    function repositionTokens(code) {
      const chain = [];
      let cur = code;
      let guard = 0;
      while (cur && cur !== "*") {
        chain.unshift(cur);
        cur = parentOf[cur] || null;
        if (++guard > 32) break;
      }
      return chain.map(function (c) {
        return c + "|" + c;
      });
    }

    if (lineFor(tokens).length <= maxLen) {
      return [lineFor(tokens)];
    }

    const rootChildren = entries
      .filter(function (e) {
        return !e.parent;
      })
      .map(function (e) {
        return e.code;
      });

    /** Split one root subtree; resume mid-tree with path reposition. */
    function packChunk(chunk) {
      if (!chunk.length) return [];
      if (lineFor(chunk).length <= maxLen) return [lineFor(chunk)];

      const out = [];
      let buf = [];
      let resumeAt = null;

      function withResume(toks) {
        return resumeAt ? repositionTokens(resumeAt).concat(toks) : toks;
      }

      function flushAt(splitIdx) {
        const keep = buf.slice(0, splitIdx + 1);
        const rest = buf.slice(splitIdx + 1);
        const last = keep[keep.length - 1];
        const jump = tokenJump(last);
        if (jump) keep[keep.length - 1] = last.slice(0, last.indexOf("|"));
        out.push(lineFor(withResume(keep)));
        resumeAt = jump;
        buf = rest;
      }

      function flushWhileTooLong() {
        while (buf.length > 1 && lineFor(withResume(buf)).length > maxLen) {
          const idx = lastJumpIndex(buf);
          if (idx < 0 || idx === buf.length - 1) break;
          flushAt(idx);
        }
      }

      chunk.forEach(function (tok) {
        const trial = buf.concat([tok]);
        if (buf.length && lineFor(withResume(trial)).length > maxLen) {
          const splitIdx = lastJumpIndex(buf);
          if (splitIdx >= 0) {
            flushAt(splitIdx);
            buf = buf.concat([tok]);
            flushWhileTooLong();
          } else {
            // Linear prefix with no jump yet — cannot cut mid-block.
            buf = trial;
          }
        } else {
          buf = trial;
        }
      });
      if (buf.length) out.push(lineFor(withResume(buf)));
      return out;
    }

    const lines = [];
    rootChildren.forEach(function (root) {
      const start = tokens.findIndex(function (t) {
        return isRootToken(t, root);
      });
      if (start < 0) return;
      let end = tokens.length;
      rootChildren.forEach(function (other) {
        if (other === root) return;
        const idx = tokens.findIndex(function (t) {
          return isRootToken(t, other);
        });
        if (idx > start && idx < end) end = idx;
      });
      let chunk = tokens.slice(start, end);
      // Trailing |* only linked sibling roots on one line — drop per-chunk.
      if (chunk.length) {
        const last = chunk[chunk.length - 1];
        if (last.slice(-2) === "|*") {
          chunk = chunk.slice(0, -1).concat([last.slice(0, -2)]);
        }
      }
      Array.prototype.push.apply(lines, packChunk(chunk));
    });

    return lines.length ? lines : [lineFor(tokens)];
  }

  /** Build region def CLI line(s) for the needed region set (FW 1.16+). */
  function buildOrderedRegionDefLines(needed, homeCityRow) {
    const entries = buildOrderedRegionEntries(needed, homeCityRow);
    const tokens = buildRegionDefTokens(entries);
    return packRegionDefLines(tokens, entries);
  }

  function policyRow(labelHtml, code, opts) {
    opts = opts || {};
    const idBase = "pc_" + code.replace(/[^a-zA-Z0-9]/g, "_");
    const allowChk = opts.allow === false ? "" : " checked";
    const denyChk = opts.deny ? " checked" : "";
    const esc = escapeHtml(code);
    return (
      '<div class="policy-row">' +
      '<span class="policy-row-label" title="Region transport code ' +
      esc +
      ' — Allow/Deny maps to region allowf / denyf">' +
      labelHtml +
      '</span><div class="policy-row-clear-slot" aria-hidden="true"></div><div class="policy-cell policy-cell--allow"><input type="checkbox" class="policy-allow" data-code="' +
      esc +
      '" id="' +
      idBase +
      '_a"' +
      allowChk +
      ' aria-label="Allow flood for ' +
      esc +
      '" title="Allow flood for ' +
      esc +
      ' (CLI: region allowf ' +
      esc +
      ')"></div><div class="policy-cell policy-cell--deny"><input type="checkbox" class="policy-deny" data-code="' +
      esc +
      '" id="' +
      idBase +
      '_d"' +
      denyChk +
      ' aria-label="Deny flood for ' +
      esc +
      '" title="Deny flood for ' +
      esc +
      ' (CLI: region denyf ' +
      esc +
      ')"></div></div>'
    );
  }

  function syncScopeMasters(subsection) {
    if (!subsection) return;
    const ma = subsection.querySelector(".policy-scope-master-allow");
    const md = subsection.querySelector(".policy-scope-master-deny");
    if (!ma || !md) return;

    const allowInputs = Array.from(
      subsection.querySelectorAll("input.policy-allow"),
    ).filter(function (el) {
      return !el.disabled;
    });
    const denyInputs = Array.from(
      subsection.querySelectorAll("input.policy-deny"),
    ).filter(function (el) {
      return !el.disabled;
    });

    ma.indeterminate = false;
    md.indeterminate = false;

    if (allowInputs.length > 0 && allowInputs.length === denyInputs.length) {
      const n = allowInputs.length;
      let cAllow = 0;
      let cDeny = 0;
      for (let i = 0; i < n; i++) {
        if (allowInputs[i].checked) cAllow++;
        if (denyInputs[i].checked) cDeny++;
      }
      if (!ma.disabled) {
        ma.checked = cAllow === n && cDeny === 0;
        if (cAllow > 0 && cAllow < n) ma.indeterminate = true;
      }
      if (!md.disabled) {
        md.checked = cDeny === n && cAllow === 0;
        if (cDeny > 0 && cDeny < n) md.indeterminate = true;
      }
      return;
    }

    if (!ma.disabled) {
      if (allowInputs.length === 0) {
        ma.checked = false;
      } else {
        const na = allowInputs.length;
        let cAllow = 0;
        for (let i = 0; i < na; i++) {
          if (allowInputs[i].checked) cAllow++;
        }
        ma.checked = cAllow === na;
        if (cAllow > 0 && cAllow < na) ma.indeterminate = true;
      }
    }
    if (!md.disabled) {
      if (denyInputs.length === 0) {
        md.checked = false;
      } else {
        const nd = denyInputs.length;
        let cDeny = 0;
        for (let i = 0; i < nd; i++) {
          if (denyInputs[i].checked) cDeny++;
        }
        md.checked = cDeny === nd;
        if (cDeny > 0 && cDeny < nd) md.indeterminate = true;
      }
    }
  }

  function homeAllowChecked(homeSub, code) {
    if (!homeSub || code == null || code === "") return false;
    const inputs = homeSub.querySelectorAll("input.policy-allow");
    for (let i = 0; i < inputs.length; i++) {
      if (inputs[i].getAttribute("data-code") === code) {
        return inputs[i].checked;
      }
    }
    return false;
  }

  function homeDenyChecked(homeSub, code) {
    if (!homeSub || code == null || code === "") return false;
    const inputs = homeSub.querySelectorAll("input.policy-deny");
    for (let i = 0; i < inputs.length; i++) {
      if (inputs[i].getAttribute("data-code") === code) {
        return inputs[i].checked;
      }
    }
    return false;
  }


  /**
   * Default region home when Home override is off: smallest Allow in home
   * table — city (only in city mode), state, then be. Country-only: be.
   */
  function deepestAllowedHomeRegionCode(anchor) {
    if (!anchor) return "*";
    const home = policyCard
      ? policyCard.querySelector('.policy-subsection[data-policy-scope="home"]')
      : null;
    if (anchor.mode === "country") {
      if (homeAllowChecked(home, ROOT_CODE)) return ROOT_CODE;
      return "*";
    }
    const chain = [];
    if (anchor.mode === "city" && anchor.row && anchor.row.city_code)
      chain.push(anchor.row.city_code);
    const pc =
      anchor.mode === "state"
        ? anchor.state_code
        : anchor.row && anchor.row.state_code;
    if (pc) chain.push(pc);
    chain.push(ROOT_CODE);
    for (let i = 0; i < chain.length; i++) {
      if (homeAllowChecked(home, chain[i])) return chain[i];
    }
    return "*";
  }

  function anyAllowChecked(code) {
    if (!policyCard || !code) return false;
    const el = policyCard.querySelector(
      'input.policy-allow[data-code="' +
        String(code).replace(/"/g, "") +
        '"]:checked',
    );
    return Boolean(el && !el.disabled);
  }

  /**
   * Automatic default flood scope. Deliberately recommends nothing: changing
   * `region default` shifts which hop budget a node's own adverts get, so the
   * automatic choice is to keep whatever the device already reported and
   * otherwise leave it null.
   */
  function recommendedDefaultScopeCode(_anchor) {
    const dev = deviceDefaultRegionFromRead;
    if (dev && String(dev).toLowerCase() !== "<null>") return dev;
    return null;
  }

  /**
   * Repopulate the home-override dropdown: No home, Default, every
   * Allow-checked scope, plus * (wildcard root). Option text shows full name
   * in brackets where known.
   */
  function refreshHomeOverrideSelect() {
    const sel = document.getElementById("policy-home-override-select");
    const ov = document.getElementById("policy-home-override");
    if (!sel || !policyCard) return;
    const prev = sel.value;
    const codes = [];
    const seen = new Set();
    policyCard
      .querySelectorAll("input.policy-allow:checked")
      .forEach(function (el) {
        const c = el.getAttribute("data-code");
        if (!c || seen.has(c)) return;
        seen.add(c);
        codes.push(c);
      });
    if (!seen.has("*")) {
      seen.add("*");
      codes.push("*");
    }
    const sorted = starFirst(sortCodesForCli(codes));
    sel.innerHTML = "";
    const opt0 = document.createElement("option");
    opt0.value = "";
    opt0.textContent = "Default";
    sel.appendChild(opt0);
    const optNoHome = document.createElement("option");
    optNoHome.value = HOME_OVERRIDE_OMIT;
    optNoHome.textContent = "No home";
    sel.appendChild(optNoHome);
    sorted.forEach(function (c) {
      const o = document.createElement("option");
      o.value = c;
      o.textContent = homeOverrideOptionLabel(c);
      sel.appendChild(o);
    });
    if (prev === HOME_OVERRIDE_OMIT) {
      sel.value = HOME_OVERRIDE_OMIT;
    } else if (prev && seen.has(prev)) {
      sel.value = prev;
    }
    if (ov) {
      sel.disabled = !ov.checked;
    }
  }

  /**
   * Repopulate Default flood scope select from Allow-checked named regions.
   * Empty value = automatic (keep the device value, else none). __none__ = <null>.
   */
  function refreshDefaultScopeSelect() {
    const sel = document.getElementById("policy-default-scope-select");
    if (!sel || !policyCard) return;
    const prev = sel.value;
    const codes = [];
    const seen = new Set();
    policyCard
      .querySelectorAll("input.policy-allow:checked")
      .forEach(function (el) {
        const c = el.getAttribute("data-code");
        if (!c || c === "*" || seen.has(c)) return;
        seen.add(c);
        codes.push(c);
      });
    const sorted = sortCodesForCli(codes);
    sel.innerHTML = "";
    const optAuto = document.createElement("option");
    optAuto.value = "";
    const autoCode = recommendedDefaultScopeCode(getAnchor());
    optAuto.textContent = autoCode
      ? "Automatic (keep device value → " + autoCode + ")"
      : "Automatic (none)";
    sel.appendChild(optAuto);
    const optNone = document.createElement("option");
    optNone.value = DEFAULT_SCOPE_NONE;
    optNone.textContent = "None (<null>)";
    sel.appendChild(optNone);
    sorted.forEach(function (c) {
      const o = document.createElement("option");
      o.value = c;
      o.textContent = homeOverrideOptionLabel(c);
      sel.appendChild(o);
    });
    if (prev === DEFAULT_SCOPE_NONE) {
      sel.value = DEFAULT_SCOPE_NONE;
    } else if (prev && seen.has(prev)) {
      sel.value = prev;
    } else if (prev && prev !== "") {
      // Keep an explicit override that is no longer Allow-checked visible.
      const o = document.createElement("option");
      o.value = prev;
      o.textContent = homeOverrideOptionLabel(prev) + " (not Allow)";
      sel.appendChild(o);
      sel.value = prev;
    } else {
      sel.value = "";
    }
  }

  /**
   * Full `region home …` line for the CLI, or null to omit the command.
   * Override off, or on with empty select → automatic smallest home Allow, else *.
   * Override on with "No home" → omit line. Override on with a code → that code.
   */
  function regionHomeLineForCli(anchor) {
    const ov = document.getElementById("policy-home-override");
    if (ov && ov.checked) {
      const sel = document.getElementById("policy-home-override-select");
      const v = sel && sel.value;
      if (v === HOME_OVERRIDE_OMIT) {
        return null;
      }
      if (!v) {
        return "region home " + deepestAllowedHomeRegionCode(anchor);
      }
      return "region home " + v;
    }
    return "region home " + deepestAllowedHomeRegionCode(anchor);
  }

  /**
   * Full `region default …` line for the CLI.
   * Empty select → automatic (keep the device value, else none). __none__ → <null>.
   */
  function regionDefaultLineForCli(anchor) {
    const sel = document.getElementById("policy-default-scope-select");
    const v = sel ? sel.value : "";
    if (v === DEFAULT_SCOPE_NONE) {
      // Already null on the device: nothing to write.
      return deviceDefaultRegionFromRead === null && policyFromDevice
        ? ""
        : "region default <null>";
    }
    if (v) {
      return "region default " + v;
    }
    // Automatic never changes the device's own value.
    if (policyFromDevice) {
      return "";
    }
    const auto = recommendedDefaultScopeCode(anchor);
    if (auto) {
      return "region default " + auto;
    }
    return "region default <null>";
  }

  /**
   * Neighbour scopes: shown, and Allow/Deny enabled, only when the
   * matching home row (be / state / place) has Allow or Deny checked.
   * Otherwise the whole subsection is hidden and its checkboxes cleared.
   */
  function applyNeighborPolicyGating(anchor) {
    if (!policyCard) return;
    ["neighbor-cities", "neighbor-states", "meshmapper"].forEach(function (key) {
      const sub = policyCard.querySelector(
        '.policy-subsection[data-policy-scope="' + key + '"]',
      );
      if (!sub) return;
      sub.removeAttribute("hidden");
      sub
        .querySelectorAll(
          "input.policy-allow, input.policy-deny, .policy-scope-master-allow, .policy-scope-master-deny",
        )
        .forEach(function (el) {
          el.disabled = false;
        });
    });
  }

  function finalizePolicyUiChange() {
    if (!policyCard) return;
    const anchor = getAnchor();
    if (anchor) applyNeighborPolicyGating(anchor);
    policyCard.querySelectorAll(".policy-subsection").forEach(syncScopeMasters);
    refreshHomeOverrideSelect();
    refreshDefaultScopeSelect();
    refreshFoundCodesAndCli();
  }

  /**
   * @param {{ c: object, km: number }[]} neighborsScope — cities shown in policy (capped)
   * @param {{ c: object, km: number }[]} neighborsRadius — all within radius; states + empty checks
   */
  function isMeshMapperCode(code) {
    return /-mm$/.test(String(code || ""));
  }

  function collectMeshMapperCodes() {
    const out = [];
    const seen = new Set();
    function add(code) {
      const c = String(code || "").trim();
      if (!c || !isMeshMapperCode(c) || seen.has(c)) return;
      seen.add(c);
      out.push(c);
    }
    CITIES.forEach(function (city) {
      add(city.city_code);
    });
    Object.keys(STATE_NAMES).forEach(add);
    WIDER_SCOPES.forEach(function (w) {
      add(w.code);
    });
    if (deviceNamedRegionsFromRead) deviceNamedRegionsFromRead.forEach(add);
    out.sort();
    return out;
  }

  function renderPolicyGrids(
    anchor,
    neighborsScope,
    neighborsRadius,
    hasCoords,
  ) {
    if (!policyGridsContainer || !anchor) return;
    const columnHtml = { home: "", scopes: "" };
    const homeStateCode =
      anchor.mode === "state"
        ? anchor.state_code
        : anchor.row
          ? anchor.row.state_code
          : null;
    function addSubsection(title, rows, emptyNote, scopeKey, opts) {
      opts = opts || {};
      const skipIfEmpty = !!opts.skipIfEmpty;
      const subNoteHtml = opts.subNoteHtml || "";
      const column = opts.column === "home" ? "home" : "scopes";
      const scrollClass =
        opts.scrollable && rows && rows.length > 6
          ? " policy-subsection--scroll"
          : "";
      const scopeAttr = scopeKey
        ? ' data-policy-scope="' + escapeHtml(scopeKey) + '"'
        : "";
      const defaultAllow = scopeKey === "home";
      if (skipIfEmpty && (!rows || !rows.length)) {
        return;
      }
      let subsectionHtml =
        '<div class="policy-subsection' + scrollClass + '"' + scopeAttr + ">";
      if (!rows.length) {
        subsectionHtml +=
          '<div class="policy-subhead policy-subhead--empty"><h3 class="policy-subtitle">' +
          escapeHtml(title) +
          "</h3></div>";
        subsectionHtml +=
          '<p class="result-muted-note policy-empty">' +
          escapeHtml(emptyNote || "Nothing to list here.") +
          "</p></div>";
        columnHtml[column] += subsectionHtml;
        return;
      }
      subsectionHtml +=
        '<div class="policy-subhead"><h3 class="policy-subtitle">' +
        escapeHtml(title) +
        "</h3></div>";
      if (subNoteHtml) {
        subsectionHtml +=
          '<p class="policy-subsection-note">' + subNoteHtml + "</p>";
      }
      subsectionHtml +=
        '<div class="policy-table-head" role="row">' +
        '<div class="policy-head-scope" role="columnheader">Scope</div>' +
        '<div class="policy-head-clear-wrap" role="columnheader">' +
        '<button type="button" class="policy-head-clear-link" data-bulk="clear" aria-label="' +
        escapeHtml(title + " — clear all checkboxes in this scope") +
        '" title="Clear Allow and Deny in this scope (neither allowf nor denyf for these codes)">Clear scope</button>' +
        "</div>" +
        '<div class="policy-head-col" role="columnheader">' +
        '<span class="policy-head-label" title="Allow flood packets tagged with this region (CLI: region allowf)">Allow</span>' +
        '<input type="checkbox" class="policy-scope-master-allow" aria-label="' +
        escapeHtml(title + " — allow all in this scope") +
        '" title="Allow flood for every row in this scope (region allowf)">' +
        "</div>" +
        '<div class="policy-head-col" role="columnheader">' +
        '<span class="policy-head-label" title="Block flood packets tagged with this region (CLI: region denyf)">Deny</span>' +
        '<input type="checkbox" class="policy-scope-master-deny" aria-label="' +
        escapeHtml(title + " — deny all in this scope") +
        '" title="Deny flood for every row in this scope (region denyf)">' +
        "</div></div>";
      rows.forEach(function (row) {
        const allow =
          row.allow !== undefined ? row.allow !== false : defaultAllow;
        const deny = !!row.deny;
        subsectionHtml += policyRow(row.label, row.code, {
          allow: allow,
          deny: deny,
        });
      });
      subsectionHtml += "</div>";
      columnHtml[column] += subsectionHtml;
    }

    const homeRows = [
      { label: ROOT_LABEL + " (" + ROOT_CODE + ")", code: ROOT_CODE, allow: false },
    ];
    let homeTitle = "Home scopes: region \u2192 state";
    if (anchor.mode === "country") {
      /* only be */
    } else if (anchor.mode === "state" && anchor.state_code) {
      const pc = anchor.state_code;
      homeRows.push({
        label:
          escapeHtml(STATE_NAMES[pc] || pc) + " (" + escapeHtml(pc) + ")",
        code: pc,
        allow: false,
      });
    } else if (anchor.mode === "city" && anchor.row) {
      const city = anchor.row;
      homeTitle += " \u2192 city";
      homeRows.push({
        label:
          escapeHtml(STATE_NAMES[city.state_code] || city.state_code) +
          " (" +
          escapeHtml(city.state_code) +
          ")",
        code: city.state_code,
        allow: false,
      });
      homeRows.push({
        label:
          escapeHtml(city.name) + " (" + escapeHtml(city.city_code) + ")",
        code: city.city_code,
        allow: false,
      });
    }

    addSubsection(homeTitle, homeRows, undefined, "home", {
      column: "home",
    });

    const nEmptyGeo = !hasCoords
      ? "No coordinates for this location."
      : !neighborsRadius.length
        ? "No other mapped places within ~" + NEIGHBOR_RADIUS_MI + " mi."
        : "";

    const npCodes = neighborStatesFromNeighbors(
      neighborsRadius,
      homeStateCode,
    );

    const munNoteCity =
      escapeHtml(
        "Shows the nearest " +
          NEIGHBOR_SCOPE_MAX_CITIES +
          " cities within ~" +
          NEIGHBOR_RADIUS_MI +
          " mi. See ",
      ) +
      "<code>Codes for this selection</code>" +
      escapeHtml(" for the full list inside that radius.");
    const munNoteProv = escapeHtml(
      "Optional: pick a city to populate distance-based neighbours. State-only setups can skip this.",
    );

    addSubsection(
      "Neighbouring cities (by place)",
      neighborsScope.length > 0
        ? neighborsScope
            .filter(function (item) {
              return !isMeshMapperCode(item.c.city_code);
            })
            .map(function (item) {
            const o = item.c;
            const km = item.km;
            return {
              label:
                escapeHtml(o.name) +
                " (~" +
                Math.round(kmToMi(km)) +
                " mi) (" +
                escapeHtml(o.city_code) +
                ")",
              code: o.city_code,
            };
          })
        : [],
      nEmptyGeo || "No neighbouring cities in the dataset yet.",
      "neighbor-cities",
      {
        column: "scopes",
        scrollable: true,
        skipIfEmpty: anchor.mode === "city",
        subNoteHtml: anchor.mode === "city" ? munNoteCity : munNoteProv,
      },
    );

    addSubsection(
      "Neighbouring states",
      npCodes.map(function (pc) {
        const name = STATE_NAMES[pc] || pc;
        return {
          label: escapeHtml(name) + " (" + escapeHtml(pc) + ")",
          code: pc,
        };
      }),
      nEmptyGeo ||
        "No neighbouring states for this selection (none of the home state's land-border states have a mapped place within ~" +
          NEIGHBOR_RADIUS_MI +
          " mi).",
      "neighbor-states",
      {
        column: "scopes",
        skipIfEmpty: true,
        subNoteHtml: escapeHtml(
          "States on the coast next to the home state.",
        ),
      },
    );

    const homeCode = anchor.mode === "city" && anchor.row ? anchor.row.city_code : null;
    addSubsection(
      "MeshMapper",
      collectMeshMapperCodes()
        .filter(function (code) {
          return code !== homeCode;
        })
        .map(function (code) {
          const name = expandedNameForRegionCode(code);
          return {
            label:
              name && name !== code
                ? escapeHtml(name) + " (" + escapeHtml(code) + ")"
                : escapeHtml(code),
            code: code,
          };
        }),
      undefined,
      "meshmapper",
      {
        column: "home",
        scrollable: true,
        skipIfEmpty: true,
        subNoteHtml: escapeHtml(
          "Every scope code ending in -mm, wherever it sits in the tree.",
        ),
      },
    );

    addSubsection(
      "Wider scopes (CLI)",
      WIDER_SCOPES.map(function (w) {
        return {
          label: escapeHtml(w.label) + " (" + escapeHtml(w.code) + ")",
          code: w.code,
        };
      }),
      undefined,
      "wider",
      { column: "scopes" },
    );

    policyGridsContainer.innerHTML =
      '<div class="policy-grids-layout">' +
      '<div class="policy-grids-col policy-grids-col--home">' +
      columnHtml.home +
      "</div>" +
      '<div class="policy-grids-col policy-grids-col--scopes">' +
      columnHtml.scopes +
      "</div></div>";
    applyNeighborPolicyGating(anchor);
    policyGridsContainer
      .querySelectorAll(".policy-subsection")
      .forEach(syncScopeMasters);
    refreshHomeOverrideSelect();
    refreshDefaultScopeSelect();
  }

  /** One subsection listing exactly the regions the device reported. */
  function renderDeviceRegionGrid(allowed, denied, homeRegion, defaultRegion) {
    if (!policyGridsContainer) return;
    const allowedSet = new Set(withoutStar(allowed || []));
    const deniedSet = new Set(withoutStar(denied || []));
    const codes = [];
    const seen = new Set();
    function add(code) {
      const c = String(code || "").trim();
      if (!c || c === "*" || seen.has(c)) return;
      seen.add(c);
      codes.push(c);
    }
    (allowed || []).forEach(add);
    (denied || []).forEach(add);
    add(homeRegion);
    if (defaultRegion && String(defaultRegion).toLowerCase() !== "<null>") {
      add(defaultRegion);
    }
    codes.sort(function (a, b) {
      const d = regionHierarchyDepth(a) - regionHierarchyDepth(b);
      return d !== 0 ? d : a.localeCompare(b);
    });

    let rows = "";
    codes.forEach(function (code) {
      const name = expandedNameForRegionCode(code);
      const label =
        name && name !== code
          ? escapeHtml(name) + " (" + escapeHtml(code) + ")"
          : escapeHtml(code);
      rows += policyRow(label, code, {
        allow: allowedSet.has(code),
        deny: deniedSet.has(code),
      });
    });

    const head =
      '<div class="policy-table-head" role="row">' +
      '<div class="policy-head-scope" role="columnheader">Scope</div>' +
      '<div class="policy-head-clear-wrap" role="columnheader">' +
      '<button type="button" class="policy-head-clear-link" data-bulk="clear" ' +
      'title="Clear Allow and Deny for every row read from the device">Clear scope</button>' +
      "</div>" +
      '<div class="policy-head-col" role="columnheader">' +
      '<span class="policy-head-label" title="Allow flood (CLI: region allowf)">Allow</span>' +
      '<input type="checkbox" class="policy-scope-master-allow" aria-label="Allow all rows read from the device">' +
      "</div>" +
      '<div class="policy-head-col" role="columnheader">' +
      '<span class="policy-head-label" title="Block flood (CLI: region denyf)">Deny</span>' +
      '<input type="checkbox" class="policy-scope-master-deny" aria-label="Deny all rows read from the device">' +
      "</div></div>";

    const body = codes.length
      ? head + rows
      : '<p class="result-muted-note policy-empty">The device reported no named regions.</p>';

    policyGridsContainer.innerHTML =
      '<div class="policy-grids-layout">' +
      '<div class="policy-grids-col policy-grids-col--home">' +
      '<div class="policy-subsection" data-policy-scope="device">' +
      '<div class="policy-subhead"><h3 class="policy-subtitle">Regions on the device</h3></div>' +
      '<p class="policy-subsection-note">Read from the repeater. Allow and Deny are exactly what it reported; picking a location above does not change them.</p>' +
      body +
      "</div></div>" +
      '<div class="policy-grids-col policy-grids-col--scopes"></div>' +
      "</div>";

    policyGridsContainer
      .querySelectorAll(".policy-subsection")
      .forEach(syncScopeMasters);
  }

  /** Rebuild the location-derived grid, e.g. after dropping the device view. */
  function rebuildPolicyGridsForAnchor(anchor) {
    if (!policyGridsContainer) return;
    if (!anchor) {
      policyGridsContainer.innerHTML = "";
      return;
    }
    const seed = neighborSeedRow(anchor);
    const neighborsRadius = findGeographicNeighbors(seed, {
      maxKm: NEIGHBOR_RADIUS_KM,
    });
    const neighborsScope = neighborsRadius.slice(0, NEIGHBOR_SCOPE_MAX_CITIES);
    const hasCoords =
      seed &&
      seed.lat != null &&
      seed.lon != null &&
      Number.isFinite(seed.lat) &&
      Number.isFinite(seed.lon);
    lastNeighbors = neighborsRadius;
    lastHasCoords = hasCoords;
    renderPolicyGrids(anchor, neighborsScope, neighborsRadius, hasCoords);
    refreshPolicySection(anchor);
  }

  function applyPolicyDefaults() {
    if (!policyCard) return;
    policyCard.querySelectorAll(".policy-subsection").forEach(function (sub) {
      const scope = sub.getAttribute("data-policy-scope");
      if (scope === "home") {
        sub.querySelectorAll("input.policy-allow").forEach(function (el) {
          const code = el.getAttribute("data-code");
          if (selectionMode === "country") {
            el.checked = code === ROOT_CODE;
          } else if (selectionMode === "state" && selectedStateCode) {
            el.checked = code === ROOT_CODE || code === selectedStateCode;
          } else if (selectionMode === "city" && selectedCity) {
            const st = selectedCity.state_code || "";
            el.checked = code === ROOT_CODE || (st && code === st);
          } else {
            el.checked = false;
          }
        });
        sub.querySelectorAll("input.policy-deny").forEach(function (el) {
          el.checked = false;
        });
      } else {
        sub
          .querySelectorAll("input.policy-allow, input.policy-deny")
          .forEach(function (el) {
            el.checked = false;
          });
      }
    });
    const untagged = document.getElementById("policy-untagged-flood");
    if (untagged) untagged.checked = true;
    const ho = document.getElementById("policy-home-override");
    const hs = document.getElementById("policy-home-override-select");
    if (ho) ho.checked = false;
    if (hs) hs.value = "";
    const ds = document.getElementById("policy-default-scope-select");
    if (ds) ds.value = "";
    finalizePolicyUiChange();
  }

  /** Deepest scope code the current selection stands for. */
  function anchorScopeCode(anchor) {
    if (!anchor) return "*";
    if (anchor.mode === "city" && anchor.row) return anchor.row.city_code;
    if (anchor.mode === "state" && anchor.state_code) return anchor.state_code;
    return ROOT_CODE;
  }

  let lastMapScope = null;

  /** Drive the embedded region map without reloading its frame. */
  function syncRegionMapFrame(anchor) {
    const frame = document.getElementById("region-map-frame");
    if (!frame || !frame.contentWindow) return;
    const code = anchorScopeCode(anchor);
    if (code === lastMapScope) return;
    lastMapScope = code;
    try {
      frame.contentWindow.postMessage(
        { type: "region-map:select", code: code },
        window.location.origin,
      );
    } catch (_e) {
      /* frame not ready yet; the next refresh retries */
    }
  }

  // The frame is lazy-loaded, so replay the current scope once it is ready.
  (function watchRegionMapFrame() {
    const frame = document.getElementById("region-map-frame");
    if (!frame) return;
    frame.addEventListener("load", function () {
      lastMapScope = null;
      syncRegionMapFrame(getAnchor());
    });
  })();

  /** Reveal or hide the mod card and gate its buttons on a live connection. */
  const MOD_CONSOLE_COMMANDS = [
    ["start ota wan update", "Start OTA update from the stored URL"],
    ["start ota wan ", "Start OTA update from a URL"],
    ["ota wan join", "Join the OTA WiFi network only"],
    ["ota wan check", "Check WAN reachability"],
    ["ota wan leave", "Disconnect WiFi and drop WAN power"],
    ["get ota.fw.url", "Get the stored OTA firmware URL"],
    ["set ota.fw.url ", "Set the OTA firmware URL"],
    ["set ota.wan.wifi ", "Set OTA WiFi credentials: ssid,password"],
    ["get ota.wan.pwr", "Get the WAN power switch state"],
    ["set ota.wan.pwr ", "Set the WAN power switch (on|off)"],
    ["set ota.fw.sha256 ", "Set the firmware SHA-256 (RAM only)"],
    ["set ota.fw.sha256 clear", "Clear the firmware SHA-256"],
    ["set ota.fw.marker ", "Marker check for the next OTA (on|off)"],
    ["get ota.slot", "Show OTA slots and rollback state"],
    ["ota slot boot ", "Boot into OTA slot A or B"],
  ];

  function syncModConsoleCommands() {
    const db = document.getElementById("serial-command-db");
    if (!db) return;
    db.querySelectorAll("option[data-mod]").forEach(function (el) {
      el.remove();
    });
    if (!modFirmwareDetected) return;
    MOD_CONSOLE_COMMANDS.forEach(function (entry) {
      const opt = document.createElement("option");
      opt.value = entry[0];
      opt.textContent = entry[1];
      opt.dataset.mod = "1";
      db.insertBefore(opt, db.firstChild);
    });
  }

  function refreshModUi() {
    if (modCard) modCard.hidden = !modFirmwareDetected;
    syncModConsoleCommands();
    const rs = getRepeaterSerial();
    const live =
      modFirmwareDetected && Boolean(rs && rs.isConnected()) && !isSerialBusy();
    [
      modWanJoinBtn,
      modWanCheckBtn,
      modWanLeaveBtn,
      modWanPwrBtn,
      deviceWanOtaBtn,
    ].forEach(function (btn) {
      if (btn) btn.disabled = !live;
    });
    // Sits in Device tools, so it needs hiding as well as disabling.
    if (deviceWanOtaBtn) deviceWanOtaBtn.hidden = !modFirmwareDetected;
    if (modWanPwrBtn) {
      modWanPwrBtn.textContent =
        "WAN power: " + (modWanPower === null ? "—" : modWanPower);
    }
  }

  function setModDetected(detected) {
    const changed = modFirmwareDetected !== detected;
    modFirmwareDetected = detected;
    if (!detected) {
      // Drop values read from a previous device so a stock node cannot inherit them.
      deviceOtaFwUrl = "";
      if (settingOtaFwUrlEl) settingOtaFwUrlEl.value = "";
      if (settingOtaWifiSsidEl) settingOtaWifiSsidEl.value = "";
      if (settingOtaWifiPassEl) settingOtaWifiPassEl.value = "";
      if (modInfoSlotEl) modInfoSlotEl.textContent = "—";
    }
    refreshModUi();
    return changed;
  }

  /** One probe command; leaves detection untouched if the device did not answer. */
  async function probeModFirmware() {
    const rs = getRepeaterSerial();
    if (!rs || !rs.isConnected()) return false;
    try {
      const res = await rs.sendLine(MOD_PROBE_COMMAND);
      if (!res || !res.ok) return false;
      const reply = stripCliReply(res.reply);
      if (isUnknownCommandReply(reply)) {
        setModDetected(false);
        return false;
      }
      modWanPower = /^on\b/i.test(reply) ? "on" : "off";
      setModDetected(true);
      return true;
    } catch (_e) {
      return false;
    }
  }

  /** Fold the mod replies from a full read into the form. */
  function applyModReadResults(byCmd, failures, mark) {
    const probe = takeReadReply(byCmd, MOD_PROBE_COMMAND, failures);
    if (probe === undefined || isUnknownCommandReply(probe)) {
      setModDetected(false);
      return;
    }
    modWanPower = /^on\b/i.test(probe) ? "on" : "off";
    setModDetected(true);

    const url = takeReadReply(byCmd, "get ota.fw.url", failures);
    if (url !== undefined && !isUnknownCommandReply(url)) {
      deviceOtaFwUrl = /^\(not set\)$/i.test(url) ? "" : url;
      if (settingOtaFwUrlEl) settingOtaFwUrlEl.value = deviceOtaFwUrl;
      mark("Firmware URL", "general");
    }

    const slot = takeReadReply(byCmd, "get ota.slot", failures);
    if (modInfoSlotEl) {
      modInfoSlotEl.textContent =
        slot !== undefined && !isUnknownCommandReply(slot) ? slot : "—";
    }
    refreshModUi();
  }

  /** Send one mod command and echo its reply into the session log. */
  async function runModCommand(line, label) {
    const rs = await ensureSerialReady(label);
    if (!rs) return;
    try {
      appendSerialLog(label + "…");
      const res = await rs.sendLine(line, { timeoutMs: 20000 });
      const reply = stripCliReply(res && res.reply);
      appendSerialLog(
        reply || (res && res.ok ? "OK" : "No reply."),
        res && res.ok ? "is-ok" : "is-error",
      );
      if (line.indexOf("ota.wan.pwr") >= 0) await probeModFirmware();
    } catch (err) {
      appendSerialLog(
        label + " failed: " + (err && err.message ? err.message : String(err)),
        "is-error",
      );
    }
    refreshModUi();
  }

  function refreshPolicySection(anchor) {
    syncRegionMapFrame(anchor);
    if (!policyCard) return;
    const emptyEl = document.getElementById("policy-scope-empty");
    const bodyEl = document.getElementById("policy-regions-body");
    const headActions = document.getElementById("policy-head-actions");
    if (!anchor && !policyFromDevice) {
      if (emptyEl) {
        emptyEl.hidden = false;
      }
      if (bodyEl) {
        bodyEl.hidden = true;
      }
      if (headActions) {
        headActions.hidden = true;
      }
      if (policyGridsContainer) {
        policyGridsContainer.innerHTML = "";
      }
      return;
    }
    if (emptyEl) {
      emptyEl.hidden = true;
    }
    if (bodyEl) {
      bodyEl.hidden = false;
    }
    if (headActions) {
      headActions.hidden = false;
    }
  }

  function collectPolicyNeededRegionCodes(anchor, allowCodes, denyCodes) {
    const needed = new Set();
    if (!anchor) return needed;
    withoutStar(allowCodes || []).forEach(function (c) {
      needed.add(c);
    });
    withoutStar(denyCodes || []).forEach(function (c) {
      needed.add(c);
    });
    expandRegionNeeded(needed, anchor);
    return needed;
  }

  function readPolicyAllowDenyCodes() {
    const allowCodes = [];
    const denyCodes = [];
    if (policyCard) {
      policyCard
        .querySelectorAll("input.policy-allow:checked")
        .forEach(function (el) {
          allowCodes.push(el.getAttribute("data-code"));
        });
      policyCard
        .querySelectorAll("input.policy-deny:checked")
        .forEach(function (el) {
          denyCodes.push(el.getAttribute("data-code"));
        });
    }
    return { allowCodes: allowCodes, denyCodes: denyCodes };
  }

  function syncDeviceRegionSnapshotFromForm(anchor) {
    const pol = readPolicyAllowDenyCodes();
    deviceNamedRegionsFromRead = collectPolicyNeededRegionCodes(
      anchor,
      pol.allowCodes,
      pol.denyCodes,
    );
    const homeLine = regionHomeLineForCli(anchor);
    if (homeLine && /^region home\s+/.test(homeLine)) {
      deviceHomeRegionFromRead = homeLine.replace(/^region home\s+/, "").trim();
    } else {
      deviceHomeRegionFromRead = "*";
    }
    const defLine = regionDefaultLineForCli(anchor);
    if (defLine && /^region default\s+/.test(defLine)) {
      const d = defLine.replace(/^region default\s+/, "").trim();
      deviceDefaultRegionFromRead =
        !d || d.toLowerCase() === "<null>" ? null : d;
    } else {
      deviceDefaultRegionFromRead = null;
    }
  }

  function buildRegionCommandLines(anchor) {
    if (!anchor) return [];

    refreshHomeOverrideSelect();
    refreshDefaultScopeSelect();
    const pol = readPolicyAllowDenyCodes();
    const untaggedEl = document.getElementById("policy-untagged-flood");
    const allowUntagged = !untaggedEl || untaggedEl.checked;
    let denyForCli = withoutStar(pol.denyCodes);
    if (!allowUntagged) {
      denyForCli.push("*");
    }
    const denySorted = starFirst(sortCodesForCli(denyForCli));

    const needed = collectPolicyNeededRegionCodes(
      anchor,
      pol.allowCodes,
      pol.denyCodes,
    );
    const defLine = regionDefaultLineForCli(anchor);
    if (defLine && /^region default\s+/.test(defLine)) {
      const defCode = defLine.replace(/^region default\s+/, "").trim();
      if (defCode && defCode.toLowerCase() !== "<null>") {
        needed.add(defCode);
        expandRegionNeeded(needed, anchor);
      }
    }
    const homeCityRow =
      anchor.mode === "city" && anchor.row ? anchor.row : null;
    const defLines = buildOrderedRegionDefLines(needed, homeCityRow);
    const lines = [];

    // Drop named regions that were on the device at last read but are no
    // longer selected (Allow/Deny) and not required as ancestors. Children
    // before parents — MeshCore removeRegion fails if children remain.
    const toRemove = [];
    if (deviceNamedRegionsFromRead && deviceNamedRegionsFromRead.size) {
      deviceNamedRegionsFromRead.forEach(function (code) {
        if (!code || code === "*") return;
        if (!needed.has(code)) toRemove.push(code);
      });
    }
    if (
      deviceDefaultRegionFromRead &&
      toRemove.indexOf(deviceDefaultRegionFromRead) >= 0
    ) {
      lines.push("region default <null>");
    }
    if (
      deviceHomeRegionFromRead &&
      deviceHomeRegionFromRead !== "*" &&
      toRemove.indexOf(deviceHomeRegionFromRead) >= 0
    ) {
      lines.push("region home *");
    }
    orderRegionRemovesDeepestFirst(toRemove).forEach(function (code) {
      lines.push("region remove " + code);
    });

    defLines.forEach(function (line) {
      lines.push(line);
    });

    // region def sets flood-allowed (flags = 0). Skip redundant allowf for
    // named regions. Wildcard * is not created by def — still needs allowf/denyf.
    // Deny rows still need region denyf (def would leave them allowed).
    if (allowUntagged) {
      lines.push("region allowf *");
    }
    denySorted.forEach(function (r) {
      lines.push("region denyf " + r);
    });
    const homeLine = regionHomeLineForCli(anchor);
    if (homeLine) {
      lines.push(homeLine);
    }
    if (defLine) {
      lines.push(defLine);
    }
    lines.push("region save");
    return lines;
  }

  function buildConfiguratorSections(anchor, options) {
    const enforceFirmwareDefaults = Boolean(
      options && options.enforceFirmwareDefaults,
    );
    const sections = [];
    const settings = [];
    if (namePreviewState && namePreviewState.isValid && namePreviewState.name) {
      settings.push("set name " + namePreviewState.name);
    }

    const setupLines = buildGeneralSettingsCli(enforceFirmwareDefaults);
    if (setupLines) {
      settings.push(setupLines);
    }

    let regionLines = anchor ? buildRegionCommandLines(anchor) : [];

    if (deviceCliBaseline && !enforceFirmwareDefaults) {
      const alreadyOnDevice = deviceCliBaseline.settings;
      const kept = settings
        .join("\n")
        .split("\n")
        .filter(function (line) {
          const trimmed = line.trim();
          return trimmed.length > 0 && !alreadyOnDevice.has(trimmed);
        });
      settings.length = 0;
      if (kept.length) settings.push(kept.join("\n"));

      if (regionLines.join("\n") === deviceCliBaseline.regions) {
        regionLines = [];
      }
    }

    settings.forEach(function (block) {
      sections.push(block);
    });
    if (regionLines.length) {
      sections.push(regionLines.join("\n"));
    }
    return sections;
  }

  function captureDeviceCliBaseline(anchor) {
    const settings = new Set();
    if (namePreviewState && namePreviewState.isValid && namePreviewState.name) {
      settings.add("set name " + namePreviewState.name);
    }
    String(buildGeneralSettingsCli(false) || "")
      .split("\n")
      .forEach(function (line) {
        const trimmed = line.trim();
        if (trimmed) settings.add(trimmed);
      });
    deviceCliBaseline = {
      settings: settings,
      regions: anchor ? buildRegionCommandLines(anchor).join("\n") : "",
    };
  }

  function buildConfiguratorCommandLines(anchor, options) {
    return buildConfiguratorSections(anchor, options)
      .join("\n")
      .split("\n")
      .map(function (line) {
        return line.trim();
      })
      .filter(function (line) {
        return line.length > 0 && line.charAt(0) !== "#";
      });
  }

  function updateMeshcoreCliBlock(anchor) {
    if (!commandsBlock) return;

    const sections = buildConfiguratorSections(anchor, {
      enforceFirmwareDefaults: shouldEnforceDefaults(),
    });
    commandsBlock.textContent = sections.join("\n\n");
    updateUsbApplyUi(anchor);
  }

  /** @deprecated Use refreshConfiguratorOutputs — kept for call sites that only need CLI text. */
  function refreshMeshcoreCli() {
    refreshConfiguratorOutputs();
  }

  /** Neighbour states for policy / "Codes for this selection". */
  function neighborStatesFromNeighbors(neighbors, homeStateCode) {
    const pc =
      homeStateCode != null && typeof homeStateCode === "string"
        ? homeStateCode.trim()
        : homeStateCode;
    if (!pc || !Object.prototype.hasOwnProperty.call(STATE_ADJACENCY, pc)) {
      return [];
    }
    // Every adjacent state is a real scope here, so list them all rather than
    // only the ones with a mapped city nearby -- gc-ms has no city of its own.
    return STATE_ADJACENCY[pc].slice().sort();
  }

  function neighborStatesSectionHTML(neighbors, homeStateCode) {
    const codes = neighborStatesFromNeighbors(neighbors, homeStateCode);
    const n = codes.length;
    if (!codes.length) {
      return (
        '<details class="result-neighbors-details result-state-neighbors">' +
        "<summary>" +
        escapeHtml("Neighbouring states") +
        "</summary>" +
        '<p class="result-muted-note">' +
        escapeHtml(
          "No neighbouring states for this selection.",
        ) +
        "</p></details>"
      );
    }
    const sum =
      "Neighbouring states · " +
      n +
      (n === 1 ? " neighbour" : " neighbours");
    return (
      '<details class="result-neighbors-details result-state-neighbors">' +
      "<summary>" +
      escapeHtml(sum) +
      "</summary>" +
      '<div class="result-neighbors-list">' +
      codes
        .map(function (pc) {
          const name = STATE_NAMES[pc] || pc;
          return (
            '<div class="result-neighbor-card"><p class="result-neighbor-title">' +
            escapeHtml(name) +
            ' <span class="result-code-inline">(' +
            escapeHtml(pc) +
            ')</span> <span class="result-neighbor-distance">' +
            escapeHtml("· neighbour") +
            "</span></p></div>"
          );
        })
        .join("") +
      "</div></details>"
    );
  }

  function buildFoundCodesInnerHTML(anchor, neighbors, hasCoords) {
    let neighborSection = "";
    let neighborStatesSection = "";
    const homeStateCodeVal =
      anchor.mode === "state"
        ? anchor.state_code
        : anchor.row
          ? anchor.row.state_code
          : null;
    if (hasCoords) {
      if (neighbors.length) {
        const nMun = neighbors.length;
        const munLabel =
          nMun === 1 ? "1 city" : nMun + " cities";
        const detailsOpen = nMun <= 6 ? " open" : "";
        neighborSection =
          '<details class="result-neighbors-details"' +
          detailsOpen +
          ">" +
          "<summary>" +
          escapeHtml(
            "Neighbouring cities (~" +
              NEIGHBOR_RADIUS_MI +
              " mi) · " +
              munLabel,
          ) +
          "</summary>" +
          '<div class="result-neighbors-list">' +
          neighbors
            .map(function (item) {
              const o = item.c;
              const km = item.km;
              return (
                '<div class="result-neighbor-card"><p class="result-neighbor-title">' +
                escapeHtml(o.name) +
                ' <span class="result-code-inline">(' +
                escapeHtml(o.city_code) +
                ')</span> <span class="result-neighbor-distance">· ~' +
                Math.round(kmToMi(km)) +
                " mi</span></p></div>"
              );
            })
            .join("") +
          "</div></details>";
      } else {
        neighborSection =
          '<details class="result-neighbors-details">' +
          "<summary>" +
          escapeHtml(
            "Neighbouring cities (~" + NEIGHBOR_RADIUS_MI + " mi)",
          ) +
          "</summary>" +
          '<p class="result-muted-note">No other mapped places within ~' +
          NEIGHBOR_RADIUS_MI +
          " mi.</p></details>";
      }
    } else {
      neighborSection =
        '<details class="result-neighbors-details">' +
        "<summary>" +
        escapeHtml(
          "Neighbouring cities (~" + NEIGHBOR_RADIUS_MI + " mi)",
        ) +
        "</summary>" +
        '<p class="result-muted-note">No coordinates for this location — neighbours not computed.</p></details>';
    }
    if (homeStateCodeVal) {
      neighborStatesSection = neighborStatesSectionHTML(
        neighbors,
        homeStateCodeVal,
      );
    }

    return (
      '<div class="result-codes-section">' +
      '<h3 class="result-block-heading">Selected location</h3><div class="result-grid">' +
      chosenLocationRowsFromAnchor(anchor) +
      "</div></div>" +
      neighborStatesSection +
      neighborSection
    );
  }

  function refreshConfiguratorOutputs() {
    const anchor = getAnchor();
    refreshNamingUi(anchor);
    refreshRadioSettingsUi();
    refreshPolicySection(anchor);
    if (resultGrid) {
      if (!anchor) {
        resultGrid.innerHTML = "";
      } else {
        resultGrid.innerHTML = buildFoundCodesInnerHTML(
          anchor,
          lastNeighbors,
          lastHasCoords,
        );
      }
    }
    updateMeshcoreCliBlock(anchor);
  }

  function locationSearchLabel() {
    if (selectionMode === "country") return ROOT_LABEL + " (" + ROOT_CODE + ")";
    if (selectionMode === "state" && selectedStateCode) {
      return STATE_NAMES[selectedStateCode] || selectedStateCode;
    }
    if (selectionMode === "city" && selectedCity) return selectedCity.name;
    return "";
  }

  function clearLocationSelection() {
    selectionMode = "none";
    selectedStateCode = null;
    selectedCity = null;
    lastNeighbors = [];
    lastHasCoords = false;
  }

  function syncLocationSelectionFromSearch() {
    if (!getAnchor()) return;
    const q = String(input.value || "").trim();
    const label = locationSearchLabel();
    if (!q || (label && q !== label)) {
      clearLocationSelection();
      refreshConfiguratorOutputs();
    }
  }

  function refreshFoundCodesAndCli() {
    refreshConfiguratorOutputs();
  }

  function selectLocation(anchor, label, coordMode) {
    if (label != null && input) {
      input.value = label;
    }
    dropdown.style.display = "none";

    const seed = neighborSeedRow(anchor);
    const neighborsRadius = findGeographicNeighbors(seed, {
      maxKm: NEIGHBOR_RADIUS_KM,
    });
    const neighborsScope = neighborsRadius.slice(0, NEIGHBOR_SCOPE_MAX_CITIES);
    const hasCoords =
      seed &&
      seed.lat != null &&
      seed.lon != null &&
      Number.isFinite(seed.lat) &&
      Number.isFinite(seed.lon);

    lastNeighbors = neighborsRadius;
    lastHasCoords = hasCoords;

    const api = positionApi();
    const mode = coordMode || "seed";
    if (api && mode === "seed" && hasCoords) {
      api.setCoords(seed.lat, seed.lon, { source: "search" });
    }

    renderPolicyGrids(anchor, neighborsScope, neighborsRadius, hasCoords);
    refreshPolicySection(anchor);
    resetNamingForLocation(anchor);
    if (policyFromDevice) {
      const deviceCodes = [];
      if (deviceNamedRegionsFromRead) {
        deviceNamedRegionsFromRead.forEach(function (c) {
          deviceCodes.push(c);
        });
      }
      ensureDeviceRegionScopeRows(deviceCodes);
      applyDeviceCheckState();
      finalizePolicyUiChange();
    }
    if (resultCard) resultCard.classList.add("visible");
    if (commandsCard) commandsCard.classList.add("visible");
    refreshConfiguratorOutputs();
  }

  let pendingLocationChoice = null;

  function formatCoordPair(lat, lon) {
    const api = positionApi();
    if (api && typeof api.formatCoord === "function") {
      return api.formatCoord(lat) + ", " + api.formatCoord(lon);
    }
    return Number(lat).toFixed(6) + ", " + Number(lon).toFixed(6);
  }

  function coordsNearlyEqual(a, b) {
    if (!a || !b) return false;
    return (
      Math.abs(Number(a.lat) - Number(b.lat)) < 1e-6 &&
      Math.abs(Number(a.lon) - Number(b.lon)) < 1e-6
    );
  }

  function locationChoiceToAnchor(choice) {
    if (!choice) return null;
    if (choice.type === "city" && choice.city) {
      return {
        mode: "city",
        state_code: choice.city.state_code,
        row: choice.city,
      };
    }
    if (choice.type === "state" && choice.code) {
      return {
        mode: "state",
        state_code: choice.code,
        row: null,
      };
    }
    if (choice.type === "country") {
      return { mode: "country", state_code: null, row: null };
    }
    return null;
  }

  function defaultCenterLabel(anchor) {
    if (anchor && anchor.mode === "city" && anchor.row) {
      return "City center (" + anchor.row.name + ")";
    }
    if (anchor && anchor.mode === "state" && anchor.state_code) {
      return (
        "State center (" +
        (STATE_NAMES[anchor.state_code] || anchor.state_code) +
        ")"
      );
    }
    return ROOT_LABEL + " centre";
  }

  function commitLocationChoice(choice, coordMode) {
    if (!choice) return;
    if (choice.type === "city" && choice.city) {
      selectionMode = "city";
      selectedStateCode = choice.city.state_code;
      selectedCity = choice.city;
    } else if (choice.type === "state" && choice.code) {
      selectionMode = "state";
      selectedStateCode = choice.code;
      selectedCity = null;
    } else if (choice.type === "country") {
      selectionMode = "country";
      selectedStateCode = null;
      selectedCity = null;
    } else {
      return;
    }
    selectLocation(getAnchor(), choice.label, coordMode);
  }

  function closeLocationCoordsModal() {
    const modal = document.getElementById("location-coords-modal");
    if (modal) modal.hidden = true;
    document.body.classList.remove("config-confirm-modal-open");
    pendingLocationChoice = null;
  }

  function openLocationCoordsModal(existing, seed, anchor) {
    const modal = document.getElementById("location-coords-modal");
    const currentEl = document.getElementById("location-coords-current");
    const defaultEl = document.getElementById("location-coords-default");
    const defaultLabelEl = document.getElementById(
      "location-coords-default-label",
    );
    if (!modal) return;
    if (currentEl) {
      currentEl.textContent = formatCoordPair(existing.lat, existing.lon);
    }
    if (defaultEl) {
      defaultEl.textContent = formatCoordPair(seed.lat, seed.lon);
    }
    if (defaultLabelEl) {
      defaultLabelEl.textContent = defaultCenterLabel(anchor);
    }
    modal.hidden = false;
    document.body.classList.add("config-confirm-modal-open");
    const keepBtn = document.getElementById("location-coords-keep-btn");
    if (keepBtn) keepBtn.focus({ preventScroll: true });
  }

  function requestSelectLocation(choice) {
    const anchor = locationChoiceToAnchor(choice);
    if (!anchor) return;
    const seed = neighborSeedRow(anchor);
    const hasSeed =
      seed &&
      seed.lat != null &&
      seed.lon != null &&
      Number.isFinite(seed.lat) &&
      Number.isFinite(seed.lon);
    const existing = getFormCoords();

    const coordsAreDefault =
      App && App.state && App.state.coordSource === "default";

    if (
      existing.valid &&
      hasSeed &&
      !coordsAreDefault &&
      !coordsNearlyEqual(
        { lat: existing.lat, lon: existing.lon },
        { lat: seed.lat, lon: seed.lon },
      )
    ) {
      pendingLocationChoice = { choice: choice, seed: seed, anchor: anchor };
      openLocationCoordsModal(existing, seed, anchor);
      return;
    }

    commitLocationChoice(choice, hasSeed ? "seed" : "keep");
  }

  function initLocationCoordsModal() {
    const keepBtn = document.getElementById("location-coords-keep-btn");
    const useDefaultBtn = document.getElementById(
      "location-coords-use-default-btn",
    );
    const modal = document.getElementById("location-coords-modal");
    if (keepBtn) {
      keepBtn.addEventListener("click", function () {
        const pending = pendingLocationChoice;
        if (!pending || !pending.choice) return;
        const choice = pending.choice;
        pendingLocationChoice = null;
        const modal = document.getElementById("location-coords-modal");
        if (modal) modal.hidden = true;
        document.body.classList.remove("config-confirm-modal-open");
        commitLocationChoice(choice, "keep");
      });
    }
    if (useDefaultBtn) {
      useDefaultBtn.addEventListener("click", function () {
        const pending = pendingLocationChoice;
        if (!pending || !pending.choice) return;
        const choice = pending.choice;
        pendingLocationChoice = null;
        const modal = document.getElementById("location-coords-modal");
        if (modal) modal.hidden = true;
        document.body.classList.remove("config-confirm-modal-open");
        commitLocationChoice(choice, "seed");
      });
    }
    if (modal) {
      modal
        .querySelectorAll("[data-location-coords-dismiss]")
        .forEach(function (el) {
          el.addEventListener("click", closeLocationCoordsModal);
        });
    }
    document.addEventListener("keydown", function (e) {
      if (e.key !== "Escape") return;
      const m = document.getElementById("location-coords-modal");
      if (m && !m.hidden) {
        closeLocationCoordsModal();
      }
    });
  }

  function selectState(pc) {
    requestSelectLocation({
      type: "state",
      code: pc,
      label: STATE_NAMES[pc] || pc,
    });
  }

  function selectCountryBe() {
    requestSelectLocation({
      type: "country",
      label: ROOT_LABEL + " (" + ROOT_CODE + ")",
    });
  }

  const DEFAULT_LOCATION_CODE = "gc-al-mob";

  function applyDefaultLocation() {
    if (!CITIES.length || getAnchor()) return;
    let city = null;
    for (let i = 0; i < CITIES.length; i++) {
      if (CITIES[i].city_code === DEFAULT_LOCATION_CODE) {
        city = CITIES[i];
        break;
      }
    }
    if (!city) return;
    if (input) input.value = city.name;
    commitLocationChoice({ type: "city", city: city, label: city.name }, "seed");
    if (App && App.state) App.state.coordSource = "default";
  }

  function selectCity(city) {
    requestSelectLocation({
      type: "city",
      city: city,
      label: city.name,
    });
  }

  input.addEventListener("input", () => {
    syncLocationSelectionFromSearch();
    const matches = filterLocationChoices(input.value);
    renderDropdown(matches);
  });

  input.addEventListener("keydown", (e) => {
    if (e.key === "ArrowDown") {
      e.preventDefault();
      activeIndex = Math.min(activeIndex + 1, dropdown.children.length - 1);
      dropdown
        .querySelectorAll(".search-dropdown-item")
        .forEach((el, i) => el.classList.toggle("active", i === activeIndex));
    } else if (e.key === "ArrowUp") {
      e.preventDefault();
      activeIndex = Math.max(activeIndex - 1, 0);
      dropdown
        .querySelectorAll(".search-dropdown-item")
        .forEach((el, i) => el.classList.toggle("active", i === activeIndex));
    } else if (
      e.key === "Enter" &&
      dropdown.style.display !== "none" &&
      lastMatches[activeIndex]
    ) {
      e.preventDefault();
      const item = lastMatches[activeIndex];
      if (item.type === "country") selectCountryBe();
      else if (item.type === "state") selectState(item.code);
      else if (item.type === "place" && item.row) selectCity(item.row);
    } else if (e.key === "Escape") {
      dropdown.style.display = "none";
    }
  });

  input.addEventListener("focus", () => {
    const matches = filterLocationChoices(input.value);
    if (matches.length) renderDropdown(matches);
  });

  document.addEventListener("click", (e) => {
    if (!input.contains(e.target) && !dropdown.contains(e.target)) {
      dropdown.style.display = "none";
    }
  });

  copyBtn.addEventListener("click", () => {
    refreshFoundCodesAndCli();
    navigator.clipboard.writeText(commandsBlock.textContent).then(() => {
      copyBtn.textContent = "Copied";
      copyBtn.classList.add("copied");
      setTimeout(() => {
        copyBtn.textContent = "Copy command script";
        copyBtn.classList.remove("copied");
      }, 2000);
    });
  });

  if (cliShowDefaultsEl) {
    cliShowDefaultsEl.addEventListener("change", refreshConfiguratorOutputs);
  }

  if (serialUsbBtn) {
    serialUsbBtn.addEventListener("click", onSerialUsbToggleClick);
  }
  if (serialReadBtn) {
    serialReadBtn.addEventListener("click", readFromRepeater);
  }
  if (modCard) {
    // The general-card delegate does not reach this section.
    modCard.addEventListener("input", function () {
      refreshConfiguratorOutputs();
    });
  }
  if (modWanJoinBtn) {
    modWanJoinBtn.addEventListener("click", function () {
      runModCommand("ota wan join", "Joining WiFi");
    });
  }
  if (modWanCheckBtn) {
    modWanCheckBtn.addEventListener("click", function () {
      runModCommand("ota wan check", "Checking WAN");
    });
  }
  if (modWanLeaveBtn) {
    modWanLeaveBtn.addEventListener("click", function () {
      runModCommand("ota wan leave", "Leaving WiFi");
    });
  }
  if (modWanPwrBtn) {
    modWanPwrBtn.addEventListener("click", function () {
      const next = modWanPower === "on" ? "off" : "on";
      runModCommand("set ota.wan.pwr " + next, "WAN power " + next);
    });
  }
  function startWanOtaUpdate() {
    const url = settingOtaFwUrlEl
      ? String(settingOtaFwUrlEl.value || "").trim()
      : "";
    if (!url) {
      appendSerialLog(
        "Set a firmware URL first, then Apply so the device stores it.",
        "is-error",
      );
      return;
    }
    if (
      !window.confirm(
        "Download and flash firmware over WiFi from:\n\n" +
          url +
          "\n\nThe repeater reboots on success. Continue?",
      )
    ) {
      return;
    }
    runModCommand("start ota wan update", "Starting WAN OTA update");
  }

  if (deviceWanOtaBtn) {
    deviceWanOtaBtn.addEventListener("click", startWanOtaUpdate);
  }
  if (serialAdvertZerohopBtn) {
    serialAdvertZerohopBtn.addEventListener("click", function () {
      sendRepeaterAdvert("zerohop");
    });
  }
  if (serialAdvertZerohopBtn2) {
    serialAdvertZerohopBtn2.addEventListener("click", function () {
      sendRepeaterAdvert("zerohop");
    });
  }
  if (serialAdvertFloodBtn) {
    serialAdvertFloodBtn.addEventListener("click", function () {
      sendRepeaterAdvert("flood");
    });
  }
  if (serialAdvertFloodBtn2) {
    serialAdvertFloodBtn2.addEventListener("click", function () {
      sendRepeaterAdvert("flood");
    });
  }
  if (serialApplyBtn) {
    serialApplyBtn.addEventListener("click", applyToRepeater);
  }
  if (serialApplyBtn2) {
    serialApplyBtn2.addEventListener("click", applyToRepeater);
  }
  if (serialConsoleForm) {
    serialConsoleForm.addEventListener("submit", onSerialConsoleSubmit);
  }
  if (serialConsoleInput) {
    serialConsoleInput.addEventListener("keydown", onSerialConsoleKeydown);
  }
  if (serialConsoleClearBtn) {
    serialConsoleClearBtn.addEventListener("click", function () {
      clearSerialLog();
      if (serialConsoleInput) {
        serialConsoleInput.focus({ preventScroll: true });
      }
    });
  }

  // ---------------------------------------------------------------------------
  // Device tools: info, identity, config backup, and maintenance actions.
  // ---------------------------------------------------------------------------
  function setButtonSpanText(btn, text) {
    if (!btn) return;
    const span = btn.querySelector("span");
    if (span) span.textContent = text;
  }

  function flashButtonSpan(btn, text, revertText, ms) {
    if (!btn) return;
    const span = btn.querySelector("span");
    if (!span) return;
    const original = revertText || span.textContent;
    span.textContent = text;
    setTimeout(function () {
      span.textContent = original;
    }, ms || 1600);
  }

  async function runDeviceCommand(cmd, opts) {
    opts = opts || {};
    const rs = await ensureSerialReady(opts.label || "Device command");
    if (!rs) return null;
    try {
      appendSerialLog("> " + cmd);
      const res = await rs.sendLine(cmd, opts.sendOptions);
      if (res && res.disconnected) {
        appendSerialLog(
          (opts.successMsg || "Command sent; device is disconnecting."),
          "is-ok",
        );
        return res;
      }
      if (res.reply) {
        appendSerialLog(res.reply, res.ok ? "is-ok" : "is-err");
      }
      if (!res.ok) {
        appendSerialLog((opts.label || "Command") + " failed.", "is-err");
      } else if (opts.successMsg) {
        appendSerialLog(opts.successMsg, "is-ok");
      }
      return res;
    } catch (err) {
      appendSerialLog(
        (opts.label || "Command") +
          " error: " +
          (err && err.message ? err.message : String(err)),
        "is-err",
      );
      return null;
    } finally {
      updateUsbApplyUi(getAnchor());
    }
  }

  if (deviceSyncClockBtn) {
    deviceSyncClockBtn.addEventListener("click", async function () {
      const epoch = Math.floor(Date.now() / 1000);
      await runDeviceCommand("time " + epoch, {
        label: "Clock sync",
        successMsg: "Device clock set to computer time.",
      });
      const r = await runDeviceCommand("clock", { label: "Clock" });
      if (r && r.ok && r.reply && deviceInfoClockEl) {
        deviceInfoClockEl.textContent = r.reply;
      }
    });
  }

  if (deviceCopyPubkeyBtn) {
    deviceCopyPubkeyBtn.addEventListener("click", function () {
      const pk = deviceInfoPubkeyEl
        ? deviceInfoPubkeyEl.textContent.trim()
        : "";
      if (!pk || pk === "—") {
        appendSerialLog(
          "Read from the device first to load the public key.",
          "is-err",
        );
        return;
      }
      navigator.clipboard.writeText(pk);
      flashButtonSpan(deviceCopyPubkeyBtn, "Copied", "Copy public key");
    });
  }

  async function ensurePrivateKeyLoaded() {
    if (!devicePrvkeyEl) return "";
    if (devicePrvkeyEl.value) return devicePrvkeyEl.value;
    const res = await runDeviceCommand("get prv.key", {
      label: "Read private key",
    });
    if (res && res.ok && res.reply) {
      devicePrvkeyEl.value = res.reply.trim();
    }
    return devicePrvkeyEl.value || "";
  }

  if (devicePrvkeyRevealBtn) {
    devicePrvkeyRevealBtn.addEventListener("click", async function () {
      if (!devicePrvkeyEl) return;
      if (devicePrvkeyEl.type === "password") {
        await ensurePrivateKeyLoaded();
        devicePrvkeyEl.type = "text";
        setButtonSpanText(devicePrvkeyRevealBtn, "Hide");
      } else {
        devicePrvkeyEl.type = "password";
        setButtonSpanText(devicePrvkeyRevealBtn, "Reveal");
      }
    });
  }

  if (devicePrvkeyCopyBtn) {
    devicePrvkeyCopyBtn.addEventListener("click", async function () {
      const key = await ensurePrivateKeyLoaded();
      if (!key) {
        appendSerialLog(
          "No private key available (connect and read first).",
          "is-err",
        );
        return;
      }
      navigator.clipboard.writeText(key);
      flashButtonSpan(devicePrvkeyCopyBtn, "Copied", "Copy");
    });
  }

  if (deviceRebootBtn) {
    deviceRebootBtn.addEventListener("click", function () {
      if (!window.confirm("Reboot the device now?")) return;
      runDeviceCommand("reboot", {
        label: "Reboot",
        successMsg:
          "Reboot sent. Connection closed — reconnect over USB when the device is back.",
        sendOptions: { timeoutMs: 3000 },
      });
    });
  }

  if (deviceOtaBtn) {
    deviceOtaBtn.addEventListener("click", function () {
      if (
        !window.confirm(
          "Start local over-the-air firmware update?\n\nThe device enters OTA mode; follow your board's firmware upload steps (see MeshCore FAQ).",
        )
      ) {
        return;
      }
      runDeviceCommand("start ota", {
        label: "Start Local OTA",
        successMsg:
          "OTA started. Connection closed — use your flasher, then reconnect.",
      });
    });
  }

  if (deviceFactoryResetBtn) {
    deviceFactoryResetBtn.addEventListener("click", function () {
      if (
        !window.confirm(
          "FACTORY RESET\n\nThis erases the identity (private key) and ALL settings from the device. This cannot be undone. Continue?",
        )
      ) {
        return;
      }
      if (
        !window.confirm(
          "Are you absolutely sure? The device identity will be permanently lost.",
        )
      ) {
        return;
      }
      runDeviceCommand("erase", {
        label: "Factory reset",
        successMsg:
          "Erase sent. Connection closed — reconnect after the device restarts.",
      });
    });
  }

  // Config backup: export/import the form fields as JSON.
  function collectConfigFieldValues() {
    const data = {};
    const main = document.getElementById("config-main");
    if (!main) return data;
    main
      .querySelectorAll("input[id], select[id], textarea[id]")
      .forEach(function (el) {
        if (el.type === "file" || el.type === "button") return;
        if (el.readOnly) return;
        if (el.type === "checkbox") {
          data[el.id] = el.checked;
        } else {
          data[el.id] = el.value;
        }
      });
    return data;
  }

  function applyConfigFieldValues(data) {
    if (!data || typeof data !== "object") return;
    Object.keys(data).forEach(function (id) {
      const el = document.getElementById(id);
      if (!el) return;
      if (el.type === "checkbox") {
        el.checked = Boolean(data[id]);
      } else {
        el.value = data[id];
      }
      el.dispatchEvent(new Event("input", { bubbles: true }));
      el.dispatchEvent(new Event("change", { bubbles: true }));
    });
    if (typeof refreshConfiguratorOutputs === "function") {
      refreshConfiguratorOutputs();
    }
  }

  if (configExportBtn) {
    configExportBtn.addEventListener("click", function () {
      const payload = {
        app: "gcmesh-configurator",
        version: 1,
        exportedAt: new Date().toISOString(),
        fields: collectConfigFieldValues(),
      };
      const blob = new Blob([JSON.stringify(payload, null, 2)], {
        type: "application/json",
      });
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = "gcmesh-repeater-config.json";
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      setTimeout(function () {
        URL.revokeObjectURL(url);
      }, 1000);
    });
  }

  if (configImportBtn && configImportFileEl) {
    configImportBtn.addEventListener("click", function () {
      configImportFileEl.click();
    });
    configImportFileEl.addEventListener("change", function () {
      const file = configImportFileEl.files && configImportFileEl.files[0];
      if (!file) return;
      const reader = new FileReader();
      reader.onload = function () {
        try {
          const parsed = JSON.parse(String(reader.result || "{}"));
          const fields = parsed && parsed.fields ? parsed.fields : parsed;
          applyConfigFieldValues(fields);
          appendSerialLog("Configuration imported from file.", "is-ok");
        } catch (err) {
          appendSerialLog(
            "Import failed: " +
              (err && err.message ? err.message : String(err)),
            "is-err",
          );
        }
        configImportFileEl.value = "";
      };
      reader.readAsText(file);
    });
  }

  // Vanity public key generation (Ed25519 via tweetnacl).
  function bytesToHex(bytes) {
    let s = "";
    for (let i = 0; i < bytes.length; i++) {
      s += bytes[i].toString(16).padStart(2, "0");
    }
    return s;
  }

  function generateVanityKey() {
    const nacl = window.nacl;
    if (!nacl || !nacl.sign || !nacl.sign.keyPair) {
      appendSerialLog("Key generation library not loaded.", "is-err");
      return;
    }
    const raw = window.prompt(
      "Vanity public key: enter a hex prefix (1-4 chars, 0-9 a-f). Longer prefixes take much longer.",
      "",
    );
    if (raw == null) return;
    const prefix = raw.trim().toLowerCase();
    if (!/^[0-9a-f]{1,4}$/.test(prefix)) {
      appendSerialLog("Invalid prefix — use 1-4 hex characters.", "is-err");
      return;
    }
    appendSerialLog(
      "Generating an identity whose public key starts with '" +
        prefix +
        "'… this can take a while.",
    );
    if (deviceVanityBtn) deviceVanityBtn.disabled = true;
    const maxAttempts = 5000000;
    let attempts = 0;
    const start = Date.now();
    function finish() {
      if (deviceVanityBtn) deviceVanityBtn.disabled = false;
    }
    function batch() {
      for (let i = 0; i < 400; i++) {
        attempts++;
        const seed = nacl.randomBytes(32);
        const kp = nacl.sign.keyPair.fromSeed(seed);
        const pubHex = bytesToHex(kp.publicKey);
        if (pubHex.startsWith(prefix)) {
          // `set prv.key` wants the expanded key — clamped scalar then signing component.
          // keyPair().secretKey is seed+pubkey instead, which the device rejects as bad.
          const h = nacl.hash(seed);
          const scalar = h.slice(0, 32);
          scalar[0] &= 248;
          scalar[31] &= 63;
          scalar[31] |= 64;
          const prvHex = bytesToHex(scalar) + bytesToHex(h.slice(32, 64));
          const secs = ((Date.now() - start) / 1000).toFixed(1);
          appendSerialLog(
            "Found after " + attempts + " tries in " + secs + "s.",
            "is-ok",
          );
          appendSerialLog("New public key: " + pubHex, "is-ok");
          finish();
          const write = window.confirm(
            "Found a matching key!\n\nPublic key:\n" +
              pubHex +
              "\n\nWrite this new identity to the connected device now? " +
              "(runs 'set prv.key', then reboot to apply)",
          );
          if (write) {
            runDeviceCommand("set prv.key " + prvHex, {
              label: "Set identity",
              successMsg: "Identity written. Reboot the device to apply.",
            });
          } else {
            appendSerialLog("Generated key discarded.");
          }
          return;
        }
        if (attempts >= maxAttempts) {
          appendSerialLog(
            "Gave up after " + attempts + " attempts. Try a shorter prefix.",
            "is-err",
          );
          finish();
          return;
        }
      }
      if (attempts % 8000 === 0) {
        appendSerialLog("… " + attempts + " keys tried");
      }
      setTimeout(batch, 0);
    }
    batch();
  }

  if (deviceVanityBtn) {
    deviceVanityBtn.addEventListener("click", generateVanityKey);
  }

  window.addEventListener("beforeunload", function () {
    const rs = getRepeaterSerial();
    if (rs && rs.isConnected()) {
      rs.disconnect();
    }
  });

  const generalCard = document.getElementById("general-card");
  if (generalCard) {
    generalCard.addEventListener("input", function (e) {
      const t = e.target;
      if (t instanceof HTMLElement && t.id === "setting-radio-bw") {
        clampRadioBwInput();
      }
      if (
        t instanceof HTMLElement &&
        (t.id === "name-suffix" || t.id === "name-power-emoji")
      ) {
        const anchor = getAnchor();
        clampNamingInput(anchor);
      }
      refreshConfiguratorOutputs();
    });
    generalCard.addEventListener("change", function (e) {
      const t = e.target;
      if (t instanceof HTMLElement && t.id === "setting-radio-bw") {
        clampRadioBwInput();
      }
      if (
        t instanceof HTMLElement &&
        t.id === "setting-radio-preset" &&
        settingRadioPresetEl
      ) {
        if (isCustomRadioPreset()) {
          const idx = parseInt(
            settingRadioPresetEl.dataset.lastPreset || "",
            10,
          );
          if (Number.isFinite(idx)) {
            fillCustomRadioFields(getPresetRadioByIndex(idx));
          }
        } else {
          settingRadioPresetEl.dataset.lastPreset = settingRadioPresetEl.value;
        }
      }
      if (
        t instanceof HTMLElement &&
        (t.id === "name-location-mode" || t.id === "name-power-emoji")
      ) {
        const anchor = getAnchor();
        if (t.id === "name-location-mode") {
          syncPrefixField(anchor);
        }
        clampNamingInput(anchor);
      }
      refreshConfiguratorOutputs();
    });
  }

  initRadioPresetSelect();
  initSerialShowCommandLogToggle();
  initSerialDisconnectWatch();
  initRecommendedSettingsUi();
  initLocationCoordsModal();
  initSerialReadConfirmModal();

  if (App && App.position && App.position.init) {
    App.position.init(function () {
      refreshConfiguratorOutputs();
    });
  }

  const untaggedFloodEl = document.getElementById("policy-untagged-flood");
  if (untaggedFloodEl) {
    untaggedFloodEl.addEventListener("change", function () {
      refreshConfiguratorOutputs();
    });
  }

  if (commandsCard) commandsCard.classList.add("visible");
  if (resultCard) resultCard.classList.add("visible");
  if (policyCard) policyCard.classList.add("visible");
  refreshPolicySection(null);
  setCurrentLocationMode("none");
  refreshConfiguratorOutputs();

  if (policyCard) {
    policyCard.addEventListener("change", function (e) {
      const t = e.target;
      if (
        t instanceof HTMLSelectElement &&
        (t.id === "policy-home-override-select" ||
          t.id === "policy-default-scope-select")
      ) {
        finalizePolicyUiChange();
        return;
      }
      if (!(t instanceof HTMLInputElement) || t.type !== "checkbox") return;

      if (t.id === "policy-untagged-flood") {
        refreshConfiguratorOutputs();
        return;
      }

      if (t.id === "policy-home-override") {
        refreshHomeOverrideSelect();
        finalizePolicyUiChange();
        return;
      }

      if (t.classList.contains("policy-scope-master-allow")) {
        const subsection = t.closest(".policy-subsection");
        if (subsection) {
          if (t.checked) {
            subsection
              .querySelectorAll("input.policy-deny")
              .forEach(function (el) {
                if (!el.disabled) el.checked = false;
              });
            subsection
              .querySelectorAll("input.policy-allow")
              .forEach(function (el) {
                if (!el.disabled) el.checked = true;
              });
          } else {
            subsection
              .querySelectorAll("input.policy-allow")
              .forEach(function (el) {
                if (!el.disabled) el.checked = false;
              });
          }
          syncScopeMasters(subsection);
        }
        finalizePolicyUiChange();
        return;
      }
      if (t.classList.contains("policy-scope-master-deny")) {
        const subsection = t.closest(".policy-subsection");
        if (subsection) {
          if (t.checked) {
            subsection
              .querySelectorAll("input.policy-allow")
              .forEach(function (el) {
                if (!el.disabled) el.checked = false;
              });
            subsection
              .querySelectorAll("input.policy-deny")
              .forEach(function (el) {
                if (!el.disabled) el.checked = true;
              });
          } else {
            subsection
              .querySelectorAll("input.policy-deny")
              .forEach(function (el) {
                if (!el.disabled) el.checked = false;
              });
          }
          syncScopeMasters(subsection);
        }
        finalizePolicyUiChange();
        return;
      }

      if (t.classList.contains("policy-allow") && t.checked) {
        const code = t.getAttribute("data-code");
        if (code) {
          const d = policyCard.querySelector(
            'input.policy-deny[data-code="' + code + '"]',
          );
          if (d && !d.disabled) d.checked = false;
        }
      } else if (t.classList.contains("policy-deny") && t.checked) {
        const code = t.getAttribute("data-code");
        if (code) {
          const a = policyCard.querySelector(
            'input.policy-allow[data-code="' + code + '"]',
          );
          if (a && !a.disabled) a.checked = false;
        }
      }

      const sub = t.closest(".policy-subsection");
      if (sub) syncScopeMasters(sub);
      finalizePolicyUiChange();
    });

    policyCard.addEventListener("click", function (e) {
      const gBtn = e.target.closest(".policy-global-btn");
      if (gBtn && policyCard.contains(gBtn)) {
        e.preventDefault();
        const mode = gBtn.getAttribute("data-global-bulk");
        if (mode === "defaults") {
          if (policyFromDevice) {
            // Hand the grid back to the picked location.
            policyFromDevice = false;
            rebuildPolicyGridsForAnchor(getAnchor());
          }
          applyPolicyDefaults();
          refreshConfiguratorOutputs();
          return;
        }
        if (mode === "allow") {
          const homeSub = policyCard.querySelector(
            '.policy-subsection[data-policy-scope="home"]',
          );
          if (homeSub) {
            homeSub
              .querySelectorAll("input.policy-deny")
              .forEach(function (el) {
                el.checked = false;
              });
            homeSub
              .querySelectorAll("input.policy-allow")
              .forEach(function (el) {
                el.checked = true;
              });
          }
          const widerSub = policyCard.querySelector(
            '.policy-subsection[data-policy-scope="wider"]',
          );
          if (widerSub) {
            widerSub
              .querySelectorAll("input.policy-deny")
              .forEach(function (el) {
                el.checked = false;
              });
            widerSub
              .querySelectorAll("input.policy-allow")
              .forEach(function (el) {
                el.checked = true;
              });
          }
          const anchorAllow = getAnchor();
          if (anchorAllow) applyNeighborPolicyGating(anchorAllow);
          policyCard
            .querySelectorAll("input.policy-allow")
            .forEach(function (el) {
              if (!el.disabled) el.checked = true;
            });
          policyCard
            .querySelectorAll("input.policy-deny")
            .forEach(function (el) {
              if (!el.disabled) el.checked = false;
            });
        } else if (mode === "deny") {
          const homeSubDeny = policyCard.querySelector(
            '.policy-subsection[data-policy-scope="home"]',
          );
          if (homeSubDeny) {
            homeSubDeny
              .querySelectorAll("input.policy-allow")
              .forEach(function (el) {
                el.checked = false;
              });
            homeSubDeny
              .querySelectorAll("input.policy-deny")
              .forEach(function (el) {
                el.checked = true;
              });
          }
          const anchorDeny = getAnchor();
          if (anchorDeny) applyNeighborPolicyGating(anchorDeny);
          policyCard
            .querySelectorAll("input.policy-deny")
            .forEach(function (el) {
              if (!el.disabled) el.checked = true;
            });
          policyCard
            .querySelectorAll("input.policy-allow")
            .forEach(function (el) {
              if (!el.disabled) el.checked = false;
            });
        } else if (mode === "clear") {
          policyCard
            .querySelectorAll("input.policy-allow, input.policy-deny")
            .forEach(function (el) {
              el.checked = false;
            });
          policyCard
            .querySelectorAll(
              ".policy-scope-master-allow, .policy-scope-master-deny",
            )
            .forEach(function (el) {
              el.checked = false;
              el.indeterminate = false;
            });
          const ho = document.getElementById("policy-home-override");
          const hs = document.getElementById("policy-home-override-select");
          if (ho) ho.checked = false;
          if (hs) hs.value = "";
          const ds = document.getElementById("policy-default-scope-select");
          if (ds) ds.value = "";
        }
        finalizePolicyUiChange();
        return;
      }

      const btn = e.target.closest(".policy-head-clear-link");
      if (!btn || !policyCard.contains(btn)) return;
      e.preventDefault();
      const subsection = btn.closest(".policy-subsection");
      if (!subsection) return;
      const bulk = btn.getAttribute("data-bulk");
      if (bulk === "clear") {
        subsection
          .querySelectorAll("input.policy-allow, input.policy-deny")
          .forEach(function (el) {
            el.checked = false;
          });
        subsection
          .querySelectorAll(
            ".policy-scope-master-allow, .policy-scope-master-deny",
          )
          .forEach(function (el) {
            el.checked = false;
            el.indeterminate = false;
          });
        if (subsection.getAttribute("data-policy-scope") === "home") {
          const ho = document.getElementById("policy-home-override");
          const hs = document.getElementById("policy-home-override-select");
          if (ho) ho.checked = false;
          if (hs) hs.value = "";
          const ds = document.getElementById("policy-default-scope-select");
          if (ds) ds.value = "";
        }
        syncScopeMasters(subsection);
      }
      finalizePolicyUiChange();
    });
  }
})();
