#include "sar/env.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sar {

namespace {

// Unit vector the wind blows toward, from a meteorological "from" direction (degrees).
Vec2 downwind(double from_deg) {
  const double r = from_deg * kDeg;
  return {-std::sin(r), -std::cos(r)};
}

// Right of downwind.
Vec2 crosswind(Vec2 d) { return {d.y, -d.x}; }

double action_angle(int a) { return std::atan2(static_cast<double>(kDy[a]), static_cast<double>(kDx[a])); }

}  // namespace

Env::Env(std::shared_ptr<const Lake> lake, EnvConfig cfg) : lake_(std::move(lake)), cfg_(cfg) {
  if (!lake_) throw std::invalid_argument("lake is required");
  if (cfg_.grid_n < 8 || cfg_.cell_m <= 0.0 || cfg_.dt_s <= 0.0 || cfg_.n_particles < 16)
    throw std::invalid_argument("invalid environment configuration");
  if (cfg_.crop % 2 == 0 || cfg_.grid_n % cfg_.coarse != 0)
    throw std::invalid_argument("crop must be odd and grid_n divisible by coarse");
}

void Env::reset(const Scenario& s) {
  if (s.objects.empty() || s.objects.size() != s.object_prior.size())
    throw std::invalid_argument("objects and object_prior must be non-empty and the same length");
  if (s.sensor.sweep_width_m.size() != s.objects.size() || s.sensor.lrc.size() != s.objects.size())
    throw std::invalid_argument("sensor model must cover every object");
  if (s.n_uav < 1 || s.n_uav > cfg_.max_agents) throw std::invalid_argument("n_uav out of range");
  if (s.truth.object < 0 || s.truth.object >= static_cast<int>(s.objects.size()))
    throw std::invalid_argument("truth.object out of range");
  sc_ = s;
  rng_belief_.reseed(derive_seed(s.seed, 1));
  rng_truth_.reseed(derive_seed(s.seed, 2));
  t_ = 0;
  horizon_ = s.horizon_steps;
  done_ = false;

  // --- belief: Monte Carlo particles over position, object, leeway, wind and current errors
  const int n = cfg_.n_particles;
  px_.assign(n, 0.0);
  py_.assign(n, 0.0);
  pvx_.assign(n, 0.0);
  pvy_.assign(n, 0.0);
  pw_.assign(n, 1.0f / static_cast<float>(n));
  pobj_.assign(n, 0);
  pbeached_.assign(n, 0);
  pcell_.assign(n, -1);
  for (int i = 0; i < n; ++i) {
    const int obj = rng_belief_.categorical(s.object_prior);
    const ObjectType& o = s.objects[obj];
    const double u = std::max(0.0, s.wind_speed + rng_belief_.normal(0.0, s.wind_speed_sd));
    const double from = s.wind_from_deg + rng_belief_.normal(0.0, s.wind_dir_sd_deg);
    const double cur = std::max(0.0, s.current_factor + rng_belief_.normal(0.0, s.current_factor_sd));
    const double dw = std::max(0.0, o.dw_slope * u + o.dw_icpt + rng_belief_.normal(0.0, o.dw_sd));
    const double side = rng_belief_.uniform() < 0.5 ? -1.0 : 1.0;
    const double cw = side * std::max(0.0, o.cw_slope * u + o.cw_icpt + rng_belief_.normal(0.0, o.cw_sd));
    const Vec2 d = downwind(from), c = crosswind(d);
    const Vec2 v = d * (cur * u + dw) + c * cw;
    pvx_[i] = v.x;
    pvy_[i] = v.y;
    pobj_[i] = static_cast<std::uint8_t>(obj);
    Vec2 p;
    for (int tries = 0; tries < 50; ++tries) {
      p = {s.lkp.x + rng_belief_.normal(0.0, s.lkp_sigma_m), s.lkp.y + rng_belief_.normal(0.0, s.lkp_sigma_m)};
      if (lake_->is_water(p)) break;
    }
    px_[i] = p.x;
    py_[i] = p.y;
    pbeached_[i] = lake_->is_water(p) ? 0 : 1;
  }
  pod_obj_.resize(s.objects.size());
  for (std::size_t k = 0; k < s.objects.size(); ++k)
    pod_obj_[k] = 1.0 - std::exp(-s.sensor.sweep_width_m[k] / cfg_.cell_m);
  total_w_ = 1.0;

  // --- truth: the object drifts from where it really entered the water
  tpos_ = s.truth.start;
  tbeached_ = !lake_->is_water(tpos_);
  gust_ = rng_truth_.normal(0.0, s.truth.gust_sd);
  t_wind_now_ = s.truth.wind_speed;
  found_step_ = -1;
  found_by_ = -1;

  // --- drift from the incident to the start of the search
  double remaining = s.delay_s;
  while (remaining > 1e-9) {
    const double dt = std::min(cfg_.pre_search_dt_s, remaining);
    drift_particles(dt, true);
    drift_truth(dt);
    remaining -= dt;
  }

  // --- grid: centred on the belief's datum at mid-search (IAMSAR), never on the truth
  {
    const double half = 0.5 * horizon_ * cfg_.dt_s;
    double sx = 0.0, sy = 0.0, sw = 0.0;
    for (int i = 0; i < n; ++i) {
      const double k = pbeached_[i] ? 0.0 : half;
      sx += pw_[i] * (px_[i] + pvx_[i] * k);
      sy += pw_[i] * (py_[i] + pvy_[i] * k);
      sw += pw_[i];
    }
    const double cx = sx / sw, cy = sy / sw;
    const double side = cfg_.grid_n * cfg_.cell_m;
    gx0_ = cx - side / 2.0;
    gy0_ = cy - side / 2.0;
  }
  const int gn = cfg_.grid_n;
  const std::size_t cells = static_cast<std::size_t>(gn) * gn;
  std::vector<std::uint8_t> water(cells, 0), wet_any(cells, 0);
  no_fly_.assign(cells, 0);
  return_s_.assign(cells, 0.0);
  for (int iy = 0; iy < gn; ++iy)
    for (int ix = 0; ix < gn; ++ix) {
      int wet = 0;
      for (int sy = 0; sy < 3; ++sy)
        for (int sx = 0; sx < 3; ++sx) {
          const Vec2 q{gx0_ + (ix + (sx + 0.5) / 3.0) * cfg_.cell_m, gy0_ + (iy + (sy + 0.5) / 3.0) * cfg_.cell_m};
          wet += lake_->is_water(q) ? 1 : 0;
        }
      const std::size_t c = static_cast<std::size_t>(iy) * gn + ix;
      water[c] = wet >= 5;
      wet_any[c] = wet > 0;
      const Vec2 cc = cell_center(ix, iy);
      no_fly_[c] = lake_->in_no_fly(cc) ? 1 : 0;
      return_s_[c] = norm(cc - s.base) / cfg_.transit_speed;
    }
  searchable_.assign(cells, 0);
  for (int iy = 0; iy < gn; ++iy)
    for (int ix = 0; ix < gn; ++ix) {
      const std::size_t c = static_cast<std::size_t>(iy) * gn + ix;
      bool shore = wet_any[c];
      for (int k = 1; k < kActions && !shore; ++k) {
        const int jx = ix + kDx[k], jy = iy + kDy[k];
        if (jx >= 0 && jy >= 0 && jx < gn && jy < gn && water[static_cast<std::size_t>(jy) * gn + jx]) shore = true;
      }
      searchable_[c] = water[c] || shore;
    }

  // --- UAVs launch from the base and enter the grid at the nearest allowed cell
  int ex = std::clamp(static_cast<int>(std::floor((s.base.x - gx0_) / cfg_.cell_m)), 0, gn - 1);
  int ey = std::clamp(static_cast<int>(std::floor((s.base.y - gy0_) / cfg_.cell_m)), 0, gn - 1);
  if (no_fly_[static_cast<std::size_t>(ey) * gn + ex]) {
    double best = std::numeric_limits<double>::max();
    for (int iy = 0; iy < gn; ++iy)
      for (int ix = 0; ix < gn; ++ix)
        if (!no_fly_[static_cast<std::size_t>(iy) * gn + ix]) {
          const double d = norm(cell_center(ix, iy) - s.base);
          if (d < best) {
            best = d;
            ex = ix;
            ey = iy;
          }
        }
  }
  agents_.assign(s.n_uav, AgentState{});
  const double transit = norm(cell_center(ex, ey) - s.base) / cfg_.transit_speed;
  for (auto& a : agents_) {
    a.ix = a.pix = ex;
    a.iy = a.piy = ey;
    a.endurance_left = s.endurance_s - transit;
    a.active = a.endurance_left - return_time_s(ex, ey) - cfg_.reserve_s >= cfg_.dt_s;
  }
  map_particles();
  histogram();
}

