"""Upload an approved search plan to a UAV's autopilot over MAVLink (UDP) and follow where it flies.
Framing and messages come from the C++ codec; the mission protocol runs here:
MISSION_COUNT -> (MISSION_REQUEST_INT -> MISSION_ITEM_INT)* -> MISSION_ACK, with timeouts and
retransmission, as PX4 and ArduPilot expect."""

from __future__ import annotations

import socket
import time
from dataclasses import dataclass, field

from sar import _sarsim as core

mav = core.mavlink

GCS_SYSID, GCS_COMPID = 255, 190


@dataclass
class Waypoint:
    lat: float
    lon: float
    alt_m: float = 40.0
    command: int = 16  # NAV_WAYPOINT


@dataclass
class UploadResult:
    ok: bool
    items: int
    retransmissions: int
    seconds: float
    error: str = ""
    log: list[str] = field(default_factory=list)


def mission_from_track(
    cells_geo: list[tuple[float, float]], base: tuple[float, float], alt_m: float = 40.0
) -> list[Waypoint]:
    """Take off at the base, fly the planned cells (merging straight runs), return to launch."""
    pts: list[tuple[float, float]] = []
    for p in cells_geo:
        if len(pts) >= 2:
            (a, b), (c, d) = pts[-2], pts[-1]
            # drop the middle point of three in a line
            if abs((c - a) * (p[1] - b) - (d - b) * (p[0] - a)) < 1e-12:
                pts[-1] = p
                continue
        if not pts or pts[-1] != p:
            pts.append(p)
    items = [Waypoint(base[0], base[1], alt_m, 22)]
    items += [Waypoint(lat, lon, alt_m) for lat, lon in pts]
    items.append(Waypoint(base[0], base[1], 0.0, 20))
    return items


class MavlinkLink:
    def __init__(
        self, host: str, port: int, target_system: int = 1, target_component: int = 1, timeout_s: float = 1.0
    ) -> None:
        self.addr = (host, port)
        self.target = (target_system, target_component)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(0.05)
        self.parser = mav.Parser()
        self.seq = 0
        self.timeout_s = timeout_s

    def close(self) -> None:
        self.sock.close()

    def send(self, msgid: int, payload: bytes) -> None:
        frame = mav.encode(msgid, payload, self.seq & 0xFF, GCS_SYSID, GCS_COMPID)
        self.seq += 1
        self.sock.sendto(frame, self.addr)

    def receive(self, wait_s: float) -> list[tuple[int, bytes]]:
        out: list[tuple[int, bytes]] = []
        deadline = time.monotonic() + wait_s
        while time.monotonic() < deadline:
            try:
                data, _ = self.sock.recvfrom(4096)
            except TimeoutError:
                continue
            for msgid, payload, _seq, _sys, _comp in self.parser.feed(data):
                out.append((msgid, payload))
            if out:
                break
        return out

    def upload(self, items: list[Waypoint], max_retries: int = 6) -> UploadResult:
        t0 = time.monotonic()
        log: list[str] = []
        retrans = 0
        count = mav.MissionCount()
        count.count = len(items)
        count.target_system, count.target_component = self.target
        last: tuple[int, bytes] = (44, mav.pack(count))
        self.send(*last)
        log.append(f"MISSION_COUNT {len(items)}")
        retries = 0
        while True:
            msgs = self.receive(self.timeout_s)
            if not msgs:
                if retries >= max_retries:
                    return UploadResult(
                        False, len(items), retrans, time.monotonic() - t0, "vehicle stopped answering", log
                    )
                retries += 1
                retrans += 1
                self.send(*last)
                log.append("timeout: resent")
                continue
            for msgid, payload in msgs:
                if msgid == 51:  # MISSION_REQUEST_INT
                    req = mav.unpack_mission_request_int(payload)
                    if req.seq >= len(items):
                        return UploadResult(
                            False, len(items), retrans, time.monotonic() - t0, f"vehicle asked for item {req.seq}", log
                        )
                    wp = items[req.seq]
                    it = mav.MissionItemInt()
                    it.seq = req.seq
                    it.command = wp.command
                    it.x = round(wp.lat * 1e7)
                    it.y = round(wp.lon * 1e7)
                    it.z = wp.alt_m
                    it.current = 1 if req.seq == 0 else 0
                    it.target_system, it.target_component = self.target
                    last = (73, mav.pack(it))
                    self.send(*last)
                    retries = 0
                    log.append(f"ITEM {req.seq}")
                elif msgid == 47:  # MISSION_ACK
                    ack = mav.unpack_mission_ack(payload)
                    ok = ack.type == 0
                    log.append(f"ACK {ack.type}")
                    return UploadResult(
                        ok, len(items), retrans, time.monotonic() - t0, "" if ok else f"rejected ({ack.type})", log
                    )

    def start(self) -> None:
        cmd = mav.CommandLong()
        cmd.command = 300  # MAV_CMD_MISSION_START
        cmd.target_system, cmd.target_component = self.target
        self.send(76, mav.pack(cmd))

    def positions(self, wait_s: float) -> list[tuple[float, float, float, float]]:
        """GLOBAL_POSITION_INT reports received within wait_s: (lat, lon, rel_alt_m, heading_deg)."""
        out = []
        deadline = time.monotonic() + wait_s
        while time.monotonic() < deadline:
            for msgid, payload in self.receive(max(0.0, deadline - time.monotonic())):
                if msgid == 33:
                    p = mav.unpack_global_position_int(payload)
                    out.append((p.lat / 1e7, p.lon / 1e7, p.relative_alt / 1000.0, p.hdg / 100.0))
        return out
