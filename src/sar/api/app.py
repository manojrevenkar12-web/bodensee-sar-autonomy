"""HTTP API for the coordinator console.

GET  /api/health                         liveness and what is loaded
GET  /api/lake                           shoreline, no-fly zones, launch sites
GET  /api/scenarios                      exercise scenarios
POST /api/missions                       open a mission from a scenario: every planner flies ahead in the belief
GET  /api/missions/{id}                  the mission, its start state and the plans' expected success
GET  /api/missions/{id}/plans/{planner}  one plan's frames and tracks
POST /api/missions/{id}/approve          a person approves one plan; it is uploaded to the UAVs over MAVLink
GET  /api/missions/{id}/approvals        approvals and uploads (append-only)
POST /api/missions/{id}/debrief          exercise debrief against the truth, only after an approval
"""

from __future__ import annotations

import os
import random
from contextlib import asynccontextmanager
from typing import Any

from fastapi import FastAPI, HTTPException
from fastapi.middleware.gzip import GZipMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field
from sqlalchemy import select

from sar import conf
from sar.api import db
from sar.api.debrief import debrief
from sar.api.planning import available_planners, fly_ahead, lake_geometry, start_state
from sar.spec import ScenarioSpec, load_dir

SCENARIO_DIR = conf.path("scenarios", "dev")


def scenarios() -> dict[str, ScenarioSpec]:
    return {s.id: s for s in load_dir(SCENARIO_DIR)}


class MissionIn(BaseModel):
    scenario_id: str
    seed: int | None = Field(None, ge=0, le=1_000_000)


class ApprovalIn(BaseModel):
    planner: str
    approver: str = Field(min_length=2, max_length=80)
    note: str = Field("", max_length=500)


@asynccontextmanager
async def lifespan(app: FastAPI):  # type: ignore[no-untyped-def]
    db.engine()
    from sar.api.vehicles import DemoFleet, fleet_from_env

    app.state.vehicles = fleet_from_env()
    if os.environ.get("SAR_DEMO_VEHICLES") == "1":
        app.state.vehicles = DemoFleet(4).__enter__()
    yield
    if isinstance(app.state.vehicles, DemoFleet):
        app.state.vehicles.__exit__(None, None, None)


app = FastAPI(title="Bodensee SAR autonomy", version="0.1.0", lifespan=lifespan)
app.add_middleware(GZipMiddleware, minimum_size=2048)


@app.get("/api/health")
def health() -> dict[str, Any]:
    return {"status": "ok", "planners": available_planners(), "scenarios": len(scenarios())}


@app.get("/api/lake")
def lake() -> dict[str, Any]:
    return lake_geometry()


@app.get("/api/scenarios")
def list_scenarios() -> list[dict[str, Any]]:
    out = []
    for s in scenarios().values():
        out.append(
            {
                "id": s.id,
                "description": s.description,
                "objects": s.incident.reported_objects,
                "time_of_day": s.incident.time_of_day,
                "minutes_since": s.incident.minutes_since,
                "wind": s.forecast.model_dump(),
                "uavs": s.uavs.model_dump(),
                "last_known": s.incident.last_known.model_dump(),
            }
        )
    return out


def _mission(mid: int) -> db.Mission:
    with db.session() as s:
        m = s.get(db.Mission, mid)
        if m is None:
            raise HTTPException(404, "no such mission")
        return m


def _plans(mid: int) -> list[db.Plan]:
    with db.session() as s:
        return list(s.scalars(select(db.Plan).where(db.Plan.mission_id == mid).order_by(db.Plan.id)))


@app.post("/api/missions", status_code=201)
def create_mission(body: MissionIn) -> dict[str, Any]:
    spec = scenarios().get(body.scenario_id)
    if spec is None:
        raise HTTPException(404, "no such scenario")
    seed = body.seed if body.seed is not None else random.randint(0, 999_999)
    start = start_state(spec, seed)
    plans = [fly_ahead(spec, seed, name) for name in available_planners()]
    with db.session() as s:
        m = db.Mission(scenario_id=spec.id, spec=spec.model_dump(mode="json"), seed=seed, start=start)
        s.add(m)
        s.flush()
        for p in plans:
            s.add(db.Plan(mission_id=m.id, **p))
        mid = m.id
    return get_mission(mid)


