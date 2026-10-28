"""Fly every planner on every scenario and seed and compare them. Scenarios run in parallel
(processes, or Ray tasks on a cluster); results are ordered and rounded so a report is
byte-for-byte reproducible."""

from __future__ import annotations

import hashlib
import json
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from typing import Any

import numpy as np

from sar import conf
from sar.evaluation.stats import bootstrap_mean, restricted_mean_time
from sar.planners import BASELINES, Planner
from sar.runner import run
from sar.sim.build import make_env
from sar.spec import ScenarioSpec, load_dir


def make_planner(name: str, seed: int, model: str | None) -> Planner:
    if name == "rl":
        from sar.policy import OnnxPolicy

        return OnnxPolicy(model or conf.path("models", "policy.onnx"))
    if name == "random":
        return BASELINES["random"](seed)
    if name == "lookahead":
        return BASELINES["lookahead"](int(conf.read("evaluation")["lookahead_depth"]))
    return BASELINES[name]()


def run_case(spec_json: str, seed: int, planners: list[str], model: str | None) -> list[dict[str, Any]]:
    spec = ScenarioSpec.model_validate_json(spec_json)
    out = []
    for name in planners:
        env = make_env(spec, seed)
        o = run(env, make_planner(name, seed, model), spec.id, seed)
        d = o.as_dict()
        curve = d.pop("pos_curve")
        d.pop("tracks")
        steps_per_min = 60.0 / env.config.dt_s
        d["pos_expected_at"] = {
            str(m): curve[min(len(curve), int(m * steps_per_min)) - 1] if curve else 0.0 for m in conf.read("evaluation")["milestones_min"]
        }
        out.append(d)
    return out


def _ray_run(cases: list[tuple[str, int]], planners: list[str], model: str | None) -> list[list[dict[str, Any]]]:
    import ray

    if not ray.is_initialized():
        ray.init(include_dashboard=False, log_to_driver=False, ignore_reinit_error=True)
    remote = ray.remote(num_cpus=1)(run_case)
    return ray.get([remote.remote(s, seed, planners, model) for s, seed in cases])


def collect(specs: list[ScenarioSpec], seeds: int, planners: list[str], model: str | None, workers: int, backend: str) -> list[dict[str, Any]]:
    cases = [(s.model_dump_json(), seed) for s in specs for seed in range(seeds)]
    if backend == "ray":
        results = _ray_run(cases, planners, model)
    elif workers > 1:
        with ProcessPoolExecutor(max_workers=workers) as pool:
            results = list(pool.map(run_case, [c[0] for c in cases], [c[1] for c in cases], [planners] * len(cases), [model] * len(cases)))
    else:
        results = [run_case(s, seed, planners, model) for s, seed in cases]
    return [row for rows in results for row in rows]


def summarise(rows: list[dict[str, Any]], specs: list[ScenarioSpec], planners: list[str]) -> dict[str, Any]:
    ev = conf.read("evaluation")
    bs = ev["bootstrap"]
    horizon = {s.id: s.search_minutes for s in specs}
    by: dict[str, list[dict[str, Any]]] = {p: sorted([r for r in rows if r["planner"] == p], key=lambda r: (r["scenario"], r["seed"])) for p in planners}
    summary: dict[str, Any] = {}
    for p, rs in by.items():
        keys = [r["scenario"] for r in rs]
        found = np.array([1.0 if r["found"] else 0.0 for r in rs])
        point, lo, hi = bootstrap_mean(found, keys, bs["resamples"], bs["seed"], bs["confidence"])
        at = {}
        for m in ev["milestones_min"]:
            hit = np.array([1.0 if r["found"] and r["found_minute"] <= m else 0.0 for r in rs])
            at[str(m)] = round(float(np.mean([hit[[i for i, k in enumerate(keys) if k == s]].mean() for s in sorted(set(keys))])), 4)
        rmt = np.mean([restricted_mean_time([r["found_minute"] for r in rs if r["scenario"] == s], horizon[s]) for s in sorted(set(keys))])
        summary[p] = {
            "success": round(point, 4),
            "success_ci": [round(lo, 4), round(hi, 4)],
            "success_by_minute": at,
            "mean_minutes_to_find": round(float(rmt), 2),
            "expected_success": round(float(np.mean([r["pos_expected"] for r in rs])), 4),
            "shield_interventions": int(sum(r["interventions"] for r in rs)),
            "rule_violations": int(sum(r["violations"] for r in rs)),
            "runs": len(rs),
        }
    paired: dict[str, Any] = {}
    if "rl" in by:
        base = by["rl"]
        keys = [r["scenario"] for r in base]
        for p in planners:
            if p == "rl":
                continue
            diff = np.array([(1.0 if a["found"] else 0.0) - (1.0 if b["found"] else 0.0) for a, b in zip(base, by[p])])
            point, lo, hi = bootstrap_mean(diff, keys, bs["resamples"], bs["seed"], bs["confidence"])
            paired[f"rl_minus_{p}"] = {"success_difference": round(point, 4), "ci": [round(lo, 4), round(hi, 4)]}
    scenarios = {}
    for s in sorted(horizon):
        scenarios[s] = {p: round(float(np.mean([1.0 if r["found"] else 0.0 for r in by[p] if r["scenario"] == s])), 3) for p in planners}
    return {"planners": summary, "paired": paired, "by_scenario": scenarios}


