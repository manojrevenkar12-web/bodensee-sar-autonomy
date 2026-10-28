import { useQuery } from "@tanstack/react-query";
import { motion } from "motion/react";
import { useEffect, useState } from "react";

import { api, type Approval, type Debrief, type Lake, type Mission } from "../../shared/api/client";
import { motionTokens, plannerMeta } from "../../shared/theme/tokens";
import { ErrorState, Panel, Skeleton } from "../../shared/ui/States";
import { ApprovalPanel } from "../approval/ApprovalPanel";
import { SearchChart } from "../chart/SearchChart";
import { Timeline } from "../chart/Timeline";
import { DebriefPanel } from "../debrief/DebriefPanel";
import { PlanComparison } from "../plans/PlanComparison";

export function MissionView({ mission, lake }: { mission: Mission; lake: Lake }) {
  const best = [...mission.plans].sort((a, b) => b.expected_success - a.expected_success)[0]?.planner ?? "greedy";
  const [planner, setPlanner] = useState(best);
  const [index, setIndex] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [approval, setApproval] = useState<Approval>();
  const [debrief, setDebrief] = useState<Debrief>();
  const plan = useQuery({ queryKey: ["plan", mission.id, planner], queryFn: () => api.plan(mission.id, planner) });
  useEffect(() => {
    setIndex(0);
    setPlaying(true);
  }, [planner, mission.id]);
  const frame = plan.data?.frames[Math.min(index, (plan.data?.frames.length ?? 1) - 1)];
  const color = plannerMeta[planner]?.color ?? "var(--color-rescue)";
  return (
    <div className="grid min-h-0 flex-1 grid-cols-1 gap-4 lg:grid-cols-[minmax(0,1fr)_360px]">
      <motion.div initial={{ opacity: 0, y: 8 }} animate={{ opacity: 1, y: 0 }} transition={motionTokens.reveal} className="panel flex min-h-[420px] flex-col p-3">
        <div className="relative min-h-0 flex-1">
          {plan.isPending && <Skeleton className="absolute inset-0" />}
          {plan.isError && <ErrorState title="This plan could not be loaded" error={plan.error} onRetry={() => plan.refetch()} />}
          {plan.data && frame && (
            <SearchChart
              lake={lake}
              mission={mission}
              frame={frame}
              tracks={plan.data.tracks.slice(0, frame.t + 1)}
              color={color}
              truth={debrief?.truth_track}
            />
          )}
        </div>
        {plan.data && (
          <div className="mt-3">
            <Timeline frames={plan.data.frames} index={index} onIndex={setIndex} playing={playing} onPlaying={setPlaying} dtS={lake.dt_s} />
          </div>
        )}
      </motion.div>
      <div className="flex min-h-0 flex-col gap-4 overflow-y-auto">
        {[
          <Panel key="plans" title="Plans" aside={<span className="text-xs text-muted">flown ahead in the forecast</span>}>
            <PlanComparison mission={mission} selected={planner} onSelect={setPlanner} dtS={lake.dt_s} />
          </Panel>,
          <Panel key="approve" title="Approve and send">
            <ApprovalPanel missionId={mission.id} planner={planner} approved={approval} onApproved={setApproval} />
          </Panel>,
          <Panel key="debrief" title="Exercise debrief">
            <DebriefPanel missionId={mission.id} enabled={!!approval} result={debrief} onResult={setDebrief} />
          </Panel>,
        ].map((child, i) => (
          <motion.div key={i} initial={{ opacity: 0, x: 12 }} animate={{ opacity: 1, x: 0 }} transition={{ ...motionTokens.reveal, delay: (i + 1) * motionTokens.stagger }}>
            {child}
          </motion.div>
        ))}
      </div>
    </div>
  );
}
