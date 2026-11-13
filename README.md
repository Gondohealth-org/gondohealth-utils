# gondohealth-utils

**English** | [Português](README.pt.md)

Small dependency-free utilities for medical drone logistics, from
[**Gondohealth**](https://github.com/Gondohealth-org).

| file | what it is |
|---|---|
| [`geo.ts`](geo.ts) | Distance, bearing and route length between facilities, plus point-in-polygon checks for service areas and no-fly zones |
| [`flight-estimate.ts`](flight-estimate.ts) | Wind-corrected ground speed, flight time and battery margin for a delivery leg or round trip |
| [`cold-chain.ts`](cold-chain.ts) | Find temperature excursions in a payload sensor log (e.g. vaccines at 2–8 °C) for proof of delivery |
| [`qgc-plan.ts`](qgc-plan.ts) | Build a QGroundControl `.plan` mission (VTOL takeoff → waypoints → land) for PX4 / ArduPilot |
| [`contingency.ts`](contingency.ts) | Point of no return, in-flight continue / divert / land-now decisions, and routing through charging or battery-swap stations when a clinic is out of range |
| [`solar.ts`](solar.ts) | Size solar charging stations: daily charge energy, flights per day, charge time, panel power, storage and spare batteries for swap stations |
| [`station-panel.qml`](station-panel.qml) | Qt 6 / QML touchscreen panel for an embedded swap station: solar input, battery bays, next arrival |
| [`sail_track.cpp`](sail_track.cpp) | C++17 (standard library only) sailing race analyser for GPX / CSV tracks: speed, VMG, tack and gybe losses, polar diagram, leg efficiency, per-second CSV for training data |

```ts
import { distanceM, bearingDeg } from "./geo"
import { estimateRoundTrip } from "./flight-estimate"

const quelimane = { lat: -17.8786, lng: 36.8883 }
const mocuba = { lat: -16.8392, lng: 36.9856 }

estimateRoundTrip(distanceM(quelimane, mocuba), bearingDeg(quelimane, mocuba), {
  cruiseSpeedMps: 25, cruisePowerW: 900, hoverPowerW: 3000, batteryWh: 4000,
}, { speedMps: 6, fromDeg: 0 })
// → { minutes: ~167, feasible: true, marginPct: ... }
```

```ts
import { planViaStations } from "./contingency"
import { panelWNeeded } from "./solar"

// Quelimane → Gurué (268 km) is out of range on 2.4 kWh, so hop via a solar station at Mocuba
planViaStations(quelimane, gurue, [mocuba], aircraft, { chargeW: 1000 })
// → { stops: [mocuba], minutes: ~256, stopMinutes: ~74 }   recharge
// with an autonomous battery swap at Mocuba ({ ...mocuba, swapMinutes: 3 }):
// → { stops: [mocuba], minutes: ~185, stopMinutes: 3 }

panelWNeeded(6, 1300, 5) // → 2312 W of panels for 6 recharges a day at 5 peak sun hours
```

```sh
g++ -std=c++17 -O2 -o sail_track sail_track.cpp
./sail_track race.gpx --wind 20 --mark -17.89,36.95 --mark -17.90,36.95 --svg polar.svg --csv samples.csv
```

Architecture decisions live in [docs/adr](docs/adr) — starting with
[why relay stations swap batteries rather than aircraft](docs/adr/0001-battery-swap-at-relay-stations.md).

Estimates are planning aids, not flight-safety tools. MIT licensed.
