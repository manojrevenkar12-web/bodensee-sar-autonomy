// Typed access to the API. Errors carry the server's message so screens can say what went wrong.

export type LatLon = { lat: number; lon: number };

export type Scenario = {
  id: string;
  description: string;
  objects: Record<string, number>;
  time_of_day: "day" | "night";
  minutes_since: number;
  wind: { wind_speed: number; wind_from_deg: number };
  uavs: { count: number; endurance_min: number; base: string };
  last_known: LatLon;
};

export type Lake = {
  shoreline: [number, number][];
  no_fly: { name: string; polygon: [number, number][] }[];
  sites: { key: string; name: string; lat: number; lon: number }[];
  dt_s: number;
};

export type Grid = { lat0: number; lon0: number; dlat: number; dlon: number; n: number; cell_m: number };

export type Mission = {
  id: number;
  scenario: { id: string; description: string; search_minutes: number; uavs: { count: number; base: string } };
  start: {
    grid: Grid;
    belief: number[][];
    p_out: number;
    no_fly: number[][];
    uavs: [number, number][];
    base: { key: string; name: string; lat: number; lon: number };
    lkp: LatLon;
    minutes: number;
  };
  plans: { planner: string; expected_success: number; pos_curve: number[] }[];
  approved: string[];
};

export type Frame = { t: number; belief: number[][]; uavs: [number, number][]; active: number[]; pos: number };
export type Plan = { planner: string; expected_success: number; pos_curve: number[]; frames: Frame[]; tracks: [number, number][][] };

export type Upload = { uav: number; vehicle: string; ok: boolean; items: number; retransmissions: number; error: string };
export type Approval = { approval_id: number; planner: string; uploads: Upload[] };
export type Debrief = {
  planners: Record<string, { found: boolean; found_minute: number | null; found_by: number }>;
  truth_track: [number, number][];
  beached: boolean;
};

export class ApiError extends Error {
  constructor(public status: number, message: string) {
    super(message);
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(path, { headers: { "content-type": "application/json" }, ...init });
  if (!res.ok) {
    let detail = res.statusText;
    try {
      const body = await res.json();
      detail = typeof body.detail === "string" ? body.detail : JSON.stringify(body.detail);
    } catch {
      /* not JSON */
    }
    throw new ApiError(res.status, detail || `request failed (${res.status})`);
  }
  return (await res.json()) as T;
}

export const api = {
  lake: () => request<Lake>("/api/lake"),
  scenarios: () => request<Scenario[]>("/api/scenarios"),
  createMission: (scenarioId: string, seed?: number) =>
    request<Mission>("/api/missions", { method: "POST", body: JSON.stringify({ scenario_id: scenarioId, seed }) }),
  mission: (id: number) => request<Mission>(`/api/missions/${id}`),
  plan: (id: number, planner: string) => request<Plan>(`/api/missions/${id}/plans/${planner}`),
  approve: (id: number, planner: string, approver: string, note: string) =>
    request<Approval>(`/api/missions/${id}/approve`, { method: "POST", body: JSON.stringify({ planner, approver, note }) }),
  debrief: (id: number) => request<Debrief>(`/api/missions/${id}/debrief`, { method: "POST" }),
};
