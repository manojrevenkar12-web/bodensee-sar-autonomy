import { useMutation } from "@tanstack/react-query";

import { api, type Debrief } from "../../shared/api/client";
import { plannerMeta } from "../../shared/theme/tokens";
import { ErrorState } from "../../shared/ui/States";

type Props = { missionId: number; enabled: boolean; result?: Debrief; onResult: (d: Debrief) => void };

export function DebriefPanel({ missionId, enabled, result, onResult }: Props) {
  const m = useMutation({ mutationFn: () => api.debrief(missionId), onSuccess: onResult });
  if (!enabled) return <p className="text-sm text-muted">After a plan is approved, the exercise can show what really happened.</p>;
  if (result) {
    return (
      <div>
        <p className="text-sm">
          The green dotted line is where the object really drifted{result.beached ? "; it washed ashore" : ""}.
        </p>
        <table className="mt-2 w-full text-sm">
          <tbody>
            {Object.entries(result.planners).map(([p, r]) => (
              <tr key={p} className="border-t border-line">
                <td className="py-1.5">{plannerMeta[p]?.label ?? p}</td>
                <td className={`py-1.5 text-right font-medium ${r.found ? "text-found" : "text-ink-soft"}`}>
                  {r.found ? `found at minute ${Math.round(r.found_minute ?? 0)}` : "not found in time"}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    );
  }
  return (
    <div className="space-y-2">
      {m.isError && <ErrorState title="The debrief failed" error={m.error} onRetry={() => m.mutate()} />}
      <button
        type="button"
        onClick={() => m.mutate()}
        disabled={m.isPending}
        className="w-full rounded-[var(--radius-control)] border border-ink px-3 py-2 font-semibold disabled:opacity-50"
      >
        {m.isPending ? "Replaying against the truth" : "Show what really happened"}
      </button>
    </div>
  );
}
