// sail_track.cpp: sailing race analysis from a GPS track. Speed, VMG, tacks and gybes, polars and legs.
//
// C++17, standard library only, so it builds the same on a laptop or an onboard Linux board.
// Uses course over ground as heading (no leeway or current), a steady true wind, and UTC times.
//
//   g++ -std=c++17 -O2 -o sail_track sail_track.cpp
//   ./sail_track race.gpx --wind 20 --mark -17.89,36.95 --mark -17.90,36.95 --svg polar.svg --csv samples.csv

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sail {

constexpr double KN = 1.943844;  // m/s -> knots
constexpr double EARTH_RADIUS_M = 6371008.8;
constexpr double PI = 3.14159265358979323846;

struct LatLng { double lat, lng; };
struct Fix { double t, lat, lng; };  // t = seconds since epoch
struct Sample {
    double t, lat, lng;
    double sog_kn;   // speed over ground
    double cog_deg;  // course over ground
    double twa_deg;  // true wind angle, -180..180, + = wind on starboard
    double vmg_kn;   // + upwind, - downwind
};
struct Maneuver { double t; std::string kind; double loss_m; };  // kind: "tack" | "gybe"
struct PolarPoint {
    double twa_deg;
    double best_kn;  // 80th percentile: best sustained, ignoring GPS spikes
    double avg_kn;
    int n;
};
struct Leg {
    int mark;
    double seconds, sailed_m, direct_m;
    double efficiency;  // direct / sailed
    double vmc_kn;      // velocity made good towards the mark
};
struct Analysis {
    std::vector<Sample> samples;
    std::vector<Maneuver> maneuvers;
    std::vector<PolarPoint> polar;
    std::vector<Leg> legs;
};

inline double rad(double d) { return d * PI / 180; }
inline double deg(double r) { return r * 180 / PI; }

double distance_m(LatLng a, LatLng b) {
    double p1 = rad(a.lat), p2 = rad(b.lat);
    double h = std::pow(std::sin((p2 - p1) / 2), 2) +
               std::cos(p1) * std::cos(p2) * std::pow(std::sin(rad(b.lng - a.lng) / 2), 2);
    return 2 * EARTH_RADIUS_M * std::asin(std::min(1.0, std::sqrt(h)));
}

double bearing_deg(LatLng a, LatLng b) {
    double p1 = rad(a.lat), p2 = rad(b.lat), dl = rad(b.lng - a.lng);
    double y = std::sin(dl) * std::cos(p2);
    double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
    return std::fmod(deg(std::atan2(y, x)) + 360, 360);
}

double wrap180(double d) {
    d = std::fmod(d + 180, 360);
    return (d < 0 ? d + 360 : d) - 180;
}

static std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n\"'");
    auto e = s.find_last_not_of(" \t\r\n\"'");
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

static std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::vector<std::string> split(const std::string& s, char sep = ',') {
    std::vector<std::string> out;
    std::stringstream ss(s);
    for (std::string item; std::getline(ss, item, sep);) out.push_back(trim(item));
    return out;
}

static long long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

// ISO 8601 ("2026-11-12T09:00:00Z", "+02:00" offsets, no offset = UTC) or epoch seconds / milliseconds.
double parse_time(const std::string& raw) {
    std::string s = trim(raw);
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (!s.empty() && *end == '\0') return v > 1e11 ? v / 1000 : v;
    int y, mo, d, h, mi;
    double sec = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%d%*c%d:%d:%lf", &y, &mo, &d, &h, &mi, &sec) < 5)
        throw std::runtime_error("bad time: " + s);
    double offset = 0;
    auto tz = s.find_first_of("Z+-", 16);
    if (tz != std::string::npos && s[tz] != 'Z') {
        std::string digits;
        for (char c : s.substr(tz + 1)) if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
        digits.resize(4, '0');
        offset = (std::stoi(digits.substr(0, 2)) * 60 + std::stoi(digits.substr(2, 2))) * 60.0 * (s[tz] == '-' ? -1 : 1);
    }
    return static_cast<double>(days_from_civil(y, mo, d)) * 86400 + h * 3600 + mi * 60 + sec - offset;
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Track points from a GPX file (phones, watches, chartplotters).
std::vector<Fix> parse_gpx(const std::string& path) {
    static const std::regex lat_re(R"(\blat\s*=\s*["']([^"']+)["'])"), lon_re(R"(\blon\s*=\s*["']([^"']+)["'])");
    const std::string x = read_file(path);
    std::vector<Fix> out;
    for (auto p = x.find("<trkpt"); p != std::string::npos; p = x.find("<trkpt", p + 6)) {
        auto gt = x.find('>', p);
        if (gt == std::string::npos) break;
        if (x[gt - 1] == '/') continue;  // no <time> inside
        auto close = x.find("</trkpt>", gt);
        std::string tag = x.substr(p, gt - p), body = x.substr(gt + 1, close - gt - 1);
        auto ts = body.find("<time>"), te = body.find("</time>");
        std::smatch la, lo;
        if (ts == std::string::npos || te == std::string::npos ||
            !std::regex_search(tag, la, lat_re) || !std::regex_search(tag, lo, lon_re))
            continue;
        out.push_back({parse_time(body.substr(ts + 6, te - ts - 6)), std::stod(la[1]), std::stod(lo[1])});
    }
    return out;
}

