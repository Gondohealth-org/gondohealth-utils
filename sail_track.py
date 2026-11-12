"""Sailing race analysis from a GPS track: speed, VMG, tacks and gybes, polars and legs.

Standard library only. Uses course over ground as heading (no leeway or current)
and a steady true wind.

    python sail_track.py race.gpx --wind 20 --mark=-17.89,36.95 --mark=-17.90,36.95 \
        --svg polar.svg --csv samples.csv
"""

from __future__ import annotations

import argparse
import csv
import math
import xml.etree.ElementTree as ET
from dataclasses import asdict, dataclass
from datetime import datetime

KN = 1.943844  # m/s -> knots
EARTH_RADIUS_M = 6_371_008.8


@dataclass
class Fix:
    t: float  # seconds since epoch
    lat: float
    lng: float


@dataclass
class Sample:
    t: float
    lat: float
    lng: float
    sog_kn: float  # speed over ground
    cog_deg: float  # course over ground
    twa_deg: float  # true wind angle, -180..180, + = wind on starboard
    vmg_kn: float  # + upwind, - downwind


@dataclass
class Maneuver:
    t: float
    kind: str  # "tack" | "gybe"
    loss_m: float


@dataclass
class PolarPoint:
    twa_deg: float
    best_kn: float  # 80th percentile: best sustained, ignoring GPS spikes
    avg_kn: float
    n: int


@dataclass
class Leg:
    mark: int
    seconds: float
    sailed_m: float
    direct_m: float
    efficiency: float  # direct / sailed
    vmc_kn: float  # velocity made good towards the mark


