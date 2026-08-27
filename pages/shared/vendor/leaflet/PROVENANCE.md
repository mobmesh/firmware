# Leaflet

`leaflet.js`, `leaflet.css`, `images/` and `LICENSE` are the unmodified dist files from
Leaflet 1.9.4, BSD-2-Clause, © 2010-2024 Volodymyr Agafonkin, © 2010-2011 CloudMade.

- Release: `https://github.com/Leaflet/Leaflet/releases/tag/v1.9.4`
- Dist archive: `https://cdn.jsdelivr.net/npm/leaflet@1.9.4/dist/`
- No build step; the CSS resolves its icons relatively against `images/`, so the
  directory moves as a unit.

Used by the configurator and the region map. Shared rather than vendored per page --
the two copies were byte-identical and drifted apart on any update.
