import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import type { Mission } from "../../shared/api/client";
import { PlanComparison } from "./PlanComparison";

afterEach(cleanup);

const mission = {
  id: 1,
  plans: [
    { planner: "iamsar", expected_success: 0.52, pos_curve: [0.1, 0.3, 0.52] },
    { planner: "lookahead", expected_success: 0.81, pos_curve: [0.2, 0.6, 0.81] },
  ],
} as unknown as Mission;

describe("plan comparison", () => {
  it("lists plans with their expected success and marks the most likely", () => {
    render(<PlanComparison mission={mission} selected="iamsar" onSelect={() => {}} dtS={600} />);
    expect(screen.getByText("81%")).toBeTruthy();
    expect(screen.getByText("most likely to find")).toBeTruthy();
    expect(screen.getByRole("radio", { name: /IAMSAR pattern/ }).getAttribute("aria-checked")).toBe("true");
    expect(screen.getByText("30 min")).toBeTruthy();
  });
  it("selects a plan", () => {
    const onSelect = vi.fn();
    render(<PlanComparison mission={mission} selected="iamsar" onSelect={onSelect} dtS={600} />);
    fireEvent.click(screen.getByRole("radio", { name: /Lookahead/ }));
    expect(onSelect).toHaveBeenCalledWith("lookahead");
  });
});
