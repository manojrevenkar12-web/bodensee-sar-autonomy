// The lake: shoreline, no-fly zones and launch sites, with a water raster for fast lookups.
#pragma once

#include <string>
#include <vector>

#include "sar/geo.hpp"

namespace sar {

struct Zone {
  std::string name;
  std::vector<Vec2> polygon;  // local metres
};

struct Site {
  std::string name;
  Vec2 pos;
};

class Lake {
 public:
  // raster_m: resolution of the water raster; margin_m: land kept around the shoreline's bounds.
  Lake(Projection projection, std::vector<Vec2> shoreline, std::vector<Zone> no_fly,
       std::vector<Site> sites, double raster_m = 50.0, double margin_m = 3000.0);

  bool is_water(Vec2 p) const;
  bool is_water_exact(Vec2 p) const { return point_in_polygon(shoreline_, p); }
  // Name of the first no-fly zone containing p, or nullptr.
  const Zone* no_fly_at(Vec2 p) const;
  bool in_no_fly(Vec2 p) const { return no_fly_at(p) != nullptr; }

  const Projection& projection() const { return projection_; }
  const std::vector<Vec2>& shoreline() const { return shoreline_; }
  const std::vector<Zone>& no_fly() const { return no_fly_; }
  const std::vector<Site>& sites() const { return sites_; }
  double raster_m() const { return res_; }
  double water_area_km2() const;

 private:
  Projection projection_;
  std::vector<Vec2> shoreline_;
  std::vector<Zone> no_fly_;
  std::vector<Site> sites_;
  double res_, x0_, y0_;
  int nx_, ny_;
  std::vector<unsigned char> water_;
};

}  // namespace sar