// CSV with a header containing time (ISO or epoch), lat and lon / lng.
std::vector<Fix> parse_csv(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::string line;
    if (!std::getline(f, line)) return {};
    int ti = -1, la = -1, lo = -1;
    auto head = split(line);
    for (int i = 0; i < static_cast<int>(head.size()); ++i) {
        auto k = lower(head[i]);
        if (ti < 0 && (k == "t" || k.rfind("time", 0) == 0)) ti = i;
        else if (k == "lat" || k == "latitude") la = i;
        else if (lo < 0 && (k == "lon" || k == "lng" || k == "long" || k == "longitude")) lo = i;
    }
    if (ti < 0 || la < 0 || lo < 0) throw std::runtime_error("CSV needs time, lat and lon columns");
    std::vector<Fix> out;
    const size_t need = static_cast<size_t>(std::max({ti, la, lo}));
    while (std::getline(f, line)) {
        auto c = split(line);
        if (c.size() > need) out.push_back({parse_time(c[ti]), std::stod(c[la]), std::stod(c[lo])});
    }
    return out;
}

// Speed, course, wind angle and VMG per fix, smoothed over window_s seconds.
std::vector<Sample> to_samples(const std::vector<Fix>& fixes, double wind_from_deg, double window_s = 8) {
    std::vector<Sample> out;
    size_t j = 0;
    for (size_t i = 0; i < fixes.size(); ++i) {
        const Fix& b = fixes[i];
        while (b.t - fixes[j].t > window_s) ++j;
        if (i == j) continue;
        const Fix& a = fixes[j];
        double sog = distance_m({a.lat, a.lng}, {b.lat, b.lng}) / (b.t - a.t) * KN;
        double cog = bearing_deg({a.lat, a.lng}, {b.lat, b.lng});
        double twa = wrap180(cog - wind_from_deg);
        out.push_back({b.t, b.lat, b.lng, sog, cog, twa, sog * std::cos(rad(twa))});
    }
    return out;
}

static LatLng mean_pos(const std::vector<Sample>& xs, size_t from, size_t to) {
    double lat = 0, lng = 0;
    for (size_t i = from; i < to; ++i) lat += xs[i].lat, lng += xs[i].lng;
    return {lat / static_cast<double>(to - from), lng / static_cast<double>(to - from)};
}

static std::optional<Maneuver> measure(const std::vector<Sample>& s, size_t i) {
    const double t0 = s[i].t;
    std::vector<Sample> before, win;
    for (const auto& x : s) {
        if (x.t >= t0 - 30 && x.t < t0 - 10) before.push_back(x);
        if (x.t >= t0 - 10 && x.t <= t0 + 20) win.push_back(x);
    }
    if (before.empty() || win.size() < 2) return std::nullopt;
    double abs_twa = 0, vmg = 0;
    for (const auto& x : before) abs_twa += std::fabs(x.twa_deg);
    const bool tack = abs_twa / static_cast<double>(before.size()) < 90;
    const int d = tack ? 1 : -1;
    for (const auto& x : before) vmg += d * x.vmg_kn;
    const double steady_mps = vmg / static_cast<double>(before.size()) / KN;
    const size_t k = std::min<size_t>(3, win.size());
    const LatLng a = mean_pos(win, 0, k), b = mean_pos(win, win.size() - k, win.size());  // average out GPS noise
    const double wind_from = before[0].cog_deg - before[0].twa_deg;
    const double made = d * distance_m(a, b) * std::cos(rad(bearing_deg(a, b) - wind_from));
    return Maneuver{t0, tack ? "tack" : "gybe", steady_mps * (win[win.size() - 2].t - win[1].t) - made};
}

