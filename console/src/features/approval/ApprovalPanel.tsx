import { useMutation } from "@tanstack/react-query";
import { AnimatePresence, motion } from "motion/react";
import { useState } from "react";

import { api, type Approval } from "../../shared/api/client";
import { motionTokens, plannerMeta } from "../../shared/theme/tokens";
import { ErrorState } from "../../shared/ui/States";

type Props = { missionId: number; planner: string; onApproved: (a: Approval) => void; approved?: Approval };

export function ApprovalPanel({ missionId, planner, onApproved, approved }: Props) {
  const [approver, setApprover] = useState("");
  const [note, setNote] = useState("");
  const m = useMutation({ mutationFn: () => api.approve(missionId, planner, approver.trim(), note.trim()), onSuccess: onApproved });
  const label = plannerMeta[planner]?.label ?? planner;
  if (approved) {
    const ok = approved.uploads.filter((u) => u.ok).length;
    return (
      <div aria-live="polite">
        <p className="text-sm">
          <span className="font-semibold">{plannerMeta[approved.planner]?.label ?? approved.planner}</span> approved.{" "}
          {approved.uploads.length === 0
            ? "No UAVs are connected, so nothing was sent."
            : `Sent to ${ok} of ${approved.uploads.length} UAVs.`}
        </p>
        <ul className="mt-2 space-y-1">
          <AnimatePresence>
            {approved.uploads.map((u, i) => (
              <motion.li
                key={u.uav}
                initial={{ opacity: 0, x: -8 }}
                animate={{ opacity: 1, x: 0 }}
                transition={{ ...motionTokens.reveal, delay: i * motionTokens.stagger }}
                className="flex items-center justify-between rounded-[var(--radius-control)] bg-sunk px-2 py-1.5 text-sm"
              >
                <span>
                  UAV {u.uav + 1} <span className="text-muted">{u.vehicle}</span>
                </span>
                <span className={u.ok ? "font-medium text-found" : "font-medium text-alert"}>
                  {u.ok ? `${u.items} waypoints loaded${u.retransmissions ? `, ${u.retransmissions} resent` : ""}` : u.error}
                </span>
              </motion.li>
            ))}
          </AnimatePresence>
        </ul>
      </div>
    );
  }
  return (
    <form
      className="space-y-2"
      onSubmit={(e) => {
        e.preventDefault();
        if (approver.trim().length >= 2) m.mutate();
      }}
    >
      <p className="text-sm text-ink-soft">The plan goes to the UAVs only after you approve it. The approval is recorded and cannot be edited.</p>
      <label className="block text-sm">
        <span className="font-medium">Your name</span>
        <input
          value={approver}
          onChange={(e) => setApprover(e.target.value)}
          required
          minLength={2}
          maxLength={80}
          autoComplete="name"
          className="mt-1 w-full rounded-[var(--radius-control)] border border-line bg-panel px-2 py-1.5"
        />
      </label>
      <label className="block text-sm">
        <span className="font-medium">Note</span>
        <input value={note} onChange={(e) => setNote(e.target.value)} maxLength={500} className="mt-1 w-full rounded-[var(--radius-control)] border border-line bg-panel px-2 py-1.5" />
      </label>
      {m.isError && <ErrorState title="The plan was not approved" error={m.error} onRetry={() => m.mutate()} />}
      <button
        type="submit"
        disabled={m.isPending || approver.trim().length < 2}
        className="w-full rounded-[var(--radius-control)] bg-rescue px-3 py-2 font-semibold text-white disabled:opacity-50"
      >
        {m.isPending ? "Sending to the UAVs" : `Approve ${label} and send`}
      </button>
    </form>
  );
}
