# gondohealth-utils

Small dependency-free TypeScript utilities for medical drone logistics, from
[**Gondohealth**](https://github.com/Gondohealth-org).

| file | what it is |
|---|---|
| [`geo.ts`](geo.ts) | Distance, bearing and route length between facilities, plus point-in-polygon checks for service areas and no-fly zones |
| [`flight-estimate.ts`](flight-estimate.ts) | Wind-corrected ground speed, flight time and battery margin for a delivery leg or round trip |
| [`cold-chain.ts`](cold-chain.ts) | Find temperature excursions in a payload sensor log (e.g. vaccines at 2–8 °C) for proof of delivery |
| [`qgc-plan.ts`](qgc-plan.ts) | Build a QGroundControl `.plan` mission (VTOL takeoff → waypoints → land) for PX4 / ArduPilot |

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

Estimates are planning aids, not flight-safety tools. MIT licensed.
