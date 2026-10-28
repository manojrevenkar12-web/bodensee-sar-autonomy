import type { Scenario } from "../../shared/api/client";
import { objectLabels } from "../../shared/theme/tokens";

const COMPASS = ["north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"];

export function compass(fromDeg: number): string {
  const sectors = COMPASS.length;
  const idx = Math.round((((fromDeg % 360) + 360) % 360) / (360 / sectors)) % sectors;
  return COMPASS[idx] ?? "north";
}

export function reportedObject(objects: Record<string, number>): string {
  const entries = Object.entries(objects).sort((a, b) => b[1] - a[1]);
  if (entries.length === 1) return capitalise(objectLabels[entries[0]![0]] ?? entries[0]![0]);
  return capitalise(entries.map(([k]) => objectLabels[k] ?? k).join(" or "));
}

export function summary(s: Scenario): string {
  const wind = `${s.wind.wind_speed.toFixed(0)} m/s from the ${compass(s.wind.wind_from_deg)}`;
  return `${reportedObject(s.objects)}, reported ${Math.round(s.minutes_since)} min ago. Wind ${wind}${s.time_of_day === "night" ? ", at night" : ""}. ${s.uavs.count} UAVs.`;
}

function capitalise(s: string): string {
  return s.charAt(0).toUpperCase() + s.slice(1);
}