@app.get("/api/missions/{mid}")
def get_mission(mid: int) -> dict[str, Any]:
    m = _mission(mid)
    return {
        "id": m.id,
        "scenario": m.spec,
        "created_at": m.created_at.isoformat(),
        "start": m.start,
        "plans": [
            {"planner": p.planner, "expected_success": p.expected_success, "pos_curve": p.pos_curve}
            for p in _plans(mid)
        ],
        "approved": [a["planner"] for a in approvals(mid)["approvals"]],
    }


@app.get("/api/missions/{mid}/plans/{planner}")
def get_plan(mid: int, planner: str) -> dict[str, Any]:
    _mission(mid)
    for p in _plans(mid):
        if p.planner == planner:
            return {
                "planner": p.planner,
                "expected_success": p.expected_success,
                "pos_curve": p.pos_curve,
                "frames": p.frames,
                "tracks": p.tracks,
            }
    raise HTTPException(404, "no such plan")


@app.post("/api/missions/{mid}/approve", status_code=201)
def approve(mid: int, body: ApprovalIn) -> dict[str, Any]:
    m = _mission(mid)
    plan = next((p for p in _plans(mid) if p.planner == body.planner), None)
    if plan is None:
        raise HTTPException(404, "no such plan")
    with db.session() as s:
        a = db.Approval(mission_id=mid, planner=body.planner, approver=body.approver, note=body.note)
        s.add(a)
        s.flush()
        aid = a.id
    uploads: list[dict[str, Any]] = []
    fleet = app.state.vehicles
    if fleet is not None:
        from sar.api.vehicles import upload_plan

        uploads = upload_plan(fleet, m.start, plan.tracks)
        with db.session() as s:
            for u in uploads:
                s.add(db.Upload(approval_id=aid, **u))
    return {"approval_id": aid, "planner": body.planner, "uploads": uploads}


@app.get("/api/missions/{mid}/approvals")
def approvals(mid: int) -> dict[str, Any]:
    with db.session() as s:
        rows = list(s.scalars(select(db.Approval).where(db.Approval.mission_id == mid).order_by(db.Approval.id)))
        out = []
        for a in rows:
            ups = list(s.scalars(select(db.Upload).where(db.Upload.approval_id == a.id).order_by(db.Upload.id)))
            out.append(
                {
                    "id": a.id,
                    "planner": a.planner,
                    "approver": a.approver,
                    "note": a.note,
                    "created_at": a.created_at.isoformat(),
                    "uploads": [
                        {
                            "uav": u.uav,
                            "vehicle": u.vehicle,
                            "ok": bool(u.ok),
                            "items": u.items,
                            "retransmissions": u.retransmissions,
                            "error": u.error,
                        }
                        for u in ups
                    ],
                }
            )
    return {"approvals": out}


@app.post("/api/missions/{mid}/debrief")
def run_debrief(mid: int) -> dict[str, Any]:
    m = _mission(mid)
    if not approvals(mid)["approvals"]:
        raise HTTPException(409, "approve a plan first: the debrief reveals the truth")
    spec = ScenarioSpec.model_validate(m.spec)
    return debrief(spec, m.seed, [p.planner for p in _plans(mid)])


CONSOLE = conf.path("console", "dist")
if CONSOLE.exists():
    app.mount("/assets", StaticFiles(directory=CONSOLE / "assets"), name="assets")

    @app.get("/{path:path}", include_in_schema=False)
    def console(path: str) -> FileResponse:
        target = CONSOLE / path
        if path and target.is_file() and CONSOLE in target.resolve().parents:
            return FileResponse(target)
        return FileResponse(CONSOLE / "index.html")
