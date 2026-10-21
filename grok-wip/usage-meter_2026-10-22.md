# usage-meter notes (2026-10-22)

## sources (read this session)

- claude.exe 2.1.307 (C:\Users\pablo\.local\bin\claude.exe, 297079456 bytes).
  - /api/oauth/usage and anthropic-ratelimit-unified-5h-utilization / -5h-reset / -7d-utilization / -7d-reset / -surpassed-threshold. Offsets ~132883017, ~134692076.
  - statusline stdin schema: rate_limits.five_hour|seven_day.used_percentage + resets_at (unix seconds); spend_limit is a gateway cap. Offset ~256430800.
  - UserPromptSubmit stdin is the prompt, not rate_limits. Offset ~127772056. Stdout exit 0 is wrapped as additionalContext (offset ~279319491). Zod output: hookEventName UserPromptSubmit + additionalContext (offset ~247148202).
  - pricing_tiers USD/Mtok at offset 245952187. models[].pricing map follows. Opus 5.5 = tier_4_20_cache_read_0_20 (4/20/5/8/0.20). Sonnet 5.5 = tier_2_10_cache_read_0_10 (cache read 0.10).
  - Missing hook matcher becomes "*" (matcher??"*").
  - Git Bash eats backslashes in hook command strings (statusline text ~131730800).
- changelog C:\Users\pablo\.claude\cache\changelog.md:1645 plan-usage endpoint; :1632 status line rate_limits.spend_limit; :1630 Sonnet 5.5 "$2/$10" and "$0.20/Mtok cache reads" (disagrees with the 2.1.307 catalog's $0.10).
- No plan percent on disk. stats-cache.json keys are activity totals. sessions\72984.json keys are pid/cwd/status. policy-limits.json is policy flags. .credentials.json not opened.
- Transcripts: 89 project jsonl + 2 tasks\*.output. Assistant lines monotonic by message.id (6174 dups, 0 decreases). Models: claude-opus-5-5, claude-sonnet-5-5, <synthetic> (0 tokens). cache_creation is {ephemeral_5m_input_tokens, ephemeral_1h_input_tokens}.
- ccusage source not on disk and not fetched (no network). Block rule used: start at first entry, +5h, next entry at or after the end starts the next block.

## checks

- synthetic logs/grok/usage-meter-synth: 5h weight 20.0 (deduped; 8d and previous block excluded), week weight 130.0 (8d excluded). Status `5h ~40% (resets 21:30), week ~10%, calibrated 0m`. Hook at 50, hook at 78, third hook empty. Notified 50,60,70,75,78.
- real --status: `5h n/a (resets 02:35), week n/a, uncalibrated`. Cold 569 ms. Warm 217 ms and 220 ms. --hook stdout empty, 220 ms. No logs/usage_calibration.json (fake percent was not written onto the real history).
