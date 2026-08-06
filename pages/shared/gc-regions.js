/**
 * Gulf Coast region model: scope codes, names, adjacency, and the data-path helper.
 *
 * Mirrors the `region def` tree the flasher writes (pages/flasher/member-config-mobile.json):
 *   *  -> us -> us-al, us-fl, us-ms, us-la
 *   *  -> gc -> gc-al -> gc-al-mob, gc-al-gs, gc-al-fhp
 *              gc-fl -> gc-fl-pns
 *              gc-ms
 *              gc-la -> gc-la-lft
 *
 * Exposed as window.GCRegions (plain globals, no bundler needed).
 * Data resolves relative to this file, not the page that loads it.
 */
(function (global) {
  "use strict";

  var ROOT_CODE = "gc";
  var ROOT_LABEL = "Gulf Coast";

  var STATE_NAMES = {
    "gc-la": "Louisiana",
    "gc-ms": "Mississippi",
    "gc-al": "Alabama",
    "gc-fl": "Florida",
  };

  /** Canonical display/iteration order: west to east along the coast. */
  var STATE_CODES = ["gc-la", "gc-ms", "gc-al", "gc-fl"];

  /** Coastline neighbours, not full state borders. */
  var STATE_ADJACENCY = {
    "gc-la": ["gc-ms"],
    "gc-ms": ["gc-la", "gc-al"],
    "gc-al": ["gc-ms", "gc-fl"],
    "gc-fl": ["gc-al"],
  };

  /** Rough coastal centre per state, used when a state is picked without a city. */
  var STATE_CENTROIDS = {
    "gc-la": [30.0, -91.5],
    "gc-ms": [30.4, -88.9],
    "gc-al": [30.6, -87.9],
    "gc-fl": [30.4, -86.9],
  };

  /** The parallel us-* branch, offered as wider scopes rather than home scopes. */
  var WIDER_SCOPES = [
    { code: "us", label: "United States" },
    { code: "us-la", label: "Louisiana (US)" },
    { code: "us-ms", label: "Mississippi (US)" },
    { code: "us-al", label: "Alabama (US)" },
    { code: "us-fl", label: "Florida (US)" },
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
