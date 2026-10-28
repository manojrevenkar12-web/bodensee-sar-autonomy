import { describe, expect, it } from "vitest";

import { cellCenter, gridBounds, pad, projector } from "./projection";

const grid = { lat0: 47.6, lon0: 9.3, dlat: 0.00135, dlon: 0.002, n: 32, cell_m: 150 };

describe("chart projection", () => {
  it("maps the box corners to the frame corners", () => {
    const b = gridBounds(grid);
    const p = projector(b, 1000);
    expect(p.xy(b.latMax, b.lonMin)).toEqual([0, 0]);
    const [x, y] = p.xy(b.latMin, b.lonMax);
    expect(x).toBeCloseTo(1000);
    expect(y).toBeCloseTo(p.height);
  });
  it("keeps metres square: a square grid stays square", () => {
    const p = projector(gridBounds(grid), 1000);
    expect(p.height / p.width).toBeCloseTo((32 * 0.00135) / (32 * 0.002 * Math.cos(((47.6 + 47.6 + 32 * 0.00135) / 2) * (Math.PI / 180))), 6);
  });
  it("pads boxes and finds cell centres", () => {
    const b = pad({ latMin: 0, latMax: 1, lonMin: 0, lonMax: 2 }, 0.5);
    expect(b).toEqual({ latMin: -0.5, latMax: 1.5, lonMin: -1, lonMax: 3 });
    const [la, lo] = cellCenter(grid, 0, 0);
    expect(la).toBeCloseTo(47.6 + 0.000675);
    expect(lo).toBeCloseTo(9.3 + 0.001);
  });
});
