#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "fixtures.hpp"
#include "sar/geo.hpp"
#include "sar/rng.hpp"

using Catch::Approx;
using namespace sar;

TEST_CASE("projection round-trips and has the right scale") {
  Projection p(47.6, 9.4);
  const Vec2 v = p.to_local({47.61, 9.42});
  CHECK(v.y == Approx(1105.4).epsilon(1e-6));
  CHECK(v.x == Approx(0.02 * 111320.0 * std::cos(47.6 * kDeg)).epsilon(1e-9));
  const LatLon g = p.to_geo(v);
  CHECK(g.lat == Approx(47.61).margin(1e-12));
  CHECK(g.lon == Approx(9.42).margin(1e-12));
}

TEST_CASE("point in polygon, including concave shapes") {
  const std::vector<Vec2> l_shape{{0, 0}, {4, 0}, {4, 1}, {1, 1}, {1, 4}, {0, 4}};
  CHECK(point_in_polygon(l_shape, {0.5, 0.5}));
  CHECK(point_in_polygon(l_shape, {0.5, 3.5}));
  CHECK_FALSE(point_in_polygon(l_shape, {2, 2}));
  CHECK_FALSE(point_in_polygon(l_shape, {-1, 0.5}));
}

TEST_CASE("distance to a segment") {
  CHECK(distance_to_segment({0, 1}, {-1, 0}, {1, 0}) == Approx(1.0));
  CHECK(distance_to_segment({3, 0}, {-1, 0}, {1, 0}) == Approx(2.0));
  CHECK(distance_to_segment({2, 2}, {2, 2}, {2, 2}) == Approx(0.0));
}

TEST_CASE("WGS84 earth-centred coordinates round-trip to millimetres") {
  for (double lat : {-60.0, 0.0, 47.66, 89.0})
    for (double lon : {-170.0, 9.37, 120.0}) {
      const auto e = geodetic_to_ecef(lat, lon, 395.0);
      const auto g = ecef_to_geodetic(e[0], e[1], e[2]);
      CHECK(g[0] == Approx(lat).margin(1e-8));
      CHECK(g[1] == Approx(lon).margin(1e-8));
      CHECK(g[2] == Approx(395.0).margin(1e-3));
    }
}

TEST_CASE("lake raster agrees with the exact polygon test away from the shore") {
  auto lake = test::square_lake();
  CHECK(lake->is_water({0, 0}));
  CHECK(lake->is_water({9900, -9900}));
  CHECK_FALSE(lake->is_water({10200, 0}));
  CHECK_FALSE(lake->is_water({50000, 0}));
  CHECK(lake->in_no_fly({2000, 2000}));
  CHECK_FALSE(lake->in_no_fly({0, 0}));
  CHECK(lake->water_area_km2() == Approx(400.0).epsilon(0.01));
}

TEST_CASE("random numbers are reproducible and roughly normal") {
  Rng a(42), b(42);
  for (int i = 0; i < 100; ++i) REQUIRE(a.next() == b.next());
  Rng r(1);
  double s = 0, s2 = 0;
  const int n = 200000;
  for (int i = 0; i < n; ++i) {
    const double x = r.normal();
    s += x;
    s2 += x * x;
  }
  CHECK(s / n == Approx(0.0).margin(0.01));
  CHECK(s2 / n == Approx(1.0).margin(0.01));
  CHECK(derive_seed(5, 1) != derive_seed(5, 2));
}
