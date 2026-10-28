import pytest
from pydantic import ValidationError

from sar.spec import ScenarioSpec


def base():
    return {
        "id": "t-1",
        "incident": {"last_known": {"lat": 47.65, "lon": 9.37}, "reported_objects": {"piw": 2, "sup_board": 2}},
        "forecast": {"wind_speed": 4, "wind_from_deg": 240},
        "uavs": {"count": 3, "endurance_min": 30, "base": "immenstaad"},
    }


def test_dev_scenarios_load_and_are_varied(dev_specs):
    assert len(dev_specs) == 12
    assert {s.incident.time_of_day for s in dev_specs} == {"day", "night"}
    assert len({tuple(s.incident.reported_objects) for s in dev_specs}) >= 5
    assert all(s.horizon_steps == 120 for s in dev_specs)


def test_reported_objects_are_normalised():
    s = ScenarioSpec.model_validate(base())
    assert s.incident.reported_objects == {"piw": 0.5, "sup_board": 0.5}


@pytest.mark.parametrize(
    "path,value",
    [
        (("incident", "reported_objects"), {"submarine": 1}),
        (("uavs", "base"), "atlantis"),
        (("uavs", "count"), 9),
        (("incident", "last_known"), {"lat": 52.5, "lon": 13.4}),
        (("id",), "Has Spaces"),
        (("forecast", "wind_speed"), -1),
    ],
)
def test_bad_scenarios_are_refused(path, value):
    d = base()
    target = d
    for k in path[:-1]:
        target = target[k]
    target[path[-1]] = value
    with pytest.raises(ValidationError):
        ScenarioSpec.model_validate(d)


def test_unknown_fields_are_refused():
    d = base()
    d["incident"]["victim_name"] = "x"
    with pytest.raises(ValidationError):
        ScenarioSpec.model_validate(d)
