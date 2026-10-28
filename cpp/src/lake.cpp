#include "sar/lake.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sar {

Lake::Lake(Projection projection, std::vector<Vec2> shoreline, std::vector<Zone> no_fly,
           std::vector<Site> sites, double raster_m, double margin_m)
    : projection_(projection),
      shoreline_(std::move(shoreline)),
      no_fly_(std::move(no_fly)),
      sites_(std::move(sites)),
      res_(raster_m) {
  if (shoreline_.size() < 3) throw std::invalid_argument("shoreline needs at least 3 points");
  if (raster_m <= 0.0) throw std::invalid_argument("raster_m must be positive");
  double xmin = std::numeric_limits<double>::max(), ymin = xmin;
  double xmax = std::numeric_limits<double>::lowest(), ymax = xmax;
  for (const auto& p : shoreline_) {
    xmin = std::min(xmin, p.x);
    xmax = std::max(xmax, p.x);
    ymin = std::min(ymin, p.y);
    ymax = std::max(ymax, p.y);
  }
  x0_ = xmin - margin_m;
  y0_ = ymin - margin_m;
  nx_ = static_cast<int>(std::ceil((xmax + margin_m - x0_) / res_));
  ny_ = static_cast<int>(std::ceil((ymax + margin_m - y0_) / res_));
  water_.assign(static_cast<std::size_t>(nx_) * ny_, 0);
  // Scanline fill: for each raster row, the polygon's crossings at the row's centre line.
  std::vector<double> xs;
  for (int j = 0; j < ny_; ++j) {
    const double y = y0_ + (j + 0.5) * res_;
    xs.clear();
    const std::size_t n = shoreline_.size();
    for (std::size_t i = 0, k = n - 1; i < n; k = i++) {
      const Vec2 a = shoreline_[i], b = shoreline_[k];
      if ((a.y > y) != (b.y > y)) xs.push_back(a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y));
    }
    std::sort(xs.begin(), xs.end());
    for (std::size_t m = 0; m + 1 < xs.size(); m += 2) {
      int i0 = static_cast<int>(std::ceil((xs[m] - x0_) / res_ - 0.5));
      int i1 = static_cast<int>(std::floor((xs[m + 1] - x0_) / res_ - 0.5));
      i0 = std::max(i0, 0);
      i1 = std::min(i1, nx_ - 1);
      for (int i = i0; i <= i1; ++i) water_[static_cast<std::size_t>(j) * nx_ + i] = 1;
    }
  }
}

bool Lake::is_water(Vec2 p) const {
  const int i = static_cast<int>(std::floor((p.x - x0_) / res_));
  const int j = static_cast<int>(std::floor((p.y - y0_) / res_));
  if (i < 0 || j < 0 || i >= nx_ || j >= ny_) return false;
  return water_[static_cast<std::size_t>(j) * nx_ + i] != 0;
}

const Zone* Lake::no_fly_at(Vec2 p) const {
  for (const auto& z : no_fly_)
    if (point_in_polygon(z.polygon, p)) return &z;
  return nullptr;
}

double Lake::water_area_km2() const {
  std::size_t n = 0;
  for (auto w : water_) n += w;
  return static_cast<double>(n) * res_ * res_ / 1e6;
}

}  // namespace sar
