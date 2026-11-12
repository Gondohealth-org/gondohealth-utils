// What to do when the battery won't make it: point of no return, diversions,
// and routing through (solar) charging stations.

import { type LatLng, bearingDeg, distanceM } from "./geo"
import { type Aircraft, type Wind, estimateLeg, groundSpeedMps } from "./flight-estimate"

export type Site = {
  id: string
  pos: LatLng
  charging?: boolean
  swapMinutes?: number // set if the station swaps batteries autonomously
}

const reserveWh = (a: Aircraft) => a.batteryWh * ((a.reservePct ?? 25) / 100)
const hoverWh = (a: Aircraft, seconds: number) => (seconds * (a.hoverPowerW ?? a.cruisePowerW)) / 3600

/** Energy to fly from an airborne position to `to` and land there (no takeoff). */
export function airborneLegWh(from: LatLng, to: LatLng, a: Aircraft, wind?: Wind): number {
  const leg = estimateLeg(distanceM(from, to), bearingDeg(from, to), a, wind)
  return leg.energyWh - hoverWh(a, (a.vtolSecondsPerLeg ?? 90) / 2)
}

/**
 * Furthest distance along the straight route home → destination at which the
 * aircraft can still turn back and land at home with its reserve intact.
 */
export function pointOfNoReturn(
  home: LatLng,
  destination: LatLng,
  a: Aircraft,
  wind?: Wind,
  startWh = a.batteryWh,
) {
  const track = bearingDeg(home, destination)
  const out = groundSpeedMps(a.cruiseSpeedMps, track, wind)
  const back = groundSpeedMps(a.cruiseSpeedMps, (track + 180) % 360, wind)
  const routeM = distanceM(home, destination)
  const spareWh = startWh - reserveWh(a) - hoverWh(a, a.vtolSecondsPerLeg ?? 90)
  const whPerM = out > 0 && back > 0 ? (a.cruisePowerW / out + a.cruisePowerW / back) / 3600 : Infinity
  const pnrM = Math.max(0, spareWh / whPerM)
  return { pnrM, routeM, beyondDestination: pnrM >= routeM }
}

export type Reachable = { site: Site; distanceM: number; energyWh: number; leftWh: number }

/** Sites the aircraft can still land at from `pos`, cheapest first. May dip into the reserve. */
export function reachableSites(
  pos: LatLng,
  remainingWh: number,
  sites: Site[],
  a: Aircraft,
  wind?: Wind,
): Reachable[] {
  return sites
    .map((site) => {
      const energyWh = airborneLegWh(pos, site.pos, a, wind)
      return { site, distanceM: distanceM(pos, site.pos), energyWh, leftWh: remainingWh - energyWh }
    })
    .filter((r) => r.leftWh >= 0)
    .sort((x, y) => x.energyWh - y.energyWh)
}

export type Decision =
  | { action: "continue"; leftWh: number }
  | { action: "divert"; to: Reachable }
  | { action: "land-now" }

/**
 * In-flight call: continue if the destination is reachable with the reserve
 * intact, otherwise divert to the nearest reachable site (charging / swap stations
 * preferred), otherwise land immediately.
 */
export function decide(
  pos: LatLng,
  destination: LatLng,
  remainingWh: number,
  sites: Site[],
  a: Aircraft,
  wind?: Wind,
): Decision {
  const leftWh = remainingWh - airborneLegWh(pos, destination, a, wind)
  if (leftWh >= reserveWh(a)) return { action: "continue", leftWh }
  const options = reachableSites(pos, remainingWh, sites, a, wind)
  const to = options.find((r) => r.site.charging || r.site.swapMinutes != null) ?? options[0]
  return to ? { action: "divert", to } : { action: "land-now" }
}

export type StationPlan = { stops: Site[]; minutes: number; flightMinutes: number; stopMinutes: number }

/**
 * Fastest route from `from` to `to` hopping through charging stations, assuming a
 * full battery at takeoff. At each stop the battery is swapped (`swapMinutes`) or
 * recharged with whatever was used at `chargeW`.
 * Every leg must keep the reserve. Returns null if no chain of stations works.
 */
export function planViaStations(
  from: Site,
  to: Site,
  stations: Site[],
  a: Aircraft,
  opts: { wind?: Wind; chargeW?: number } = {},
): StationPlan | null {
  const nodes = [from, ...stations.filter((s) => s.charging !== false || s.swapMinutes != null), to]
  const best = nodes.map(() => Infinity)
  const prev = nodes.map(() => -1)
  const flight = nodes.map(() => 0)
  const stop = nodes.map(() => 0)
  const done = nodes.map(() => false)
  best[0] = 0

  for (;;) {
    let i = -1
    for (let k = 0; k < nodes.length; k++) if (!done[k] && best[k] < Infinity && (i < 0 || best[k] < best[i])) i = k
    if (i < 0 || i === nodes.length - 1) break
    done[i] = true
    for (let j = 1; j < nodes.length; j++) {
      if (done[j] || j === i) continue
      const leg = estimateLeg(distanceM(nodes[i].pos, nodes[j].pos), bearingDeg(nodes[i].pos, nodes[j].pos), a, opts.wind)
      if (!leg.feasible) continue
      const stopMin =
        j === nodes.length - 1
          ? 0
          : (nodes[j].swapMinutes ?? (opts.chargeW ? (leg.energyWh / opts.chargeW) * 60 : 0))
      const cost = best[i] + leg.minutes + stopMin
      if (cost < best[j]) {
        best[j] = cost
        prev[j] = i
        flight[j] = flight[i] + leg.minutes
        stop[j] = stop[i] + stopMin
      }
    }
  }

  const end = nodes.length - 1
  if (best[end] === Infinity) return null
  const stops: Site[] = []
  for (let k = prev[end]; k > 0; k = prev[k]) stops.unshift(nodes[k])
  return { stops, minutes: best[end], flightMinutes: flight[end], stopMinutes: stop[end] }
}
