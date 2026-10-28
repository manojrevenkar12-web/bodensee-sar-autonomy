# Evaluation: `scenarios/dev`

12 scenarios x 40 seeds; every planner flies the same seeds.

| Planner | Found in time | 95% CI | by 10 / 20 / 30 min | Mean minutes to find | Expected (belief) | Rule violations |
|---|---|---|---|---|---|---|
| random | 5.4% | 3.5-7.5% | 2% / 4% / 5% | 29.1 | 5.9% | 0 |
| iamsar | 46.2% | 42.1-50.4% | 27% / 40% / 46% | 21.0 | 56.5% | 0 |
| greedy | 57.1% | 53.1-61.0% | 40% / 54% / 57% | 17.7 | 72.0% | 0 |
| lookahead | 59.6% | 55.6-63.5% | 41% / 57% / 60% | 17.1 | 72.5% | 0 |
| rl | 43.3% | 39.4-47.3% | 27% / 41% / 43% | 20.8 | 67.9% | 0 |

Paired difference in the share found (RL minus planner, same scenarios and seeds):

- vs random: +37.9 points (95% CI +33.1 to +42.1)
- vs iamsar: -2.9 points (95% CI -8.3 to +2.1)
- vs greedy: -13.8 points (95% CI -18.1 to -9.6)
- vs lookahead: -16.2 points (95% CI -20.4 to -11.9)

| Scenario | random | iamsar | greedy | lookahead | rl |
|---|---|---|---|---|---|
| arbon-swimmer-onshore-wind | 10% | 30% | 38% | 35% | 35% |
| hagnau-kayak-vague-position | 0% | 18% | 25% | 40% | 15% |
| immenstaad-short-endurance | 2% | 45% | 60% | 68% | 48% |
| immenstaad-swimmer-calm | 5% | 50% | 72% | 62% | 52% |
| konstanz-night-swimmer | 0% | 75% | 78% | 70% | 42% |
| konstanz-wrong-report | 0% | 32% | 25% | 35% | 20% |
| langenargen-sup-near-airspace | 2% | 70% | 90% | 90% | 68% |
| lindau-dinghy-westerly | 2% | 60% | 78% | 85% | 68% |
| meersburg-sup-southwest-wind | 10% | 68% | 88% | 88% | 75% |
| mid-lake-unsure-report | 0% | 8% | 40% | 25% | 20% |
| romanshorn-kayak-bise | 8% | 45% | 50% | 57% | 50% |
| ueberlingen-arm-night-late | 25% | 55% | 42% | 60% | 28% |
