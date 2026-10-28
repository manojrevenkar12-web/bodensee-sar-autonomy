import numpy as np
import pytest
import yaml

from sar import conf
from sar.sim.build import make_env, scenario, sensor_model, wind_class
from sar.sim.generator import random_spec
from sar.sim.lake import lake, projection


def test_lake_outline_matches_the_real_lake():
    # Obersee (about 412 km2) + Überlinger See (about 61 km2)
    assert lake().water_area_km2() == pytest.approx(473, rel=0.03)


def test_every_dev_scenario_starts_in_open_water_outside_no_fly_zones(dev_specs):
    for s in dev_specs:
        p = projection().to_local(s.incident.last_known.lat, s.incident.last_known.lon)
        assert lake().is_water(p.x, p.y), s.id
        assert lake().no_fly_at(p.x, p.y) is None, s.id


def test_launch_sites_are_outside_no_fly_zones():
    for site in lake().sites:
        assert lake().no_fly_at(site.pos.x, site.pos.y) is None


def test_sensor_calibration_is_generated_and_sane():
    cal = yaml.safe_load(conf.path("config", "sensor_calibration.yaml").read_text())
    assert cal["generated_by"] == "sar calibrate"
    for classes in cal["sensors"].values():
        for table in classes.values():
            for obj, row in table.items():
                lrc = row["lrc"]
                assert all(0 <= x <= 1 for x in lrc)
                assert lrc[-1] == 0.0  # outside the swath
                assert 0 < row["sweep_width_m"] <= 110, obj
    # bigger objects are easier to see than a head in the water
    eo = cal["sensors"]["eo"]["moderate"]
    assert eo["dinghy_capsized"]["sweep_width_m"] > eo["piw"]["sweep_width_m"]


def test_planner_uses_forecast_conditions_truth_uses_actual():
    m = sensor_model(["piw"], "day", forecast_wind=2.0, true_wind=9.0)
    cal = conf.read("sensor_calibration")
    assert m.sweep_width_m[0] == cal["sensors"]["eo"]["calm"]["piw"]["sweep_width_m"]
    assert m.lrc[0] == cal["sensors"]["eo"]["fresh"]["piw"]["lrc"]
    assert wind_class(3.9) == "calm" and wind_class(4.0) == "moderate" and wind_class(12) == "fresh"


def test_truth_is_reproducible_per_seed_and_varies_between_seeds(spec):
    a, b, c = scenario(spec, 1), scenario(spec, 1), scenario(spec, 2)
    assert (a.truth.start.x, a.truth.wind_speed, a.truth.cw_icpt) == (
        b.truth.start.x,
        b.truth.wind_speed,
        b.truth.cw_icpt,
    )
    assert (a.truth.start.x, a.truth.wind_speed) != (c.truth.start.x, c.truth.wind_speed)


def test_truth_overrides_are_respected(dev_specs):
    s = next(x for x in dev_specs if x.id == "konstanz-wrong-report")
    sc = scenario(s, 0)
    assert [o.name for o in sc.objects] == ["sup_board", "piw"]
    assert sc.object_prior == [1.0, 0.0]  # the coordinator still believes the report
    assert sc.truth.object == 1


def test_environment_belief_is_a_distribution(spec):
    env = make_env(spec, 3)
    assert float(env.belief().sum()) + env.p_out == pytest.approx(1.0, abs=1e-4)
    assert env.n_agents == 3 and all(env.agent_active())


def test_training_scenarios_are_valid_and_reproducible():
    a, b = random_spec(5), random_spec(5)
    assert a == b
    specs = [random_spec(i) for i in range(30)]
    assert len({s.uavs.base for s in specs}) >= 4
    for s in specs:
        p = projection().to_local(s.incident.last_known.lat, s.incident.last_known.lon)
        assert lake().is_water(p.x, p.y)
    assert np.mean([s.incident.time_of_day == "night" for s in specs]) > 0.1
