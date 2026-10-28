import { useMutation, useQuery } from "@tanstack/react-query";
import { motion } from "motion/react";
import { useEffect, useState } from "react";

import { IncidentList } from "../features/incidents/IncidentList";
import { MissionView } from "../features/mission/MissionView";
import { api, type Scenario } from "../shared/api/client";
import { motionTokens } from "../shared/theme/tokens";
import { EmptyState, ErrorState, Panel, Skeleton } from "../shared/ui/States";

type Theme = "day" | "night";

function initialTheme(): Theme {
  try {
    const saved = localStorage.getItem("chart-theme");
    if (saved === "day" || saved === "night") return saved;
  } catch {
    /* storage blocked */
  }
  return window.matchMedia?.("(prefers-color-scheme: dark)").matches ? "night" : "day";
}

export function App() {
  const [theme, setTheme] = useState<Theme>(initialTheme);
  const [scenario, setScenario] = useState<Scenario>();
  useEffect(() => {
    document.documentElement.dataset.theme = theme;
    try {
      localStorage.setItem("chart-theme", theme);
    } catch {
      /* storage blocked */
    }
  }, [theme]);
  const lake = useQuery({ queryKey: ["lake"], queryFn: api.lake, staleTime: Infinity });
  const open = useMutation({ mutationFn: (s: Scenario) => api.createMission(s.id) });
  const choose = (s: Scenario) => {
    setScenario(s);
    open.mutate(s);
  };
  return (
    <div className="flex h-dvh flex-col gap-4 p-4">
      <motion.header initial={{ opacity: 0, y: -6 }} animate={{ opacity: 1, y: 0 }} transition={motionTokens.reveal} className="flex items-end justify-between gap-4">
        <div>
          <h1 className="text-2xl font-semibold tracking-tight">Bodensee search planner</h1>
          <p className="text-sm text-ink-soft">Plan a UAV search for someone in the water, compare plans, approve one and send it.</p>
        </div>
        <button
          type="button"
          onClick={() => setTheme(theme === "day" ? "night" : "day")}
          className="shrink-0 whitespace-nowrap rounded-[var(--radius-control)] border border-line bg-panel px-3 py-1.5 text-sm font-medium"
          aria-label={`Switch to the ${theme === "day" ? "night" : "day"} chart`}
        >
          {theme === "day" ? "Night chart" : "Day chart"}
        </button>
      </motion.header>
      <div className="grid min-h-0 flex-1 grid-cols-1 gap-4 lg:grid-cols-[300px_minmax(0,1fr)]">
        <motion.div initial={{ opacity: 0, x: -12 }} animate={{ opacity: 1, x: 0 }} transition={motionTokens.reveal} className="min-h-0 overflow-y-auto">
          <IncidentList selected={scenario?.id} onSelect={choose} />
        </motion.div>
        <main className="flex min-h-0 flex-col">
          {!scenario && (
            <Panel title="Search area">
              <EmptyState title="Choose an incident">The planners fly the search ahead in the drift forecast, and you pick the plan to send.</EmptyState>
            </Panel>
          )}
          {scenario && (open.isPending || lake.isPending) && (
            <div className="grid flex-1 grid-cols-1 gap-4 lg:grid-cols-[minmax(0,1fr)_360px]" role="status" aria-label="Planning the search">
              <Skeleton className="min-h-[420px]" />
              <div className="space-y-4">
                <Skeleton className="h-64" />
                <Skeleton className="h-40" />
              </div>
            </div>
          )}
          {scenario && open.isError && <ErrorState title="The search could not be planned" error={open.error} onRetry={() => open.mutate(scenario)} />}
          {scenario && lake.isError && <ErrorState title="The lake chart could not be loaded" error={lake.error} onRetry={() => lake.refetch()} />}
          {open.data && lake.data && <MissionView key={open.data.id} mission={open.data} lake={lake.data} />}
        </main>
      </div>
    </div>
  );
}
