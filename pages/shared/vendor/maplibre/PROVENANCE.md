# MapLibre GL JS

`maplibre-gl.js`, `maplibre-gl.css` and `LICENSE` are the unmodified dist files from
MapLibre GL JS 5.6.1, BSD-3-Clause, © MapLibre contributors.

- Release: `https://github.com/maplibre/maplibre-gl-js/releases/tag/v5.6.1`
- Dist archive: `https://unpkg.com/maplibre-gl@5.6.1/dist/`
- No build step. Unlike Leaflet's, the CSS references no image files, so the two files
  move together with nothing beside them.

Used by flasher2's location step, which renders CARTO's vector basemap. Leaflet cannot
draw a vector style, and the style's label layers are individually addressable -- which
is what lets street names arrive earlier and carry our own palette. The configurator and
region map still use Leaflet against the raster tiles.
