import { describe, expect, it } from "vitest";

import { compass, reportedObject, summary } from "./describe";

describe("incident wording", () => {
  it("names wind directions the way a forecast does", () => {
    expect(compass(0)).toBe("north");
    expect(compass(45)).toBe("north-east");
    expect(compass(230)).toBe("south-west");
    expect(compass(359)).toBe("north");
    expect(compass(-90)).toBe("west");
  });
  it("says what may be in the water, most likely first", () => {
    expect(reportedObject({ piw: 1 })).toBe("Person in the water");
    expect(reportedObject({ sup_board: 0.3, piw: 0.7 })).toBe("Person in the water or paddle board");
  });
  it("summarises an incident in one plain sentence", () => {
    const s = summary({
      id: "x", description: "", objects: { piw: 1 }, time_of_day: "night", minutes_since: 29.6,
      wind: { wind_speed: 3.2, wind_from_deg: 50 }, uavs: { count: 3, endurance_min: 30, base: "konstanz" }, last_known: { lat: 47.6, lon: 9.3 },
    });
    expect(s).toBe("Person in the water, reported 30 min ago. Wind 3 m/s from the north-east, at night. 3 UAVs.");
  });
});
