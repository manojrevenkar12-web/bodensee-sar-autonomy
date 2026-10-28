#include "sar/dis.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>

#include "sar/geo.hpp"

namespace sar::dis {

namespace {

// DIS is big-endian (network order).
class Writer {
 public:
  template <class T>
  Writer& put(T v) {
    std::uint8_t raw[sizeof(T)];
    std::memcpy(raw, &v, sizeof(T));
    for (std::size_t i = 0; i < sizeof(T); ++i) b.push_back(raw[sizeof(T) - 1 - i]);
    return *this;
  }
  std::vector<std::uint8_t> b;
};

class Reader {
 public:
  explicit Reader(const std::vector<std::uint8_t>& p) : p_(p) {}
  template <class T>
  T get() {
    if (pos_ + sizeof(T) > p_.size()) throw std::invalid_argument("PDU too short");
    std::uint8_t raw[sizeof(T)];
    for (std::size_t i = 0; i < sizeof(T); ++i) raw[sizeof(T) - 1 - i] = p_[pos_ + i];
    pos_ += sizeof(T);
    T v;
    std::memcpy(&v, raw, sizeof(T));
    return v;
  }
  void skip(std::size_t n) { pos_ += n; }

 private:
  const std::vector<std::uint8_t>& p_;
  std::size_t pos_ = 0;
};

using Mat = std::array<std::array<double, 3>, 3>;

Mat mul(const Mat& a, const Mat& b) {
  Mat r{};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      for (int k = 0; k < 3; ++k) r[i][j] += a[i][k] * b[k][j];
  return r;
}

Mat transpose(const Mat& a) {
  Mat r{};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) r[i][j] = a[j][i];
  return r;
}

// Columns: north, east, down expressed in ECEF.
Mat ned_to_ecef(double lat_deg, double lon_deg) {
  const double sl = std::sin(lat_deg * kDeg), cl = std::cos(lat_deg * kDeg);
  const double so = std::sin(lon_deg * kDeg), co = std::cos(lon_deg * kDeg);
  return {{{-sl * co, -so, -cl * co}, {-sl * so, co, -cl * so}, {cl, 0.0, -sl}}};
}

Mat euler_zyx(double psi, double theta, double phi) {
  const double cps = std::cos(psi), sps = std::sin(psi), ct = std::cos(theta), st = std::sin(theta);
  const double cph = std::cos(phi), sph = std::sin(phi);
  const Mat rz{{{cps, -sps, 0}, {sps, cps, 0}, {0, 0, 1}}};
  const Mat ry{{{ct, 0, st}, {0, 1, 0}, {-st, 0, ct}}};
  const Mat rx{{{1, 0, 0}, {0, cph, -sph}, {0, sph, cph}}};
  return mul(rz, mul(ry, rx));
}

}  // namespace

std::vector<std::uint8_t> encode(const EntityState& e) {
  Writer w;
  // PDU header
  w.put<std::uint8_t>(7).put<std::uint8_t>(e.exercise).put<std::uint8_t>(1).put<std::uint8_t>(1);
  w.put<std::uint32_t>(e.timestamp).put<std::uint16_t>(kEntityStateLength).put<std::uint8_t>(0).put<std::uint8_t>(0);
  // entity id, force, variable parameters
  w.put(e.site).put(e.application).put(e.entity).put(e.force).put<std::uint8_t>(0);
  // entity type and alternative entity type
  for (int pass = 0; pass < 2; ++pass)
    w.put(e.type.kind).put(e.type.domain).put(e.type.country).put(e.type.category).put(e.type.subcategory)
        .put(e.type.specific).put(e.type.extra);
  for (float v : e.velocity) w.put(v);
  for (double v : e.location) w.put(v);
  for (float v : e.orientation) w.put(v);
  w.put(e.appearance);
  // dead reckoning: algorithm, 15 bytes of other parameters, linear acceleration, angular velocity
  w.put(e.dead_reckoning);
  for (int i = 0; i < 15; ++i) w.put<std::uint8_t>(0);
  for (int i = 0; i < 6; ++i) w.put<float>(0.0f);
  // marking: ASCII character set, 11 characters
  w.put<std::uint8_t>(1);
  for (std::size_t i = 0; i < 11; ++i)
    w.put<std::uint8_t>(i < e.marking.size() ? static_cast<std::uint8_t>(e.marking[i]) : 0);
  w.put(e.capabilities);
  if (w.b.size() != kEntityStateLength) throw std::logic_error("entity state length");
  return w.b;
}

