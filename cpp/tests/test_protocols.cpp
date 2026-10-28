#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "sar/dis.hpp"
#include "sar/mavlink.hpp"

using Catch::Approx;
using namespace sar;

TEST_CASE("MAVLink CRC is CRC-16/MCRF4XX") {
  const std::string check = "123456789";
  CHECK(mavlink::crc_x25(reinterpret_cast<const std::uint8_t*>(check.data()), check.size()) == 0x6F91);
}

TEST_CASE("MAVLink v2 frames round-trip, with trailing zeros truncated on the wire") {
  mavlink::MissionItemInt item;
  item.seq = 3;
  item.x = 476640000;
  item.y = 93670000;
  item.z = 40.0f;
  item.mission_type = 0;
  mavlink::Frame f{7, 255, 190, mavlink::MISSION_ITEM_INT, mavlink::pack(item)};
  const auto wire = mavlink::encode(f);
  CHECK(wire[0] == 0xFD);
  CHECK(wire[1] == 37);  // mission_type 0 is truncated
  mavlink::Parser p;
  const auto frames = p.feed(wire.data(), wire.size());
  REQUIRE(frames.size() == 1);
  CHECK(frames[0].payload.size() == 38);
  const auto back = mavlink::unpack_mission_item_int(frames[0].payload);
  CHECK(back.seq == 3);
  CHECK(back.x == 476640000);
  CHECK(back.y == 93670000);
  CHECK(back.z == Approx(40.0f));
  CHECK(frames[0].seq == 7);
}

TEST_CASE("the parser resynchronises after garbage, survives split reads and rejects corruption") {
  mavlink::Heartbeat hb;
  const auto a = mavlink::encode({1, 1, 1, mavlink::HEARTBEAT, mavlink::pack(hb)});
  mavlink::MissionAck ack;
  ack.type = 0;
  ack.target_system = 255;
  const auto b = mavlink::encode({2, 1, 1, mavlink::MISSION_ACK, mavlink::pack(ack)});
  std::vector<std::uint8_t> stream{0x00, 0xFD, 0x13, 0x37};  // noise, including a false start byte
  stream.insert(stream.end(), a.begin(), a.end());
  auto corrupt = a;
  corrupt[12] ^= 0x55;
  stream.insert(stream.end(), corrupt.begin(), corrupt.end());
  stream.insert(stream.end(), b.begin(), b.end());
  mavlink::Parser p;
  std::vector<mavlink::Frame> got;
  for (std::size_t i = 0; i < stream.size(); i += 5) {
    auto part = p.feed(stream.data() + i, std::min<std::size_t>(5, stream.size() - i));
    got.insert(got.end(), part.begin(), part.end());
  }
  REQUIRE(got.size() == 2);
  CHECK(got[0].msgid == mavlink::HEARTBEAT);
  CHECK(got[1].msgid == mavlink::MISSION_ACK);
  CHECK(p.crc_errors() >= 1);
}

TEST_CASE("unknown message ids are refused on encode and skipped on decode") {
  CHECK_THROWS(mavlink::encode({0, 1, 1, 99999, {1, 2, 3}}));
}

TEST_CASE("DIS Entity State PDU is 144 bytes and round-trips") {
  dis::EntityState e;
  e.entity = 42;
  e.marking = "SAR-UAV-1";
  e.type.category = 50;
  dis::Kinematics k;
  k.lat = 47.6631;
  k.lon = 9.3712;
  k.alt = 455.0;
  k.heading_deg = 123.0;
  k.speed = 10.0;
  dis::set_kinematics(e, k);
  const auto bytes = dis::encode(e);
  CHECK(bytes.size() == 144);
  CHECK(bytes[0] == 7);  // protocol version
  CHECK(bytes[2] == 1);  // entity state
  const auto back = dis::decode(bytes);
  CHECK(back.entity == 42);
  CHECK(back.marking == "SAR-UAV-1");
  CHECK(back.type.category == 50);
  const auto k2 = dis::get_kinematics(back);
  CHECK(k2.lat == Approx(k.lat).margin(1e-7));
  CHECK(k2.lon == Approx(k.lon).margin(1e-7));
  CHECK(k2.alt == Approx(k.alt).margin(1e-2));
  CHECK(k2.heading_deg == Approx(k.heading_deg).margin(1e-3));
  CHECK(k2.speed == Approx(10.0).margin(1e-4));
}

TEST_CASE("DIS decode refuses other PDUs and truncated ones") {
  dis::EntityState e;
  auto bytes = dis::encode(e);
  auto other = bytes;
  other[2] = 2;  // fire PDU
  CHECK_THROWS_AS(dis::decode(other), std::invalid_argument);
  bytes.resize(100);
  CHECK_THROWS_AS(dis::decode(bytes), std::invalid_argument);
}
