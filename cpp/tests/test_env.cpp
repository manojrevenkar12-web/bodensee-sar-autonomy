#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <numeric>

#include "fixtures.hpp"
#include "sar/planners.hpp"

using Catch::Approx;
using namespace sar;

namespace {

double belief_sum(const Env& e) {
  double s = e.p_out();
  for (float p : e.belief()) s += p;
  return s;
}

std::vector<float> observe_all(const Env& e) {
  std::vector<float> out(static_cast<std::size_t>(e.n_agents()) * e.config().obs_dim());
  for (int k = 0; k < e.n_agents(); ++k) e.observe(k, out.data() + static_cast<std::size_t>(k) * e.config().obs_dim());
  return out;
}

}  // namespace

TEST_CASE("reset is deterministic for a seed and the belief is a distribution") {
  auto lake = test::square_lake();
  Env a(lake, test::small_config()), b(lake, test::small_config());
  a.reset(test::simple_scenario(3));
  b.reset(test::simple_scenario(3));
  CHECK(a.belief() == b.belief());
  CHECK(belief_sum(a) == Approx(1.0).margin(1e-5));
  Env c(lake, test::small_config());
  c.reset(test::simple_scenario(4));
  CHECK(a.belief() != c.belief());
}

TEST_CASE("the forecast drifts the belief downwind") {
  auto lake = test::square_lake(false);
  Env e(lake, test::small_config());
  auto s = test::simple_scenario();
  s.delay_s = 3600;  // wind from the west for an hour
  e.reset(s);
  std::vector<double> xy;
  std::vector<float> w;
  e.particles(xy, w);
  double mx = 0, my = 0;
  for (std::size_t i = 0; i < w.size(); ++i) {
    mx += xy[2 * i];
    my += xy[2 * i + 1];
  }
  mx /= static_cast<double>(w.size());
  my /= static_cast<double>(w.size());
  // leeway + current at 5 m/s: about (0.025 + 0.011) * 5 + 0.068 = 0.25 m/s eastward
  CHECK(mx == Approx(0.25 * 3600).epsilon(0.2));
  CHECK(std::fabs(my) < 150.0);
}

TEST_CASE("an unsuccessful look removes exactly the detectable share of a cell's probability") {
  auto lake = test::square_lake(false);
  auto cfg = test::small_config();
  Env e(lake, cfg);
  auto s = test::simple_scenario();
  s.n_uav = 1;
  s.rw_sigma = 0.0;
  s.wind_speed = 0.0;
  s.wind_speed_sd = 0.0;
  s.current_factor = 0.0;
  s.current_factor_sd = 0.0;
  s.objects[0].dw_icpt = s.objects[0].cw_icpt = 0.0;
  s.objects[0].dw_sd = s.objects[0].cw_sd = 0.0;
  s.truth.start = {5000, 5000};  // far away: no detection
  e.reset(s);
  const auto before = e.belief();
  const double pod = 1.0 - std::exp(-60.0 / cfg.cell_m);
  // the UAV moves north; the cell it enters is scanned
  const auto cells = e.agent_cells();
  const int target = (cells[1] + 1) * cfg.grid_n + cells[0];
  const double w0 = 1.0 - e.pos_expected();
  const double mass_target = before[static_cast<std::size_t>(target)] * w0;
  const auto info = e.step({1});
  CHECK(info.reward == Approx(mass_target * pod).margin(1e-6));
  CHECK(e.pos_expected() == Approx(mass_target * pod).margin(1e-6));
  CHECK(belief_sum(e) == Approx(1.0).margin(1e-5));
}

TEST_CASE("decisions see only the belief: changing the truth changes no observation or reward") {
  auto lake = test::square_lake();
  auto s1 = test::simple_scenario(11), s2 = test::simple_scenario(11);
  s2.truth.start = {-3000, 4000};
  s2.truth.wind_speed = 12;
  s2.truth.wind_from_deg = 90;
  s2.truth.gust_sd = 3;
  Env a(lake, test::small_config()), b(lake, test::small_config());
  a.reset(s1);
  b.reset(s2);
  Rng rng(5);
  for (int t = 0; t < 60 && !a.done(); ++t) {
    REQUIRE(observe_all(a) == observe_all(b));
    const auto va = a.view();
    const auto acts = random_actions(va, rng);
    const auto ia = a.step(acts), ib = b.step(acts);
    REQUIRE(ia.reward == ib.reward);
    REQUIRE(a.belief() == b.belief());
  }
}

