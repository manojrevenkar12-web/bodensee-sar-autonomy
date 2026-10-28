import pytest
from fastapi.testclient import TestClient


@pytest.fixture(scope="module")
def client(tmp_path_factory):
    import os

    from sar.api import db

    os.environ["DATABASE_URL"] = f"sqlite:///{tmp_path_factory.mktemp('db')}/t.db"
    os.environ["SAR_DEMO_VEHICLES"] = "1"
    db.reset_engine()
    from sar.api.app import app

    with TestClient(app) as c:
        yield c
    os.environ.pop("SAR_DEMO_VEHICLES")
    db.reset_engine()


@pytest.fixture(scope="module")
def mission(client):
    r = client.post("/api/missions", json={"scenario_id": "lindau-dinghy-westerly", "seed": 5})
    assert r.status_code == 201, r.text
    return r.json()


def test_health_lake_and_scenarios(client):
    h = client.get("/api/health").json()
    assert h["status"] == "ok" and h["scenarios"] == 12 and {"iamsar", "greedy", "lookahead"} <= set(h["planners"])
    lake = client.get("/api/lake").json()
    assert len(lake["shoreline"]) > 30 and len(lake["no_fly"]) == 3
    sc = client.get("/api/scenarios").json()
    assert {s["id"] for s in sc} >= {"lindau-dinghy-westerly", "konstanz-night-swimmer"}


def test_a_mission_compares_plans_by_expected_success(mission):
    assert mission["start"]["grid"]["n"] == 32
    assert len(mission["start"]["belief"]) == 32
    plans = {p["planner"]: p for p in mission["plans"]}
    assert {"iamsar", "greedy", "lookahead"} <= set(plans)
    for p in plans.values():
        assert 0 <= p["expected_success"] <= 1 and len(p["pos_curve"]) > 0
        assert p["pos_curve"] == sorted(p["pos_curve"])  # cumulative
    assert mission["approved"] == []


def test_plan_frames_and_tracks(client, mission):
    p = client.get(f"/api/missions/{mission['id']}/plans/greedy").json()
    assert p["frames"][0]["t"] == 0 and len(p["frames"]) >= 10
    assert len(p["tracks"][0]) == 4  # four UAVs on this scenario


def test_debrief_waits_for_an_approval(client, mission):
    assert client.post(f"/api/missions/{mission['id']}/debrief").status_code == 409


def test_approving_uploads_the_plan_to_every_uav(client, mission):
    r = client.post(
        f"/api/missions/{mission['id']}/approve",
        json={"planner": "lookahead", "approver": "Einsatzleiter A", "note": "go"},
    )
    assert r.status_code == 201, r.text
    ups = r.json()["uploads"]
    assert len(ups) == 4 and all(u["ok"] for u in ups), ups
    hist = client.get(f"/api/missions/{mission['id']}/approvals").json()["approvals"]
    assert hist[-1]["approver"] == "Einsatzleiter A" and len(hist[-1]["uploads"]) == 4


def test_debrief_reveals_the_truth_after_approval(client, mission):
    d = client.post(f"/api/missions/{mission['id']}/debrief").json()
    assert set(d["planners"]) >= {"iamsar", "greedy", "lookahead"}
    assert len(d["truth_track"]) > 5


def test_errors(client, mission):
    assert client.post("/api/missions", json={"scenario_id": "nope"}).status_code == 404
    assert client.get("/api/missions/9999").status_code == 404
    assert client.get(f"/api/missions/{mission['id']}/plans/nope").status_code == 404
    assert (
        client.post(f"/api/missions/{mission['id']}/approve", json={"planner": "greedy", "approver": ""}).status_code
        == 422
    )


def test_approvals_cannot_be_changed(client):
    routes = {(r.path, m) for r in client.app.routes if hasattr(r, "methods") for m in r.methods}
    assert not any(m in {"PUT", "PATCH", "DELETE"} for _, m in routes)
