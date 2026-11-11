// Build a QGroundControl .plan file (simple waypoint mission) from coordinates.
// Opens in QGroundControl and can be uploaded to PX4 or ArduPilot vehicles.

export type Waypoint = { lat: number; lng: number; altM: number }

const MAV_CMD_NAV_WAYPOINT = 16
const MAV_CMD_NAV_LAND = 21
const MAV_CMD_NAV_VTOL_TAKEOFF = 84
const MAV_CMD_NAV_VTOL_LAND = 85
const MAV_FRAME_GLOBAL_RELATIVE_ALT = 3

type SimpleItem = {
  type: "SimpleItem"
  autoContinue: true
  command: number
  doJumpId: number
  frame: number
  params: (number | null)[]
}

const item = (id: number, command: number, p: Waypoint, hold = 0): SimpleItem => ({
  type: "SimpleItem",
  autoContinue: true,
  command,
  doJumpId: id,
  frame: MAV_FRAME_GLOBAL_RELATIVE_ALT,
  params: [hold, 0, 0, null, p.lat, p.lng, p.altM],
})

export type PlanOptions = {
  vtol?: boolean // use VTOL takeoff/land commands (default true)
  cruiseSpeedMps?: number
  firmware?: "px4" | "ardupilot"
}

/**
 * Mission: take off at `home`, fly `route`, land at `destination`.
 * Altitudes are relative to the home position.
 */
export function buildPlan(
  home: Waypoint,
  route: Waypoint[],
  destination: Waypoint,
  opts: PlanOptions = {},
) {
  const vtol = opts.vtol ?? true
  const items: SimpleItem[] = []
  let id = 1
  items.push(item(id++, vtol ? MAV_CMD_NAV_VTOL_TAKEOFF : MAV_CMD_NAV_WAYPOINT, home))
  for (const wp of route) items.push(item(id++, MAV_CMD_NAV_WAYPOINT, wp))
  items.push(item(id++, vtol ? MAV_CMD_NAV_VTOL_LAND : MAV_CMD_NAV_LAND, destination))

  return {
    fileType: "Plan",
    version: 1,
    groundStation: "QGroundControl",
    geoFence: { circles: [], polygons: [], version: 2 },
    rallyPoints: { points: [], version: 2 },
    mission: {
      version: 2,
      firmwareType: opts.firmware === "ardupilot" ? 3 : 12, // MAV_AUTOPILOT
      vehicleType: vtol ? 22 : 1, // MAV_TYPE_VTOL_FIXEDROTOR : FIXED_WING
      cruiseSpeed: opts.cruiseSpeedMps ?? 20,
      hoverSpeed: 5,
      plannedHomePosition: [home.lat, home.lng, home.altM],
      items,
    },
  }
}

/** Serialize to the JSON text QGroundControl expects in a .plan file. */
export const toPlanFile = (plan: ReturnType<typeof buildPlan>) => JSON.stringify(plan, null, 2)