void Env::drift_particles(double dt, bool random_walk) {
  const double rw = sc_.rw_sigma * std::sqrt(dt);
  const int n = static_cast<int>(px_.size());
  for (int i = 0; i < n; ++i) {
    if (pbeached_[i]) continue;
    double x = px_[i] + pvx_[i] * dt, y = py_[i] + pvy_[i] * dt;
    if (random_walk) {
      x += rw * rng_belief_.normal();
      y += rw * rng_belief_.normal();
    }
    px_[i] = x;
    py_[i] = y;
    if (!lake_->is_water({x, y})) pbeached_[i] = 1;
  }
}

void Env::drift_truth(double dt) {
  const Truth& tr = sc_.truth;
  const double phi = std::exp(-dt / std::max(1.0, tr.gust_tau_s));
  gust_ = phi * gust_ + std::sqrt(1.0 - phi * phi) * tr.gust_sd * rng_truth_.normal();
  const double u = std::max(0.0, tr.wind_speed + gust_);
  t_wind_now_ = u;
  const double rw = tr.rw_sigma * std::sqrt(dt);
  const double nx = rng_truth_.normal(), ny = rng_truth_.normal();  // drawn always: same stream
  if (tbeached_) return;
  const Vec2 d = downwind(tr.wind_from_deg), c = crosswind(d);
  const Vec2 v = d * (tr.current_factor * u + tr.dw_slope * u + tr.dw_icpt) + c * (tr.cw_slope * u + tr.cw_icpt);
  tpos_ = tpos_ + v * dt + Vec2{rw * nx, rw * ny};
  if (!lake_->is_water(tpos_)) tbeached_ = true;
}