// Tacks and gybes: the wind changes side and stays there for hold_s seconds.
// loss_m is the distance (upwind for tacks, downwind for gybes) lost against the VMG
// held before the turn, over a 30 s window around it.
std::vector<Maneuver> detect_maneuvers(const std::vector<Sample>& s, double hold_s = 15) {
    std::vector<Maneuver> out;
    int side = 0;
    long flip = -1;
    for (size_t i = 0; i < s.size(); ++i) {
        const int sd = s[i].twa_deg ? (std::signbit(s[i].twa_deg) ? -1 : 1) : side;
        if (!side) {
            side = sd;
        } else if (sd == side) {
            flip = -1;
        } else {
            if (flip < 0) flip = static_cast<long>(i);
            if (s[i].t - s[flip].t >= hold_s) {
                if (auto m = measure(s, static_cast<size_t>(flip))) out.push_back(*m);
                side = sd;
                flip = -1;
            }
        }
    }
    return out;
}

// Boat speed by true wind angle, both tacks combined.
std::vector<PolarPoint> build_polar(const std::vector<Sample>& s, int bin_deg = 10) {
    std::map<int, std::vector<double>> bins;
    const int last = static_cast<int>(std::ceil(180.0 / bin_deg)) - 1;
    for (const auto& x : s)
        bins[std::min(static_cast<int>(std::fabs(x.twa_deg) / bin_deg), last)].push_back(x.sog_kn);
    std::vector<PolarPoint> out;
    for (auto& [b, v] : bins) {
        std::sort(v.begin(), v.end());
        double sum = 0;
        for (double x : v) sum += x;
        const size_t n = v.size();
        out.push_back({b * bin_deg + bin_deg / 2.0, v[std::min(n - 1, n * 8 / 10)], sum / static_cast<double>(n),
                       static_cast<int>(n)});
    }
    return out;
}

// Split the race at each mark (rounded within round_radius_m) and summarise each leg.
std::vector<Leg> split_legs(const std::vector<Sample>& s, const std::vector<LatLng>& marks, double round_radius_m = 30) {
    std::vector<Leg> legs;
    size_t start = 0;
    double sailed = 0;
    for (size_t i = 1; i < s.size() && legs.size() < marks.size(); ++i) {
        sailed += s[i].sog_kn / KN * (s[i].t - s[i - 1].t);  // smoothed, so GPS jitter doesn't add distance
        const LatLng m = marks[legs.size()];
        if (distance_m({s[i].lat, s[i].lng}, m) > round_radius_m) continue;
        const double secs = s[i].t - s[start].t;
        const double direct = distance_m({s[start].lat, s[start].lng}, m);
        legs.push_back({static_cast<int>(legs.size()), secs, sailed, direct, direct / sailed, direct / secs * KN});
        start = i;
        sailed = 0;
    }
    return legs;
}

// Half-circle polar diagram (0 deg = head to wind at the top).
std::string polar_svg(const std::vector<PolarPoint>& polar, int size = 420, int min_samples = 3) {
    std::vector<PolarPoint> pts;
    double top = 2;
    for (const auto& p : polar)
        if (p.n >= min_samples && p.best_kn > 0) pts.push_back(p), top = std::max(top, p.best_kn);
    const int max_kn = static_cast<int>(std::ceil(top / 2)) * 2;
    const double r = size / 2.0 - 30, cx = 30, cy = size / 2.0;
    auto xy = [&](double d, double kn) {
        return std::pair<double, double>{cx + kn / max_kn * r * std::sin(rad(d)), cy - kn / max_kn * r * std::cos(rad(d))};
    };
    const char* label = R"(font-size="11" fill="#64748b")";
    std::ostringstream o;
    o << std::fixed << std::setprecision(1) << R"(<svg xmlns="http://www.w3.org/2000/svg" width=")" << size / 2 + 40
      << R"(" height=")" << size << R"(" font-family="sans-serif"><rect width="100%" height="100%" fill="white"/>)";
    for (int d : {0, 45, 90, 135, 180}) {
        auto [x, y] = xy(d, max_kn);
        o << R"(<line x1=")" << cx << R"(" y1=")" << cy << R"(" x2=")" << x << R"(" y2=")" << y << R"(" stroke="#e2e8f0"/>)"
          << R"(<text x=")" << x << R"(" y=")" << y << R"(" dx="4" dy=")" << (d == 0 ? 12 : d == 180 ? -4 : 4) << "\" "
          << label << ">" << d << "&#176;</text>";
    }
    for (int kn = 2; kn <= max_kn; kn += 2) {
        const double rr = kn * r / max_kn;
        o << R"(<path d="M)" << cx << " " << cy - rr << " A" << rr << " " << rr << " 0 0 1 " << cx << " " << cy + rr
          << R"(" fill="none" stroke="#cbd5e1"/><text x=")" << cx + rr << R"(" y=")" << cy + 14
          << R"(" text-anchor="middle" )" << label << ">" << kn << "</text>";
    }
    o << R"(<text x=")" << cx + r << R"(" y=")" << cy + 28 << R"(" text-anchor="end" )" << label
      << R"(>knots</text><polyline points=")";
    for (size_t i = 0; i < pts.size(); ++i) {
        auto [x, y] = xy(pts[i].twa_deg, pts[i].best_kn);
        o << (i ? " " : "") << x << "," << y;
    }
    o << R"(" fill="none" stroke="#2563eb" stroke-width="2.5"/></svg>)";
    return o.str();
}

