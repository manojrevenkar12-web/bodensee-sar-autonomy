// Paint a belief grid (0-255, rows south to north) into a small image the chart scales up.
import { chartTokens } from "../../shared/theme/tokens";

function cssColor(name: string): [number, number, number] {
  const fallback: [number, number, number] = [227, 86, 10];
  if (typeof window === "undefined") return fallback;
  const v = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  const m = /^#?([0-9a-f]{6})$/i.exec(v);
  if (!m) return fallback;
  const n = parseInt(m[1]!, 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

export function heatImage(belief: number[][]): string {
  const n = belief.length;
  if (n === 0 || typeof document === "undefined") return "";
  const canvas = document.createElement("canvas");
  canvas.width = n;
  canvas.height = n;
  const ctx = canvas.getContext("2d");
  if (!ctx) return "";
  const img = ctx.createImageData(n, n);
  const [r, g, b] = cssColor("--color-heat");
  for (let iy = 0; iy < n; iy++) {
    const row = belief[iy]!;
    for (let ix = 0; ix < n; ix++) {
      const v = (row[ix] ?? 0) / 255;
      const o = ((n - 1 - iy) * n + ix) * 4; // image rows run north to south
      img.data[o] = r;
      img.data[o + 1] = g;
      img.data[o + 2] = b;
      img.data[o + 3] = Math.round(255 * chartTokens.heatAlphaMax * Math.pow(v, chartTokens.heatGamma));
    }
  }
  ctx.putImageData(img, 0, 0);
  return canvas.toDataURL();
}
