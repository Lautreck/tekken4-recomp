# collaboration (Claude <-> Grok; AI-facing, dense; both obey)
Source: mutual review 2026-10-21 (logs/grok/review_claude_on_grok.json, logs/grok/review-claude_20261021_215022.json).
Goal (user): one seamless team, each keeps its specialisation.

## ROLES
- Claude (lead): live loop = instrument -> one game/PCSX2 run -> read logs -> decide; patches; test suite; commits;
  docs/status.md; memory; ALL user-facing text; final say on what ships. Evidence: stall moved only through Claude's
  trace+run loop; commits are Claude's by rule.
- Grok (specialist): static RE of ELF / generated C++ (addresses, call chains, packed data), primary-source research
  (PCSX2 source, EE/VU manuals) with quotes, upstream-readiness review of a patch file, second opinion on a trace ALREADY
  on disk, scoped fixes in -Owns files with a fixed run script and DOD. Evidence: sound-iop, vif-mode section 2,
  patch09 must-fixes, pad2-fix (patch 13). Weak at: guessing a live mechanism without the decisive log lines.
- Do not give both the same open mechanism at the same time. Grok gets a mechanism question only with a stable log
  (pasted lines) or after Claude has one.

## HANDOFF
- Claude -> Grok: tools/grok_task.ps1, prompt blocks TASK | CTX (refs) | FACTS (decisive log lines VERBATIM: addresses,
  counters; hypotheses labelled "hypothesis") | ASK | LIMITS | DOD. No prose, no word cap (the schema is the cap).
- Grok -> Claude: the final JSON only (verdict, conf, facts[{c,ref,v}], fix[{where,change}], contra[], checks[],
  open[], files_changed[]). Notes/scripts under logs/grok/ are scratch, not the handoff.
- Claude reads v=true facts, contra[] and open[] before acting; adopted facts cite the json path in status/commits.

## VERIFICATION
- Claude checks every Grok fix against the pasted log numbers; a contradicted fix is dropped (e.g. TADR==MADR rule vs
  write pos 0x13d490 != 0x13d350).
- Grok code: Claude reads the diff, the named screenshot/stdout, runs the tests, then commits (commit body names the
  Grok log). Claude patch aimed upstream: Grok reviews the patch file first.
- "Cause found" / "fixed" (to the user, notifications, commits) only after a later run shows the named symptom gone.
  A fix that does not fire on the failing trace is never called the fix; commit it only with a test and a status line
  saying the symptom did not move.

## RESOURCES
- Run lock: one of run_capture / pcsx2_capture / pine at a time (logs/run.lock, taken by the run scripts).
- Builds: out\runtime (Claude) and out\grok (Grok) may overlap only while no -Owns source file is mid-edit; Claude does
  not build out\runtime while a Grok-owned file is dirty (wait for its JSON or set the file aside).
- Grok tasks in flight: no cap (user: "as many as wise"); Grok's thinking runs on xAI servers, its tools on the user's
  PC. What must not overlap a timed game / PCSX2 run (keys are pressed at fixed seconds, fps is measured) is CPU-heavy
  side work: builds (a Grok build waits for logs\run.lock to be free: build_runtime.cmd) and big scripts (scans of the
  ELF / DATA.BIN / generated C++ taking more than ~1 min: check logs\run.lock first, wait while it is held).
- Ask mode cannot run the game or PCSX2 (deny rules); work mode may, holding the run lock.
- Permissions are frozen at auto + deny (e20d920 + later deny additions): no mode changes on live tasks.

## WORK LOOP RULES
- Claude C2: one fight iteration changes one log field; write the predicted line before the run; stop the series when
  the new field repeats; <=3 fight runs per merged cause before re-planning (or handing the trace to Grok).
- Claude C7: a work task whose notes file is newer than its last run dir and meets the DOD is stopped
  (tools/grok_stop.ps1 -Name <task>); no waiting for a second write-up.
- Claude C8: user asks "progress/what's left" -> done / running / next + the one number that changed.
- Grok G1: no fix unless FACTS hold the numbers it must match; else missing lines -> open[], conf<=0.5.
- Grok G2: one mechanism in the verdict; a second mechanism fitting the same bytes -> open[].
- Grok G3: work mode, DOD met -> notes + final JSON, stop.

## DISAGREEMENT
- Contradicted fix dropped. Next step = the single deciding observation (e.g. PCSX2 ring dump), done by whoever holds
  the run lock. Still disagreeing after it -> ask the user. No third mechanism write-up before that observation.

## RECORDS
- Only Claude writes docs/status.md, memory and commits; Grok never commits/pushes. Upstream PR text stays as in
  status "Publishing". Every Grok result json keeps turns / cost_usd / seconds (cost claims must be checkable).

## WHEN CLAUDE IS OUT OF USAGE
- Claude commits+pushes everything first (restore point), writes docs/ai/handoff.md (steps: ### STEP name | ask|work | owned files, then the prompt blocks), and starts `powershell -File tools\grok_lead.ps1`.
- The driver runs the steps in order through grok_task.ps1 and stops at the first failure (unparsed JSON, contra[] non-empty, work step without owned-file change); journal logs\grok\lead_journal.json.
- Grok never commits; on return Claude reads the journal, reviews each diff, runs the tests, commits what holds and restores the rest.
