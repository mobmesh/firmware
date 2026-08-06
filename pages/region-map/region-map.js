/**
 * Region map: the GulfCoastMesh scope tree drawn over state areas and the
 * mapped repeater cities. Scope codes and places come from the configurator's
 * data so the two pages never disagree.
 */
(function () {
  "use strict";

  const REGIONS = window.GCRegions || {};
  const ROOT_CODE = REGIONS.ROOT_CODE || "gc";
  const ROOT_LABEL = REGIONS.ROOT_LABEL || "Gulf Coast";
  const STATE_NAMES = REGIONS.STATE_NAMES || {};
  const WIDER_SCOPES = REGIONS.WIDER_SCOPES || [];

  const CITIES_URL = (REGIONS.dataUrl || function (f) { return "../shared/data/" + f; })(
    "gc-locations.json",
  );
  const STATES_URL = "data/gc-states.geojson";
  const TILE_URL =
    "https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png";
  const TILE_SUBDOMAINS = "abcd";
  const TILE_ATTRIBUTION =
    '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors &copy; <a href="https://carto.com/attributions">CARTO</a>';
  const DEFAULT_VIEW = [30.2, -89.0];
  const DEFAULT_ZOOM = 6;

  const params = new URLSearchParams(window.location.search);
  const embedded = params.get("embed") === "1";
  const showPanel = params.get("panel") === "1";
  const initialScope = params.get("scope") || "*";
  if (embedded) document.body.classList.add("is-embedded");
  if (embedded && showPanel) document.body.classList.add("show-panel");

  // The configurator drives the embedded copy without reloading the frame.
  window.addEventListener("message", function (event) {
    if (event.origin !== window.location.origin) return;
    const data = event.data;
    if (!data || data.type !== "region-map:select") return;
    if (nodesByCode[data.code] || data.code === "*") {
      select(data.code, { zoom: true });
    }
  });

  const els = {
    map: document.getElementById("region-map-container"),
    hierarchy: document.getElementById("hierarchy"),
    counter: document.getElementById("counter"),
    search: document.getElementById("code-input"),
    showStates: document.getElementById("show-states"),
    showPlaces: document.getElementById("show-places"),
    toggle: document.getElementById("toggle-controls"),
    controls: document.getElementById("controls"),
    note: document.getElementById("selection-note"),
  };

  let cities = [];
  let stateLayer = null;
  let placeLayer = null;
  let selected = "*";
  const nodesByCode = Object.create(null);

  const map = L.map(els.map, { zoomControl: true }).setView(
    DEFAULT_VIEW,
    DEFAULT_ZOOM,
  );
  L.tileLayer(TILE_URL, {
    maxZoom: 20,
    subdomains: TILE_SUBDOMAINS,
    attribution: TILE_ATTRIBUTION,
  }).addTo(map);

  /** Ancestor chain of a dashed code, e.g. gc-al-mob -> [gc, gc-al]. */
  function ancestorsOf(code) {
    const parts = String(code).split("-");
    const out = [];
    for (let i = 1; i < parts.length; i++) {
      out.push(parts.slice(0, i).join("-"));
    }
    return out;
  }

  function isCovered(code, sel) {
    if (sel === "*") return true;
    if (code === sel) return true;
    return ancestorsOf(code).indexOf(sel) >= 0;
  }

  function labelFor(code) {
    if (code === "*") return "All scopes";
    if (code === ROOT_CODE) return ROOT_LABEL;
    if (STATE_NAMES[code]) return STATE_NAMES[code];
    for (let i = 0; i < WIDER_SCOPES.length; i++) {
      if (WIDER_SCOPES[i].code === code) return WIDER_SCOPES[i].label;
    }
    for (let i = 0; i < cities.length; i++) {
      if (cities[i].city_code === code) return cities[i].name;
    }
    return code;
  }

  /** Every code in the model, as a parent -> children tree keyed by dash depth. */
  function buildTree() {
    const codes = [ROOT_CODE];
    Object.keys(STATE_NAMES).forEach(function (c) {
      codes.push(c);
    });
    cities.forEach(function (c) {
      codes.push(c.city_code);
    });
    WIDER_SCOPES.forEach(function (w) {
      codes.push(w.code);
    });

    const known = new Set(codes);
    const children = Object.create(null);
    children["*"] = [];
    codes.forEach(function (c) {
      if (!children[c]) children[c] = [];
    });
    codes.forEach(function (code) {
      const chain = ancestorsOf(code).filter(function (a) {
        return known.has(a);
      });
      const parent = chain.length ? chain[chain.length - 1] : "*";
      children[parent].push(code);
    });
    Object.keys(children).forEach(function (k) {
      children[k].sort();
    });
    return { children: children, count: codes.length };
  }

  function renderTree() {
    const tree = buildTree();
    els.hierarchy.innerHTML = "";

    function addNode(code, depth) {
      const row = document.createElement("button");
      row.type = "button";
      row.className = "hierarchy-node";
      row.dataset.code = code;
      row.style.paddingLeft = 0.4 + depth * 0.85 + "rem";
      row.innerHTML =
        '<span class="hierarchy-code">' +
        code +
        '</span><span class="hierarchy-label">' +
        labelFor(code) +
        "</span>";
      row.addEventListener("click", function () {
        select(code, { zoom: true });
      });
      els.hierarchy.appendChild(row);
      nodesByCode[code] = row;
      (tree.children[code] || []).forEach(function (child) {
        addNode(child, depth + 1);
      });
    }

    addNode("*", 0);

    els.counter.textContent =
      tree.count + " scopes · " + cities.length + " mapped cities";
  }

  function styleForState(feature) {
    const code = feature.properties.gc_code;
    const on = isCovered(code, selected) || isCovered(feature.properties.us_code, selected);
    return {
      color: on ? "#2dd1bd" : "#4b6785",
      weight: on ? 2 : 1,
      fillColor: on ? "#2dd1bd" : "#22364f",
      fillOpacity: on ? 0.22 : 0.08,
    };
  }

  function refreshStyles() {
    if (stateLayer) stateLayer.setStyle(styleForState);
    if (placeLayer) {
      placeLayer.eachLayer(function (marker) {
        const on = isCovered(marker.options.scopeCode, selected);
        marker.setStyle({
          color: on ? "#f9a228" : "#6b7f96",
          fillColor: on ? "#f9a228" : "#33495f",
          fillOpacity: on ? 0.9 : 0.45,
          weight: on ? 2 : 1,
        });
      });
    }
    Object.keys(nodesByCode).forEach(function (code) {
      nodesByCode[code].classList.toggle("is-selected", code === selected);
      nodesByCode[code].classList.toggle(
        "is-covered",
        code !== selected && isCovered(code, selected),
      );
    });
  }

  function boundsForSelection() {
    const group = [];
    if (stateLayer) {
      stateLayer.eachLayer(function (layer) {
        const p = layer.feature.properties;
        if (isCovered(p.gc_code, selected) || isCovered(p.us_code, selected)) {
          group.push(layer);
        }
      });
    }
    if (placeLayer) {
      placeLayer.eachLayer(function (marker) {
        if (isCovered(marker.options.scopeCode, selected)) group.push(marker);
      });
    }
    if (!group.length) return null;
    return L.featureGroup(group).getBounds();
  }

  function revealInPanel(node) {
    const scroller = document.querySelector(".region-map-controls-scroll");
    if (!node || !scroller) return;
    const nodeBox = node.getBoundingClientRect();
    const scrollBox = scroller.getBoundingClientRect();
    if (nodeBox.top < scrollBox.top) {
      scroller.scrollTop += nodeBox.top - scrollBox.top;
    } else if (nodeBox.bottom > scrollBox.bottom) {
      scroller.scrollTop += nodeBox.bottom - scrollBox.bottom;
    }
  }

  function select(code, opts) {
    selected = code;
    refreshStyles();
    revealInPanel(nodesByCode[code]);
    els.note.textContent =
      code === "*"
        ? "Showing every scope in the tree."
        : "Selected " + code + " — " + labelFor(code) + ".";
    if (opts && opts.zoom) {
      const b = boundsForSelection();
      if (b && b.isValid()) map.fitBounds(b, { padding: [40, 40], maxZoom: 11 });
    }
  }

  function drawStates(geojson) {
    stateLayer = L.geoJSON(geojson, {
      style: styleForState,
      onEachFeature: function (feature, layer) {
        const p = feature.properties;
        layer.bindTooltip(
          p.name + " — " + p.gc_code + " / " + p.us_code,
          { sticky: true },
        );
        layer.on("click", function () {
          select(p.gc_code, { zoom: true });
        });
      },
    });
    if (els.showStates.checked) stateLayer.addTo(map);
  }

  function drawPlaces() {
    placeLayer = L.featureGroup(
      cities.map(function (c) {
        const marker = L.circleMarker([c.lat, c.lon], {
          radius: 7,
          scopeCode: c.city_code,
        });
        marker.bindTooltip(c.name + " — " + c.city_code, {
          direction: "top",
          offset: L.point(0, -6),
        });
        marker.on("click", function () {
          select(c.city_code, { zoom: true });
        });
        return marker;
      }),
    );
    if (els.showPlaces.checked) placeLayer.addTo(map);
  }

  function findByQuery(q) {
    const needle = q.trim().toLowerCase();
    if (!needle) return null;
    const codes = Object.keys(nodesByCode);
    for (let i = 0; i < codes.length; i++) {
      if (codes[i].toLowerCase() === needle) return codes[i];
    }
    for (let i = 0; i < codes.length; i++) {
      if (
        codes[i].toLowerCase().indexOf(needle) >= 0 ||
        labelFor(codes[i]).toLowerCase().indexOf(needle) >= 0
      ) {
        return codes[i];
      }
    }
    return null;
  }

  els.search.addEventListener("input", function () {
    const hit = findByQuery(els.search.value);
    if (hit) select(hit, { zoom: true });
  });

  els.showStates.addEventListener("change", function () {
    if (!stateLayer) return;
    if (els.showStates.checked) stateLayer.addTo(map);
    else map.removeLayer(stateLayer);
  });

  els.showPlaces.addEventListener("change", function () {
    if (!placeLayer) return;
    if (els.showPlaces.checked) placeLayer.addTo(map);
    else map.removeLayer(placeLayer);
  });

  els.toggle.addEventListener("click", function () {
    const hidden = document.body.classList.toggle("controls-hidden");
    els.toggle.textContent = hidden ? "Show panel" : "Hide panel";
    els.toggle.setAttribute("aria-expanded", String(!hidden));
    window.setTimeout(function () {
      map.invalidateSize();
    }, 220);
  });

  Promise.all([
    fetch(CITIES_URL).then(function (r) {
      return r.ok ? r.json() : [];
    }),
    fetch(STATES_URL).then(function (r) {
      return r.ok ? r.json() : { type: "FeatureCollection", features: [] };
    }),
  ])
    .then(function (results) {
      cities = Array.isArray(results[0]) ? results[0] : [];
      drawStates(results[1]);
      drawPlaces();
      renderTree();
      select(nodesByCode[initialScope] ? initialScope : "*", { zoom: true });
    })
    .catch(function () {
      els.counter.textContent = "Could not load region data.";
    });
})();
