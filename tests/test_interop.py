"""Interfaces to real systems, checked against independent implementations: pymavlink for
MAVLink, Open-DIS for DIS."""

import io

import numpy as np
import pytest
from opendis.DataInputStream import DataInputStream
from opendis.DataOutputStream import DataOutputStream
from opendis.dis7 import EntityStatePdu
from pymavlink.dialects.v20 import common as m2

from sar import _sarsim as core
from sar.interop.dis_bridge import BOAT_TYPE, DisSocket, entity_state, external_scans, parse
from sar.interop.fake_vehicle import FakeVehicle
from sar.interop.mavlink_link import MavlinkLink, mission_from_track
from sar.sim.build import make_env
from sar.sim.lake import projection

mav = core.mavlink


def pymav():
    m = m2.MAVLink(None, srcSystem=255, srcComponent=190)
    m.robust_parsing = True
    return m


def parse_with_pymavlink(frame: bytes):
    p = pymav()
    msgs = [x for x in (p.parse_char(bytes([b])) for b in frame) if x is not None]
    assert len(msgs) == 1
    return msgs[0]


def test_our_frames_decode_in_pymavlink_field_by_field():
    item = mav.MissionItemInt()
    item.seq, item.command, item.x, item.y, item.z = 4, 16, 476550000, 93700000, 40.0
    item.target_system, item.target_component = 3, 1
    msg = parse_with_pymavlink(mav.encode(73, mav.pack(item), 9, 255, 190))
    assert msg.get_type() == "MISSION_ITEM_INT"
    assert (msg.seq, msg.command, msg.x, msg.y, msg.z, msg.target_system) == (4, 16, 476550000, 93700000, 40.0, 3)
    count = mav.MissionCount()
    count.count, count.target_system = 12, 2
    msg = parse_with_pymavlink(mav.encode(44, mav.pack(count)))
    assert (msg.get_type(), msg.count, msg.target_system) == ("MISSION_COUNT", 12, 2)
    pos = mav.GlobalPositionInt()
    pos.lat, pos.lon, pos.relative_alt, pos.hdg, pos.vx = 476640000, 93670000, 40000, 12345, -150
    msg = parse_with_pymavlink(mav.encode(33, mav.pack(pos)))
    assert (msg.lat, msg.lon, msg.relative_alt, msg.hdg, msg.vx) == (476640000, 93670000, 40000, 12345, -150)


@pytest.mark.parametrize(
    "build,unpack,check",
    [
        (lambda m: m.mission_request_int_encode(255, 190, 7), "unpack_mission_request_int", lambda x: x.seq == 7),
        (
            lambda m: m.mission_ack_encode(255, 190, 0),
            "unpack_mission_ack",
            lambda x: x.type == 0 and x.target_system == 255,
        ),
        (lambda m: m.mission_item_reached_encode(5), "unpack_mission_item_reached", lambda x: x.seq == 5),
        (
            lambda m: m.global_position_int_encode(1000, 476000000, 94000000, 400000, 40000, 1, 2, 3, 9000),
            "unpack_global_position_int",
            lambda x: (x.lat, x.lon, x.hdg) == (476000000, 94000000, 9000),
        ),
    ],
)
def test_pymavlink_frames_decode_in_our_parser(build, unpack, check):
    m = pymav()
    frame = build(m).pack(m)
    p = mav.Parser()
    got = p.feed(frame)
    assert len(got) == 1 and p.crc_errors == 0
    assert check(getattr(mav, unpack)(got[0][1]))


def test_mission_upload_survives_packet_loss():
    zigzag = [(47.65 + 0.001 * i, 9.37 + 0.001 * (i % 2)) for i in range(12)]
    wps = mission_from_track(zigzag, (47.664, 9.367))
    assert len(wps) == 14
    with FakeVehicle(drop_rate=0.35, seed=11) as v:
        link = MavlinkLink("127.0.0.1", v.port, timeout_s=0.3)
        r = link.upload(wps)
        link.close()
    assert r.ok, r.log
    assert r.retransmissions > 0
    assert [(i.x, i.y) for i in v.items] == [(round(w.lat * 1e7), round(w.lon * 1e7)) for w in wps]
    assert v.items[0].command == 22 and v.items[-1].command == 20  # takeoff ... return to launch


