# usage meter

Estimate of Claude Code plan usage (5-hour window and rolling 7 days) for the lead session. Claude cannot see `/usage`. This meter injects one context line when a threshold is crossed. It does not read credentials, does not call the network, and does not print transcript text.

Script: `python -I tools/usage_meter.py` (stdlib only).

## What /usage actually is

Claude Code 2.1.307 does not compute the plan percent from the jsonl files.

- `/usage` reads the plan-usage endpoint. The changelog names it (`C:\Users\pablo\.claude\cache\changelog.md` line 1645, v2.1.284: repeated calls to the plan-usage endpoint). The binary calls `/api/oauth/usage` (`claude.exe` string near offset 132883017) and reads response headers `anthropic-ratelimit-unified-5h-utilization`, `anthropic-ratelimit-unified-5h-reset`, `anthropic-ratelimit-unified-7d-utilization`, `anthropic-ratelimit-unified-7d-reset`, plus `*-surpassed-threshold` (strings near offset 134692076).
- After the first API response, the status-line command's stdin JSON carries the same numbers under `rate_limits.five_hour` and `rate_limits.seven_day` as `used_percentage` (0-100) and `resets_at` (unix seconds). `rate_limits.spend_limit` is a gateway spend cap, not the Pro/Max plan meter (status-line schema text in `claude.exe` near offset 256430800; changelog line 1632). That stdin is delivered only to the status-line process, which the user sees. It is not in the model context.
- UserPromptSubmit hook stdin is the user prompt, not `rate_limits` (hook event text in `claude.exe` near offset 127772056). This tool does not read stdin, so it never loads the prompt.
- Local files checked for a stored meter: `stats-cache.json` (activity totals and a model token rollup, no percents), `sessions\*.json` (pid, cwd, status), `policy-limits.json` (policy flags). No local file holds `used_percentage`. `.credentials.json` was not opened.

So there is no exact local copy of the plan percent. The meter estimates, then scales with one calibration point the user reads off `/usage`.

## Windows

5-hour block (fallback, because the binary never buckets transcripts; the server sends `resets_at`):

- Sort deduped assistant timestamps.
- A block starts at the first entry. It ends 5 hours later.
- The next entry at or after that end starts the next block (the block does not start at the empty reset instant; it starts at the next message). This is ccusage's blocks rule. ccusage's source was not fetched this session (no network). The gap check in that tool is redundant: the gap since the previous entry cannot exceed the time since the block start.
- The open block is the one that contains now. Its reset clock is start+5h, shown in local `HH:MM`. If the last block has already ended, the current 5-hour usage is 0 and the reset is `--:--` until the next message.

Weekly figure: sum of weights with `timestamp >= now-7d` (rolling). The server's `seven_day.resets_at` is a fixed reset, not a rolling window. Calibration does not fix that mismatch.

## Tokens

Roots, every run:

