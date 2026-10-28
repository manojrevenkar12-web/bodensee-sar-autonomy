"""Command line: sar calibrate | train | evaluate | heldout | bench | serve."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import subprocess
import sys
import time
from pathlib import Path

from sar import conf


def cmd_calibrate(a: argparse.Namespace) -> None:
    from sar.perception.calibrate import main

    main(a.frames)


def cmd_train(a: argparse.Namespace) -> None:
    from sar.train.train import train

    card = train(smoke=a.smoke, iterations=a.iterations, out=Path(a.out) if a.out else None)
    print(json.dumps(card, indent=1))


def cmd_evaluate(a: argparse.Namespace) -> None:
    from sar.evaluation.evaluate import evaluate, markdown, write

    directory = conf.path(a.set) if not Path(a.set).is_absolute() else Path(a.set)
    if (directory / "STATUS").exists() and (directory / "STATUS").read_text().strip() == "heldout":
        sys.exit(f"{a.set} is a held-out set: use `sar heldout` for its one recorded first run")
    planners = a.planners.split(",") if a.planners else None
    report = evaluate(directory, planners=planners, seeds=a.seeds, model=a.model, workers=a.workers, backend=a.backend)
    path = write(report, a.name or directory.name)
    print(markdown(report))
    print(f"written {path.relative_to(conf.ROOT)}")


def cmd_heldout(a: argparse.Namespace) -> None:
    """The one recorded run of a held-out set. Never rewritten (tests/test_heldout.py pins it)."""
    from sar.evaluation.evaluate import evaluate, markdown

    directory = conf.path(a.set)
    status = (directory / "STATUS").read_text().strip() if (directory / "STATUS").exists() else ""
    if status != "heldout":
        sys.exit(f"{a.set} has STATUS {status!r}, not 'heldout'")
    out = conf.path("reports", "heldout", directory.name)
    first = out / "first_run.json"
    if first.exists():
        sys.exit(f"{first.relative_to(conf.ROOT)} exists: a held-out set runs once")
    report = evaluate(directory, workers=a.workers, backend=a.backend)
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=conf.ROOT, text=True).strip()
    dirty = subprocess.check_output(["git", "status", "--porcelain", "--", "src", "cpp", "config", "models"], cwd=conf.ROOT, text=True).strip()
    if dirty:
        sys.exit("commit the code, config and model before the first run, so it is tied to a commit")
    report.update({"status": "heldout", "code_commit": commit, "recorded_at": dt.datetime.now(dt.UTC).strftime("%Y-%m-%dT%H:%M:%SZ")})
    out.mkdir(parents=True, exist_ok=True)
    first.write_text(json.dumps(report, indent=1, sort_keys=True) + "\n")
    (out / "first_run.md").write_text(markdown(report))
    print(markdown(report))


def cmd_bench(a: argparse.Namespace) -> None:
    from sar import _sarsim as core
    from sar.planners import Greedy, Lookahead, observe
    from sar.sim.build import make_env
    from sar.spec import load_dir

    spec = load_dir(conf.path("scenarios", "dev"))[0]
    out = {}
    for name, planner in (("step_random", None), ("greedy", Greedy()), ("lookahead", Lookahead(4))):
        steps, t0 = 0, time.perf_counter()
        for seed in range(a.episodes):
            env = make_env(spec, seed)
            rng = core.Rng(seed)
            while not env.done:
                o = observe(env)
                acts = core.random_actions(o.view, rng) if planner is None else planner.act(o)
                env.step(acts)
                steps += 1
        out[f"{name}_steps_per_s"] = round(steps / (time.perf_counter() - t0))
    print(json.dumps(out))


def cmd_serve(a: argparse.Namespace) -> None:
    import uvicorn

    uvicorn.run("sar.api.app:app", host=a.host, port=a.port, log_level="info")


def main(argv: list[str] | None = None) -> None:
    p = argparse.ArgumentParser(prog="sar", description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("calibrate", help="measure the OpenCV detector and write config/sensor_calibration.yaml")
    c.add_argument("--frames", type=int, default=40)
    c.set_defaults(fn=cmd_calibrate)
    t = sub.add_parser("train", help="train the policy with PPO on Ray RLlib")
    t.add_argument("--smoke", action="store_true")
    t.add_argument("--iterations", type=int)
    t.add_argument("--out")
    t.set_defaults(fn=cmd_train)
    e = sub.add_parser("evaluate", help="compare planners on a scenario set")
    e.add_argument("--set", default="scenarios/dev")
    e.add_argument("--name")
    e.add_argument("--seeds", type=int)
    e.add_argument("--planners")
    e.add_argument("--model")
    e.add_argument("--workers", type=int, default=2)
    e.add_argument("--backend", choices=["local", "ray"], default="local")
    e.set_defaults(fn=cmd_evaluate)
    h = sub.add_parser("heldout", help="the one recorded first run of a held-out set")
    h.add_argument("--set", required=True)
    h.add_argument("--workers", type=int, default=2)
    h.add_argument("--backend", choices=["local", "ray"], default="local")
    h.set_defaults(fn=cmd_heldout)
    b = sub.add_parser("bench", help="simulation and planner throughput")
    b.add_argument("--episodes", type=int, default=5)
    b.set_defaults(fn=cmd_bench)
    s = sub.add_parser("serve", help="run the API and console")
    s.add_argument("--host", default="127.0.0.1")
    s.add_argument("--port", type=int, default=8000)
    s.set_defaults(fn=cmd_serve)
    args = p.parse_args(argv)
    args.fn(args)


if __name__ == "__main__":
    main()
