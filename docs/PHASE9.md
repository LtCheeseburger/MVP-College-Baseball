# Phase 9 optional layers — implementation and acceptance status

Updated 2026-11-07. Phase 9 remains in progress. All five optional preview mods exist, are disabled by default, and have `productionReady: false`. No Phase 10 release or publication was performed. The required league archive still embeds the roster data; optional layers do not replace those rosters.

## Implemented

- Annual schedules: embedded 2027 fixtures and deterministic subsequent-year generation, 72 native schedule slots, 8,588 games across 309 schools and 30 conferences, retaining original 20-byte game records.
- Competition: 29 authored tournament graph profiles plus the separate Patriot series, division qualification, dated standings snapshots, and native 64-team national-field validation. MAAC reseeding owns reset cancellation so a required second final remains playable.
- Standings and awards: active-school scratch collection covering all 309 schools and ID 310; expanded standings row clearing and bounds checks. Original award records, rank arrays and save layouts remain intact.
- Modern dynasty: five class labels, restoration of 341 imported graduate classifications, class progression limits, no new redshirts, and a 966-prospect listed recruiting pool (580 position quota), behind its own save-affecting feature gate.
- Saves and launcher: Continue Dynasty selects the newest compatible complete native save; result-triggered autosave queues a final user-game transition and waits for a stable dynasty menu. Publication uses save_safety staging, history, last-good and compatibility stamps. Native payloads retain original serialization and four-section signatures.

## Native evidence

Evidence lives in the workspace's `artifacts/phase9` test directories; these are local artifacts, not distributed source or game bodies.

| Check | Result |
| --- | --- |
| Annual native import | Exact 8,588 fixture comparison, 309 schools, 30 conferences |
| `competition-results6` | 30 champions, 321 controlled postseason results, native 64-team collector validated against the full 309-school poll; normal exit |
| `all-five-save` | All five mods plus UI loaded; native awards collected all 309 active schools through ID 310; signed native save published (8,691,976 bytes); normal exit |
| `all-five-continue` | Cold Continue Dynasty loaded the isolated all-five save; exact 8,588-fixture comparison passed; normal exit |
| Optional-layer disabled exhibition | Previously passed normal startup and exit |
| Focused unit checks | Annual generation, tournament graph capacities/home assignment, awards bounds/lifetime, class normalization, poll/rank refresh, autosave trigger guards, save publication/compatibility, translated-patch atomicity/guarded fallback |

The competition fixture writes controlled scores and invokes native result callbacks. It is not a played or simulated season. The save fixture forces serialization after reaching a stable menu; it does not prove gameplay-triggered autosaving. The integration driver signals success only after the competition probe returns; the harness rejects probe failures and fatal C++ exceptions.

The translation manifest contains 421 hash-verified edits and 188 function copies, with original game bodies generated and compiled only on the player's PC. Guarded replacements preserve original instructions when optional features are off.

## Dynasty menu regression fixes (2026-11-07)

- Playoff Tree no longer loops forever when the native pending-game search encounters a completed expanded bracket. The bounded search covers every descriptor, skips cancelled if-needed games, and checks row bounds. The copied SE Louisiana May 2027 dynasty opened and rendered the ACC bracket successfully.
- Calendar allocation, construction, bindings, refresh, cleanup and FEL day/highlight/promotion grids now support six weeks. The native five-week month-end cutoff is bypassed in the expansion, retaining May 30 and May 31, 2027. Native controller navigation and actual date text were verified for both days, with a rendered screenshot showing May 31 selected.
- Regression checks cover an 18-game completed bracket, a pending game beyond the original 12-descriptor window, cancelled resets, invalid rows, and all three calendar FEL resources. These menu checks do not certify a complete season or every tournament's visual layout.

## Remaining acceptance and limitations