- `%USERPROFILE%\.claude\projects\**\*.jsonl` (session files and `subagents\` jsonl).
- `%LOCALAPPDATA%\Temp\claude\**\tasks\*.output` only when the file starts with `{`. On this machine that is 2 transcript-shaped outputs (46 assistant ids, all already in a project jsonl); the other 97 `.output` files are not JSON. Message ids that also appear in a project jsonl are deduped.

Counted records: `type=="assistant"`, not `message.model=="<synthetic>"` (31 rows, all zero tokens). Fields read: `type`, `timestamp`, `requestId`, `message.model`, `message.id`, `message.usage` `input_tokens`, `output_tokens`, `cache_creation_input_tokens`, `cache_read_input_tokens`, and when present the numeric `cache_creation.ephemeral_5m_input_tokens` / `ephemeral_1h_input_tokens`. `isSidechain` is not required; sidechain rows are included. `cost-state` rows (8 on this machine) are session snapshots, not added.

Dedupe key: `message.id`, else `requestId`. Same id keeps component-wise max token counts and the earliest timestamp. On this history, 10868 assistant lines collapsed to 4694 ids, 6174 duplicate lines, 0 cases where a later line had a smaller count, 0 request ids mapping to two message ids.

`cache_creation_input_tokens` is not added on top of the 5m/1h split. If the split is absent, or both halves are 0 while the total is not, the total is priced as a 5-minute write.

## Weights

USD per million tokens, from the `pricing_tiers` object in Claude Code 2.1.307 (`claude.exe` offset 245952187) and the `models[].pricing` map just after it. Weight = that cost. Opus 5.5 input is the 1.0 unit.

| model | in | out | cache write 5m | cache write 1h | cache read | in vs opus 5.5 |
| --- | --- | --- | --- | --- | --- | --- |
| claude-opus-5-5 | 4 | 20 | 5 | 8 | 0.20 | 1 |
| claude-opus-5, opus-4-5 .. opus-4-8 | 5 | 25 | 6.25 | 10 | 0.50 | 1.25 |
| claude-opus-4, opus-4-1 | 15 | 75 | 18.75 | 30 | 1.50 | 3.75 |
| claude-sonnet-5-5 | 2 | 10 | 2.5 | 4 | 0.10 | 0.5 |
| claude-sonnet-5 | 2 | 10 | 2.5 | 4 | 0.20 | 0.5 |
| sonnet 3.5, 3.7, 4, 4.5, 4.6 | 3 | 15 | 3.75 | 6 | 0.30 | 0.75 |
| claude-haiku-5-5 | 0.10 | 0.50 | 0.125 | 0.20 | 0.01 | 0.025 |
| claude-haiku-4-5 | 1 | 5 | 1.25 | 2 | 0.10 | 0.25 |
| claude-3-5-haiku | 0.80 | 4 | 1 | 1.6 | 0.08 | 0.2 |
| fable-5, mythos-5 | 10 | 50 | 12.5 | 20 | 1 | 2.5 |
| fable-5-1, mythos-5-1 | 10 | 50 | 12.5 | 20 | 0.25 | 2.5 |

Output is 5x input on every tier above. Cache-write 5m is 1.25x input. Opus 5.5 cache read is 0.05x its input. Haiku 5.5 uses the long-prompt tier (in 0.50, out 2.50, write 5m 0.625, write 1h 1, read 0.05) on the whole request when input + cache tokens exceed 100000. An unknown model id is priced as the longest matching known id, else by the family name, else as Opus 5.5.

Changelog v2.1.284 line 1630 says Sonnet 5.5 cache reads are $0.20/Mtok. The 2.1.307 catalog tier `tier_2_10_cache_read_0_10` says $0.10. This meter uses the catalog in the installed binary.

## Calibration

`python -I tools/usage_meter.py --calibrate <5h percent from /usage> [--weekly <week percent>]`

Stores `logs/usage_calibration.json`: the weighted USD in the open 5-hour block (and the rolling week, if given) next to the percent you saw, plus `budget_usd = weighted / (percent/100)` and `at`. Later percent = current weight / budget, rounded half up. The status age is the 5-hour `at` (else the weekly `at`): `0m`, `Nm`, `Nh`, `Nd`.

Refuses (exit 1, nothing written) if the window you are calibrating has zero weight. A new `--calibrate` replaces the 5-hour side and, only when `--weekly` is passed, the weekly side. Recalibrate after a plan change or when the estimate and `/usage` disagree by more than a few points: one point assumes the quota is linear in this weight and that our block matches the server block.

`--status` line:

`5h ~NN% (resets HH:MM), week ~NN%, calibrated <age>`

Uncalibrated sides show `n/a`. No open block shows `resets --:--` and a 5-hour weight of 0.

## Hook

Event `UserPromptSubmit`. No matcher (a missing matcher is `*` in this build). Timeout 10 seconds. Command, forward slashes, because Claude Code runs the command through Git Bash (`claude.exe` status-line text near offset 131730800):

```json
{
  "hooks": {
    "UserPromptSubmit": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "python -I \"F:/claude troubleshoot/tekken 4 recomp/tools/usage_meter.py\" --hook",
            "timeout": 10
          }
        ]
      }
    ]
  }
}
```

Install target is the user or project Claude settings. This repo does not edit those files.

Stdout is empty unless a threshold in `(50, 60, 70, 75, 78, 85, 90, 95)` was crossed since the last notice. State: `logs/usage_meter_state.json` (5-hour notices reset when the block start changes; a notice is armed again if the percent falls under it). One JSON object, which this build accepts for UserPromptSubmit (`hookEventName` + `additionalContext` in the zod schema near `claude.exe` offset 247148202; plain stdout would also be wrapped as additionalContext, offset 279319491):

```json
{"hookSpecificOutput":{"hookEventName":"UserPromptSubmit","additionalContext":"Usage ~78% of the 5-hour window (estimate): hand off per the 78% rule"}}
```

At or above 78 the line says to hand off. Below 78 it is the same sentence without the hand-off clause. If the 5-hour and weekly percents both cross on the same run, one line is emitted for the higher hand-off (else the 5-hour one) and both crossings are marked noticed. A new session does not repeat a notice it already stored: the state file is global, not per session.

The note arrives at the next user prompt, not in the middle of a turn.

## Speed and privacy

Per-file offset cache: `logs/usage_meter_cache.json` (size, mtime, byte offset, deduped id -> tokens). A finished snapshot is not parsed again. Parsing the assistant lines alone took 0.34 s; the warm process, cache included, stays under 300 ms.

The parser `json.loads` each assistant line (the usage object sits after the message body) and drops every field it does not use. It never writes those fields. Do not point `--root` at a directory of real transcripts unless the cache/state/calibration paths are the synthetic ones (`--root` defaults those three under `logs/grok/usage-meter-root\`).

Measured on this history (2026-10-22): first uncached `--status` 569 ms; warm `--status` 217 ms and 220 ms; warm `--hook` 220 ms. Under the 300 ms cap once the offset cache exists.

Overrides for tests: `--root`, `--cache`, `--state`, `--calibration`, `--now` (ISO time).

## Limits

- The percent is an estimate. The plan meter is the server utilization header, which is not on disk.
- A block that starts at the next message, rather than at the server's `resets_at`, runs long by the gap between the reset and that message.
- Rolling 7 days is not the server week.
- One calibration point. It goes stale if the plan quota changes, if the wrong window was on screen, or if most of the weight is a model whose price ratio does not match how the plan actually burns.
- Cache-write price follows the 5m/1h split. A missing split is priced as 5m.
- Tokens still only in a not-yet-flushed jsonl line are invisible until the next append.
- `<synthetic>` rows are ignored.
