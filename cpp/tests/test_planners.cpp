#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "fixtures.hpp"
#include "sar/planners.hpp"

using namespace sar;

namespace {

View blank_view(int n = 16) {
  View v;
  v.n = n;
  v.cell_m = 150;
  v.dt_s = 15;
  v.horizon = 100;
  const auto cells = static_cast<std::size_t>(n) * n;
  v.p.assign(cells, 0.0f);
  v.pod.assign(cells, 0.3f);
  v.searchable.assign(cells, 1);
  v.no_fly.assign(cells, 0);
  v.return_s.assign(cells, 0.0);
  v.sweep_width_m = 40;
  return v;
}

void add_agent(View& v, int x, int y) {
  v.ax.push_back(x);
  v.ay.push_back(y);
  v.active.push_back(1);
  std::uint16_t m = 0;
  for (int a = 0; a < kActions; ++a) {
    const int nx = x + kDx[a], ny = y + kDy[a];
    if (nx >= 0 && ny >= 0 && nx < v.n && ny < v.n && !v.no_fly[static_cast<std::size_t>(ny) * v.n + nx]) m |= (1u << a);
  }
  v.mask.push_back(m);
  v.margin_s.push_back(1e6);
}

}  // namespace

TEST_CASE("greedy moves to the neighbour with the most probability") {
  auto v = blank_view();
  v.p[static_cast<std::size_t>(9 * v.n + 9)] = 0.5f;  // north-east of (8, 8)
  add_agent(v, 8, 8);
  CHECK(greedy_actions(v)[0] == 2);
}

TEST_CASE("two greedy UAVs do not both take the same cell when another is as good") {
  auto v = blank_view();
  v.p[static_cast<std::size_t>(9 * v.n + 8)] = 0.4f;  // north of both
  v.p[static_cast<std::size_t>(7 * v.n + 8)] = 0.3f;  // south of both
  add_agent(v, 8, 8);
  add_agent(v, 8, 8);
  const auto a = greedy_actions(v);
  CHECK(a[0] == 1);
  CHECK(a[1] == 5);  // 0.3 * 0.3 > 0.4 * 0.3 * 0.7
}

TEST_CASE("lookahead reaches probability beyond the next cell and respects the mask") {
  auto v = blank_view();
  v.p[static_cast<std::size_t>(8 * v.n + 11)] = 0.6f;  // three cells east
  for (int y = 0; y < v.n; ++y) v.no_fly[static_cast<std::size_t>(y) * v.n + 9] = (y >= 9);  // wall above
  add_agent(v, 8, 8);
  const auto a = lookahead_actions(v, 4);
  CHECK((v.mask[0] & (1u << a[0])) != 0);
  CHECK((a[0] == 3 || a[0] == 2 || a[0] == 4));  // eastward
}

TEST_CASE("the IAMSAR sweep covers the probable area and splits it between UAVs") {
  auto v = blank_view(32);
  for (int y = 10; y < 20; ++y)
    for (int x = 4; x < 28; ++x) v.p[static_cast<std::size_t>(y) * v.n + x] = 1.0f / 240.0f;
  add_agent(v, 0, 0);
  add_agent(v, 0, 0);
  SweepPlanner s;
  s.reset(v);
  CHECK_FALSE(s.expanding_square());
  const auto& wps = s.waypoints();
  REQUIRE(wps.size() == 2);
  // legs run along the long side (x); each UAV takes half of the rows
  int ymin0 = 99, ymax0 = -1, ymin1 = 99, ymax1 = -1;
  for (auto [x, y] : wps[0]) ymin0 = std::min(ymin0, y), ymax0 = std::max(ymax0, y);
  for (auto [x, y] : wps[1]) ymin1 = std::min(ymin1, y), ymax1 = std::max(ymax1, y);
  CHECK(ymax0 < ymin1);
  CHECK(ymin0 == 10);
  CHECK(ymax1 >= 18);  // the 90% box may leave out the last row of a uniform block
}

TEST_CASE("a small, certain area gets an expanding square") {
  auto v = blank_view(32);
  v.p[static_cast<std::size_t>(16 * v.n + 16)] = 0.9f;
  v.p[static_cast<std::size_t>(16 * v.n + 17)] = 0.1f;
  add_agent(v, 0, 0);
  SweepPlanner s;
  s.reset(v);
  CHECK(s.expanding_square());
  CHECK(s.waypoints()[0].front() == std::make_pair(16, 16));
}

TEST_CASE("every planner returns allowed actions in a real episode") {
  auto lake = test::square_lake();
  Env e(lake, test::small_config());
  auto s = test::simple_scenario(21);
  s.lkp = {2000, 800};
  e.reset(s);
  SweepPlanner sweep;
  sweep.reset(e.view());
  Rng rng(1);
  int step = 0;
  while (!e.done()) {
    const auto v = e.view();
    std::vector<std::vector<int>> all{greedy_actions(v), lookahead_actions(v, 3), sweep.act(v), random_actions(v, rng)};
    for (const auto& acts : all)
      for (int k = 0; k < e.n_agents(); ++k)
        if (v.active[k] && v.mask[k]) REQUIRE((v.mask[k] & (1u << acts[k])) != 0);
    const auto info = e.step(all[static_cast<std::size_t>(step++ % 4)]);
    REQUIRE(info.interventions == 0);
    REQUIRE(info.violations == 0);
  }
}