- A complete played/simulated season and annual rollover, including modern-class graduation, are not certified.
- Completed user game followed by autosave (2026-11-09, `MVP06_TEST_DYNASTY_PLAY`, copy of the May 24 2027 save): the driver opened the native calendar, used Go To Day on the June 4 regional, chose MANAGE on the native play card (the native coach mode plays every pitch; defaults taken at each decision), and the game ran to its result. Log: `dynasty autosave: final user result queued`, `original serializer verified 3249382 used / 8691712 capacity bytes`, `saves: native dynasty autosave committed (8691976 bytes)`. A new process loaded it through Continue Dynasty at **Sat. June 5th, 2027**. PLAY mode could not be driven unattended: dynasty keeps the controller on the user team (no CPU-vs-CPU handoff), and plain A input stalls at the pitcher-substitution menu.
- Manual standings scrolling and visual class/redshirt flows remain pending.
- The authored league policy intentionally makes every active program postseason-eligible, including transition schools; this is a game policy rather than literal NCAA transition eligibility.
- The required roster table retains its stock-compatible clamp; the optional modern-dynasty runtime restores graduate classes from the matching ledger without editing source assets. Five-class graduation and a complete annual rollover remain pending.
- Authored schedules and tournament policies include documented fallbacks; they are not a claim that every 2027 policy has been officially announced.
- Existing Phase 8 short squads, reserve selection, source-year exceptions and estimated ratings remain documented in PHASE8.md and ROSTER_IMPORT.md.

All source changes must be authored in BASE and mirrored exactly to DEV. Source equality is checked by `tools/sync-check.ps1`; it does not certify pending gameplay acceptance.

## Roster / recruiting acceptance (2026-11-07)

League preview 4, modern-dynasty preview 2 and combined preview 2 contain the reviewed roster/class changes. A fresh isolated combined dynasty reported both native listed-prospect containers at 966, imported the exact 8,588 fixtures, published a signed native save, and cold-loaded it through Continue Dynasty with fixture equality. A full-table check verified exactly 341 graduate byte changes in the 7,515-player table, preserving all other bytes. This does not certify a full recruiting cycle, signing, graduation or rollover. Older save compatibility stamps are retained and enforced.

The field-loading acceptance exposed a renderer/resource-loader lock inversion. The overhaul UI callback now skips a frame while the mod mount is busy, allowing native loader/render work to proceed; the isolated exhibition then reached HUD/gameplay.

Modern on-field enforcement is not implemented by these dynasty changes. See [ON_FIELD_RULES.md](ON_FIELD_RULES.md) for the researched 2027 scope and remaining native integration.

## Expanded regression testing (2026-11-07)

All 22 focused cases across 15 suites passed. A real native TEXAS simulation completed 56 regular games at 44–12, saved at May 24, cold-loaded successfully, and advanced normally to May 31 with Regionals scheduled. The native 64-team field validated against 30 conference champions and the full 309-school poll. A played-save audit found all 8,588 original fixtures preserved among 8,652 regular rows; the 64 additional native rows explain why the strict fresh-season equality probe cannot be reused for played saves. No controlled score injection was used for this season test.

Further native simulation reached June 8, Super Regionals, the CWS scheduling notification, the champions screen and the offseason retirement screen. Testing exposed an actual modal ownership defect: alternate native text/format popup setup paths could leave the live CWS notification hidden. Those three setup paths now reopen host modal ownership; the notification was displayed and accepted in retesting. These milestones do not certify recruiting/signing, five-class graduation or next-year rollover, and the remaining acceptance list above still applies. Some successful integration cases required forced process termination after recording their result; the workspace report distinguishes those from normal exits.

Detailed results, screenshots, archive hashes and diagnostic failures are in `artifacts/all-rules-tests/TEST_REPORT.md` in the paired workspace.

## Native annual rollover acceptance (2026-11-08)

The isolated copy of the year-end TEXAS dynasty (`rollover-user`, copied from `year-end-user`; the original was not modified) was advanced natively by the `MVP06_TEST_ROLLOVER` driver. The driver follows the actual native screens and dialogs; no results or rosters were injected. Sequence: NCAA Championships sim, CWS scheduling notice, CHAMPIONS, advance to off-season, FRETIREMENT, FPLAYERPROGRESS (an optional position-change dialog was cancelled), FINCOMING, FPROMOTEDEMOTE, FHIRESTAFF, then the dynasty home on **Fri. February 18th, 2028**.

