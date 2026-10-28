import numpy as np

from sar.perception.calibrate import OFFSETS_M, calibrate, hit
from sar.perception.detect import detect
from sar.perception.render import FRAME_PX, Scene, render


def test_frames_are_reproducible():
    a = render(Scene("eo", "piw", 4.0, 10.0), np.random.default_rng(3))
    b = render(Scene("eo", "piw", 4.0, 10.0), np.random.default_rng(3))
    assert a.shape == (FRAME_PX, FRAME_PX, 3) and np.array_equal(a, b)
    t = render(Scene("thermal", "piw", 4.0, 10.0), np.random.default_rng(3))
    assert t.shape == (FRAME_PX, FRAME_PX)


def test_a_capsized_dinghy_under_the_track_is_found_in_calm_weather():
    assert hit(Scene("eo", "dinghy_capsized", 2.0, 0.0), 10, np.random.default_rng(1)) >= 0.9
    assert hit(Scene("thermal", "piw", 2.0, 0.0), 10, np.random.default_rng(1)) >= 0.7


def test_nothing_is_found_outside_the_swath():
    assert hit(Scene("eo", "dinghy_capsized", 2.0, 60.0), 5, np.random.default_rng(1)) == 0.0


def test_empty_night_water_raises_no_alarms():
    rng = np.random.default_rng(0)
    assert (
        sum(len(detect(render(Scene("thermal", "piw", 5.0, 0.0, present=False), rng), "thermal")) for _ in range(5))
        == 0
    )


def test_calibration_output_shape(monkeypatch):
    from sar.perception import calibrate as cal

    monkeypatch.setattr(cal, "WIND_OF_CLASS", {"calm": 2.5})
    out = calibrate(frames=2, seed=1)
    assert out["offsets_m"] == OFFSETS_M
    row = out["sensors"]["thermal"]["calm"]["piw"]
    assert len(row["lrc"]) == len(OFFSETS_M) and row["sweep_width_m"] >= 0
