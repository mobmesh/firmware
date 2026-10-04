/**
 * Repeater coordinates: form fields, validation, and Leaflet map picker modal.
 */
(function (App) {
  "use strict";

  // Mobile, Alabama
  const DEFAULT_MAP_CENTER = [30.6954, -88.0399];
  const DEFAULT_MAP_ZOOM = 10;
  const COORD_DECIMALS = 6;

  let onChangeCallback = null;
  let mapInstance = null;
  let mapMarker = null;
  let pickLat = null;
  let pickLon = null;

  function getEl(id) {
    return document.getElementById(id);
  }

  function formatCoord(value) {
    const n = Number(value);
    if (!Number.isFinite(n)) return "";
    return n.toFixed(COORD_DECIMALS);
  }

  function parseCoordInput(raw) {
    const text = String(raw || "").trim().replace(",", ".");
    if (!text) return null;
    const n = Number(text);
    if (!Number.isFinite(n)) return null;
    return n;
  }

  function isUnsetCoordPair(lat, lon) {
    return lat === 0 && lon === 0;
  }

  function hasValidCoords(lat, lon) {
    if (!Number.isFinite(lat) || !Number.isFinite(lon)) return false;
    if (isUnsetCoordPair(lat, lon)) return false;
    return true;
  }

  // Coastal box from the upper Texas coast around to the Florida Atlantic side,
  // reaching far enough north to cover sites a little way inland.
  function outOfRegionWarning(lat, lon) {
    if (!hasValidCoords(lat, lon)) return false;
    return lat < 24.4 || lat > 31.5 || lon < -95.4 || lon > -79.8;
  }

  function getFormElements() {
    return {
      latEl: getEl("setting-lat"),
      lonEl: getEl("setting-lon"),
      advertLocEl: getEl("setting-advert-loc"),
    };
  }

  function notifyChange() {
    if (typeof onChangeCallback === "function") {
      onChangeCallback();
    }
  }

  function updatePickPreview() {
    const previewEl = getEl("position-map-pick-preview");
    if (!previewEl) return;
    if (hasValidCoords(pickLat, pickLon)) {
      previewEl.textContent =
        formatCoord(pickLat) + ", " + formatCoord(pickLon);
    } else {
      previewEl.textContent = "—";
    }
  }

  function setMarkerPosition(lat, lon) {
    pickLat = lat;
    pickLon = lon;
    if (mapMarker) {
      mapMarker.setLatLng([lat, lon]);
    }
    updatePickPreview();
  }

  function ensureMap() {
    if (mapInstance) return mapInstance;
    const container = getEl("position-map-container");
    if (!container || typeof L === "undefined") return null;

    mapInstance = L.map(container, {
      center: DEFAULT_MAP_CENTER,
      zoom: DEFAULT_MAP_ZOOM,
      scrollWheelZoom: true,
    });

    L.tileLayer(
      "https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png",
      {
        attribution:
          '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors &copy; <a href="https://carto.com/attributions">CARTO</a>',
        subdomains: "abcd",
        maxZoom: 20,
      },
    ).addTo(mapInstance);

    mapMarker = L.marker(DEFAULT_MAP_CENTER, { draggable: true }).addTo(
      mapInstance,
    );

    mapMarker.on("dragend", function () {
      const ll = mapMarker.getLatLng();
      setMarkerPosition(ll.lat, ll.lng);
    });

    mapInstance.on("click", function (e) {
      setMarkerPosition(e.latlng.lat, e.latlng.lng);
    });

    return mapInstance;
  }

  function readFormCoords() {
    const { latEl, lonEl } = getFormElements();
    const lat = latEl ? parseCoordInput(latEl.value) : null;
    const lon = lonEl ? parseCoordInput(lonEl.value) : null;
    if (!hasValidCoords(lat, lon)) {
      return { valid: false, lat: null, lon: null };
    }
    return { valid: true, lat: lat, lon: lon };
  }

  function writeFormCoords(lat, lon, options) {
    const { latEl, lonEl } = getFormElements();
    if (!latEl || !lonEl) return;
    if (lat == null || lon == null || !hasValidCoords(lat, lon)) {
      latEl.value = "";
      lonEl.value = "";
      latEl.classList.remove("is-warning");
      lonEl.classList.remove("is-warning");
      if (options && options.source) {
        App.state.coordSource = null;
      }
      return;
    }
    latEl.value = formatCoord(lat);
    lonEl.value = formatCoord(lon);
    const warn = outOfRegionWarning(lat, lon);
    latEl.classList.toggle("is-warning", warn);
    lonEl.classList.toggle("is-warning", warn);
    if (options && options.source) {
      App.state.coordSource = options.source;
    }
  }

  function closeModal() {
    const modal = getEl("position-map-modal");
    if (!modal) return;
    modal.hidden = true;
    document.body.classList.remove("position-map-modal-open");
  }

  function openModal(initialLat, initialLon) {
    const modal = getEl("position-map-modal");
    if (!modal) return;

    const map = ensureMap();
    if (!map) return;

    const hasFormCoords = hasValidCoords(initialLat, initialLon);
    const viewLat = hasFormCoords ? initialLat : DEFAULT_MAP_CENTER[0];
    const viewLon = hasFormCoords ? initialLon : DEFAULT_MAP_CENTER[1];
    const viewZoom = hasFormCoords ? 12 : DEFAULT_MAP_ZOOM;

    modal.hidden = false;
    document.body.classList.add("position-map-modal-open");

    window.requestAnimationFrame(function () {
      map.invalidateSize();
      map.setView([viewLat, viewLon], viewZoom);
      setMarkerPosition(viewLat, viewLon);
      window.requestAnimationFrame(function () {
        map.invalidateSize();
      });
    });
  }

  function openMapPickerFromForm() {
    const { latEl, lonEl } = getFormElements();
    const lat = latEl ? parseCoordInput(latEl.value) : null;
    const lon = lonEl ? parseCoordInput(lonEl.value) : null;
    if (hasValidCoords(lat, lon)) {
      openModal(lat, lon);
    } else {
      openModal(null, null);
    }
  }

  function confirmPick() {
    if (!hasValidCoords(pickLat, pickLon)) return;
    writeFormCoords(pickLat, pickLon, { source: "manual" });
    closeModal();
    notifyChange();
  }

  function clearCoords() {
    writeFormCoords(null, null, { source: null });
    notifyChange();
  }

  function normalizeFieldInput(el) {
    if (!(el instanceof HTMLInputElement)) return;
    const latEl = getEl("setting-lat");
    const lonEl = getEl("setting-lon");
    const lat = latEl ? parseCoordInput(latEl.value) : null;
    const lon = lonEl ? parseCoordInput(lonEl.value) : null;
    if (el === latEl && lat != null) {
      latEl.value = formatCoord(lat);
    }
    if (el === lonEl && lon != null) {
      lonEl.value = formatCoord(lon);
    }
    if (hasValidCoords(lat, lon)) {
      const warn = outOfRegionWarning(lat, lon);
      if (latEl) latEl.classList.toggle("is-warning", warn);
      if (lonEl) lonEl.classList.toggle("is-warning", warn);
    } else {
      if (latEl) latEl.classList.remove("is-warning");
      if (lonEl) lonEl.classList.remove("is-warning");
    }
    App.state.coordSource = "manual";
    notifyChange();
  }

  App.position = {
    formatCoord: formatCoord,
    hasValidCoords: hasValidCoords,
    getCoords: readFormCoords,
    setCoords: writeFormCoords,
    clearCoords: clearCoords,
    getAdvertLocPolicy: function () {
      const { advertLocEl } = getFormElements();
      const value = advertLocEl ? advertLocEl.value : "prefs";
      if (value === "none" || value === "share" || value === "prefs") {
        return value;
      }
      return "prefs";
    },
    advertIncludesLocation: function () {
      return App.position.getAdvertLocPolicy() !== "none";
    },
    openMapPicker: function () {
      openMapPickerFromForm();
    },
    init: function (onChange) {
      onChangeCallback = onChange;
      App.state.coordSource = App.state.coordSource || null;

      const pickBtn = getEl("position-pick-map-btn");
      const clearBtn = getEl("position-clear-btn");
      const useBtn = getEl("position-map-use-btn");
      const latEl = getEl("setting-lat");
      const lonEl = getEl("setting-lon");
      const advertLocEl = getEl("setting-advert-loc");
      const modal = getEl("position-map-modal");

      if (pickBtn) {
        pickBtn.addEventListener("click", function () {
          App.position.openMapPicker();
        });
      }
      if (clearBtn) {
        clearBtn.addEventListener("click", function () {
          clearCoords();
        });
      }
      if (useBtn) {
        useBtn.addEventListener("click", confirmPick);
      }
      if (latEl) {
        latEl.addEventListener("change", function () {
          normalizeFieldInput(latEl);
        });
        latEl.addEventListener("blur", function () {
          normalizeFieldInput(latEl);
        });
      }
      if (lonEl) {
        lonEl.addEventListener("change", function () {
          normalizeFieldInput(lonEl);
        });
        lonEl.addEventListener("blur", function () {
          normalizeFieldInput(lonEl);
        });
      }
      if (advertLocEl) {
        advertLocEl.addEventListener("change", notifyChange);
      }
      if (modal) {
        modal.querySelectorAll("[data-position-map-dismiss]").forEach(function (el) {
          el.addEventListener("click", closeModal);
        });
      }
      document.addEventListener("keydown", function (e) {
        if (e.key === "Escape" && modal && !modal.hidden) {
          closeModal();
        }
      });
    },
  };
})(window.ConfiguratorApp);