def set_digest(directory: Path) -> tuple[str, dict[str, str]]:
    files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(directory.glob("*.yaml"))}
    h = hashlib.sha256()
    for name, digest in files.items():
        h.update(name.encode())
        h.update(digest.encode())
    return h.hexdigest(), files


def evaluate(directory: Path, planners: list[str] | None = None, seeds: int | None = None, model: str | None = None,
             workers: int = 2, backend: str = "local") -> dict[str, Any]:
    ev = conf.read("evaluation")
    planners = planners or list(ev["planners"])
    model_path = Path(model) if model else conf.path("models", "policy.onnx")
    if "rl" in planners and not model_path.exists():
        planners = [p for p in planners if p != "rl"]
    specs = load_dir(directory)
    seeds = seeds or int(ev["seeds_per_scenario"])
    rows = collect(specs, seeds, planners, str(model_path) if "rl" in planners else None, workers, backend)
    digest, files = set_digest(directory)
    report = {
        "set": str(directory.relative_to(conf.ROOT)) if directory.is_relative_to(conf.ROOT) else str(directory),
        "set_sha256": digest,
        "scenarios": len(specs),
        "seeds_per_scenario": seeds,
        "planners": planners,
        "model_sha256": hashlib.sha256(model_path.read_bytes()).hexdigest() if "rl" in planners else None,
        **summarise(rows, specs, planners),
    }
    return report


def markdown(report: dict[str, Any]) -> str:
    lines = [
        f"# Evaluation: `{report['set']}`",
        "",
        f"{report['scenarios']} scenarios x {report['seeds_per_scenario']} seeds; every planner flies the same seeds.",
        "",
        "| Planner | Found in time | 95% CI | by 10 / 20 / 30 min | Mean minutes to find | Expected (belief) | Rule violations |",
        "|---|---|---|---|---|---|---|",
    ]
    for p, s in report["planners"].items():
        by = " / ".join(f"{100 * s['success_by_minute'][m]:.0f}%" for m in s["success_by_minute"])
        lines.append(
            f"| {p} | {100 * s['success']:.1f}% | {100 * s['success_ci'][0]:.1f}-{100 * s['success_ci'][1]:.1f}% | {by} | "
            f"{s['mean_minutes_to_find']:.1f} | {100 * s['expected_success']:.1f}% | {s['rule_violations']} |"
        )
    if report["paired"]:
        lines += ["", "Paired difference in the share found (RL minus planner, same scenarios and seeds):", ""]
        for k, v in report["paired"].items():
            lines.append(f"- {k.replace('rl_minus_', 'vs ')}: {100 * v['success_difference']:+.1f} points (95% CI {100 * v['ci'][0]:+.1f} to {100 * v['ci'][1]:+.1f})")
    lines += ["", "| Scenario | " + " | ".join(report["planners"]) + " |", "|---" * (1 + len(report["planners"])) + "|"]
    for s, row in report["by_scenario"].items():
        lines.append(f"| {s} | " + " | ".join(f"{100 * row[p]:.0f}%" for p in report["planners"]) + " |")
    return "\n".join(lines) + "\n"


def write(report: dict[str, Any], name: str) -> Path:
    out = conf.path("reports", "evaluation")
    out.mkdir(parents=True, exist_ok=True)
    (out / f"{name}.json").write_text(json.dumps(report, indent=1, sort_keys=True) + "\n")
    (out / f"{name}.md").write_text(markdown(report))
    return out / f"{name}.json"
