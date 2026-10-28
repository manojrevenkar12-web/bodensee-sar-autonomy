# 0004: MAVLink to vehicles, DIS to other simulators

## Context

The plan must reach real autopilots, and the simulation must exchange entities with other
simulators and hardware in the loop.

## Decision

- **MAVLink v2** (PX4, ArduPilot): an approved plan becomes a mission (takeoff, the planned
  cells with straight runs merged, return to launch) uploaded by the mission protocol with
  timeouts and retransmission; positions come back as GLOBAL_POSITION_INT. Framing, CRC-16/
  MCRF4XX with CRC_EXTRA and payload truncation are implemented in C++ and checked against
  pymavlink in both directions; a pymavlink stand-in autopilot drops packets in tests.
- **DIS v7** (IEEE 1278.1): UAVs are published as Entity State PDUs (ECEF position, Euler
  angles); surface entities from other simulators (a rescue boat) become extra searchers whose
  looks update the belief. Our PDUs are byte-identical to Open-DIS's.

## Consequences

- Not flown against a real autopilot or a real DIS exercise in this repository.
