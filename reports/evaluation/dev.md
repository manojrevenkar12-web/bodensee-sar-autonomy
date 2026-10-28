# Evaluation: `scenarios/dev`

12 scenarios x 40 seeds; every planner flies the same seeds.

| Planner | Found in time | 95% CI | by 10 / 20 / 30 min | Mean minutes to find | Expected (belief) | Rule violations |
|---|---|---|---|---|---|---|
| random | 5.4% | 3.5-7.5% | 2% / 4% / 5% | 29.1 | 5.9% | 0 |
| iamsar | 46.2% | 42.1-50.4% | 27% / 40% / 46% | 21.0 | 56.5% | 0 |
| greedy | 57.1% | 53.1-61.0% | 40% / 54% / 57% | 17.7 | 72.0% | 0 |
| lookahead | 59.6% | 55.6-63.5% | 41% / 57% / 60% | 17.1 | 72.5% | 0 |
| rl | 43.5% | 39.6-47.5% | 28% / 39% / 44% | 20.9 | 67.6% | 0 |

Paired difference in the share found (RL minus planner, same scenarios and seeds):

- vs random: +38.1 points (95% CI +33.8 to +42.5)
- vs iamsar: -2.7 points (95% CI -8.1 to +2.9)
- vs greedy: -13.5 points (95% CI -17.9 to -9.0)
- vs lookahead: -16.0 points (95% CI -20.6 to -11.5)

| Scenario | random | iamsar | greedy | lookahead | rl |
|---|---|---|---|---|---|
| arbon-swimmer-onshore-wind | 10% | 30% | 38% | 35% | 38% |
| hagnau-kayak-vague-position | 0% | 18% | 25% | 40% | 10% |
| immenstaad-short-endurance | 2% | 45% | 60% | 68% | 35% |
| immenstaad-swimmer-calm | 5% | 50% | 72% | 62% | 65% |
| konstanz-night-swimmer | 0% | 75% | 78% | 70% | 57% |
| konstanz-wrong-report | 0% | 32% | 25% | 35% | 25% |
| langenargen-sup-near-airspace | 2% | 70% | 90% | 90% | 65% |
| lindau-dinghy-westerly | 2% | 60% | 78% | 85% | 68% |
| meersburg-sup-southwest-wind | 10% | 68% | 88% | 88% | 70% |
| mid-lake-unsure-report | 0% | 8% | 40% | 25% | 22% |
| romanshorn-kayak-bise | 8% | 45% | 50% | 57% | 32% |
| ueberlingen-arm-night-late | 25% | 55% | 42% | 60% | 35% |
