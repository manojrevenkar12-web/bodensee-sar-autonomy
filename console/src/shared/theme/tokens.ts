// Layout and motion constants used from TypeScript (colours live in theme.css as CSS variables).

export const motionTokens = {
  reveal: { duration: 0.42, ease: [0.22, 1, 0.36, 1] as const },
  stagger: 0.07,
  swap: { duration: 0.24, ease: "easeOut" as const },
  playbackMsPerFrame: 420,
} as const;

export const chartTokens = {
  // padding around the area shown, as a share of its size
  framePad: 0.08,
  // lake context shown around the search grid, as a share of the grid size
  contextPad: 0.18,
  heatAlphaMax: 0.82,
  heatGamma: 1.0,
  trackWidth: 2.2,
  uavRadius: 5.5,
  shoreWidth: 1.2,
  gridLine: 0.5,
} as const;

export const curveTokens = {
  width: 360,
  height: 170,
  padLeft: 34,
  padRight: 10,
  padTop: 12,
  padBottom: 24,
  lineWidth: 1.8,
  selectedLineWidth: 3,
  ticks: [0, 0.25, 0.5, 0.75, 1] as const,
} as const;

export const plannerMeta: Record<string, { label: string; note: string; color: string; dashed?: boolean }> = {
  iamsar: { label: "IAMSAR pattern", note: "Parallel sweep or expanding square, as the manual tasks it", color: "var(--color-plan-iamsar)", dashed: true },
  greedy: { label: "Greedy", note: "Each UAV takes the richest next cell", color: "var(--color-plan-greedy)" },
  lookahead: { label: "Lookahead", note: "Plans four moves ahead for every UAV", color: "var(--color-plan-lookahead)" },
  rl: { label: "Learned policy", note: "Trained with reinforcement learning", color: "var(--color-plan-rl)" },
};

export const objectLabels: Record<string, string> = {
  piw: "person in the water",
  sup_board: "paddle board",
  kayak_capsized: "capsized kayak",
  dinghy_capsized: "capsized dinghy",
};
