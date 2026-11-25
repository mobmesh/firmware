// Subscription zones: which data/zones.geojson area holds a point; shared by the flasher and the app's settings page.

// Ray casting against a feature's outer rings; holes are ignored, since none of these shapes has one and a wrong answer inside a hole would still be a neighbouring zone.
export function featureContains(feature, lat, lon) {
  const { type, coordinates } = feature.geometry;
  const polygons = type === 'Polygon' ? [coordinates] : coordinates;
  return polygons.some((polygon) => {
    const ring = polygon[0];
    let inside = false;
    for (let i = 0, j = ring.length - 1; i < ring.length; j = i++) {
      const [xi, yi] = ring[i];
      const [xj, yj] = ring[j];
      if ((yi > lat) !== (yj > lat) && lon < ((xj - xi) * (lat - yi)) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
  });
}

/** The home area containing a point: a zone if one covers it, otherwise the state around it. */
export function zoneAt(zones, lat, lon) {
  if (!zones) return null;
  const hits = zones.features.filter((feature) => featureContains(feature, lat, lon));
  if (!hits.length) return null;
  const pick = hits.find((feature) => feature.properties.kind === 'zone') ?? hits[0];
  return { id: pick.properties.id, settings: pick.properties.settings ?? null };
}
