// IEEE 1278.1-2012 (DIS v7) Entity State PDU: how simulators on a shared exercise network tell
// each other where their entities are. Positions are earth-centred (ECEF, metres), orientations
// are Euler angles of the body in that frame. Checked against Open-DIS.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sar::dis {

constexpr std::size_t kEntityStateLength = 144;

struct EntityType {
  std::uint8_t kind = 1, domain = 2;
  std::uint16_t country = 78;  // Germany
  std::uint8_t category = 0, subcategory = 0, specific = 0, extra = 0;
};

struct EntityState {
  std::uint8_t exercise = 1;
  std::uint32_t timestamp = 0;
  std::uint16_t site = 1, application = 1, entity = 1;
  std::uint8_t force = 1;  // friendly
  EntityType type;
  std::array<float, 3> velocity{};      // ECEF m/s
  std::array<double, 3> location{};     // ECEF m
  std::array<float, 3> orientation{};   // psi, theta, phi (rad)
  std::uint32_t appearance = 0;
  std::uint8_t dead_reckoning = 2;      // DRM(F, P, W)
  std::string marking;                  // up to 11 ASCII characters
  std::uint32_t capabilities = 0;
};

std::vector<std::uint8_t> encode(const EntityState& e);
// Throws std::invalid_argument for anything that is not a well-formed Entity State PDU.
EntityState decode(const std::vector<std::uint8_t>& bytes);

// Geodetic position, heading (deg from north) and ground speed to ECEF location, velocity and
// DIS Euler angles; and back.
struct Kinematics {
  double lat = 0, lon = 0, alt = 0;
  double heading_deg = 0, speed = 0;
};
void set_kinematics(EntityState& e, const Kinematics& k);
Kinematics get_kinematics(const EntityState& e);

}  // namespace sar::dis
