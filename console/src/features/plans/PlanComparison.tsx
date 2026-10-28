import type { Mission } from "../../shared/api/client";
import { curveTokens as C, plannerMeta } from "../../shared/theme/tokens";

type Props = { mission: Mission; selected: string; onSelect: (p: string) => void; dtS: number };

export function PlanComparison({ mission, selected, onSelect, dtS }: Props) {
  const steps = Math.max(1, ...mission.plans.map((p) => p.pos_curve.length));
  const minutes = (steps * dtS) / 60;
  const x = (i: number) => C.padLeft + ((C.width - C.padLeft - C.padRight) * i) / steps;
  const y = (v: number) => C.padTop + (C.height - C.padTop - C.padBottom) * (1 - v);
  const best = [...mission.plans].sort((a, b) => b.expected_success - a.expected_success)[0];
  return (
    <div>
      <svg viewBox={`0 0 ${C.width} ${C.height}`} className="w-full" role="img" aria-label="Expected probability of finding the object over time, per plan">
        {C.ticks.map((t) => (
          <g key={t}>
            <line x1={C.padLeft} x2={C.width - C.padRight} y1={y(t)} y2={y(t)} stroke="var(--color-line)" strokeWidth={1} />
            <text x={C.padLeft - 6} y={y(t) + 4} textAnchor="end" className="fill-muted" style={{ font: "11px var(--font-sans)" }}>
              {Math.round(t * 100)}%
            </text>
          </g>
        ))}
        {[0, 0.5, 1].map((s) => (
          <text key={s} x={x(s * steps)} y={C.height - 6} textAnchor="middle" className="fill-muted" style={{ font: "11px var(--font-sans)" }}>
            {Math.round(s * minutes)} min
          </text>
        ))}
        {mission.plans.map((p) => {
          const meta = plannerMeta[p.planner];
          const on = p.planner === selected;
          const d = [0, ...p.pos_curve].map((v, i) => `${i ? "L" : "M"}${x(i).toFixed(1)},${y(v).toFixed(1)}`).join("");
          return (
            <path
              key={p.planner}
              d={d}
              fill="none"
              stroke={meta?.color ?? "var(--color-ink)"}
              strokeWidth={on ? C.selectedLineWidth : C.lineWidth}
              strokeOpacity={on ? 1 : 0.55}
              strokeDasharray={meta?.dashed ? "5 4" : undefined}
            />
          );
        })}
      </svg>
      <div role="radiogroup" aria-label="Plans" className="mt-2 space-y-1">
        {mission.plans.map((p) => {
          const meta = plannerMeta[p.planner];
          const on = p.planner === selected;
          return (
            <button
              key={p.planner}
              type="button"
              role="radio"
              aria-checked={on}
              onClick={() => onSelect(p.planner)}
              className={`flex w-full items-center gap-3 rounded-[var(--radius-control)] px-2 py-1.5 text-left ${on ? "bg-sunk ring-1 ring-line" : "hover:bg-sunk"}`}
            >
              <span className="h-2.5 w-2.5 shrink-0 rounded-full" style={{ background: meta?.color }} />
              <span className="flex-1">
                <span className="block text-sm font-medium">
                  {meta?.label ?? p.planner}
                  {best?.planner === p.planner && <span className="ml-2 text-xs font-semibold text-found">most likely to find</span>}
                </span>
                <span className="block text-xs text-muted">{meta?.note}</span>
              </span>
              <span className="text-lg font-semibold tabular-nums">{Math.round(p.expected_success * 100)}%</span>
            </button>
          );
        })}
      </div>
      <p className="mt-2 text-xs text-muted">
        Expected chance of finding the object within {Math.round(minutes)} minutes, from the drift forecast. It is a forecast: in testing it ran higher
        than what the searches really achieved.
      </p>
    </div>
  );
}
