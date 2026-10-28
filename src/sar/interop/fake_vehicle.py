"""A stand-in autopilot for tests and the demo, written with pymavlink (an independent MAVLink
implementation, so it checks our codec end to end). It accepts a mission by the standard
protocol, can drop packets to exercise retransmission, then flies the mission and reports
GLOBAL_POSITION_INT and MISSION_ITEM_REACHED."""

from __future__ import annotations

import math
import random
import socket
import threading
import time

from pymavlink.dialects.v20 import common as mavlink2


class FakeVehicle:
    def __init__(self, port: int = 0, drop_rate: float = 0.0, speed_mps: float = 200.0, seed: int = 0, sysid: int = 1) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", port))
        self.sock.settimeout(0.05)
        self.port = self.sock.getsockname()[1]
        self.mav = mavlink2.MAVLink(None, srcSystem=sysid, srcComponent=1)
        self.mav.robust_parsing = True
        self.drop = drop_rate
        self.rng = random.Random(seed)
        self.speed = speed_mps  # fast-forward flight for tests
        self.items: list = []
        self.expected = 0
        self.accepted = False
        self.reached: list[int] = []
        self.peer: tuple[str, int] | None = None
        self.received_frames = 0
        self.dropped = 0
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self.flying = False

    def __enter__(self) -> FakeVehicle:
        self._thread.start()
        return self

    def __exit__(self, *exc: object) -> None:
        self._stop.set()
        self._thread.join(timeout=2)
        self.sock.close()

    def _send(self, msg) -> None:  # type: ignore[no-untyped-def]
        if self.peer is None:
            return
        self.sock.sendto(msg.pack(self.mav), self.peer)

    def _handle(self, msg) -> None:  # type: ignore[no-untyped-def]
        t = msg.get_type()
        if t == "MISSION_COUNT":
            self.expected = msg.count
            self.items = []
            self.accepted = False
            self._send(self.mav.mission_request_int_encode(msg.get_srcSystem(), msg.get_srcComponent(), 0))
        elif t == "MISSION_ITEM_INT" and not self.accepted:
            if msg.seq == len(self.items):
                self.items.append(msg)
            if len(self.items) < self.expected:
                self._send(self.mav.mission_request_int_encode(msg.get_srcSystem(), msg.get_srcComponent(), len(self.items)))
            else:
                self.accepted = True
                self._send(self.mav.mission_ack_encode(msg.get_srcSystem(), msg.get_srcComponent(), 0))
        elif t == "COMMAND_LONG" and msg.command == 300 and self.accepted:
            self.flying = True
            threading.Thread(target=self._fly, daemon=True).start()

    def _fly(self) -> None:
        lat, lon = self.items[0].x / 1e7, self.items[0].y / 1e7
        boot = time.monotonic()
        for i, item in enumerate(self.items):
            tlat, tlon = item.x / 1e7, item.y / 1e7
            dist = math.hypot((tlat - lat) * 110540, (tlon - lon) * 111320 * math.cos(math.radians(lat)))
            steps = max(1, int(dist / self.speed / 0.02))
            for k in range(1, steps + 1):
                if self._stop.is_set():
                    return
                la = lat + (tlat - lat) * k / steps
                lo = lon + (tlon - lon) * k / steps
                hdg = int((math.degrees(math.atan2((tlon - lon), (tlat - lat))) % 360) * 100)
                self._send(self.mav.global_position_int_encode(int((time.monotonic() - boot) * 1000), int(la * 1e7), int(lo * 1e7), 400000, int(item.z * 1000), 0, 0, 0, hdg))
                time.sleep(0.02)
            lat, lon = tlat, tlon
            self.reached.append(i)
            self._send(self.mav.mission_item_reached_encode(i))
        self.flying = False

    def _loop(self) -> None:
        while not self._stop.is_set():
            try:
                data, addr = self.sock.recvfrom(4096)
            except TimeoutError:
                continue
            except OSError:
                return
            self.peer = addr
            if self.drop and self.rng.random() < self.drop:
                self.dropped += 1
                continue
            for b in data:
                msg = self.mav.parse_char(bytes([b]))
                if msg is not None:
                    self.received_frames += 1
                    self._handle(msg)