TEST_CASE("the shield keeps every UAV out of no-fly zones and home with its reserve") {
  auto lake = test::square_lake();
  int episodes = 0, violations = 0, interventions = 0, returned = 0;
  for (std::uint64_t seed = 1; seed <= 40; ++seed) {
    auto s = test::simple_scenario(seed);
    s.lkp = {2200, 1000};  // next to the no-fly square
    s.endurance_s = 600 + 30.0 * static_cast<double>(seed);
    s.horizon_steps = 80;
    Env e(lake, test::small_config());
    e.reset(s);
    Rng rng(seed);
    while (!e.done()) {
      // a reckless policy: any action, often a forbidden one
      std::vector<int> acts(static_cast<std::size_t>(e.n_agents()));
      for (auto& a : acts) a = static_cast<int>(rng.uniform() * kActions);
      const auto info = e.step(acts);
      violations += info.violations;
      interventions += info.interventions;
      const auto v = e.view();
      for (int k = 0; k < e.n_agents(); ++k)
        if (v.active[k]) {
          REQUIRE_FALSE(v.no_fly[static_cast<std::size_t>(v.ay[k]) * v.n + v.ax[k]]);
          REQUIRE(v.margin_s[k] >= -1e-6);
        }
    }
    for (auto a : e.agent_active()) returned += a ? 0 : 1;
    ++episodes;
  }
  CHECK(episodes == 40);
  CHECK(violations == 0);
  CHECK(interventions > 0);  // the shield did have work to do
  CHECK(returned > 0);       // and sent UAVs home on battery
}

TEST_CASE("a camera hovering on the object finds it; detection never changes the belief") {
  auto lake = test::square_lake(false);
  auto s = test::simple_scenario(2);
  s.n_uav = 1;
  s.delay_s = 0;
  s.base = {0, 0};
  s.truth.start = {10, 10};
  s.truth.wind_speed = 0;
  s.truth.gust_sd = 0;
  s.truth.rw_sigma = 0;
  s.truth.dw_icpt = s.truth.cw_icpt = 0;
  s.truth.current_factor = 0;
  s.sensor.offsets_m = {0, 120};
  s.sensor.lrc = {{1.0, 1.0}};  // sees the whole cell
  Env e(lake, test::small_config());
  e.reset(s);
  for (int t = 0; t < 5; ++t) e.step({0});
  const auto tr = e.truth_state();
  CHECK(tr.found_step == 0);
  CHECK(tr.found_by == 0);
}

TEST_CASE("objects drifting onto the shore stay there") {
  std::vector<Vec2> shore{{-2000, -2000}, {500, -2000}, {500, 2000}, {-2000, 2000}};  // shore at x = 500
  auto lake = std::make_shared<Lake>(Projection(47.6, 9.4), shore, std::vector<Zone>{}, std::vector<Site>{}, 25.0, 3000.0);
  auto s = test::simple_scenario(9);
  s.delay_s = 7200;
  s.wind_speed = 10;  // from the west: straight onto the eastern shore
  s.truth.wind_speed = 10;
  s.base = {0, -500};
  Env e(lake, test::small_config());
  e.reset(s);
  std::vector<double> xy;
  std::vector<float> w;
  e.particles(xy, w);
  int beyond = 0;
  for (std::size_t i = 0; i < w.size(); ++i) beyond += xy[2 * i] > 650.0;
  CHECK(beyond == 0);
  CHECK(e.truth_state().beached);
}

TEST_CASE("an external searcher's look also updates the belief") {
  auto lake = test::square_lake(false);
  Env a(lake, test::small_config()), b(lake, test::small_config());
  a.reset(test::simple_scenario(4));
  b.reset(test::simple_scenario(4));
  const auto v = a.view();
  int best = 0;
  for (int c = 0; c < v.n * v.n; ++c)
    if (v.p[static_cast<std::size_t>(c)] > v.p[static_cast<std::size_t>(best)]) best = c;
  const Vec2 boat = a.cell_center(best % v.n, best / v.n);
  const auto ia = a.step({0, 0, 0}, {ExternalScan{boat, 40.0}});
  const auto ib = b.step({0, 0, 0});
  CHECK(ia.reward > ib.reward);
}

TEST_CASE("bad input is rejected") {
  auto lake = test::square_lake();
  Env e(lake, test::small_config());
  auto s = test::simple_scenario();
  s.n_uav = 9;
  CHECK_THROWS_AS(e.reset(s), std::invalid_argument);
  s = test::simple_scenario();
  s.object_prior = {0.5, 0.5};
  CHECK_THROWS_AS(e.reset(s), std::invalid_argument);
  EnvConfig bad;
  bad.crop = 10;
  CHECK_THROWS_AS(Env(lake, bad), std::invalid_argument);
}
