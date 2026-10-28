// MAVLink v2 framing and the messages needed to upload a search mission to an autopilot and to
// follow its position (PX4 and ArduPilot speak this). Checked byte for byte against pymavlink.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sar::mavlink {

constexpr std::uint8_t kStx = 0xFD;

enum MsgId : std::uint32_t {
  HEARTBEAT = 0,
  GLOBAL_POSITION_INT = 33,
  MISSION_COUNT = 44,
  MISSION_CLEAR_ALL = 45,
  MISSION_ITEM_REACHED = 46,
  MISSION_ACK = 47,
  MISSION_REQUEST_INT = 51,
  MISSION_ITEM_INT = 73,
  COMMAND_LONG = 76,
};

enum Command : std::uint16_t {
  NAV_WAYPOINT = 16,
  NAV_RETURN_TO_LAUNCH = 20,
  NAV_TAKEOFF = 22,
  MISSION_START = 300,
};

constexpr std::uint8_t FRAME_GLOBAL_RELATIVE_ALT_INT = 6;
constexpr std::uint8_t MISSION_ACCEPTED = 0;

struct Frame {
  std::uint8_t seq = 0;
  std::uint8_t sysid = 255;
  std::uint8_t compid = 190;
  std::uint32_t msgid = 0;
  std::vector<std::uint8_t> payload;  // full length (zero-extended after decoding)
};

std::uint16_t crc_x25(const std::uint8_t* data, std::size_t n, std::uint16_t crc = 0xFFFF);
// CRC_EXTRA seed and full payload length of a known message; nullopt for unknown ids.
std::optional<std::uint8_t> crc_extra(std::uint32_t msgid);
std::optional<std::size_t> payload_length(std::uint32_t msgid);

// Encode with v2 trailing-zero truncation.
std::vector<std::uint8_t> encode(const Frame& f);

// Streaming parser: feed bytes as they arrive, take whole, checked frames out.
class Parser {
 public:
  std::vector<Frame> feed(const std::uint8_t* data, std::size_t n);
  std::size_t crc_errors() const { return crc_errors_; }
  std::size_t unknown() const { return unknown_; }

 private:
  std::vector<std::uint8_t> buf_;
  std::size_t crc_errors_ = 0;
  std::size_t unknown_ = 0;
};

struct Heartbeat {
  std::uint32_t custom_mode = 0;
  std::uint8_t type = 2, autopilot = 0, base_mode = 0, system_status = 4, mavlink_version = 3;
};
struct GlobalPositionInt {
  std::uint32_t time_boot_ms = 0;
  std::int32_t lat = 0, lon = 0, alt = 0, relative_alt = 0;  // degE7, mm
  std::int16_t vx = 0, vy = 0, vz = 0;                         // cm/s north, east, down
  std::uint16_t hdg = 0;                                       // cdeg
};
struct MissionCount {
  std::uint16_t count = 0;
  std::uint8_t target_system = 1, target_component = 1, mission_type = 0;
};
struct MissionRequestInt {
  std::uint16_t seq = 0;
  std::uint8_t target_system = 0, target_component = 0, mission_type = 0;
};
struct MissionItemInt {
  float param1 = 0, param2 = 0, param3 = 0, param4 = 0;
  std::int32_t x = 0, y = 0;  // degE7
  float z = 0;                // metres, frame-relative
  std::uint16_t seq = 0, command = NAV_WAYPOINT;
  std::uint8_t target_system = 1, target_component = 1, frame = FRAME_GLOBAL_RELATIVE_ALT_INT, current = 0,
               autocontinue = 1, mission_type = 0;
};
struct MissionAck {
  std::uint8_t target_system = 0, target_component = 0, type = MISSION_ACCEPTED, mission_type = 0;
};
struct MissionItemReached {
  std::uint16_t seq = 0;
};
struct CommandLong {
  float param1 = 0, param2 = 0, param3 = 0, param4 = 0, param5 = 0, param6 = 0, param7 = 0;
  std::uint16_t command = 0;
  std::uint8_t target_system = 1, target_component = 1, confirmation = 0;
};

std::vector<std::uint8_t> pack(const Heartbeat& m);
std::vector<std::uint8_t> pack(const GlobalPositionInt& m);
std::vector<std::uint8_t> pack(const MissionCount& m);
std::vector<std::uint8_t> pack(const MissionRequestInt& m);
std::vector<std::uint8_t> pack(const MissionItemInt& m);
std::vector<std::uint8_t> pack(const MissionAck& m);
std::vector<std::uint8_t> pack(const MissionItemReached& m);
std::vector<std::uint8_t> pack(const CommandLong& m);

Heartbeat unpack_heartbeat(const std::vector<std::uint8_t>& p);
GlobalPositionInt unpack_global_position_int(const std::vector<std::uint8_t>& p);
MissionCount unpack_mission_count(const std::vector<std::uint8_t>& p);
MissionRequestInt unpack_mission_request_int(const std::vector<std::uint8_t>& p);
MissionItemInt unpack_mission_item_int(const std::vector<std::uint8_t>& p);
MissionAck unpack_mission_ack(const std::vector<std::uint8_t>& p);
MissionItemReached unpack_mission_item_reached(const std::vector<std::uint8_t>& p);
CommandLong unpack_command_long(const std::vector<std::uint8_t>& p);

}  // namespace sar::mavlink
