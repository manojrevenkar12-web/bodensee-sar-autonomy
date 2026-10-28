// Python bindings (nanobind). Arrays come back as NumPy arrays that own a copy of the data.
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <cstring>
#include <memory>

#include "sar/dis.hpp"
#include "sar/env.hpp"
#include "sar/lake.hpp"
#include "sar/mavlink.hpp"
#include "sar/planners.hpp"

namespace nb = nanobind;
using namespace nb::literals;

namespace {

template <class T>
nb::ndarray<nb::numpy, T> to_numpy(std::vector<T> v, std::initializer_list<std::size_t> shape) {
  auto* data = new std::vector<T>(std::move(v));
  nb::capsule owner(data, [](void* p) noexcept { delete static_cast<std::vector<T>*>(p); });
  return nb::ndarray<nb::numpy, T>(data->data(), shape, owner);
}

nb::bytes to_bytes(const std::vector<std::uint8_t>& v) {
  return nb::bytes(reinterpret_cast<const char*>(v.data()), v.size());
}

std::vector<std::uint8_t> from_bytes(const nb::bytes& b) {
  const auto* p = static_cast<const std::uint8_t*>(b.data());
  return {p, p + b.size()};
}

}  // namespace

NB_MODULE(_sarsim, m) {
  m.doc() = "Bodensee SAR simulation core (C++20)";

  nb::class_<sar::Vec2>(m, "Vec2")
      .def(nb::init<>())
      .def("__init__", [](sar::Vec2* v, double x, double y) { new (v) sar::Vec2{x, y}; }, "x"_a, "y"_a)
      .def_rw("x", &sar::Vec2::x)
      .def_rw("y", &sar::Vec2::y)
      .def("__repr__", [](const sar::Vec2& v) { return "Vec2(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")"; });

  nb::class_<sar::Projection>(m, "Projection")
      .def(nb::init<double, double>(), "lat0"_a, "lon0"_a)
      .def("to_local", [](const sar::Projection& p, double lat, double lon) { return p.to_local({lat, lon}); })
      .def("to_geo", [](const sar::Projection& p, double x, double y) {
        const auto g = p.to_geo({x, y});
        return std::make_pair(g.lat, g.lon);
      });

  nb::class_<sar::Zone>(m, "Zone")
      .def(nb::init<>())
      .def_rw("name", &sar::Zone::name)
      .def_rw("polygon", &sar::Zone::polygon);
  nb::class_<sar::Site>(m, "Site")
      .def(nb::init<>())
      .def_rw("name", &sar::Site::name)
      .def_rw("pos", &sar::Site::pos);

  nb::class_<sar::Lake>(m, "Lake")
      .def(nb::init<sar::Projection, std::vector<sar::Vec2>, std::vector<sar::Zone>, std::vector<sar::Site>, double, double>(),
           "projection"_a, "shoreline"_a, "no_fly"_a, "sites"_a, "raster_m"_a = 50.0, "margin_m"_a = 3000.0)
      .def("is_water", [](const sar::Lake& l, double x, double y) { return l.is_water({x, y}); })
      .def("is_water_exact", [](const sar::Lake& l, double x, double y) { return l.is_water_exact({x, y}); })
      .def("no_fly_at", [](const sar::Lake& l, double x, double y) -> nb::object {
        const auto* z = l.no_fly_at({x, y});
        return z ? nb::cast(z->name) : nb::none();
      })
      .def_prop_ro("projection", &sar::Lake::projection)
      .def_prop_ro("shoreline", &sar::Lake::shoreline)
      .def_prop_ro("no_fly", &sar::Lake::no_fly)
      .def_prop_ro("sites", &sar::Lake::sites)
      .def("water_area_km2", &sar::Lake::water_area_km2);

  nb::class_<sar::ObjectType>(m, "ObjectType")
      .def(nb::init<>())
      .def_rw("name", &sar::ObjectType::name)
      .def_rw("dw_slope", &sar::ObjectType::dw_slope)
      .def_rw("dw_icpt", &sar::ObjectType::dw_icpt)
      .def_rw("dw_sd", &sar::ObjectType::dw_sd)
      .def_rw("cw_slope", &sar::ObjectType::cw_slope)
      .def_rw("cw_icpt", &sar::ObjectType::cw_icpt)
      .def_rw("cw_sd", &sar::ObjectType::cw_sd);

  nb::class_<sar::SensorModel>(m, "SensorModel")
      .def(nb::init<>())
      .def_rw("offsets_m", &sar::SensorModel::offsets_m)
      .def_rw("lrc", &sar::SensorModel::lrc)
      .def_rw("sweep_width_m", &sar::SensorModel::sweep_width_m);

  nb::class_<sar::Truth>(m, "Truth")
      .def(nb::init<>())
      .def_rw("object", &sar::Truth::object)
      .def_rw("start", &sar::Truth::start)
      .def_rw("wind_speed", &sar::Truth::wind_speed)
      .def_rw("wind_from_deg", &sar::Truth::wind_from_deg)
      .def_rw("gust_sd", &sar::Truth::gust_sd)
      .def_rw("gust_tau_s", &sar::Truth::gust_tau_s)
      .def_rw("current_factor", &sar::Truth::current_factor)
      .def_rw("dw_slope", &sar::Truth::dw_slope)
      .def_rw("dw_icpt", &sar::Truth::dw_icpt)
      .def_rw("cw_slope", &sar::Truth::cw_slope)
      .def_rw("cw_icpt", &sar::Truth::cw_icpt)
      .def_rw("rw_sigma", &sar::Truth::rw_sigma);

  nb::class_<sar::Scenario>(m, "Scenario")
      .def(nb::init<>())
      .def_rw("lkp", &sar::Scenario::lkp)
      .def_rw("lkp_sigma_m", &sar::Scenario::lkp_sigma_m)
      .def_rw("delay_s", &sar::Scenario::delay_s)
      .def_rw("objects", &sar::Scenario::objects)
      .def_rw("object_prior", &sar::Scenario::object_prior)
      .def_rw("wind_speed", &sar::Scenario::wind_speed)
      .def_rw("wind_from_deg", &sar::Scenario::wind_from_deg)
      .def_rw("wind_speed_sd", &sar::Scenario::wind_speed_sd)
      .def_rw("wind_dir_sd_deg", &sar::Scenario::wind_dir_sd_deg)
      .def_rw("current_factor", &sar::Scenario::current_factor)
      .def_rw("current_factor_sd", &sar::Scenario::current_factor_sd)
      .def_rw("rw_sigma", &sar::Scenario::rw_sigma)
      .def_rw("n_uav", &sar::Scenario::n_uav)
      .def_rw("endurance_s", &sar::Scenario::endurance_s)
      .def_rw("base", &sar::Scenario::base)
      .def_rw("horizon_steps", &sar::Scenario::horizon_steps)
      .def_rw("sensor", &sar::Scenario::sensor)
      .def_rw("truth", &sar::Scenario::truth)
      .def_rw("seed", &sar::Scenario::seed);

  nb::class_<sar::EnvConfig>(m, "EnvConfig")
      .def(nb::init<>())
      .def_rw("grid_n", &sar::EnvConfig::grid_n)
      .def_rw("cell_m", &sar::EnvConfig::cell_m)
      .def_rw("dt_s", &sar::EnvConfig::dt_s)
      .def_rw("n_particles", &sar::EnvConfig::n_particles)
      .def_rw("transit_speed", &sar::EnvConfig::transit_speed)
      .def_rw("reserve_s", &sar::EnvConfig::reserve_s)
      .def_rw("crop", &sar::EnvConfig::crop)
      .def_rw("coarse", &sar::EnvConfig::coarse)
      .def_rw("pre_search_dt_s", &sar::EnvConfig::pre_search_dt_s)
      .def_rw("max_agents", &sar::EnvConfig::max_agents)
      .def_rw("reach_sigma_cells", &sar::EnvConfig::reach_sigma_cells)
      .def_prop_ro("obs_dim", &sar::EnvConfig::obs_dim);

  nb::class_<sar::ExternalScan>(m, "ExternalScan")
      .def(nb::init<>())
      .def("__init__", [](sar::ExternalScan* e, double x, double y, double w) { new (e) sar::ExternalScan{{x, y}, w}; },
           "x"_a, "y"_a, "sweep_width_m"_a)
      .def_rw("pos", &sar::ExternalScan::pos)
      .def_rw("sweep_width_m", &sar::ExternalScan::sweep_width_m);

  nb::class_<sar::StepInfo>(m, "StepInfo")
      .def_ro("reward", &sar::StepInfo::reward)
      .def_ro("pos_expected", &sar::StepInfo::pos_expected)
      .def_ro("interventions", &sar::StepInfo::interventions)
      .def_ro("violations", &sar::StepInfo::violations)
      .def_ro("active", &sar::StepInfo::active)
      .def_ro("done", &sar::StepInfo::done);

  nb::class_<sar::TruthState>(m, "TruthState")
      .def_ro("pos", &sar::TruthState::pos)
      .def_ro("beached", &sar::TruthState::beached)
      .def_ro("found_step", &sar::TruthState::found_step)
      .def_ro("found_by", &sar::TruthState::found_by)
      .def_ro("wind_speed", &sar::TruthState::wind_speed);

  nb::class_<sar::View>(m, "View")
      .def_ro("n", &sar::View::n)
      .def_ro("cell_m", &sar::View::cell_m)
      .def_ro("t", &sar::View::t)
      .def_ro("horizon", &sar::View::horizon)
      .def_ro("p_out", &sar::View::p_out)
      .def_ro("sweep_width_m", &sar::View::sweep_width_m)
      .def_ro("ax", &sar::View::ax)
      .def_ro("ay", &sar::View::ay)
      .def_ro("active", &sar::View::active)
      .def_ro("mask", &sar::View::mask)
      .def_ro("margin_s", &sar::View::margin_s)
      .def_prop_ro("p", [](const sar::View& v) { return to_numpy(v.p, {std::size_t(v.n), std::size_t(v.n)}); })
      .def_prop_ro("pod", [](const sar::View& v) { return to_numpy(v.pod, {std::size_t(v.n), std::size_t(v.n)}); });

  nb::class_<sar::Env>(m, "Env")
      .def(nb::init<std::shared_ptr<const sar::Lake>, sar::EnvConfig>(), "lake"_a, "config"_a)
      .def("reset", &sar::Env::reset, "scenario"_a)
      .def("step", &sar::Env::step, "actions"_a, "external"_a = std::vector<sar::ExternalScan>{},
           nb::call_guard<nb::gil_scoped_release>())
      .def("observe", [](const sar::Env& e) {
        // all agents: obs [A, D] float32, mask [A, 9] uint8, active [A] uint8
        const int a = e.n_agents(), d = e.config().obs_dim();
        std::vector<float> obs(static_cast<std::size_t>(a) * d);
        std::vector<std::uint8_t> mask(static_cast<std::size_t>(a) * sar::kActions);
        for (int k = 0; k < a; ++k) {
          const auto bits = e.observe(k, obs.data() + static_cast<std::size_t>(k) * d);
          for (int q = 0; q < sar::kActions; ++q) mask[static_cast<std::size_t>(k) * sar::kActions + q] = (bits >> q) & 1u;
        }
        return nb::make_tuple(to_numpy(std::move(obs), {std::size_t(a), std::size_t(d)}),
                              to_numpy(std::move(mask), {std::size_t(a), std::size_t(sar::kActions)}),
                              to_numpy(e.agent_active(), {std::size_t(a)}));
      })
      .def("action_mask", &sar::Env::action_mask)
      .def("view", &sar::Env::view)
      .def_prop_ro("config", &sar::Env::config)
      .def_prop_ro("n_agents", &sar::Env::n_agents)
      .def_prop_ro("t", &sar::Env::t)
      .def_prop_ro("horizon", &sar::Env::horizon)
      .def_prop_ro("done", &sar::Env::done)
      .def_prop_ro("pos_expected", &sar::Env::pos_expected)
      .def_prop_ro("p_out", &sar::Env::p_out)
      .def_prop_ro("grid_origin", &sar::Env::grid_origin)
      .def("cell_center", &sar::Env::cell_center)
      .def("cell_of", [](const sar::Env& e, double x, double y) { return e.cell_of({x, y}); })
      .def("return_time_s", &sar::Env::return_time_s)
      .def("belief", [](const sar::Env& e) {
        const auto n = static_cast<std::size_t>(e.config().grid_n);
        return to_numpy(e.belief(), {n, n});
      })
      .def("searchable", [](const sar::Env& e) {
        const auto n = static_cast<std::size_t>(e.config().grid_n);
        return to_numpy(e.cell_searchable(), {n, n});
      })
      .def("no_fly", [](const sar::Env& e) {
        const auto n = static_cast<std::size_t>(e.config().grid_n);
        return to_numpy(e.cell_no_fly(), {n, n});
      })
      .def("agent_cells", [](const sar::Env& e) {
        return to_numpy(e.agent_cells(), {std::size_t(e.n_agents()), std::size_t(2)});
      })
      .def("agent_active", [](const sar::Env& e) { return to_numpy(e.agent_active(), {std::size_t(e.n_agents())}); })
      .def("agent_endurance", [](const sar::Env& e) { return to_numpy(e.agent_endurance(), {std::size_t(e.n_agents())}); })
      .def("particles", [](const sar::Env& e) {
        std::vector<double> xy;
        std::vector<float> w;
        e.particles(xy, w);
        const auto n = w.size();
        return nb::make_tuple(to_numpy(std::move(xy), {n, std::size_t(2)}), to_numpy(std::move(w), {n}));
      })
      .def("truth_state", &sar::Env::truth_state);

  m.attr("N_ACTIONS") = sar::kActions;
  m.def("greedy_actions", &sar::greedy_actions, "view"_a);
  m.def("lookahead_actions", &sar::lookahead_actions, "view"_a, "depth"_a = 4, "discount"_a = 0.97,
        nb::call_guard<nb::gil_scoped_release>());
  m.def("potential", &sar::potential, "view"_a, "length_cells"_a);
  nb::class_<sar::Rng>(m, "Rng")
      .def(nb::init<std::uint64_t>(), "seed"_a)
      .def("uniform", [](sar::Rng& r) { return r.uniform(); })
      .def("normal", [](sar::Rng& r) { return r.normal(); });
  m.def("random_actions", &sar::random_actions, "view"_a, "rng"_a);
  nb::class_<sar::SweepPlanner>(m, "SweepPlanner")
      .def(nb::init<>())
      .def("reset", &sar::SweepPlanner::reset)
      .def("act", &sar::SweepPlanner::act)
      .def_prop_ro("waypoints", &sar::SweepPlanner::waypoints)
      .def_prop_ro("expanding_square", &sar::SweepPlanner::expanding_square)
      .def_prop_ro("track_spacing_cells", &sar::SweepPlanner::track_spacing_cells);

  // --- MAVLink
  auto mv = m.def_submodule("mavlink", "MAVLink v2 framing and mission messages");
  mv.def("crc_x25", [](const nb::bytes& b) { auto v = from_bytes(b); return sar::mavlink::crc_x25(v.data(), v.size()); });
  mv.def("encode", [](std::uint32_t msgid, const nb::bytes& payload, std::uint8_t seq, std::uint8_t sysid, std::uint8_t compid) {
    sar::mavlink::Frame f{seq, sysid, compid, msgid, from_bytes(payload)};
    return to_bytes(sar::mavlink::encode(f));
  }, "msgid"_a, "payload"_a, "seq"_a = 0, "sysid"_a = 255, "compid"_a = 190);
  nb::class_<sar::mavlink::Parser>(mv, "Parser")
      .def(nb::init<>())
      .def("feed", [](sar::mavlink::Parser& p, const nb::bytes& b) {
        auto v = from_bytes(b);
        nb::list out;
        for (const auto& f : p.feed(v.data(), v.size()))
          out.append(nb::make_tuple(f.msgid, to_bytes(f.payload), f.seq, f.sysid, f.compid));
        return out;
      })
      .def_prop_ro("crc_errors", &sar::mavlink::Parser::crc_errors)
      .def_prop_ro("unknown", &sar::mavlink::Parser::unknown);

#define SAR_MAV_STRUCT(NAME, ...) nb::class_<sar::mavlink::NAME>(mv, #NAME).def(nb::init<>())
  SAR_MAV_STRUCT(Heartbeat)
      .def_rw("custom_mode", &sar::mavlink::Heartbeat::custom_mode).def_rw("type", &sar::mavlink::Heartbeat::type)
      .def_rw("autopilot", &sar::mavlink::Heartbeat::autopilot).def_rw("base_mode", &sar::mavlink::Heartbeat::base_mode)
      .def_rw("system_status", &sar::mavlink::Heartbeat::system_status);
  SAR_MAV_STRUCT(GlobalPositionInt)
      .def_rw("time_boot_ms", &sar::mavlink::GlobalPositionInt::time_boot_ms).def_rw("lat", &sar::mavlink::GlobalPositionInt::lat)
      .def_rw("lon", &sar::mavlink::GlobalPositionInt::lon).def_rw("alt", &sar::mavlink::GlobalPositionInt::alt)
      .def_rw("relative_alt", &sar::mavlink::GlobalPositionInt::relative_alt).def_rw("vx", &sar::mavlink::GlobalPositionInt::vx)
      .def_rw("vy", &sar::mavlink::GlobalPositionInt::vy).def_rw("vz", &sar::mavlink::GlobalPositionInt::vz)
      .def_rw("hdg", &sar::mavlink::GlobalPositionInt::hdg);
  SAR_MAV_STRUCT(MissionCount)
      .def_rw("count", &sar::mavlink::MissionCount::count).def_rw("target_system", &sar::mavlink::MissionCount::target_system)
      .def_rw("target_component", &sar::mavlink::MissionCount::target_component);
  SAR_MAV_STRUCT(MissionRequestInt)
      .def_rw("seq", &sar::mavlink::MissionRequestInt::seq).def_rw("target_system", &sar::mavlink::MissionRequestInt::target_system)
      .def_rw("target_component", &sar::mavlink::MissionRequestInt::target_component);
  SAR_MAV_STRUCT(MissionItemInt)
      .def_rw("param1", &sar::mavlink::MissionItemInt::param1).def_rw("param2", &sar::mavlink::MissionItemInt::param2)
      .def_rw("x", &sar::mavlink::MissionItemInt::x).def_rw("y", &sar::mavlink::MissionItemInt::y)
      .def_rw("z", &sar::mavlink::MissionItemInt::z).def_rw("seq", &sar::mavlink::MissionItemInt::seq)
      .def_rw("command", &sar::mavlink::MissionItemInt::command).def_rw("target_system", &sar::mavlink::MissionItemInt::target_system)
      .def_rw("target_component", &sar::mavlink::MissionItemInt::target_component).def_rw("frame", &sar::mavlink::MissionItemInt::frame)
      .def_rw("current", &sar::mavlink::MissionItemInt::current).def_rw("autocontinue", &sar::mavlink::MissionItemInt::autocontinue);
  SAR_MAV_STRUCT(MissionAck)
      .def_rw("target_system", &sar::mavlink::MissionAck::target_system)
      .def_rw("target_component", &sar::mavlink::MissionAck::target_component).def_rw("type", &sar::mavlink::MissionAck::type);
  SAR_MAV_STRUCT(MissionItemReached).def_rw("seq", &sar::mavlink::MissionItemReached::seq);
  SAR_MAV_STRUCT(CommandLong)
      .def_rw("command", &sar::mavlink::CommandLong::command).def_rw("param1", &sar::mavlink::CommandLong::param1)
      .def_rw("param2", &sar::mavlink::CommandLong::param2).def_rw("target_system", &sar::mavlink::CommandLong::target_system)
      .def_rw("target_component", &sar::mavlink::CommandLong::target_component);
#undef SAR_MAV_STRUCT
  mv.def("pack", [](const sar::mavlink::Heartbeat& x) { return to_bytes(sar::mavlink::pack(x)); });
  mv.def("pack", [](const sar::mavlink::GlobalPositionInt& x) { return to_bytes(sar::mavlink::pack(x)); });
  mv.def("pack", [](const sar::mavlink::MissionCount& x) { return to_bytes(sar::mavlink::pack(x)); });
  mv.def("pack", [](const sar::mavlink::MissionRequestInt& x) { return to_bytes(sar::mavlink::pack(x)); });
  mv.def("pack", [](const sar::mavlink::MissionItemInt& x) { return to_bytes(sar::mavlink::pack(x)); });
  mv.def("pack", [](const sar::mavlink::MissionAck& x) { return to_bytes(sar::mavlink::pack(x)); });
  mv.def("pack", [](const sar::mavlink::MissionItemReached& x) { return to_bytes(sar::mavlink::pack(x)); });
  mv.def("pack", [](const sar::mavlink::CommandLong& x) { return to_bytes(sar::mavlink::pack(x)); });
  mv.def("unpack_heartbeat", [](const nb::bytes& b) { return sar::mavlink::unpack_heartbeat(from_bytes(b)); });
  mv.def("unpack_global_position_int", [](const nb::bytes& b) { return sar::mavlink::unpack_global_position_int(from_bytes(b)); });
  mv.def("unpack_mission_count", [](const nb::bytes& b) { return sar::mavlink::unpack_mission_count(from_bytes(b)); });
  mv.def("unpack_mission_request_int", [](const nb::bytes& b) { return sar::mavlink::unpack_mission_request_int(from_bytes(b)); });
  mv.def("unpack_mission_item_int", [](const nb::bytes& b) { return sar::mavlink::unpack_mission_item_int(from_bytes(b)); });
  mv.def("unpack_mission_ack", [](const nb::bytes& b) { return sar::mavlink::unpack_mission_ack(from_bytes(b)); });
  mv.def("unpack_mission_item_reached", [](const nb::bytes& b) { return sar::mavlink::unpack_mission_item_reached(from_bytes(b)); });
  mv.def("unpack_command_long", [](const nb::bytes& b) { return sar::mavlink::unpack_command_long(from_bytes(b)); });

  // --- DIS
  auto dm = m.def_submodule("dis", "DIS v7 Entity State PDU");
  nb::class_<sar::dis::EntityType>(dm, "EntityType")
      .def(nb::init<>())
      .def_rw("kind", &sar::dis::EntityType::kind).def_rw("domain", &sar::dis::EntityType::domain)
      .def_rw("country", &sar::dis::EntityType::country).def_rw("category", &sar::dis::EntityType::category)
      .def_rw("subcategory", &sar::dis::EntityType::subcategory).def_rw("specific", &sar::dis::EntityType::specific)
      .def_rw("extra", &sar::dis::EntityType::extra);
  nb::class_<sar::dis::EntityState>(dm, "EntityState")
      .def(nb::init<>())
      .def_rw("exercise", &sar::dis::EntityState::exercise).def_rw("timestamp", &sar::dis::EntityState::timestamp)
      .def_rw("site", &sar::dis::EntityState::site).def_rw("application", &sar::dis::EntityState::application)
      .def_rw("entity", &sar::dis::EntityState::entity).def_rw("force", &sar::dis::EntityState::force)
      .def_rw("type", &sar::dis::EntityState::type).def_rw("velocity", &sar::dis::EntityState::velocity)
      .def_rw("location", &sar::dis::EntityState::location).def_rw("orientation", &sar::dis::EntityState::orientation)
      .def_rw("appearance", &sar::dis::EntityState::appearance)
      .def_rw("dead_reckoning", &sar::dis::EntityState::dead_reckoning)
      .def_rw("marking", &sar::dis::EntityState::marking).def_rw("capabilities", &sar::dis::EntityState::capabilities);
  nb::class_<sar::dis::Kinematics>(dm, "Kinematics")
      .def(nb::init<>())
      .def_rw("lat", &sar::dis::Kinematics::lat).def_rw("lon", &sar::dis::Kinematics::lon)
      .def_rw("alt", &sar::dis::Kinematics::alt).def_rw("heading_deg", &sar::dis::Kinematics::heading_deg)
      .def_rw("speed", &sar::dis::Kinematics::speed);
  dm.def("encode", [](const sar::dis::EntityState& e) { return to_bytes(sar::dis::encode(e)); });
  dm.def("decode", [](const nb::bytes& b) { return sar::dis::decode(from_bytes(b)); });
  dm.def("set_kinematics", &sar::dis::set_kinematics);
  dm.def("get_kinematics", &sar::dis::get_kinematics);
  dm.def("geodetic_to_ecef", &sar::geodetic_to_ecef);
  dm.def("ecef_to_geodetic", &sar::ecef_to_geodetic);
}
