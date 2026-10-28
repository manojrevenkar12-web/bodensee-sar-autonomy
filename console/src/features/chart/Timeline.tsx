import { useEffect } from "react";

import { motionTokens } from "../../shared/theme/tokens";

type Props = { frames: { t: number; pos: number }[]; index: number; onIndex: (i: number) => void; playing: boolean; onPlaying: (p: boolean) => void; dtS: number };

export function Timeline({ frames, index, onIndex, playing, onPlaying, dtS }: Props) {
  const last = frames.length - 1;
  useEffect(() => {
    if (!playing) return;
    if (index >= last) {
      onPlaying(false);
      return;
    }
    const id = window.setTimeout(() => onIndex(index + 1), motionTokens.playbackMsPerFrame);
    return () => window.clearTimeout(id);
  }, [playing, index, last, onIndex, onPlaying]);
  const f = frames[index];
  const minute = f ? (f.t * dtS) / 60 : 0;
  return (
    <div className="flex items-center gap-3">
      <button
        type="button"
        onClick={() => {
          if (index >= last) onIndex(0);
          onPlaying(!playing);
        }}
        className="w-20 rounded-[var(--radius-control)] bg-ink px-3 py-1.5 text-sm font-semibold text-canvas hover:opacity-90"
      >
        {playing ? "Pause" : index >= last ? "Replay" : "Play"}
      </button>
      <input
        type="range"
        min={0}
        max={last}
        value={index}
        onChange={(e) => {
          onPlaying(false);
          onIndex(Number(e.target.value));
        }}
        aria-label="Time into the search"
        className="flex-1 accent-[var(--color-rescue)]"
      />
      <p className="w-56 text-right text-sm text-ink-soft" aria-live="polite">
        minute <span className="font-semibold text-ink">{minute.toFixed(0)}</span>, expected found{" "}
        <span className="font-semibold text-ink">{f ? Math.round(f.pos * 100) : 0}%</span>
      </p>
    </div>
  );
}
