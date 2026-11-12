// Sizing solar charging stations for drone batteries.
// Use local peak sun hours (e.g. from globalsolaratlas.info) for each site.

export type SolarStation = {
  panelW: number // total rated panel power
  peakSunHours: number // daily equivalent hours of full sun
  derate?: number // dust, heat, wiring, panel angle; default 0.75
  chargerEfficiency?: number // panel/storage → drone battery; default 0.9
}

/** Energy the panels deliver to drone batteries on an average day. */
export const dailyChargeWh = (s: SolarStation) =>
  s.panelW * s.peakSunHours * (s.derate ?? 0.75) * (s.chargerEfficiency ?? 0.9)

/** Full recharges per day for flights using `whPerFlight` each. */
export const flightsPerDay = (s: SolarStation, whPerFlight: number) =>
  Math.floor(dailyChargeWh(s) / whPerFlight)

/** Minutes to put `energyWh` back into a drone battery with a `chargerW` charger. */
export const chargeMinutes = (energyWh: number, chargerW: number, efficiency = 0.9) =>
  (energyWh / (chargerW * efficiency)) * 60

/** Panel power (W) needed to support `flights` recharges a day. */
export function panelWNeeded(
  flights: number,
  whPerFlight: number,
  peakSunHours: number,
  derate = 0.75,
  chargerEfficiency = 0.9,
) {
  return Math.ceil((flights * whPerFlight) / (peakSunHours * derate * chargerEfficiency))
}

/**
 * Storage battery (Wh) so `nightFlights` can be recharged after dark and the
 * station rides through `autonomyDays` of cloud. `depthOfDischarge` protects the bank.
 */
export function storageWhNeeded(
  nightFlights: number,
  whPerFlight: number,
  { autonomyDays = 1, depthOfDischarge = 0.8, chargerEfficiency = 0.9 } = {},
) {
  return Math.ceil((nightFlights * whPerFlight * autonomyDays) / (chargerEfficiency * depthOfDischarge))
}

/**
 * Spare batteries a swap station needs so a charged pack is always ready:
 * packs on charge at peak demand, plus one in the drone and one spare.
 */
export const sparePacksNeeded = (swapsPerHour: number, chargeMinutesPerPack: number) =>
  Math.ceil((swapsPerHour * chargeMinutesPerPack) / 60) + 2
