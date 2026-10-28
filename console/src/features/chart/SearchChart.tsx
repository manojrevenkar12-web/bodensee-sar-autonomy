import { AnimatePresence, motion } from "motion/react";
import { useMemo } from "react";

import type { Frame, Lake, Mission } from "../../shared/api/client";
import { chartTokens, motionTokens } from "../../shared/theme/tokens";
import { heatImage } from "./heat";
import { cellCenter, gridBounds, pad, projector } from "./projection";

const VIEW_WIDTH = 1000;

type Props = {
  lake: Lake;
  mission: Mission;
  frame: Frame;
  tracks: [number, number][][]; // per step, per UAV, up to the current time
  color: string;
  truth?: [number, number][];
};

function path(points: [number, number][]): string {
  return points.map(([x, y], i) => `${i ? "L" : "M"}${x.toFixed(1)},${y.toFixed(1)}`).join("");
}

export function SearchChart({ lake, mission, frame, tracks, color, truth }: Props) {
  const g = mission.start.grid;
  const view = pad(gridBounds(g), chartTokens.contextPad);
  const proj = useMemo(() => projector(view, VIEW_WIDTH), [view.latMin, view.latMax, view.lonMin, view.lonMax]);
  const shore = useMemo(() => path(lake.shoreline.map(([la, lo]) => proj.xy(la, lo))) + "Z", [lake, proj]);
  const heat = useMemo(() => heatImage(frame.belief), [frame.belief]);
  const [gx0, gy1] = proj.xy(g.lat0 + g.n * g.dlat, g.lon0);
  const [gx1, gy0] = proj.xy(g.lat0, g.lon0 + g.n * g.dlon);
  const nUav = frame.uavs.length;
  const uavPaths = Array.from({ length: nUav }, (_, k) =>
    path(tracks.map((step) => {
      const [ix, iy] = step[k]!;
      const [la, lo] = cellCenter(g, ix, iy);
      return proj.xy(la, lo);
    })),
  );
  const [lkx, lky] = proj.xy(mission.start.lkp.lat, mission.start.lkp.lon);
  const [bx, by] = proj.xy(mission.start.base.lat, mission.start.base.lon);
  const truthPts = (truth ?? []).map(([la, lo]) => proj.xy(la, lo));
  const label = { font: "600 13px var(--font-sans)", paintOrder: "stroke", stroke: "var(--color-water)", strokeWidth: 4, strokeLinejoin: "round" } as const;
  return (
    <svg viewBox={`0 0 ${proj.width} ${proj.height}`} className="h-full w-full" role="img" aria-label="Search area chart">
      <defs>
        <pattern id="restricted" width="10" height="10" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">
          <line x1="0" y1="0" x2="0" y2="10" stroke="var(--color-restricted)" strokeWidth="1.6" strokeOpacity="0.55" />
        </pattern>
        <clipPath id="frame">
          <rect width={proj.width} height={proj.height} />
        </clipPath>
      </defs>
      <g clipPath="url(#frame)">
        <rect width={proj.width} height={proj.height} fill="var(--color-land)" />
        <path d={shore} fill="var(--color-water)" stroke="var(--color-water-edge)" strokeWidth={chartTokens.shoreWidth} />
        <text x={proj.width * 0.04} y={proj.height * 0.07} className="fill-ink-soft" style={{ font: "italic 22px var(--font-chart)" }}>
          Bodensee
        </text>
        <AnimatePresence initial={false}>
          <motion.image
            key={heat}
            href={heat}
            x={gx0}
            y={gy1}
            width={gx1 - gx0}
            height={gy0 - gy1}
            preserveAspectRatio="none"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={{ opacity: 0 }}
            transition={motionTokens.swap}
          />
        </AnimatePresence>
        <rect x={gx0} y={gy1} width={gx1 - gx0} height={gy0 - gy1} fill="none" stroke="var(--color-ink-soft)" strokeDasharray="6 5" strokeWidth={1} />
        {lake.no_fly.map((z) => (
          <g key={z.name}>
            <path d={path(z.polygon.map(([la, lo]) => proj.xy(la, lo))) + "Z"} fill="url(#restricted)" stroke="var(--color-restricted)" strokeWidth={1.2} />
            <title>{z.name}: no flying</title>
          </g>
        ))}
        {uavPaths.map((d, k) => (
          <path key={k} d={d} fill="none" stroke={color} strokeWidth={chartTokens.trackWidth} strokeOpacity={0.85} strokeLinejoin="round" />
        ))}
        {truthPts.length > 0 && (
          <g aria-label="Where the object really drifted">
            {truthPts.length > 1 && (
              <motion.path
                d={path(truthPts)}
                fill="none"
                stroke="var(--color-found)"
                strokeWidth={3}
                strokeDasharray="2 6"
                strokeLinecap="round"
                initial={{ pathLength: 0 }}
                animate={{ pathLength: 1 }}
                transition={{ duration: 1.2 }}
              />
            )}
            <circle cx={truthPts[truthPts.length - 1]![0]} cy={truthPts[truthPts.length - 1]![1]} r={8} fill="none" stroke="var(--color-found)" strokeWidth={2.5} />
            <text x={truthPts[truthPts.length - 1]![0] + 11} y={truthPts[truthPts.length - 1]![1] + 4} className="fill-found" style={label}>
              object
            </text>
          </g>
        )}
        <g aria-label="Last known position">
          <path d={`M${lkx - 7},${lky}h14M${lkx},${lky - 7}v14`} stroke="var(--color-ink)" strokeWidth={2} />
          <text x={lkx + 9} y={lky - 8} className="fill-ink" style={label}>
            last seen
          </text>
        </g>
        <g aria-label={`Launch site ${mission.start.base.name}`}>
          <rect x={bx - 5} y={by - 5} width={10} height={10} fill="var(--color-ink)" />
          <text x={bx + 9} y={by + 4} className="fill-ink" style={label}>
            {mission.start.base.name}
          </text>
        </g>
        {frame.uavs.map(([ix, iy], k) => {
          const [x, y] = proj.xy(...cellCenter(g, ix, iy));
          const active = frame.active[k] === 1;
          return (
            <motion.g key={k} animate={{ x, y }} transition={{ duration: motionTokens.playbackMsPerFrame / 1000, ease: "linear" }}>
              <circle r={chartTokens.uavRadius + 3} fill="var(--color-panel)" opacity={active ? 0.9 : 0.5} />
              <circle r={chartTokens.uavRadius} fill={active ? color : "var(--color-muted)"} />
              <text y={-11} textAnchor="middle" className="fill-ink" style={{ ...label, font: "600 12px var(--font-sans)" }}>
                {k + 1}
              </text>
            </motion.g>
          );
        })}
      </g>
    </svg>
  );
}
