# Independent scoring commands

Working directory: `/Users/vojta/Dev/virtual-instrument-acustra-model-audit-20261011`.

The parent rendered audio. These commands read the completed reports, check every raw audio file against its report hash, derive windowed metrics, and sweep the unchanged source poles with the provided residue overrides. They do not render, fit, normalize or edit audio.

```sh
python3 build-audit/studio-mic-variants/scoring/check_scorer.py

python3 build-audit/studio-mic-variants/scoring/score_variants.py --baseline-report build-audit/studio-mic-variants/baseline-extended/report.json --candidate-report build-audit/studio-mic-variants/measured_bridge-extended/report.json --overrides build-audit/studio-mic-variants/measured_bridge_coefficients.csv --nominal-gain 1 --output build-audit/studio-mic-variants/scoring/measured_bridge-extended.json

python3 build-audit/studio-mic-variants/scoring/score_variants.py --baseline-report build-audit/studio-mic-variants/baseline-extended/report.json --candidate-report build-audit/studio-mic-variants/measured_blend-extended/report.json --overrides build-audit/studio-mic-variants/measured_blend_coefficients.csv --nominal-gain 1 --output build-audit/studio-mic-variants/scoring/measured_blend-extended.json

python3 build-audit/studio-mic-variants/scoring/score_variants.py --baseline-report build-audit/studio-mic-variants/baseline-extended/report.json --candidate-report build-audit/studio-mic-variants/neck30_literal264-extended/report.json --overrides build-audit/studio-mic-research/runtime-candidates-v2/neck30_literal264.csv --nominal-gain 0.3333333333333333 --output build-audit/studio-mic-variants/scoring/neck30_literal264-extended.json

python3 build-audit/studio-mic-variants/scoring/score_variants.py --baseline-report build-audit/studio-mic-variants/baseline-extended/report.json --candidate-report build-audit/studio-mic-variants/offhole45_literal264-extended/report.json --overrides build-audit/studio-mic-research/runtime-candidates-v2/offhole45_literal264.csv --nominal-gain 0.22222222222222224 --output build-audit/studio-mic-variants/scoring/offhole45_literal264-extended.json

python3 build-audit/studio-mic-variants/scoring/score_variants.py --baseline-report build-audit/studio-mic-variants/baseline-extended/report.json --candidate-report build-audit/studio-mic-variants/free_neck30-extended/report.json --overrides build-audit/studio-mic-research/runtime-free-candidates-v1/free_neck30.csv --nominal-gain 0.33333333333333337 --output build-audit/studio-mic-variants/scoring/free_neck30-extended.json

python3 build-audit/studio-mic-variants/scoring/summarize_variants.py
python3 build-audit/studio-mic-variants/scoring/check_final_scores.py
```

The final combined result is `comparison-summary.json` and its readable `comparison-summary.md`. Per-note detail is retained in the five `*-extended.json` score files. Earlier 408-case/FRF-only outputs remain as historical scratch; the five final extended results supersede them for this comparison. `scorer-controls.json` records formula/identity/stereo controls; `final-scoring-checks.json` verifies the final score hashes and all 12,000 native time-window records.
