#include "sar/mavlink.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace sar::mavlink {

namespace {

class Writer {
 public:
  explicit Writer(std::size_t n) { b_.reserve(n); }
  template <class T>
  Writer& put(T v) {
    std::uint8_t raw[sizeof(T)];
    std::memcpy(raw, &v, sizeof(T));  // little-endian hosts only (x86-64, arm64)
    b_.insert(b_.end(), raw, raw + sizeof(T));
    return *this;
  }
  std::vector<std::uint8_t> take() { return std::move(b_); }

 private:
  std::vector<std::uint8_t> b_;
};

class Reader {
 public:
  explicit Reader(const std::vector<std::uint8_t>& p) : p_(p) {}
  template <class T>
  T get() {
    if (pos_ + sizeof(T) > p_.size()) throw std::out_of_range("payload too short");
    T v;
    std::memcpy(&v, p_.data() + pos_, sizeof(T));
    pos_ += sizeof(T);
    return v;
  }

 private:
  const std::vector<std::uint8_t>& p_;
  std::size_t pos_ = 0;
};

struct Info {
  std::uint32_t id;
  std::uint8_t extra;
  std::size_t len;
};

constexpr Info kInfo[] = {
    {HEARTBEAT, 50, 9},          {GLOBAL_POSITION_INT, 104, 28}, {MISSION_COUNT, 221, 5},
    {MISSION_CLEAR_ALL, 232, 3}, {MISSION_ITEM_REACHED, 11, 2},  {MISSION_ACK, 153, 4},
    {MISSION_REQUEST_INT, 196, 5}, {MISSION_ITEM_INT, 38, 38},   {COMMAND_LONG, 152, 33},
};

const Info* info(std::uint32_t id) {
  for (const auto& i : kInfo)
    if (i.id == id) return &i;
  return nullptr;
}

}  // namespace

std::uint16_t crc_x25(const std::uint8_t* data, std::size_t n, std::uint16_t crc) {
  for (std::size_t i = 0; i < n; ++i) {
    std::uint8_t tmp = data[i] ^ static_cast<std::uint8_t>(crc & 0xFF);
    tmp ^= static_cast<std::uint8_t>(tmp << 4);
    crc = static_cast<std::uint16_t>((crc >> 8) ^ (static_cast<std::uint16_t>(tmp) << 8) ^
                                     (static_cast<std::uint16_t>(tmp) << 3) ^ (tmp >> 4));
  }
  return crc;
}

std::optional<std::uint8_t> crc_extra(std::uint32_t msgid) {
  const Info* i = info(msgid);
  if (!i) return std::nullopt;
  return i->extra;
}

std::optional<std::size_t> payload_length(std::uint32_t msgid) {
  const Info* i = info(msgid);
  if (!i) return std::nullopt;
  return i->len;
}

