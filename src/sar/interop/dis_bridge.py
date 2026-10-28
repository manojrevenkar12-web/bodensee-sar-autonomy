"""Share the search with other simulators over DIS (IEEE 1278.1, UDP): publish the UAVs as
Entity State PDUs, and take in entities from others, e.g. a rescue boat in a ship simulator,
which the search then counts as an extra searcher (its looks update the belief)."""

from __future__ import annotations

import socket
from dataclasses import dataclass

from sar import _sarsim as core

dis = core.dis

# DIS enumerations (SISO-REF-010): kind 1 = platform; domain 2 = air, 3 = surface; Germany = 78.
UAV_TYPE = (1, 2, 78, 50, 0, 0, 0)
BOAT_TYPE = (1, 3, 78, 61, 0, 0, 0)


@dataclass
class Entity:
    site: int
    application: int
    entity: int
    marking: str
    lat: float
    lon: float
    alt: float
    heading_deg: float
    speed: float
    kind: int
    domain: int


def entity_state(entity_id: int, marking: str, lat: float, lon: float, alt: float, heading_deg: float, speed: float,
                 etype: tuple[int, ...] = UAV_TYPE, exercise: int = 1, site: int = 42, application: int = 7) -> bytes:
    e = dis.EntityState()
    e.exercise, e.site, e.application, e.entity = exercise, site, application, entity_id
    t = dis.EntityType()
    t.kind, t.domain, t.country, t.category, t.subcategory, t.specific, t.extra = etype
    e.type = t
    e.marking = marking[:11]
    k = dis.Kinematics()
    k.lat, k.lon, k.alt, k.heading_deg, k.speed = lat, lon, alt, heading_deg, speed
    dis.set_kinematics(e, k)
    return dis.encode(e)


def parse(data: bytes) -> Entity | None:
    try:
        e = dis.decode(data)
    except ValueError:
        return None
    k = dis.get_kinematics(e)
    return Entity(e.site, e.application, e.entity, e.marking, k.lat, k.lon, k.alt, k.heading_deg, k.speed, e.type.kind, e.type.domain)


class DisSocket:
    """One UDP socket for both directions; on an exercise network this is usually broadcast."""

    def __init__(self, bind_port: int = 0, peer: tuple[str, int] | None = None, broadcast: bool = False) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        if broadcast:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self.sock.bind(("0.0.0.0" if broadcast else "127.0.0.1", bind_port))
        self.sock.settimeout(0.05)
        self.port = self.sock.getsockname()[1]
        self.peer = peer
        self.rejected = 0

    def publish(self, pdu: bytes) -> None:
        if self.peer:
            self.sock.sendto(pdu, self.peer)

    def poll(self, max_pdus: int = 64) -> list[Entity]:
        out = []
        for _ in range(max_pdus):
            try:
                data, _ = self.sock.recvfrom(2048)
            except (TimeoutError, BlockingIOError):
                break
            ent = parse(data)
            if ent is None:
                self.rejected += 1
            else:
                out.append(ent)
        return out

    def close(self) -> None:
        self.sock.close()


def external_scans(entities: list[Entity], sweep_width_m: float = 20.0) -> list[core.ExternalScan]:
    """Surface entities (boats) become searchers in the simulation."""
    from sar.sim.lake import projection

    proj = projection()
    scans = []
    for e in entities:
        if e.kind == 1 and e.domain == 3:
            p = proj.to_local(e.lat, e.lon)
            scans.append(core.ExternalScan(p.x, p.y, sweep_width_m))
    return scans
