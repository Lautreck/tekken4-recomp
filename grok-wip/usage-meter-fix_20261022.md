# usage-meter anchor (2026-10-22)

## observed

- Before edit, `python -I tools/usage_meter.py --status` printed `5h ~55% (resets 02:35), week n/a, calibrated 12m`. Stored percent was still 53 (logs/usage_calibration.json, at 2026-10-21T23:53:51Z, no resets_at). Clock 2026-10-22 01:58 +02:00.
- Readings file: 01:41 50% "resets in ~4 h"; 01:46 53%. Task anchor is 05:45, not 01:41+4h.
- After `--calibrate 53 --resets 05:45 --note "reading 2026-10-22 01:46 local; /usage 5h 53%; resets 05:45"` at 02:13:04 +02:00, status was `5h ~53% (resets 05:45), week n/a, calibrated 0m`. Standalone `--status` repeated that line.
- Calibration: weighted_usd 30.830504000000005, budget_usd 58.17076226415095, at 2026-10-22T00:13:08.435393+00:00, resets_at 2026-10-22T05:45:00+02:00, note holds 01:46, weekly null. Prior unanchored weight was 47.462788900000014.

## rule

- Until resets_at, window is [end-5h, end). After it, chain_open seeds at that end: first assistant message at or after the end starts a 5h block. Same for an optional weekly end (7d). A later --calibrate without --resets keeps resets_at.
- Real --hook was not run. logs/usage_meter_state.json was left as it was (notified empty). Next hook will see a new window_start and may emit the 50 line once.

## synthetic (python -I logs/grok/usage_meter_synth_run.py, exit 0)

- Unanchored still `5h ~40% (resets 21:30), week ~10%, calibrated 0m`; weights 20 and 130; hooks 50 then 78; notified 50,60,70,75,78.
- Anchor weights 20 (5h) and 170 (week). A $100 row 1 minute before the window was excluded; ccusage would have kept it with the in-window row.
- ANCHOR_STATUS `5h ~40% (resets 05:30), week ~10%, calibrated 0m` (synthetic now, not the real 05:45).
- Hooks one line each at 50,60,70,75,78,85,90,95, then empty. Notified list matched.
- ROLL_STATUS `5h ~40% (resets 10:45), week ~13%, calibrated 4h`. Notices cleared. ROLL_HOOK_50 fired again. ROLL_HOOK_AGAIN_EMPTY 1.
- NEXT_STATUS `5h ~80% (resets 15:45), week ~15%, calibrated 8h`. NEXT_HOOK is the 80% hand-off line. Notified 50,60,70,75,78.
- WEEK_ROLL_STATUS `5h ~340% (resets 17:10), week ~10%, calibrated 2d`. Week percent 10 means only the post-week-end row; 340 is that same row against the 5h budget.