int Env::cell_of(Vec2 p) const {
  const int ix = static_cast<int>(std::floor((p.x - gx0_) / cfg_.cell_m));
  const int iy = static_cast<int>(std::floor((p.y - gy0_) / cfg_.cell_m));
  if (ix < 0 || iy < 0 || ix >= cfg_.grid_n || iy >= cfg_.grid_n) return -1;
  return iy * cfg_.grid_n + ix;
}

void Env::map_particles() {
  const int n = static_cast<int>(px_.size());
  for (int i = 0; i < n; ++i) pcell_[i] = cell_of({px_[i], py_[i]});
}

void Env::histogram() {
  const std::size_t cells = static_cast<std::size_t>(cfg_.grid_n) * cfg_.grid_n;
  p_cell_.assign(cells, 0.0f);
  std::vector<double> mass(cells, 0.0), pod_mass(cells, 0.0);
  double out = 0.0, total = 0.0;
  const int n = static_cast<int>(px_.size());
  for (int i = 0; i < n; ++i) {
    const double w = pw_[i];
    total += w;
    const int c = pcell_[i];
    if (c < 0) {
      out += w;
      continue;
    }
    mass[c] += w;
    pod_mass[c] += w * pod_obj_[pobj_[i]];
  }
  total_w_ = total;
  double prior_pod = 0.0, prior_sum = 0.0;
  for (std::size_t k = 0; k < pod_obj_.size(); ++k) {
    prior_pod += sc_.object_prior[k] * pod_obj_[k];
    prior_sum += sc_.object_prior[k];
  }
  prior_pod /= std::max(prior_sum, 1e-12);
  pod_cell_.assign(cells, static_cast<float>(prior_pod));
  const double inv = total > 0.0 ? 1.0 / total : 0.0;
  for (std::size_t c = 0; c < cells; ++c) {
    p_cell_[c] = static_cast<float>(mass[c] * inv);
    if (mass[c] > 0.0) pod_cell_[c] = static_cast<float>(pod_mass[c] / mass[c]);
  }
  p_out_ = static_cast<float>(out * inv);
}

