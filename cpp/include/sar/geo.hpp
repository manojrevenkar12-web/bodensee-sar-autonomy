// Geometry: a local east/north plane around the lake, polygons, and WGS84 earth-centred
// coordinates for DIS.
#pragma once

#include <array>
#include <cmath>
#include <vector>

namespace sar {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

struct Vec2 {
  double x = 0.0;  // east, metres
  double y = 0.0;  // north, metres
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double k) { return {a.x * k, a.y * k}; }
inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double norm(Vec2 a) { return std::sqrt(dot(a, a)); }

struct LatLon {
  double lat = 0.0;  // degrees
  double lon = 0.0;  // degrees
};

// Equirectangular projection about an origin. Over Lake Constance (60 km) the scale error is
// below 0.5%, far smaller than the drift uncertainty it carries.
class Projection {
 public:
  Projection() = default;
  Projection(double lat0, double lon0)
      : lat0_(lat0), lon0_(lon0), kx_(111320.0 * std::cos(lat0 * kDeg)), ky_(110540.0) {}

  Vec2 to_local(LatLon p) const { return {(p.lon - lon0_) * kx_, (p.lat - lat0_) * ky_}; }
  LatLon to_geo(Vec2 p) const { return {lat0_ + p.y / ky_, lon0_ + p.x / kx_}; }
  double lat0() const { return lat0_; }
  double lon0() const { return lon0_; }

 private:
  double lat0_ = 0.0, lon0_ = 0.0, kx_ = 1.0, ky_ = 1.0;
};

// Even-odd rule; the polygon is implicitly closed.
inline bool point_in_polygon(const std::vector<Vec2>& poly, Vec2 p) {
  bool inside = false;
  const std::size_t n = poly.size();
  for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
    const Vec2 a = poly[i], b = poly[j];
    if ((a.y > p.y) != (b.y > p.y)) {
      const double x_cross = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
      if (p.x < x_cross) inside = !inside;
    }
  }
  return inside;
}

// Distance from p to the segment a-b.
inline double distance_to_segment(Vec2 p, Vec2 a, Vec2 b) {
  const Vec2 ab = b - a;
  const double len2 = dot(ab, ab);
  if (len2 <= 1e-12) return norm(p - a);
  double t = dot(p - a, ab) / len2;
  t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
  return norm(p - (a + ab * t));
}

// WGS84 geodetic (degrees, metres) to earth-centred, earth-fixed metres.
inline std::array<double, 3> geodetic_to_ecef(double lat_deg, double lon_deg, double h) {
  constexpr double a = 6378137.0;
  constexpr double f = 1.0 / 298.257223563;
  constexpr double e2 = f * (2.0 - f);
  const double lat = lat_deg * kDeg, lon = lon_deg * kDeg;
  const double n = a / std::sqrt(1.0 - e2 * std::sin(lat) * std::sin(lat));
  return {(n + h) * std::cos(lat) * std::cos(lon), (n + h) * std::cos(lat) * std::sin(lon),
          (n * (1.0 - e2) + h) * std::sin(lat)};
}

// Inverse by Bowring's iteration; converges to millimetres in a few steps near the surface.
inline std::array<double, 3> ecef_to_geodetic(double x, double y, double z) {
  constexpr double a = 6378137.0;
  constexpr double f = 1.0 / 298.257223563;
  constexpr double e2 = f * (2.0 - f);
  const double lon = std::atan2(y, x);
  const double p = std::sqrt(x * x + y * y);
  double lat = std::atan2(z, p * (1.0 - e2));
  double h = 0.0;
  for (int i = 0; i < 6; ++i) {
    const double n = a / std::sqrt(1.0 - e2 * std::sin(lat) * std::sin(lat));
    h = p / std::cos(lat) - n;
    lat = std::atan2(z, p * (1.0 - e2 * n / (n + h)));
  }
  return {lat / kDeg, lon / kDeg, h};
}

}  // namespace sar