std::vector<std::uint8_t> encode(const Frame& f) {
  const auto extra = crc_extra(f.msgid);
  if (!extra) throw std::invalid_argument("unknown MAVLink message id");
  std::size_t len = f.payload.size();
  while (len > 1 && f.payload[len - 1] == 0) --len;  // v2: trailing zeros are not sent
  if (len > 255) throw std::invalid_argument("payload too long");
  std::vector<std::uint8_t> out;
  out.reserve(12 + len);
  out.push_back(kStx);
  out.push_back(static_cast<std::uint8_t>(len));
  out.push_back(0);  // incompat flags (no signing)
  out.push_back(0);  // compat flags
  out.push_back(f.seq);
  out.push_back(f.sysid);
  out.push_back(f.compid);
  out.push_back(static_cast<std::uint8_t>(f.msgid & 0xFF));
  out.push_back(static_cast<std::uint8_t>((f.msgid >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((f.msgid >> 16) & 0xFF));
  out.insert(out.end(), f.payload.begin(), f.payload.begin() + static_cast<std::ptrdiff_t>(len));
  std::uint16_t crc = crc_x25(out.data() + 1, out.size() - 1);
  crc = crc_x25(&*extra, 1, crc);
  out.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  out.push_back(static_cast<std::uint8_t>(crc >> 8));
  return out;
}

std::vector<Frame> Parser::feed(const std::uint8_t* data, std::size_t n) {
  buf_.insert(buf_.end(), data, data + n);
  std::vector<Frame> out;
  std::size_t i = 0;
  while (true) {
    while (i < buf_.size() && buf_[i] != kStx) ++i;  // resynchronise on the start byte
    if (buf_.size() - i < 12) break;
    const std::size_t len = buf_[i + 1];
    const std::uint8_t incompat = buf_[i + 2];
    const std::size_t sig = (incompat & 0x01) ? 13 : 0;
    const std::size_t total = 10 + len + 2 + sig;
    if (buf_.size() - i < total) break;
    const std::uint32_t msgid = buf_[i + 7] | (buf_[i + 8] << 8) | (static_cast<std::uint32_t>(buf_[i + 9]) << 16);
    const auto extra = crc_extra(msgid);
    if (!extra) {
      // a message we do not know, or a start byte that was not one: its checksum cannot be
      // verified, so skip one byte and look again rather than trust its length
      ++unknown_;
      ++i;
      continue;
    }
    std::uint16_t crc = crc_x25(buf_.data() + i + 1, 9 + len);
    crc = crc_x25(&*extra, 1, crc);
    const auto got = static_cast<std::uint16_t>(buf_[i + 10 + len] | (buf_[i + 11 + len] << 8));
    if (crc != got) {
      ++crc_errors_;
      ++i;  // not a frame after all; look for the next start byte
      continue;
    }
    Frame f;
    f.seq = buf_[i + 4];
    f.sysid = buf_[i + 5];
    f.compid = buf_[i + 6];
    f.msgid = msgid;
    f.payload.assign(buf_.begin() + static_cast<std::ptrdiff_t>(i + 10),
                     buf_.begin() + static_cast<std::ptrdiff_t>(i + 10 + len));
    f.payload.resize(std::max(len, *payload_length(msgid)), 0);  // undo truncation
    out.push_back(std::move(f));
    i += total;
  }
  buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(i));
  return out;
}

std::vector<std::uint8_t> pack(const Heartbeat& m) {
  return Writer(9).put(m.custom_mode).put(m.type).put(m.autopilot).put(m.base_mode).put(m.system_status).put(m.mavlink_version).take();
}
std::vector<std::uint8_t> pack(const GlobalPositionInt& m) {
  return Writer(28).put(m.time_boot_ms).put(m.lat).put(m.lon).put(m.alt).put(m.relative_alt).put(m.vx).put(m.vy).put(m.vz).put(m.hdg).take();
}
std::vector<std::uint8_t> pack(const MissionCount& m) {
  return Writer(5).put(m.count).put(m.target_system).put(m.target_component).put(m.mission_type).take();
}
std::vector<std::uint8_t> pack(const MissionRequestInt& m) {
  return Writer(5).put(m.seq).put(m.target_system).put(m.target_component).put(m.mission_type).take();
}
std::vector<std::uint8_t> pack(const MissionItemInt& m) {
  return Writer(38)
      .put(m.param1).put(m.param2).put(m.param3).put(m.param4).put(m.x).put(m.y).put(m.z).put(m.seq).put(m.command)
      .put(m.target_system).put(m.target_component).put(m.frame).put(m.current).put(m.autocontinue).put(m.mission_type)
      .take();
}
std::vector<std::uint8_t> pack(const MissionAck& m) {
  return Writer(4).put(m.target_system).put(m.target_component).put(m.type).put(m.mission_type).take();
}
std::vector<std::uint8_t> pack(const MissionItemReached& m) { return Writer(2).put(m.seq).take(); }
std::vector<std::uint8_t> pack(const CommandLong& m) {
  return Writer(33)
      .put(m.param1).put(m.param2).put(m.param3).put(m.param4).put(m.param5).put(m.param6).put(m.param7)
      .put(m.command).put(m.target_system).put(m.target_component).put(m.confirmation)
      .take();
}

Heartbeat unpack_heartbeat(const std::vector<std::uint8_t>& p) {
  Reader r(p);
  Heartbeat m;
  m.custom_mode = r.get<std::uint32_t>();
  m.type = r.get<std::uint8_t>();
  m.autopilot = r.get<std::uint8_t>();
  m.base_mode = r.get<std::uint8_t>();
  m.system_status = r.get<std::uint8_t>();
  m.mavlink_version = r.get<std::uint8_t>();
  return m;
}
GlobalPositionInt unpack_global_position_int(const std::vector<std::uint8_t>& p) {
  Reader r(p);
  GlobalPositionInt m;
  m.time_boot_ms = r.get<std::uint32_t>();
  m.lat = r.get<std::int32_t>();
  m.lon = r.get<std::int32_t>();
  m.alt = r.get<std::int32_t>();
  m.relative_alt = r.get<std::int32_t>();
  m.vx = r.get<std::int16_t>();
  m.vy = r.get<std::int16_t>();
  m.vz = r.get<std::int16_t>();
  m.hdg = r.get<std::uint16_t>();
  return m;
}
MissionCount unpack_mission_count(const std::vector<std::uint8_t>& p) {
  Reader r(p);
  MissionCount m;
  m.count = r.get<std::uint16_t>();
  m.target_system = r.get<std::uint8_t>();
  m.target_component = r.get<std::uint8_t>();
  m.mission_type = r.get<std::uint8_t>();
  return m;
}
MissionRequestInt unpack_mission_request_int(const std::vector<std::uint8_t>& p) {
  Reader r(p);
  MissionRequestInt m;
  m.seq = r.get<std::uint16_t>();
  m.target_system = r.get<std::uint8_t>();
  m.target_component = r.get<std::uint8_t>();
  m.mission_type = r.get<std::uint8_t>();
  return m;
}
MissionItemInt unpack_mission_item_int(const std::vector<std::uint8_t>& p) {
  Reader r(p);
  MissionItemInt m;
  m.param1 = r.get<float>();
  m.param2 = r.get<float>();
  m.param3 = r.get<float>();
  m.param4 = r.get<float>();
  m.x = r.get<std::int32_t>();
  m.y = r.get<std::int32_t>();
  m.z = r.get<float>();
  m.seq = r.get<std::uint16_t>();
  m.command = r.get<std::uint16_t>();
  m.target_system = r.get<std::uint8_t>();
  m.target_component = r.get<std::uint8_t>();
  m.frame = r.get<std::uint8_t>();
  m.current = r.get<std::uint8_t>();
  m.autocontinue = r.get<std::uint8_t>();
  m.mission_type = r.get<std::uint8_t>();
  return m;
}
MissionAck unpack_mission_ack(const std::vector<std::uint8_t>& p) {
  Reader r(p);
  MissionAck m;
  m.target_system = r.get<std::uint8_t>();
  m.target_component = r.get<std::uint8_t>();
  m.type = r.get<std::uint8_t>();
  m.mission_type = r.get<std::uint8_t>();
  return m;
}
MissionItemReached unpack_mission_item_reached(const std::vector<std::uint8_t>& p) {
  Reader r(p);
  MissionItemReached m;
  m.seq = r.get<std::uint16_t>();
  return m;
}
CommandLong unpack_command_long(const std::vector<std::uint8_t>& p) {
  Reader r(p);
  CommandLong m;
  m.param1 = r.get<float>();
  m.param2 = r.get<float>();
  m.param3 = r.get<float>();
  m.param4 = r.get<float>();
  m.param5 = r.get<float>();
  m.param6 = r.get<float>();
  m.param7 = r.get<float>();
  m.command = r.get<std::uint16_t>();
  m.target_system = r.get<std::uint8_t>();
  m.target_component = r.get<std::uint8_t>();
  m.confirmation = r.get<std::uint8_t>();
  return m;
}

}  // namespace sar::mavlink
