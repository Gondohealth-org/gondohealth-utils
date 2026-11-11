// Detect temperature excursions in a payload sensor log.

export type Reading = { t: number; celsius: number } // t = unix ms

export type Range = { min: number; max: number }

/** Common storage ranges. Check product labels: these are typical values only. */
export const RANGES = {
  vaccines: { min: 2, max: 8 },
  bloodRedCells: { min: 2, max: 6 },
  roomTemp: { min: 15, max: 25 },
} satisfies Record<string, Range>

export type Excursion = {
  start: number
  end: number
  minutes: number
  peak: number // furthest reading outside the range
  direction: "high" | "low"
}

/**
 * Group consecutive out-of-range readings into excursions. Readings must be
 * sorted by time. Excursions shorter than `toleranceMin` are ignored.
 */
export function findExcursions(readings: Reading[], range: Range, toleranceMin = 0): Excursion[] {
  const out: Excursion[] = []
  let cur: Excursion | null = null
  for (const r of readings) {
    const dir = r.celsius > range.max ? "high" : r.celsius < range.min ? "low" : null
    if (dir && cur && cur.direction === dir) {
      cur.end = r.t
      cur.peak = dir === "high" ? Math.max(cur.peak, r.celsius) : Math.min(cur.peak, r.celsius)
      continue
    }
    if (cur) out.push(cur)
    cur = dir ? { start: r.t, end: r.t, minutes: 0, peak: r.celsius, direction: dir } : null
  }
  if (cur) out.push(cur)
  return out
    .map((e) => ({ ...e, minutes: (e.end - e.start) / 60_000 }))
    .filter((e) => e.minutes >= toleranceMin)
}

export type ColdChainSummary = {
  ok: boolean
  excursions: Excursion[]
  minC: number
  maxC: number
  meanC: number
}

/** Pass/fail summary for proof of delivery. */
export function summarize(readings: Reading[], range: Range, toleranceMin = 0): ColdChainSummary {
  if (readings.length === 0) throw new Error("no readings")
  const temps = readings.map((r) => r.celsius)
  const excursions = findExcursions(readings, range, toleranceMin)
  return {
    ok: excursions.length === 0,
    excursions,
    minC: Math.min(...temps),
    maxC: Math.max(...temps),
    meanC: temps.reduce((a, b) => a + b, 0) / temps.length,
  }
}
