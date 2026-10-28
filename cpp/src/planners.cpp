#include "sar/planners.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace sar {

namespace {

constexpr double kPotentialWeight = 0.05;
constexpr double kPotentialLength = 4.0;  // cells

bool inside(const View& v, int x, int y) { return x >= 0 && y >= 0 && x < v.n && y < v.n; }
std::size_t idx(const View& v, int x, int y) { return static_cast<std::size_t>(y) * v.n + x; }

}  // namespace

std::vector<double> potential(const View& v, double length_cells) {
  const int n = v.n;
  std::vector<double> kernel(static_cast<std::size_t>(n) * n);
  for (int dy = 0; dy < n; ++dy)
    for (int dx = 0; dx < n; ++dx)
      kernel[static_cast<std::size_t>(dy) * n + dx] = std::exp(-std::sqrt(double(dx * dx + dy * dy)) / length_cells);
  std::vector<double> pot(static_cast<std::size_t>(n) * n, 0.0);
  for (int jy = 0; jy < n; ++jy)
    for (int jx = 0; jx < n; ++jx) {
      const double m = static_cast<double>(v.p[idx(v, jx, jy)]) * v.pod[idx(v, jx, jy)];
      if (m <= 1e-9) continue;
      for (int iy = 0; iy < n; ++iy) {
        const double* krow = &kernel[static_cast<std::size_t>(std::abs(iy - jy)) * n];
        double* prow = &pot[static_cast<std::size_t>(iy) * n];
        for (int ix = 0; ix < n; ++ix) prow[ix] += m * krow[std::abs(ix - jx)];
      }
    }
  return pot;
}

std::vector<int> random_actions(const View& v, Rng& rng) {
  std::vector<int> out(v.ax.size(), 0);
  for (std::size_t k = 0; k < v.ax.size(); ++k) {
    std::vector<int> ok;
    for (int a = 0; a < kActions; ++a)
      if (v.mask[k] & (1u << a)) ok.push_back(a);
    if (!ok.empty()) out[k] = ok[static_cast<std::size_t>(rng.uniform() * static_cast<double>(ok.size())) % ok.size()];
  }
  return out;
}

std::vector<int> greedy_actions(const View& v) {
  const auto pot = potential(v, kPotentialLength);
  std::vector<int> claims(static_cast<std::size_t>(v.n) * v.n, 0);
  std::vector<int> out(v.ax.size(), 0);
  for (std::size_t k = 0; k < v.ax.size(); ++k) {
    if (!v.active[k] || v.mask[k] == 0) continue;
    double best = -1.0;
    int best_a = 0;
    for (int a = 0; a < kActions; ++a) {
      if (!(v.mask[k] & (1u << a))) continue;
      const int x = v.ax[k] + kDx[a], y = v.ay[k] + kDy[a];
      const std::size_t c = idx(v, x, y);
      const double pod = v.pod[c];
      const double value = v.p[c] * pod * std::pow(1.0 - pod, claims[c]) + kPotentialWeight * pot[c];
      if (value > best + 1e-12) {
        best = value;
        best_a = a;
      }
    }
    out[k] = best_a;
    ++claims[idx(v, v.ax[k] + kDx[best_a], v.ay[k] + kDy[best_a])];
  }
  return out;
}

namespace {

struct Search {
  const View& v;
  const std::vector<double>& pot;
  std::vector<int>& claims;
  int depth;
  double discount;
  double margin0;
  double ret0;
  double best = -1.0;
  std::vector<int> best_cells;
  std::vector<int> cells;

  void dfs(int x, int y, int d, double value, double gamma) {
    if (d == depth) {
      const double total = value + gamma * kPotentialWeight * pot[idx(v, x, y)];
      if (total > best + 1e-12) {
        best = total;
        best_cells = cells;
      }
      return;
    }
    for (int a = 0; a < kActions; ++a) {
      const int nx = x + kDx[a], ny = y + kDy[a];
      if (!inside(v, nx, ny)) continue;
      const std::size_t c = idx(v, nx, ny);
      if (v.no_fly[c]) continue;
      // battery: what is left after d+1 steps must still bring it home with the reserve
      if (margin0 - (d + 1) * v.dt_s - (v.return_s[c] - ret0) < 0.0) continue;
      const double pod = v.pod[c];
      const double gain = v.p[c] * pod * std::pow(1.0 - pod, claims[c]);
      ++claims[c];
      cells.push_back(static_cast<int>(c));
      dfs(nx, ny, d + 1, value + gamma * gain, gamma * discount);
      cells.pop_back();
      --claims[c];
    }
  }
};

}  // namespace

std::vector<int> lookahead_actions(const View& v, int depth, double discount) {
  const auto pot = potential(v, kPotentialLength);
  std::vector<int> claims(static_cast<std::size_t>(v.n) * v.n, 0);
  std::vector<int> out(v.ax.size(), 0);
  for (std::size_t k = 0; k < v.ax.size(); ++k) {
    if (!v.active[k] || v.mask[k] == 0) continue;
    const std::size_t c0 = idx(v, v.ax[k], v.ay[k]);
    Search s{v, pot, claims, depth, discount, v.margin_s[k], v.return_s[c0], -1.0, {}, {}};
    s.dfs(v.ax[k], v.ay[k], 0, 0.0, 1.0);
    int a = 0;
    if (!s.best_cells.empty()) {
      const int c = s.best_cells.front();
      const int dx = c % v.n - v.ax[k], dy = c / v.n - v.ay[k];
      for (int q = 0; q < kActions; ++q)
        if (kDx[q] == dx && kDy[q] == dy) a = q;
      for (int cell : s.best_cells) ++claims[static_cast<std::size_t>(cell)];
    }
    if (!(v.mask[k] & (1u << a))) {  // the shield's mask is the final word on the first move
      a = 0;
      for (int q = 0; q < kActions; ++q)
        if (v.mask[k] & (1u << q)) {
          a = q;
          break;
        }
    }
    out[k] = a;
  }
  return out;
}

