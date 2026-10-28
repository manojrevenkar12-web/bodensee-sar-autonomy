// Simulation throughput: environment steps per second with a fixed policy (greedy) and with
// random actions, single thread. Prints JSON.
#include <chrono>
#include <cstdio>

#include "../tests/fixtures.hpp"
#include "sar/planners.hpp"

int main() {
  using clock = std::chrono::steady_clock;
  auto lake = sar::test::square_lake();
  sar::EnvConfig cfg;
  sar::Env env(lake, cfg);
  sar::Rng rng(1);
  long steps = 0;
  double sim_s = 0.0;
  for (int ep = 0; ep < 200; ++ep) {
    env.reset(sar::test::simple_scenario(static_cast<std::uint64_t>(ep + 1)));
    while (!env.done()) {
      const auto v = env.view();
      const auto a = sar::random_actions(v, rng);
      const auto t0 = clock::now();
      env.step(a);
      sim_s += std::chrono::duration<double>(clock::now() - t0).count();
      ++steps;
    }
  }
  std::vector<float> obs(static_cast<std::size_t>(cfg.obs_dim()));
  const auto t1 = clock::now();
  long obs_n = 0;
  for (int i = 0; i < 20000; ++i, ++obs_n) env.observe(0, obs.data());
  const double obs_s = std::chrono::duration<double>(clock::now() - t1).count();
  std::printf("{\"particles\": %d, \"steps\": %ld, \"steps_per_s\": %.0f, \"observations_per_s\": %.0f}\n",
              cfg.n_particles, steps, steps / sim_s, obs_n / obs_s);
  return 0;
}