def test_upload_reports_a_silent_vehicle():
    link = MavlinkLink("127.0.0.1", 9, timeout_s=0.05)  # discard port: nobody answers
    r = link.upload(mission_from_track([(47.65, 9.37)], (47.664, 9.367)), max_retries=2)
    link.close()
    assert not r.ok and "stopped answering" in r.error


def test_vehicle_flies_the_mission_and_reports_positions():
    with FakeVehicle(seed=2) as v:
        link = MavlinkLink("127.0.0.1", v.port, timeout_s=0.3)
        assert link.upload(mission_from_track([(47.655, 9.37), (47.656, 9.371)], (47.664, 9.367))).ok
        link.start()
        pos = link.positions(1.5)
        link.close()
    assert len(pos) > 10
    lat, lon, alt, _ = pos[-1]
    assert abs(lat - 47.664) < 0.02 and abs(lon - 9.367) < 0.02 and 0 <= alt <= 40.0


def test_straight_runs_are_merged_into_one_leg():
    track = [(47.65 + 0.001 * i, 9.37) for i in range(10)]
    wps = mission_from_track(track, (47.664, 9.367))
    assert len(wps) == 4  # takeoff, start of the run, end of the run, return to launch


def test_our_entity_state_is_byte_identical_to_open_dis():
    ours = entity_state(5, "SAR-UAV-1", 47.66, 9.37, 455.0, 90.0, 10.0)
    pdu = EntityStatePdu()
    pdu.parse(DataInputStream(io.BytesIO(ours)))
    assert pdu.entityID.entityID == 5 and pdu.pduType == 1 and pdu.protocolVersion == 7
    out = io.BytesIO()
    pdu.serialize(DataOutputStream(out))
    assert out.getvalue() == ours


def test_open_dis_entities_are_read_back():
    pdu = EntityStatePdu()
    # Open-DIS defaults these to None or bytes, which it cannot serialise itself
    pdu.pduStatus, pdu.forceId, pdu.entityAppearance, pdu.capabilities = 0, 1, 0, 0
    pdu.length, pdu.pduType, pdu.protocolFamily = 144, 1, 1  # nor the header our decoder checks
    ecef = core.dis.geodetic_to_ecef(47.6, 9.5, 0.0)
    pdu.entityLocation.x, pdu.entityLocation.y, pdu.entityLocation.z = ecef
    pdu.entityID.entityID = 77
    pdu.entityType.entityKind, pdu.entityType.domain = 1, 3
    out = io.BytesIO()
    pdu.serialize(DataOutputStream(out))
    e = parse(out.getvalue())
    assert e is not None and e.entity == 77 and (e.kind, e.domain) == (1, 3)
    assert e.lat == pytest.approx(47.6, abs=1e-7) and e.lon == pytest.approx(9.5, abs=1e-7)


def test_garbage_on_the_exercise_network_is_ignored():
    assert parse(b"\x07\x01\x02\x01" + bytes(140)) is None  # a fire PDU
    assert parse(b"hello") is None


def test_a_rescue_boat_from_another_simulator_joins_the_search(spec):
    env_a, env_b = make_env(spec, 6), make_env(spec, 6)
    p = env_a.belief()
    iy, ix = np.unravel_index(int(np.argmax(p)), p.shape)
    c = env_a.cell_center(int(ix), int(iy))
    lat, lon = projection().to_geo(c.x, c.y)
    rx, tx = DisSocket(), DisSocket()
    tx.peer = ("127.0.0.1", rx.port)
    tx.publish(entity_state(900, "DLRG-BOAT", lat, lon, 0.0, 45.0, 4.0, BOAT_TYPE))
    tx.publish(entity_state(901, "SAR-UAV-2", lat, lon, 60.0, 45.0, 10.0))  # air: not a surface searcher
    import time

    time.sleep(0.1)
    ents = rx.poll()
    rx.close()
    tx.close()
    assert {e.marking for e in ents} == {"DLRG-BOAT", "SAR-UAV-2"}
    scans = external_scans(ents)
    assert len(scans) == 1
    ia = env_a.step([0, 0, 0], scans)
    ib = env_b.step([0, 0, 0])
    assert ia.reward > ib.reward