int step_toward(const View& v, int agent, int tx, int ty) {
  int best = -1;
  double best_d = std::numeric_limits<double>::max();
  for (int a = 0; a < kActions; ++a) {
    if (!(v.mask[agent] & (1u << a))) continue;
    const int x = v.ax[agent] + kDx[a], y = v.ay[agent] + kDy[a];
    const double dx = x - tx, dy = y - ty;
    // Chebyshev distance, then Euclidean to break ties toward straight lines
    const double d = std::max(std::fabs(dx), std::fabs(dy)) + 1e-3 * std::sqrt(dx * dx + dy * dy);
    if (d < best_d) {
      best_d = d;
      best = a;
    }
  }
  return best < 0 ? 0 : best;
}

void SweepPlanner::reset(const View& v) {
  const int n = v.n;
  const int agents = static_cast<int>(v.ax.size());
  wps_.assign(agents, {});
  next_.assign(agents, 0);
  // the smallest set of searchable cells holding 90% of the probability, and its bounding box
  std::vector<int> order(static_cast<std::size_t>(n) * n);
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return v.p[a] > v.p[b]; });
  double in_grid = 0.0;
  for (float p : v.p) in_grid += p;
  int x0 = n, x1 = -1, y0 = n, y1 = -1;
  double acc = 0.0;
  for (int c : order) {
    if (acc >= 0.9 * in_grid || v.p[c] <= 0.0f) break;
    acc += v.p[c];
    x0 = std::min(x0, c % n);
    x1 = std::max(x1, c % n);
    y0 = std::min(y0, c / n);
    y1 = std::max(y1, c / n);
  }
  if (x1 < 0) {
    x0 = y0 = n / 2 - 2;
    x1 = y1 = n / 2 + 2;
  }
  const int w = x1 - x0 + 1, h = y1 - y0 + 1;
  int active = 0;
  double steps_avail = 0.0;
  for (int k = 0; k < agents; ++k)
    if (v.active[k]) {
      ++active;
      steps_avail += std::min(static_cast<double>(v.horizon - v.t), std::max(0.0, v.margin_s[k] / v.dt_s));
    }
  active = std::max(active, 1);
  expanding_ = w * h <= 36;
  if (expanding_) {
    // expanding square from the most likely cell; each UAV's legs rotated by 90 degrees
    const int cx = order.front() % n, cy = order.front() / n;
    const int dirs[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
    for (int k = 0; k < agents; ++k) {
      int x = cx, y = cy, leg = 1, dir = k % 4;
      wps_[k].push_back({x, y});
      for (int legs = 0; legs < 40; ++legs) {
        x += dirs[dir][0] * leg;
        y += dirs[dir][1] * leg;
        wps_[k].push_back({std::clamp(x, 0, n - 1), std::clamp(y, 0, n - 1)});
        dir = (dir + 1) % 4;
        if (legs % 2 == 1) ++leg;
      }
    }
    spacing_ = 1;
    return;
  }
  // parallel sweep: legs along the longer side, the box split across UAVs along the shorter one;
  // track spacing S = A / (effort per UAV), at least one cell (IAMSAR: S = W / C)
  const bool legs_vertical = h >= w;
  const int across = legs_vertical ? w : h;
  const int along = legs_vertical ? h : w;
  const double area = static_cast<double>(w) * h;
  spacing_ = std::max(1, static_cast<int>(std::lround(area / std::max(1.0, steps_avail))));
  int k_active = 0;
  for (int k = 0; k < agents; ++k) {
    if (!v.active[k]) continue;
    const int a0 = across * k_active / active, a1 = across * (k_active + 1) / active - 1;
    ++k_active;
    bool forward = true;
    for (int a = a0; a <= std::max(a0, a1); a += spacing_) {
      const int s0 = 0, s1 = along - 1;
      const int from = forward ? s0 : s1, to = forward ? s1 : s0;
      if (legs_vertical) {
        wps_[k].push_back({x0 + a, y0 + from});
        wps_[k].push_back({x0 + a, y0 + to});
      } else {
        wps_[k].push_back({x0 + from, y0 + a});
        wps_[k].push_back({x0 + to, y0 + a});
      }
      forward = !forward;
    }
  }
}

std::vector<int> SweepPlanner::act(const View& v) {
  const int agents = static_cast<int>(v.ax.size());
  if (static_cast<int>(wps_.size()) != agents) reset(v);
  std::vector<int> out(agents, 0);
  for (int k = 0; k < agents; ++k) {
    if (!v.active[k] || v.mask[k] == 0 || wps_[k].empty()) continue;
    auto& i = next_[k];
    const std::size_t m = wps_[k].size();
    for (std::size_t g = 0; g < m && wps_[k][i % m].first == v.ax[k] && wps_[k][i % m].second == v.ay[k]; ++g) ++i;
    // pattern finished: fly it again (the object may have drifted back into it)
    const auto [tx, ty] = wps_[k][i % m];
    out[k] = step_toward(v, k, tx, ty);
  }
  return out;
}

}  // namespace sar