def distance_m(a_lat, a_lng, b_lat, b_lng) -> float:
    p1, p2 = math.radians(a_lat), math.radians(b_lat)
    h = math.sin((p2 - p1) / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(math.radians(b_lng - a_lng) / 2) ** 2
    return 2 * EARTH_RADIUS_M * math.asin(min(1.0, math.sqrt(h)))


def bearing_deg(a_lat, a_lng, b_lat, b_lng) -> float:
    p1, p2 = math.radians(a_lat), math.radians(b_lat)
    dl = math.radians(b_lng - a_lng)
    y = math.sin(dl) * math.cos(p2)
    x = math.cos(p1) * math.sin(p2) - math.sin(p1) * math.cos(p2) * math.cos(dl)
    return (math.degrees(math.atan2(y, x)) + 360) % 360


def wrap180(d: float) -> float:
    return (d + 180) % 360 - 180


def _time(s: str) -> float:
    s = s.strip()
    try:
        v = float(s)
        return v / 1000 if v > 1e11 else v  # epoch ms or s
    except ValueError:
        return datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp()


def parse_gpx(path: str) -> list[Fix]:
    """Track points from a GPX file (phones, watches, chartplotters)."""
    fixes = []
    for el in ET.parse(path).iter():
        if el.tag.endswith("trkpt"):
            time = next((c.text for c in el if c.tag.endswith("time")), None)
            if time:
                fixes.append(Fix(_time(time), float(el.get("lat")), float(el.get("lon"))))
    return fixes


def parse_csv(path: str) -> list[Fix]:
    """CSV with a header containing time (ISO or epoch), lat and lon / lng."""
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        return []
    keys = {k.lower().strip(): k for k in rows[0]}
    tk = next(keys[k] for k in keys if k == "t" or k.startswith("time"))
    lo = next(keys[k] for k in ("lon", "lng", "long") if k in keys)
    return [Fix(_time(r[tk]), float(r[keys["lat"]]), float(r[lo])) for r in rows]


def to_samples(fixes: list[Fix], wind_from_deg: float, window_s: float = 8) -> list[Sample]:
    """Speed, course, wind angle and VMG per fix, smoothed over `window_s` seconds."""
    out, j = [], 0
    for i, b in enumerate(fixes):
        while b.t - fixes[j].t > window_s:
            j += 1
        if i == j:
            continue
        a = fixes[j]
        sog = distance_m(a.lat, a.lng, b.lat, b.lng) / (b.t - a.t) * KN
        cog = bearing_deg(a.lat, a.lng, b.lat, b.lng)
        twa = wrap180(cog - wind_from_deg)
        out.append(Sample(b.t, b.lat, b.lng, sog, cog, twa, sog * math.cos(math.radians(twa))))
    return out


def detect_maneuvers(s: list[Sample], hold_s: float = 15) -> list[Maneuver]:
    """Tacks and gybes: the wind changes side and stays there for `hold_s` seconds.

    `loss_m` is the distance (upwind for tacks, downwind for gybes) lost against
    the VMG held before the turn, over a 30 s window around it.
    """
    out, side, flip = [], 0, -1
    for i, x in enumerate(s):
        sd = int(math.copysign(1, x.twa_deg)) if x.twa_deg else side
        if not side:
            side = sd
        elif sd == side:
            flip = -1
        else:
            if flip < 0:
                flip = i
            if x.t - s[flip].t >= hold_s:
                m = _measure(s, flip)
                if m:
                    out.append(m)
                side, flip = sd, -1
    return out


def _measure(s: list[Sample], i: int) -> Maneuver | None:
    t0 = s[i].t
    before = [x for x in s if t0 - 30 <= x.t < t0 - 10]
    win = [x for x in s if t0 - 10 <= x.t <= t0 + 20]
    if not before or len(win) < 2:
        return None
    kind = "tack" if sum(abs(x.twa_deg) for x in before) / len(before) < 90 else "gybe"
    d = 1 if kind == "tack" else -1
    steady_mps = sum(d * x.vmg_kn for x in before) / len(before) / KN
    a, b = _mean_pos(win[:3]), _mean_pos(win[-3:])  # average out GPS noise at both ends
    wind_from = before[0].cog_deg - before[0].twa_deg
    made = d * distance_m(*a, *b) * math.cos(math.radians(bearing_deg(*a, *b) - wind_from))
    return Maneuver(t0, kind, steady_mps * (win[-2].t - win[1].t) - made)


def _mean_pos(xs: list[Sample]) -> tuple[float, float]:
    return sum(x.lat for x in xs) / len(xs), sum(x.lng for x in xs) / len(xs)


def build_polar(s: list[Sample], bin_deg: int = 10) -> list[PolarPoint]:
    """Boat speed by true wind angle, both tacks combined."""
    bins: dict[int, list[float]] = {}
    last = math.ceil(180 / bin_deg) - 1
    for x in s:
        bins.setdefault(min(int(abs(x.twa_deg) // bin_deg), last), []).append(x.sog_kn)
    out = []
    for b, v in sorted(bins.items()):
        v.sort()
        out.append(PolarPoint(b * bin_deg + bin_deg / 2, v[min(len(v) - 1, int(len(v) * 0.8))], sum(v) / len(v), len(v)))
    return out


def split_legs(s: list[Sample], marks: list[tuple[float, float]], round_radius_m: float = 30) -> list[Leg]:
    """Split the race at each mark (rounded within `round_radius_m`) and summarise each leg."""
    legs, start, sailed = [], 0, 0.0
    for i in range(1, len(s)):
        if len(legs) == len(marks):
            break
        sailed += s[i].sog_kn / KN * (s[i].t - s[i - 1].t)  # smoothed, so GPS jitter doesn't add distance
        mlat, mlng = marks[len(legs)]
        if distance_m(s[i].lat, s[i].lng, mlat, mlng) > round_radius_m:
            continue
        secs = s[i].t - s[start].t
        direct = distance_m(s[start].lat, s[start].lng, mlat, mlng)
        legs.append(Leg(len(legs), secs, sailed, direct, direct / sailed, direct / secs * KN))
        start, sailed = i, 0.0
    return legs


def polar_svg(polar: list[PolarPoint], size: int = 420, min_samples: int = 3) -> str:
    """Half-circle polar diagram (0 deg = head to wind at the top)."""
    pts = [p for p in polar if p.n >= min_samples and p.best_kn > 0]
    max_kn = max(2, math.ceil(max((p.best_kn for p in pts), default=2) / 2) * 2)
    r, cx, cy = size / 2 - 30, 30, size / 2

    def xy(deg, kn):
        a = math.radians(deg)
        return cx + kn / max_kn * r * math.sin(a), cy - kn / max_kn * r * math.cos(a)

    parts = []
    for d in (0, 45, 90, 135, 180):
        x, y = xy(d, max_kn)
        dy = 12 if d == 0 else -4 if d == 180 else 4
        parts.append(f'<line x1="{cx}" y1="{cy}" x2="{x:.1f}" y2="{y:.1f}" stroke="#e2e8f0"/>'
                     f'<text x="{x:.1f}" y="{y:.1f}" dx="4" dy="{dy}" font-size="11" fill="#64748b">{d}&#176;</text>')
    for kn in range(2, max_kn + 1, 2):
        rr = kn / max_kn * r
        parts.append(f'<path d="M{cx} {cy - rr:.1f} A{rr:.1f} {rr:.1f} 0 0 1 {cx} {cy + rr:.1f}" fill="none" stroke="#cbd5e1"/>'
                     f'<text x="{cx + rr:.1f}" y="{cy + 14}" text-anchor="middle" font-size="11" fill="#64748b">{kn}</text>')
    parts.append(f'<text x="{cx + r:.1f}" y="{cy + 28}" text-anchor="end" font-size="11" fill="#64748b">knots</text>')
    line = " ".join("%.1f,%.1f" % xy(p.twa_deg, p.best_kn) for p in pts)
    parts.append(f'<polyline points="{line}" fill="none" stroke="#2563eb" stroke-width="2.5"/>')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{size // 2 + 40}" height="{size}" '
            f'font-family="sans-serif"><rect width="100%" height="100%" fill="white"/>{"".join(parts)}</svg>')


def analyze(fixes: list[Fix], wind_from_deg: float, marks: list[tuple[float, float]] = ()):
    """Samples, manoeuvres, polar and legs for one race."""
    samples = to_samples(fixes, wind_from_deg)
    return {
        "samples": samples,
        "maneuvers": detect_maneuvers(samples),
        "polar": build_polar(samples),
        "legs": split_legs(samples, list(marks)),
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("track", help="GPX or CSV (time,lat,lon) file")
    ap.add_argument("--wind", type=float, required=True, help="true wind direction it blows FROM, degrees")
    ap.add_argument("--mark", action="append", default=[], help="lat,lng of each mark in rounding order")
    ap.add_argument("--svg", help="write the polar diagram here")
    ap.add_argument("--csv", help="write per-second samples here (e.g. for training data)")
    a = ap.parse_args()

    fixes = parse_gpx(a.track) if a.track.lower().endswith(".gpx") else parse_csv(a.track)
    marks = [tuple(map(float, m.split(","))) for m in a.mark]
    r = analyze(fixes, a.wind, marks)

    for m in r["maneuvers"]:
        print(f"{datetime.fromtimestamp(m.t):%H:%M:%S}  {m.kind:<4}  lost {m.loss_m:5.1f} m")
    for leg in r["legs"]:
        print(f"leg {leg.mark + 1}: {leg.seconds / 60:.1f} min, {leg.efficiency:.0%} efficient, {leg.vmc_kn:.2f} kn to mark")
    print("polar:", ", ".join(f"{p.twa_deg:.0f}° {p.best_kn:.1f} kn" for p in r["polar"] if p.n >= 3))
    if a.svg:
        with open(a.svg, "w") as f:
            f.write(polar_svg(r["polar"]))
    if a.csv:
        with open(a.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(asdict(r["samples"][0])))
            w.writeheader()
            w.writerows(asdict(x) for x in r["samples"])


if __name__ == "__main__":
    main()
