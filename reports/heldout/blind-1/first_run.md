# Evaluation: `heldout/blind-1`

20 scenarios x 40 seeds; every planner flies the same seeds.

| Planner | Found in time | 95% CI | by 10 / 20 / 30 min | Mean minutes to find | Expected (belief) | Rule violations |
|---|---|---|---|---|---|---|
| random | 2.9% | 1.9-4.0% | 1% / 2% / 3% | 32.0 | 3.1% | 0 |
| iamsar | 14.2% | 12.2-16.4% | 8% / 13% / 14% | 29.7 | 20.2% | 0 |
| greedy | 31.0% | 28.5-33.6% | 21% / 30% / 31% | 25.9 | 40.3% | 0 |
| lookahead | 31.5% | 29.0-34.1% | 23% / 30% / 32% | 25.9 | 40.8% | 0 |
| rl | 20.6% | 18.5-22.9% | 14% / 19% / 21% | 28.5 | 35.1% | 0 |

Paired difference in the share found (RL minus planner, same scenarios and seeds):

- vs random: +17.8 points (95% CI +15.2 to +20.1)
- vs iamsar: +6.4 points (95% CI +3.5 to +9.2)
- vs greedy: -10.4 points (95% CI -13.1 to -7.8)
- vs lookahead: -10.9 points (95% CI -13.5 to -8.1)

| Scenario | random | iamsar | greedy | lookahead | rl |
|---|---|---|---|---|---|
| bise-forecast-turns-westerly | 0% | 5% | 2% | 2% | 0% |
| bise-night-swimmer-romanshorn | 22% | 15% | 48% | 50% | 42% |
| calm-night-long-search-mid-lake | 5% | 5% | 15% | 8% | 0% |
| calm-sup-ueberlingen-bay | 12% | 68% | 95% | 92% | 82% |
| dinghy-report-sailor-separated | 0% | 45% | 35% | 22% | 20% |
| distant-base-ueberlingen-to-hagnau | 0% | 0% | 0% | 0% | 0% |
| ferry-overboard-near-friedrichshafen | 0% | 5% | 25% | 22% | 12% |
| foehn-kayak-off-wasserburg | 2% | 12% | 65% | 70% | 40% |
| kayak-or-dinghy-mid-lake | 0% | 0% | 0% | 0% | 0% |
| night-sup-arbon-southwesterly | 0% | 5% | 30% | 38% | 15% |
| regatta-dinghy-langenargen | 2% | 42% | 52% | 55% | 32% |
| short-battery-konstanz-bise | 0% | 0% | 0% | 0% | 0% |
| single-drone-late-report-lindau | 2% | 2% | 2% | 2% | 2% |
| storm-short-battery-langenargen | 0% | 0% | 15% | 22% | 0% |
| strong-bise-dinghy-east-lake | 2% | 12% | 25% | 30% | 10% |
| sup-report-actually-swimmer | 0% | 12% | 22% | 28% | 15% |
| swimmer-near-altenrhein-airport | 0% | 8% | 42% | 38% | 28% |
| unclear-report-off-konstanz-easterly | 2% | 0% | 5% | 5% | 2% |
| westerly-storm-kayak-meersburg | 5% | 10% | 42% | 45% | 28% |
| witnessed-fall-meersburg-calm | 0% | 38% | 98% | 100% | 82% |