EntityState decode(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < kEntityStateLength) throw std::invalid_argument("PDU too short");
  Reader r(bytes);
  EntityState e;
  const auto version = r.get<std::uint8_t>();
  e.exercise = r.get<std::uint8_t>();
  const auto type = r.get<std::uint8_t>();
  const auto family = r.get<std::uint8_t>();
  if (type != 1 || family != 1) throw std::invalid_argument("not an Entity State PDU");
  if (version < 6) throw std::invalid_argument("unsupported DIS version");
  e.timestamp = r.get<std::uint32_t>();
  const auto length = r.get<std::uint16_t>();
  r.skip(2);
  e.site = r.get<std::uint16_t>();
  e.application = r.get<std::uint16_t>();
  e.entity = r.get<std::uint16_t>();
  e.force = r.get<std::uint8_t>();
  const auto n_params = r.get<std::uint8_t>();
  if (length != kEntityStateLength + 16u * n_params || bytes.size() < length)
    throw std::invalid_argument("inconsistent PDU length");
  e.type.kind = r.get<std::uint8_t>();
  e.type.domain = r.get<std::uint8_t>();
  e.type.country = r.get<std::uint16_t>();
  e.type.category = r.get<std::uint8_t>();
  e.type.subcategory = r.get<std::uint8_t>();
  e.type.specific = r.get<std::uint8_t>();
  e.type.extra = r.get<std::uint8_t>();
  r.skip(8);
  for (auto& v : e.velocity) v = r.get<float>();
  for (auto& v : e.location) v = r.get<double>();
  for (auto& v : e.orientation) v = r.get<float>();
  e.appearance = r.get<std::uint32_t>();
  e.dead_reckoning = r.get<std::uint8_t>();
  r.skip(15 + 24);
  r.skip(1);
  e.marking.clear();
  for (int i = 0; i < 11; ++i) {
    const char c = static_cast<char>(r.get<std::uint8_t>());
    if (c == 0) {
      r.skip(static_cast<std::size_t>(10 - i));
      break;
    }
    e.marking.push_back(c);
  }
  e.capabilities = r.get<std::uint32_t>();
  return e;
}

void set_kinematics(EntityState& e, const Kinematics& k) {
  const auto p = geodetic_to_ecef(k.lat, k.lon, k.alt);
  e.location = {p[0], p[1], p[2]};
  const Mat n2e = ned_to_ecef(k.lat, k.lon);
  const double h = k.heading_deg * kDeg;
  const double vn = k.speed * std::cos(h), ve = k.speed * std::sin(h);
  for (int i = 0; i < 3; ++i) e.velocity[i] = static_cast<float>(n2e[i][0] * vn + n2e[i][1] * ve);
  const Mat body_to_ned{{{std::cos(h), -std::sin(h), 0}, {std::sin(h), std::cos(h), 0}, {0, 0, 1}}};
  const Mat r = mul(n2e, body_to_ned);
  e.orientation = {static_cast<float>(std::atan2(r[1][0], r[0][0])), static_cast<float>(std::asin(-r[2][0])),
                   static_cast<float>(std::atan2(r[2][1], r[2][2]))};
}

Kinematics get_kinematics(const EntityState& e) {
  Kinematics k;
  const auto g = ecef_to_geodetic(e.location[0], e.location[1], e.location[2]);
  k.lat = g[0];
  k.lon = g[1];
  k.alt = g[2];
  const Mat r = euler_zyx(e.orientation[0], e.orientation[1], e.orientation[2]);
  const Mat body_to_ned = mul(transpose(ned_to_ecef(k.lat, k.lon)), r);
  double heading = std::atan2(body_to_ned[1][0], body_to_ned[0][0]) / kDeg;
  if (heading < 0) heading += 360.0;
  k.heading_deg = heading;
  k.speed = std::sqrt(double(e.velocity[0]) * e.velocity[0] + double(e.velocity[1]) * e.velocity[1] +
                      double(e.velocity[2]) * e.velocity[2]);
  return k;
}

}  // namespace sar::dis
