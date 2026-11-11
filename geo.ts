// Great-circle helpers for planning flights between health facilities.

export type LatLng = { lat: number; lng: number }

const EARTH_RADIUS_M = 6_371_008.8
const toRad = (deg: number) => (deg * Math.PI) / 180
const toDeg = (rad: number) => (rad * 180) / Math.PI

/** Distance in metres between two points (haversine). */
export function distanceM(a: LatLng, b: LatLng): number {
  const dLat = toRad(b.lat - a.lat)
  const dLng = toRad(b.lng - a.lng)
  const h =
    Math.sin(dLat / 2) ** 2 +
    Math.cos(toRad(a.lat)) * Math.cos(toRad(b.lat)) * Math.sin(dLng / 2) ** 2
  return 2 * EARTH_RADIUS_M * Math.asin(Math.min(1, Math.sqrt(h)))
}

/** Initial bearing in degrees (0 = north, clockwise) from a to b. */
export function bearingDeg(a: LatLng, b: LatLng): number {
  const φ1 = toRad(a.lat)
  const φ2 = toRad(b.lat)
  const Δλ = toRad(b.lng - a.lng)
  const y = Math.sin(Δλ) * Math.cos(φ2)
  const x = Math.cos(φ1) * Math.sin(φ2) - Math.sin(φ1) * Math.cos(φ2) * Math.cos(Δλ)
  return (toDeg(Math.atan2(y, x)) + 360) % 360
}

/** Total length in metres of a route through the given points. */
export function routeLengthM(points: LatLng[]): number {
  let total = 0
  for (let i = 1; i < points.length; i++) total += distanceM(points[i - 1], points[i])
  return total
}

/**
 * Whether a point lies inside a polygon (ray casting). Use for service areas
 * or no-fly zones; fine for zones up to a few tens of kilometres across.
 */
export function insidePolygon(p: LatLng, polygon: LatLng[]): boolean {
  let inside = false
  for (let i = 0, j = polygon.length - 1; i < polygon.length; j = i++) {
    const a = polygon[i]
    const b = polygon[j]
    if (a.lat > p.lat !== b.lat > p.lat) {
      const lngAtLat = ((b.lng - a.lng) * (p.lat - a.lat)) / (b.lat - a.lat) + a.lng
      if (p.lng < lngAtLat) inside = !inside
    }
  }
  return inside
}

/** First no-fly zone the straight route a→b crosses, sampled every `stepM` metres. */
export function firstZoneCrossed(
  a: LatLng,
  b: LatLng,
  zones: LatLng[][],
  stepM = 100,
): number {
  const steps = Math.max(1, Math.ceil(distanceM(a, b) / stepM))
  for (let s = 0; s <= steps; s++) {
    const t = s / steps
    const p = { lat: a.lat + (b.lat - a.lat) * t, lng: a.lng + (b.lng - a.lng) * t }
    const hit = zones.findIndex((z) => insidePolygon(p, z))
    if (hit !== -1) return hit
  }
  return -1
}
