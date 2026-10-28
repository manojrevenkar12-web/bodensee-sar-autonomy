# Writing held-out search scenarios

You are writing test scenarios for a planner that sends drones (UAVs) to search Lake Constance
(Bodensee) for someone in the water. Write them **without looking at the planner's code, its
configuration or any other scenario**: read only this guide. The scenarios are run once, as
written, and the results are recorded as they come out.

## What a scenario is

An incident as a rescue coordinator would receive it: where the person or object was last seen,
how long ago, what may be in the water, the wind forecast, and which drones are available. The
simulator then decides, per random seed, what really happened (where the object entered the
water, the actual wind, how it drifts); you may fix parts of that in the optional `truth` block.

Each scenario is one YAML file named `<id>.yaml`:

```yaml
id: lowercase-words-with-hyphens        # 2-64 characters, a-z, 0-9 and hyphens; also the file name
description: One sentence a coordinator would understand.
incident:
  last_known: {lat: 47.6127, lon: 9.3985}   # degrees, WGS84; MUST be open water (see below)
  position_error_m: 300                  # how uncertain that position is, 50 to 3000 m (1 sigma)
  minutes_since: 30                      # from the incident to the start of the search, 0 to 180
  reported_objects: {piw: 1.0}           # what may be in the water, weights (normalised for you)
  time_of_day: day                       # day (colour cameras) or night (thermal cameras)
forecast:
  wind_speed: 5.0                        # m/s at 10 m, 0 to 25
  wind_from_deg: 240                     # where the wind comes FROM, degrees (0 = north, 90 = east)
uavs:
  count: 3                               # 1 to 4 drones
  endurance_min: 30                      # flight time per battery, 8 to 90 minutes
  base: immenstaad                       # launch site, one of the keys below
search_minutes: 30                       # how long the search lasts, 5 to 60 (use 30 unless you have a reason)
truth:                                   # optional; leave out anything you want drawn at random
  object: piw                            # what is really in the water (may differ from the report)
  wind_speed: 7.0                        # the wind that really blows
  wind_from_deg: 260
  entered_water: {lat: 47.6130, lon: 9.3990}   # where it really went in; MUST be open water
```

Unknown keys are refused.

### Objects (`reported_objects`, `truth.object`)

| Key | Meaning |
|---|---|
| `piw` | Person in the water |
| `sup_board` | Person on a stand-up paddle board |
| `kayak_capsized` | Capsized kayak with person |
| `dinghy_capsized` | Capsized sailing dinghy |

### Launch sites (`uavs.base`)

| Key | Name | lat | lon |
|---|---|---|---|
| `immenstaad` | Immenstaad | 47.664 | 9.367 |
| `konstanz` | Konstanz harbour | 47.6595 | 9.18 |
| `meersburg` | Meersburg | 47.693 | 9.271 |
| `ueberlingen` | Überlingen | 47.763 | 9.162 |
| `romanshorn` | Romanshorn | 47.566 | 9.38 |
| `arbon` | Arbon | 47.517 | 9.437 |
| `lindau` | Lindau | 47.544 | 9.683 |
| `langenargen` | Langenargen | 47.596 | 9.541 |

### No-fly zones

Drones may not enter these circles; a search near them must route around them.

| Zone | lat | lon | radius (m) |
|---|---|---|---|
| Friedrichshafen airport (EDNY), control zone | 47.6713 | 9.5115 | 3500 |
| St. Gallen-Altenrhein airport (LSZR) | 47.485 | 9.5607 | 2500 |
| Eriskircher Ried nature reserve | 47.625 | 9.53 | 1000 |

### Open water

The planner only accepts positions in the lake. The shoreline is irregular, so place every
`last_known` (and any `truth.entered_water`) **within 800 m of one of these reference points**,
which are all at least 1 km from any shore and outside the no-fly zones. For scale at this
latitude: 0.001 degrees of latitude is about 111 m, 0.001 degrees of longitude about 75 m.

| Ref | lat | lon |
|---|---|---|
| R01 | 47.4996 | 9.4985 |
| R02 | 47.4996 | 9.5318 |
| R03 | 47.4996 | 9.5984 |
| R04 | 47.4996 | 9.6317 |
| R05 | 47.5222 | 9.4651 |
| R06 | 47.5222 | 9.4985 |
| R07 | 47.5222 | 9.5318 |
| R08 | 47.5222 | 9.5651 |
| R09 | 47.5222 | 9.5984 |
| R10 | 47.5222 | 9.6317 |
| R11 | 47.5222 | 9.6651 |
| R12 | 47.5222 | 9.6984 |
| R13 | 47.5449 | 9.4318 |
| R14 | 47.5449 | 9.4651 |
| R15 | 47.5449 | 9.4985 |
| R16 | 47.5449 | 9.5318 |
| R17 | 47.5449 | 9.5651 |
| R18 | 47.5449 | 9.5984 |
| R19 | 47.5449 | 9.6317 |
| R20 | 47.5675 | 9.3985 |
| R21 | 47.5675 | 9.4318 |
| R22 | 47.5675 | 9.4651 |
| R23 | 47.5675 | 9.4985 |
| R24 | 47.5675 | 9.5318 |
| R25 | 47.5675 | 9.5651 |
| R26 | 47.5675 | 9.5984 |
| R27 | 47.5901 | 9.3652 |
| R28 | 47.5901 | 9.3985 |
| R29 | 47.5901 | 9.4318 |
| R30 | 47.5901 | 9.4651 |
| R31 | 47.5901 | 9.4985 |
| R32 | 47.6127 | 9.2986 |
| R33 | 47.6127 | 9.3319 |
| R34 | 47.6127 | 9.3652 |
| R35 | 47.6127 | 9.3985 |
| R36 | 47.6127 | 9.4318 |
| R37 | 47.6127 | 9.4651 |
| R38 | 47.6353 | 9.2652 |
| R39 | 47.6353 | 9.2986 |
| R40 | 47.6353 | 9.3319 |
| R41 | 47.6353 | 9.3652 |
| R42 | 47.6353 | 9.3985 |
| R43 | 47.6353 | 9.4318 |
| R44 | 47.6353 | 9.4651 |
| R45 | 47.6579 | 9.2319 |
| R46 | 47.6579 | 9.2652 |
| R47 | 47.6579 | 9.2986 |
| R48 | 47.6579 | 9.3319 |
| R49 | 47.6805 | 9.2319 |
| R50 | 47.6805 | 9.2652 |
| R51 | 47.7032 | 9.2319 |
| R52 | 47.7258 | 9.1986 |
| R53 | 47.7484 | 9.1653 |

## What makes a good held-out set

Write **20 scenarios** that a real coordinator on Lake Constance could face, varied on purpose:

- different parts of the lake and different launch sites (the drones fly from the base to the
  search area, so a distant base costs flight time);
- calm, moderate and strong wind from different directions (Bise from the north-east, westerly
  storms, south-westerlies);
- day and night;
- every object type, and some uncertain reports (two objects with weights);
- short and long delays, small and large position errors;
- a few hard cases: the report is wrong (`truth.object` differs), the forecast is wrong
  (`truth.wind_*` differs from `forecast`), very short battery endurance, a single drone, an
  incident close to a no-fly zone.

Do not try to make scenarios easy or hard for any particular method. Describe each incident
plainly in `description`. Write them all in one go; do not revise them after seeing any result.
