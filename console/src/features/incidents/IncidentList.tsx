import { useQuery } from "@tanstack/react-query";
import { motion } from "motion/react";

import { api, type Scenario } from "../../shared/api/client";
import { motionTokens } from "../../shared/theme/tokens";
import { EmptyState, ErrorState, Panel, SkeletonRows } from "../../shared/ui/States";
import { summary } from "./describe";

export function IncidentList({ selected, onSelect }: { selected?: string; onSelect: (s: Scenario) => void }) {
  const q = useQuery({ queryKey: ["scenarios"], queryFn: api.scenarios });
  return (
    <Panel title="Incidents" aside={<span className="text-xs text-muted">exercise scenarios</span>}>
      {q.isPending && <SkeletonRows rows={6} label="Loading incidents" />}
      {q.isError && <ErrorState title="Incidents could not be loaded" error={q.error} onRetry={() => q.refetch()} />}
      {q.isSuccess && q.data.length === 0 && (
        <EmptyState title="No incidents yet">Add a scenario file to scenarios/dev and reload.</EmptyState>
      )}
      {q.isSuccess && q.data.length > 0 && (
        <ul className="-mx-2 space-y-0.5" role="listbox" aria-label="Incidents">
          {q.data.map((s, i) => {
            const active = s.id === selected;
            return (
              <motion.li
                key={s.id}
                initial={{ opacity: 0, y: 6 }}
                animate={{ opacity: 1, y: 0 }}
                transition={{ ...motionTokens.reveal, delay: i * motionTokens.stagger * 0.5 }}
              >
                <button
                  type="button"
                  role="option"
                  aria-selected={active}
                  onClick={() => onSelect(s)}
                  className={`w-full rounded-[var(--radius-control)] px-2 py-2 text-left transition-colors ${
                    active ? "bg-rescue/10 ring-1 ring-rescue/50" : "hover:bg-sunk"
                  }`}
                >
                  <span className="block text-[14px] font-medium leading-snug">{s.description}</span>
                  <span className="mt-0.5 block text-xs leading-snug text-muted">{summary(s)}</span>
                </button>
              </motion.li>
            );
          })}
        </ul>
      )}
    </Panel>
  );
}
