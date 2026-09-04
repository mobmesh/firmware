/**
 * Gulf Coast region model: scope codes, names, adjacency, and the data-path helper.
 *
 * Mirrors the `region def` tree the flasher writes (pages/flasher/data/us-al-mob-settings.json):
 *   us -> us-gc, us-southeast, us-south, us-east   (wider scopes, no geometry)
 *   us -> us-al -> us-al-mob, us-al-fhp, us-al-guf
 *         us-fl -> us-fl-pns
 *         us-ms
 *         us-la -> us-la-lft, us-la-msy
 *
 * us-la and its cities are ahead of the flasher's tree, which defines no
 * Louisiana node yet.
 *
 * Exposed as window.GCRegions (plain globals, no bundler needed).
 * Data resolves relative to this file, not the page that loads it.
 */
(function (global) {
  "use strict";

  var ROOT_CODE = "us";
  var ROOT_LABEL = "United States";

  var STATE_NAMES = {
    "us-la": "Louisiana",
    "us-ms": "Mississippi",
    "us-al": "Alabama",
    "us-fl": "Florida",
  };

  /** Canonical display/iteration order: west to east along the coast. */
  var STATE_CODES = ["us-la", "us-ms", "us-al", "us-fl"];

  /** Coastline neighbours, not full state borders. */
  var STATE_ADJACENCY = {
    "us-la": ["us-ms"],
    "us-ms": ["us-la", "us-al"],
    "us-al": ["us-ms", "us-fl"],
    "us-fl": ["us-al"],
  };

  /** Rough coastal centre per state, used when a state is picked without a city. */
  var STATE_CENTROIDS = {
    "us-la": [30.0, -91.5],
    "us-ms": [30.4, -88.9],
    "us-al": [30.6, -87.9],
    "us-fl": [30.4, -86.9],
  };

  /** Aggregates above the states. The flasher creates these; no geometry backs them. */
  var WIDER_SCOPES = [
    { code: "us-gc", label: "Gulf Coast" },
    { code: "us-southeast", label: "Southeast" },
    { code: "us-south", label: "South" },
    { code: "us-east", label: "East" },
  ];

  var moduleUrl =
    (global.document &&
      global.document.currentScript &&
      global.document.currentScript.src) ||
    (global.location ? global.location.href : "");

  function dataUrl(filename) {
    return new URL("data/" + filename, moduleUrl).href;
  }

  function stateCentroid(code) {
    var c = STATE_CENTROIDS[code];
    return c ? { lat: c[0], lon: c[1] } : { lat: 30.4, lon: -88.5 };
  }

  /** Adjacent states that exist in the model. */
  function adjacentStates(code) {
    var list = STATE_ADJACENCY[code] || [];
    return list.filter(function (c) {
      return Object.prototype.hasOwnProperty.call(STATE_NAMES, c);
    });
  }

  global.GCRegions = {
    ROOT_CODE: ROOT_CODE,
    ROOT_LABEL: ROOT_LABEL,
    STATE_NAMES: STATE_NAMES,
    STATE_CODES: STATE_CODES,
    STATE_ADJACENCY: STATE_ADJACENCY,
    STATE_CENTROIDS: STATE_CENTROIDS,
    WIDER_SCOPES: WIDER_SCOPES,
    dataUrl: dataUrl,
    stateCentroid: stateCentroid,
    adjacentStates: adjacentStates,
  };
})(typeof window !== "undefined" ? window : this);