// Samples, manoeuvres, polar and legs for one race.
Analysis analyze(const std::vector<Fix>& fixes, double wind_from_deg, const std::vector<LatLng>& marks = {}) {
    Analysis a;
    a.samples = to_samples(fixes, wind_from_deg);
    a.maneuvers = detect_maneuvers(a.samples);
    a.polar = build_polar(a.samples);
    a.legs = split_legs(a.samples, marks);
    return a;
}

}  // namespace sail

#ifndef SAIL_TRACK_NO_MAIN
int main(int argc, char** argv) {
    using namespace sail;
    auto usage = [&](int code) {
        std::cerr << "usage: " << argv[0]
                  << " TRACK.gpx|TRACK.csv --wind DEG [--mark LAT,LNG]... [--svg FILE] [--csv FILE]\n"
                     "  --wind  true wind direction it blows FROM, degrees\n"
                     "  --mark  each mark in rounding order\n"
                     "  --svg   write the polar diagram\n"
                     "  --csv   write per-second samples (e.g. for training data)\n";
        return code;
    };
    try {
        std::string track, svg_path, csv_path;
        std::optional<double> wind;
        std::vector<LatLng> marks;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i], v;
            if (a == "-h" || a == "--help") return usage(0);
            if (a.rfind("--", 0) == 0) {
                if (auto eq = a.find('='); eq != std::string::npos) v = a.substr(eq + 1), a.resize(eq);
                else if (i + 1 < argc) v = argv[++i];
                else return usage(2);
            }
            if (a == "--wind") wind = std::stod(v);
            else if (a == "--svg") svg_path = v;
            else if (a == "--csv") csv_path = v;
            else if (a == "--mark") {
                auto c = v.find(',');
                if (c == std::string::npos) return usage(2);
                marks.push_back({std::stod(v.substr(0, c)), std::stod(v.substr(c + 1))});
            } else if (a.rfind("--", 0) == 0 || !track.empty()) return usage(2);
            else track = a;
        }
        if (track.empty() || !wind) return usage(2);

        const bool gpx = lower(track).size() > 4 && lower(track).substr(track.size() - 4) == ".gpx";
        const auto fixes = gpx ? parse_gpx(track) : parse_csv(track);
        if (fixes.size() < 2) throw std::runtime_error("need at least 2 timed fixes in " + track);
        const Analysis r = analyze(fixes, *wind, marks);

        for (const auto& m : r.maneuvers) {
            std::time_t t = static_cast<std::time_t>(m.t);
            char hms[16];
            std::strftime(hms, sizeof hms, "%H:%M:%S", std::gmtime(&t));
            std::printf("%s  %-4s  lost %5.1f m\n", hms, m.kind.c_str(), m.loss_m);
        }
        for (const auto& l : r.legs)
            std::printf("leg %d: %.1f min, %.0f%% efficient, %.2f kn to mark\n", l.mark + 1, l.seconds / 60,
                        l.efficiency * 100, l.vmc_kn);
        std::printf("polar:");
        const char* sep = " ";
        for (const auto& p : r.polar)
            if (p.n >= 3) std::printf("%s%.0f\u00b0 %.1f kn", sep, p.twa_deg, p.best_kn), sep = ", ";
        std::printf("\n");

        if (!svg_path.empty()) std::ofstream(svg_path) << polar_svg(r.polar);
        if (!csv_path.empty()) {
            std::ofstream f(csv_path);
            f << "t,lat,lng,sog_kn,cog_deg,twa_deg,vmg_kn\n" << std::fixed;
            for (const auto& x : r.samples)
                f << std::setprecision(3) << x.t << "," << std::setprecision(7) << x.lat << "," << x.lng << ","
                  << std::setprecision(3) << x.sog_kn << "," << x.cog_deg << "," << x.twa_deg << "," << x.vmg_kn << "\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "sail_track: " << e.what() << "\n";
        return 1;
    }
}
#endif