| Check | Result |
| --- | --- |
| Prospect generation | Native fill 0 → 966 listed prospects at the rollover (`recruiting acceptance` probe) |
| Recruiting/signing | Native incoming list showed signed recruits ("Written Accepted") and walk-ons. The user roster was 21/30 after departures; native Sign brought it to the 25-player minimum, which the game enforces before a new season |
| Schedule regeneration | `annual schedules: 2028 verified 8588 games / 309 schools / 30 conferences` |
| Autosave | **Defect fixed:** the rollover had no user game result, so it was never autosaved. The verified 2028 import now queues one native autosave (`QueueSeasonAutosave`, same serializer, signature checks and publication path). Retest: `saves: native dynasty autosave committed (8691976 bytes)` at the 2028 home |
| Cold Continue | A new process loaded the autosave through Continue Dynasty and reached **Fri. February 18th, 2028** |
| Identities/classes | `MVP06_TEST_PLAYER_AUDIT` audits (7,775 keys) before and after. Every player who stayed on a team advanced exactly one class: FR→SO 2,247, SO→JR 1,323, JR→SR 1,742, SR→GR 1,503. No legacy redshirt bits. Native rows are reused for newcomers, so a key is a row identity; player identity was taken from the row's first attribute word |
| Departures/arrivals | 341 of 342 GR players left their teams. Underclass departures: FR 108, SO 60, JR 110, SR 79. Arrivals: 809 freshmen and 100 transfers between teams |
| Roster bounds | Before: 263 teams at 25 and 46 short squads (13–24). After: 309 teams at 25 and one container (index 116) at 23 |

**Open defects found by this acceptance (not fixed):**
- **Sixth-year eligibility (fixed):** the native departure-reason routine `0x4EF2B0` still treated SR as the final class, so departing GR players took the transfer branch and were re-rostered elsewhere as sixth-year GR (57 in the first run). Expansion edit `4EF2E3` makes GR the final class (Graduated/Drafted) and sends seniors down the native underclass path. Retest (`rollover-gradfix` + cold Continue audit): 339 of 342 GR left, **0 re-rostered under the same identity** (their database slots were reused for 326 new freshmen and 13 JUCO players); 90 SR departures, 24 of them signed elsewhere as GR transfers; departure screen shows GR "Graduated", SR "Transfer". 3 GR stayed and were reset to FR (see below).
- Roster bounds after the fix: all 310 containers at 25 (the earlier 23-player container did not recur). Open: 3 GR and single FR/JR/SR players kept their slot with class reset to FR, consistent with slot reuse by the native generator; not yet explained.
- **Walk-on placeholders (resolved by the graduate fix):** before the fix, every incoming walk-on was an identical "1B #51" rated 37. After the fix (`rollover-gradfix`), walk-ons have generated names and varied ratings (53–69 on the TEXAS incoming screen), and all 873 new arrivals league-wide have distinct attribute rows. Existing roster players showed "POS #slot" (empty first name, last name "POS #n") because this dynasty was started from a roster that never ran the native name pass (`0x91C30`). **Fixed:** after a dynasty load, rows with that placeholder whose key is in the league's `player_names.csv` get their real names (`game::ApplyPlaceholderNames`); generated players on reused keys keep their names. Retest: 7,514 placeholder rows named on the year-end save; the departures screen showed real names. **Unmapped roster handles (fixed):** after the rollover, the last schools held newly generated recruit keys with no player-table row: the batter table (25 x schools + 50 = 7,775 rows) is full when the final CPU signings run, and callers such as `0x4F2885` store the key themselves. Raising the table capacity was tried and reverted: the batter-table save section is written at capacity with no row count (old saves misread), and the extra rows surfaced as placeholder walk-ons that stored empty handles when signed. The save format is unchanged. Instead, after the off-season (the verified new-season schedule import, before the rollover autosave), `expansion::DropUnmappedRosterHandles` empties any roster slot whose handle is not in the key map, as the native departure step empties a slot. Retest (`rollover-sweep` + cold Continue audit): one such slot emptied (school 308), 0 unmapped handles, 308 schools at 25, school 308 at 24 (short CPU rosters are native), the user team Texas at 26 with every signing kept, real-name walk-ons rated 56-69.

Runs were force-closed after recording their result. A mercy-rule ending was observed in the user's own coach-mode game (coach screen; a rendered 3D-field ending is not yet seen; see ON_FIELD_RULES.md). Manual UI review remains pending.