double Env::return_time_s(int ix, int iy) const {
  return return_s_[static_cast<std::size_t>(iy) * cfg_.grid_n + ix];
}

bool Env::allowed(const AgentState& a, int action) const {
  const int jx = a.ix + kDx[action], jy = a.iy + kDy[action];
  const int gn = cfg_.grid_n;
  if (jx < 0 || jy < 0 || jx >= gn || jy >= gn) return false;
  const std::size_t c = static_cast<std::size_t>(jy) * gn + jx;
  if (no_fly_[c]) return false;
  return a.endurance_left - cfg_.dt_s - return_s_[c] - cfg_.reserve_s >= 0.0;
}

std::uint16_t Env::action_mask(int agent) const {
  const AgentState& a = agents_.at(agent);
  if (!a.active || done_) return 0;
  std::uint16_t m = 0;
  for (int k = 0; k < kActions; ++k)
    if (allowed(a, k)) m |= static_cast<std::uint16_t>(1u << k);
  return m;
}

int Env::nearest_allowed(const AgentState& a, int requested, std::uint16_t mask) const {
  int best = -1;
  double best_score = std::numeric_limits<double>::max();
  for (int k = 0; k < kActions; ++k) {
    if (!(mask & (1u << k))) continue;
    double score;
    if (requested == 0 || k == 0) {
      // no heading to keep: prefer the move that brings it closest to home
      score = 10.0 + return_s_[static_cast<std::size_t>(a.iy + kDy[k]) * cfg_.grid_n + (a.ix + kDx[k])] / 1e4;
    } else {
      double d = std::fabs(action_angle(k) - action_angle(requested));
      if (d > kPi) d = 2.0 * kPi - d;
      score = d;
    }
    if (score < best_score) {
      best_score = score;
      best = k;
    }
  }
  return best;
}

double Env::lrc(int object, double offset) const {
  const auto& xs = sc_.sensor.offsets_m;
  const auto& ys = sc_.sensor.lrc[object];
  if (xs.empty()) return 0.0;
  if (offset <= xs.front()) return ys.front();
  if (offset >= xs.back()) return ys.back();
  const auto it = std::upper_bound(xs.begin(), xs.end(), offset);
  const std::size_t j = static_cast<std::size_t>(it - xs.begin());
  const double t = (offset - xs[j - 1]) / (xs[j] - xs[j - 1]);
  return ys[j - 1] + t * (ys[j] - ys[j - 1]);
}

void Env::scan(int ix, int iy, Vec2 from, Vec2 to, bool hover, const std::vector<double>& pod_obj, int who,
               double ext_sweep_width) {
  const int c = iy * cfg_.grid_n + ix;
  // belief: every particle in the cell survives an unsuccessful look with probability 1 - POD
  const int n = static_cast<int>(px_.size());
  for (int i = 0; i < n; ++i)
    if (pcell_[i] == c) pw_[i] = static_cast<float>(pw_[i] * (1.0 - pod_obj[pobj_[i]]));
  // truth: the camera sees the object with the measured lateral range curve
  if (found_step_ >= 0 || cell_of(tpos_) != c) return;
  double p;
  if (who >= 0) {
    const double offset = hover ? norm(tpos_ - to) : distance_to_segment(tpos_, from, to);
    p = lrc(sc_.truth.object, offset);
  } else {
    p = 1.0 - std::exp(-ext_sweep_width / cfg_.cell_m);
  }
  if (rng_truth_.uniform() < p) {
    found_step_ = t_;
    found_by_ = who >= 0 ? who : -2;
  }
}

