# ADR 0001: Battery swap at relay stations

## Status

Accepted — 2026-11-13

## Context

Delivery legs longer than a single charge require relay stations (charging
points B, C, … between origin and destination). Two architectures were
considered:

- **Battery swap** — one airframe flies the whole route A→B→C; at each relay
  station its battery module is swapped for a charged one.
- **Drone swap (handoff)** — a different drone flies each leg (X: A→B, Y: B→C);
  the payload transfers between airframes at each station.

Trade-offs:

| | Battery swap | Drone swap |
|---|---|---|
| Ground time per relay | ~3 min autonomous swap (see `solar.ts`, `contingency.ts`) | ~0 if staffed (pre-positioned drone) |
| Chain of custody | Single airframe end-to-end | Custody event per handoff |
| Cold-chain exposure | None — box stays sealed | Brief exposure at each transfer |
| Unmanned automation | Mechanical, field-proven | Payload transfer robotics unsolved |
| Resilience | One airframe carries all legs | Per-leg airframes; segment isolation |

## Decision

**Battery swap.** One airframe carries the payload origin→destination; relay
stations are battery-swap points.

Rationale:

1. It works identically whether a relay station is staffed or automated —
   swapping a battery is a solved mechanical task in both cases. Automated
   payload transfer between aircraft is not.
2. It keeps custody and the sealed cold-chain box in a single vehicle for the
   whole journey — fewer custody events, simpler compliance story for medical
   cargo.
3. The cost is bounded and known: a few minutes of ground time per relay leg
   (`swapMinutes` in `contingency.ts`), acceptable for the non-emergency
   resupply that dominates flight volume.

## Consequences

- Every relay station needs a charging rack and a buffer stock of battery
  modules sized to expected throughput — see `panelWNeeded` / battery-sizing
  helpers in `solar.ts` and the bay layout in `station-panel.qml`.
- **The battery module interface is standardized across the fleet** — hard
  requirement from day one.
- Payload packaging SHOULD also be a standardized docking module, so a future
  time-critical service (e.g. emergency blood) can adopt staffed payload
  handoff on selected legs without redesigning the fleet. This keeps the door
  open to hybrid operation.
- If a leg later proves too long for one charge even with swap at both ends,
  it splits into additional relay segments (which `planViaStations` already
  supports) rather than adopting handoff.
