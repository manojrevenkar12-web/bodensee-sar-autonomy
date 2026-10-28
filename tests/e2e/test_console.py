"""The console in a real browser against the real API: choose an incident, compare plans, approve
one (uploaded to stand-in autopilots over MAVLink), then the exercise debrief."""

import os
import socket
import threading
import time

import pytest

pytestmark = pytest.mark.e2e

playwright = pytest.importorskip("playwright.sync_api")


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@pytest.fixture(scope="module")
def server(tmp_path_factory):
    from sar import conf

    if not conf.path("console", "dist", "index.html").exists():
        pytest.skip("console not built (cd console && npm run build)")
    os.environ["DATABASE_URL"] = f"sqlite:///{tmp_path_factory.mktemp('e2e')}/e2e.db"
    os.environ["SAR_DEMO_VEHICLES"] = "1"
    import uvicorn

    from sar.api import db

    db.reset_engine()
    port = free_port()
    srv = uvicorn.Server(uvicorn.Config("sar.api.app:app", host="127.0.0.1", port=port, log_level="warning"))
    t = threading.Thread(target=srv.run, daemon=True)
    t.start()
    for _ in range(100):
        if srv.started:
            break
        time.sleep(0.1)
    yield f"http://127.0.0.1:{port}"
    srv.should_exit = True
    t.join(timeout=5)
    os.environ.pop("SAR_DEMO_VEHICLES")
    db.reset_engine()


def test_plan_approve_send_and_debrief(server):
    with playwright.sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(viewport={"width": 1440, "height": 900})
        errors: list[str] = []
        page.on("console", lambda m: errors.append(m.text) if m.type == "error" else None)
        page.goto(server)
        page.get_by_text("Choose an incident").wait_for()
        page.get_by_role("option", name="Night swimmer missed by friends off Konstanz").click()
        page.get_by_text("most likely to find").wait_for(timeout=60000)
        assert page.get_by_role("radio").count() >= 3
        page.get_by_role("radio", name="IAMSAR pattern").click()
        assert page.get_by_role("radio", name="IAMSAR pattern").get_attribute("aria-checked") == "true"
        page.get_by_label("Your name").fill("Einsatzleiter Test")
        page.get_by_role("button", name="Approve IAMSAR pattern and send").click()
        page.get_by_text("Sent to 3 of 3 UAVs").wait_for(timeout=30000)
        page.get_by_role("button", name="Show what really happened").click()
        page.get_by_text("green dotted line").wait_for(timeout=60000)
        page.get_by_role("button", name="Switch to the night chart").click()
        assert page.evaluate("document.documentElement.dataset.theme") == "night"
        browser.close()
    assert errors == []