StepInfo Env::step(const std::vector<int>& actions, const std::vector<ExternalScan>& external) {
  StepInfo info;
  if (done_) {
    info.done = true;
    info.pos_expected = 1.0 - total_w_;
    return info;
  }
  if (static_cast<int>(actions.size()) < n_agents()) throw std::invalid_argument("one action per agent");
  const double w_before = total_w_;
  // 1. the shield decides what each UAV actually does
  for (int k = 0; k < n_agents(); ++k) {
    AgentState& a = agents_[k];
    if (!a.active) continue;
    const std::uint16_t mask = action_mask(k);
    if (mask == 0) {  // nothing safe left: return to base
      a.active = false;
      continue;
    }
    int act = actions[k];
    if (act < 0 || act >= kActions || !(mask & (1u << act))) {
      ++info.interventions;
      act = nearest_allowed(a, act < 0 || act >= kActions ? 0 : act, mask);
    }
    a.pix = a.ix;
    a.piy = a.iy;
    a.ix += kDx[act];
    a.iy += kDy[act];
    a.endurance_left -= cfg_.dt_s;
  }
  // 2. time passes: the object and the forecast drift
  drift_particles(cfg_.dt_s, true);
  drift_truth(cfg_.dt_s);
  map_particles();
  // 3. every active UAV searches the cell it flew into; external searchers too
  std::vector<double> ext_pod(pod_obj_.size());
  for (int k = 0; k < n_agents(); ++k) {
    const AgentState& a = agents_[k];
    if (!a.active) continue;
    const bool hover = a.ix == a.pix && a.iy == a.piy;
    scan(a.ix, a.iy, cell_center(a.pix, a.piy), cell_center(a.ix, a.iy), hover, pod_obj_, k, 0.0);
  }
  for (const auto& e : external) {
    const int c = cell_of(e.pos);
    if (c < 0) continue;
    std::fill(ext_pod.begin(), ext_pod.end(), 1.0 - std::exp(-e.sweep_width_m / cfg_.cell_m));
    scan(c % cfg_.grid_n, c / cfg_.grid_n, e.pos, e.pos, true, ext_pod, -2, e.sweep_width_m);
  }
  histogram();
  ++t_;
  // 4. check the rules held (the shield should make this impossible)
  for (int k = 0; k < n_agents(); ++k) {
    const AgentState& a = agents_[k];
    if (!a.active) continue;
    ++info.active;
    const std::size_t c = static_cast<std::size_t>(a.iy) * cfg_.grid_n + a.ix;
    if (no_fly_[c] || a.endurance_left - return_s_[c] - cfg_.reserve_s < -1e-6) ++info.violations;
  }
  // agents with no safe move left go home now
  for (int k = 0; k < n_agents(); ++k)
    if (agents_[k].active && action_mask(k) == 0) agents_[k].active = false;
  int still = 0;
  for (const auto& a : agents_) still += a.active ? 1 : 0;
  done_ = t_ >= horizon_ || still == 0;
  info.reward = w_before - total_w_;
  info.pos_expected = 1.0 - total_w_;
  info.done = done_;
  return info;
}

