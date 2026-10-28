import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { IncidentList } from "./IncidentList";

function wrap(ui: React.ReactNode) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return render(<QueryClientProvider client={qc}>{ui}</QueryClientProvider>);
}

const scenario = {
  id: "a", description: "Swimmer off Immenstaad", objects: { piw: 1 }, time_of_day: "day", minutes_since: 20,
  wind: { wind_speed: 2, wind_from_deg: 270 }, uavs: { count: 3, endurance_min: 30, base: "immenstaad" }, last_known: { lat: 47.65, lon: 9.37 },
};

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe("incident list states", () => {
  it("shows a skeleton while loading", () => {
    vi.stubGlobal("fetch", vi.fn(() => new Promise(() => {})));
    wrap(<IncidentList onSelect={() => {}} />);
    expect(screen.getByRole("status", { name: "Loading incidents" })).toBeTruthy();
  });

  it("says what failed and retries", async () => {
    const fetchMock = vi
      .fn()
      .mockResolvedValueOnce(new Response(JSON.stringify({ detail: "database unavailable" }), { status: 503 }))
      .mockResolvedValueOnce(new Response(JSON.stringify([scenario]), { status: 200 }));
    vi.stubGlobal("fetch", fetchMock);
    wrap(<IncidentList onSelect={() => {}} />);
    expect(await screen.findByText("database unavailable")).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "Try again" }));
    expect(await screen.findByText("Swimmer off Immenstaad")).toBeTruthy();
  });

  it("explains an empty list", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(new Response("[]", { status: 200 })));
    wrap(<IncidentList onSelect={() => {}} />);
    expect(await screen.findByText("No incidents yet")).toBeTruthy();
  });

  it("selects an incident", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(new Response(JSON.stringify([scenario]), { status: 200 })));
    const onSelect = vi.fn();
    wrap(<IncidentList onSelect={onSelect} />);
    fireEvent.click(await screen.findByRole("option"));
    await waitFor(() => expect(onSelect).toHaveBeenCalledWith(scenario));
  });
});
