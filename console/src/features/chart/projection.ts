import type { Grid } from "../../shared/api/client";

export type Bounds = { latMin: number; latMax: number; lonMin: number; lonMax: number };

// Equirectangular projection fitted to a box: good to well under a percent across the lake.
export function projector(b: Bounds, width: number) {
  const k = Math.cos((((b.latMin + b.latMax) / 2) * Math.PI) / 180);
  const spanX = (b.lonMax - b.lonMin) * k;
  const spanY = b.latMax - b.latMin;
  const scale = width / spanX;
  const height = spanY * scale;
  const xy = (lat: number, lon: number): [number, number] => [(lon - b.lonMin) * k * scale, (b.latMax - lat) * scale];
  return { xy, width, height };
}

export function gridBounds(g: Grid): Bounds {
  return { latMin: g.lat0, latMax: g.lat0 + g.n * g.dlat, lonMin: g.lon0, lonMax: g.lon0 + g.n * g.dlon };
}

export function pad(b: Bounds, share: number): Bounds {
  const dl = (b.latMax - b.latMin) * share;
  const dn = (b.lonMax - b.lonMin) * share;
  return { latMin: b.latMin - dl, latMax: b.latMax + dl, lonMin: b.lonMin - dn, lonMax: b.lonMax + dn };
}

export function cellCenter(g: Grid, ix: number, iy: number): [number, number] {
  return [g.lat0 + (iy + 0.5) * g.dlat, g.lon0 + (ix + 0.5) * g.dlon];
}