std::uint16_t Env::observe(int agent, float* obs) const {
  const int gn = cfg_.grid_n, cr = cfg_.crop, h = cr / 2, kq = cfg_.coarse, bs = gn / kq;
  const AgentState& me = agents_.at(agent);
  std::fill(obs, obs + cfg_.obs_dim(), 0.0f);
  const float scale = 1.0f / std::log1p(static_cast<float>(gn * gn));
  float* belief_ch = obs;
  float* search_ch = obs + cr * cr;
  float* block_ch = obs + 2 * cr * cr;
  float* others_ch = obs + 3 * cr * cr;
  for (int dy = -h; dy <= h; ++dy)
    for (int dx = -h; dx <= h; ++dx) {
      const int jx = me.ix + dx, jy = me.iy + dy;
      const int o = (dy + h) * cr + (dx + h);
      if (jx < 0 || jy < 0 || jx >= gn || jy >= gn) {
        block_ch[o] = 1.0f;
        continue;
      }
      const std::size_t c = static_cast<std::size_t>(jy) * gn + jx;
      belief_ch[o] = std::log1p(p_cell_[c] * static_cast<float>(gn * gn)) * scale;
      search_ch[o] = searchable_[c];
      block_ch[o] = no_fly_[c];
    }
  for (int k = 0; k < n_agents(); ++k) {
    if (k == agent || !agents_[k].active) continue;
    const int dx = agents_[k].ix - me.ix, dy = agents_[k].iy - me.iy;
    if (std::abs(dx) <= h && std::abs(dy) <= h) others_ch[(dy + h) * cr + (dx + h)] += 1.0f;
  }
  float* coarse = obs + 4 * cr * cr;
  float* self_c = coarse + kq * kq;
  float* other_c = coarse + 2 * kq * kq;
  const float cscale = 1.0f / std::log1p(static_cast<float>(kq * kq));
  std::vector<float> block(static_cast<std::size_t>(kq) * kq, 0.0f);
  for (int iy = 0; iy < gn; ++iy)
    for (int ix = 0; ix < gn; ++ix)
      block[static_cast<std::size_t>(iy / bs) * kq + ix / bs] += p_cell_[static_cast<std::size_t>(iy) * gn + ix];
  for (int b = 0; b < kq * kq; ++b) coarse[b] = std::log1p(block[b] * static_cast<float>(kq * kq)) * cscale;
  self_c[(me.iy / bs) * kq + me.ix / bs] = 1.0f;
  for (int k = 0; k < n_agents(); ++k)
    if (k != agent && agents_[k].active) other_c[(agents_[k].iy / bs) * kq + agents_[k].ix / bs] += 1.0f;
  float* sc = obs + 4 * cr * cr + 3 * kq * kq;
  int active = 0;
  for (const auto& a : agents_) active += a.active ? 1 : 0;
  double prior_pod = 0.0;
  for (std::size_t k = 0; k < pod_obj_.size(); ++k) prior_pod += sc_.object_prior[k] * pod_obj_[k];
  sc[0] = static_cast<float>(t_) / static_cast<float>(std::max(1, horizon_));
  sc[1] = static_cast<float>((me.endurance_left - return_time_s(me.ix, me.iy) - cfg_.reserve_s) /
                             std::max(1.0, sc_.endurance_s));
  sc[2] = static_cast<float>(me.ix) / static_cast<float>(gn);
  sc[3] = static_cast<float>(me.iy) / static_cast<float>(gn);
  sc[4] = p_out_;
  sc[5] = static_cast<float>(prior_pod);
  sc[6] = static_cast<float>(active) / static_cast<float>(cfg_.max_agents);
  return action_mask(agent);
}

View Env::view() const {
  View v;
  v.n = cfg_.grid_n;
  v.cell_m = cfg_.cell_m;
  v.dt_s = cfg_.dt_s;
  v.t = t_;
  v.horizon = horizon_;
  v.p = p_cell_;
  v.pod = pod_cell_;
  v.searchable = searchable_;
  v.no_fly = no_fly_;
  v.return_s = return_s_;
  v.p_out = p_out_;
  double w = 0.0, ps = 0.0;
  for (std::size_t k = 0; k < sc_.sensor.sweep_width_m.size(); ++k) {
    w += sc_.object_prior[k] * sc_.sensor.sweep_width_m[k];
    ps += sc_.object_prior[k];
  }
  v.sweep_width_m = w / std::max(ps, 1e-12);
  for (int k = 0; k < n_agents(); ++k) {
    const AgentState& a = agents_[k];
    v.ax.push_back(a.ix);
    v.ay.push_back(a.iy);
    v.active.push_back(a.active ? 1 : 0);
    v.mask.push_back(action_mask(k));
    v.margin_s.push_back(a.endurance_left - return_time_s(a.ix, a.iy) - cfg_.reserve_s);
  }
  return v;
}

std::vector<int> Env::agent_cells() const {
  std::vector<int> out;
  for (const auto& a : agents_) {
    out.push_back(a.ix);
    out.push_back(a.iy);
  }
  return out;
}

std::vector<std::uint8_t> Env::agent_active() const {
  std::vector<std::uint8_t> out;
  for (const auto& a : agents_) out.push_back(a.active ? 1 : 0);
  return out;
}

std::vector<double> Env::agent_endurance() const {
  std::vector<double> out;
  for (const auto& a : agents_) out.push_back(a.endurance_left);
  return out;
}

void Env::particles(std::vector<double>& xy, std::vector<float>& w) const {
  xy.clear();
  w = pw_;
  for (std::size_t i = 0; i < px_.size(); ++i) {
    xy.push_back(px_[i]);
    xy.push_back(py_[i]);
  }
}

TruthState Env::truth_state() const {
  TruthState s;
  s.pos = tpos_;
  s.beached = tbeached_;
  s.found_step = found_step_;
  s.found_by = found_by_;
  s.wind_speed = t_wind_now_;
  return s;
}

}  // namespace sar
