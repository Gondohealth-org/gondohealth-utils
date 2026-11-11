// Rough flight time and battery margin for a delivery leg, including wind.

export type Aircraft = {
  cruiseSpeedMps: number // airspeed in still air
  hoverPowerW?: number // VTOL takeoff / landing power
  cruisePowerW: number
  batteryWh: number
  reservePct?: number // battery kept back for diversions, default 25%
  vtolSecondsPerLeg?: number // time spent in hover per takeoff + landing, default 90 s
}

export type Wind = { speedMps: number; fromDeg: number } // meteorological "from" direction

const toRad = (deg: number) => (deg * Math.PI) / 180

/** Ground speed along `trackDeg` for a given airspeed and wind (wind triangle). */
export function groundSpeedMps(airspeedMps: number, trackDeg: number, wind?: Wind): number {
  if (!wind || wind.speedMps === 0) return airspeedMps
  const windTo = toRad(wind.fromDeg + 180)
  const track = toRad(trackDeg)
  const head = wind.speedMps * Math.cos(windTo - track) // tailwind component (+)
  const cross = wind.speedMps * Math.sin(windTo - track)
  if (Math.abs(cross) >= airspeedMps) return 0 // cannot hold the track
  return Math.sqrt(airspeedMps ** 2 - cross ** 2) + head
}

export type LegEstimate = {
  minutes: number
  energyWh: number
  usableWh: number
  marginPct: number // usable battery left after the leg
  feasible: boolean
}

/** Estimate one leg of `distanceM` metres flown on `trackDeg`. */
export function estimateLeg(
  distanceM: number,
  trackDeg: number,
  aircraft: Aircraft,
  wind?: Wind,
): LegEstimate {
  const reserve = aircraft.reservePct ?? 25
  const usableWh = aircraft.batteryWh * (1 - reserve / 100)
  const gs = groundSpeedMps(aircraft.cruiseSpeedMps, trackDeg, wind)
  if (gs <= 0) return { minutes: Infinity, energyWh: Infinity, usableWh, marginPct: -100, feasible: false }

  const cruiseS = distanceM / gs
  const hoverS = aircraft.vtolSecondsPerLeg ?? 90
  const energyWh =
    (cruiseS * aircraft.cruisePowerW + hoverS * (aircraft.hoverPowerW ?? aircraft.cruisePowerW)) / 3600
  const marginPct = ((usableWh - energyWh) / usableWh) * 100
  return {
    minutes: (cruiseS + hoverS) / 60,
    energyWh,
    usableWh,
    marginPct,
    feasible: marginPct >= 0,
  }
}

/** Out-and-back estimate: delivery leg plus the return leg into the reciprocal track. */
export function estimateRoundTrip(
  distanceM: number,
  trackDeg: number,
  aircraft: Aircraft,
  wind?: Wind,
): LegEstimate {
  const out = estimateLeg(distanceM, trackDeg, aircraft, wind)
  const back = estimateLeg(distanceM, (trackDeg + 180) % 360, aircraft, wind)
  const energyWh = out.energyWh + back.energyWh
  const marginPct = ((out.usableWh - energyWh) / out.usableWh) * 100
  return {
    minutes: out.minutes + back.minutes,
    energyWh,
    usableWh: out.usableWh,
    marginPct,
    feasible: marginPct >= 0,
  }
}
