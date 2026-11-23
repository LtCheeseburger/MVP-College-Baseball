# Modern on-field rules — implementation status

Updated 2026-11-07. The user expanded the implementation scope to modern on-field rules. The dynasty feature gate currently implements eligibility/recruiting rules only. No pitch-clock or automatic-penalty implementation is shipped by this review.

## Authoritative target

Use the [NCAA 2027–28 rulebook](https://ncaaorg.s3.amazonaws.com/championships/sports/baseball/rules/PRMBA_RulesBook.pdf), published October 2026. This is the league's first season; the older clock guide alone is insufficient.

| Rule | Target | Implementation status |
| --- | --- | --- |
| Pitch clock | 20 seconds; motion begins before expiry; batter ready with at least 8 seconds left | Implemented; pitcher and batter penalties, pause, pitch-motion stop, inning end and HUD natively tested (below) |
| Between batters | Division I 30-second timing, with pitcher/batter enforcement | Implemented (30 s before first pitch to each batter); natively observed after a walk |
| Conferences | Six offensive and six defensive; coach limits and extra-inning exceptions apply | Defensive: implemented (6 through nine innings plus one per extra inning) on the native mound visit; natively tested. Offensive: no native mechanic exists in this game |
| Mound visits / offensive timeouts | Division I 30-second timing; medical exemptions and warning/penalty sequence | Not implemented |
| First base | Required double first base; 15-inch standard, 18-inch bases permitted at institutional facilities | Not implemented: bases are part of each stadium's field mesh (36 stadium packs), not separate models; needs stadium art/geometry work |
| Ten-run ending | After seven innings when conference policy or mutual pregame agreement permits; respect home-team opportunity | Native logic decoded and matches (below); runs in played games without false endings; native ten-run endings observed in simulated dynasty games |

See rules 1-7, 5-9, 6-5-f, 9-4 and Appendix F. Two-disengagement limits, a three-batter minimum, ABS challenges and an extra-inning runner must not be assumed universal requirements. The [2027 experimental rules](https://ncaaorg.s3.amazonaws.com/championships/sports/baseball/rules/2027PRMBA_ExperimentalRules.pdf) need separate explicit configuration where applicable.

## Native findings and next integration steps

The isolated exhibition reached native gameplay after fixing a resource-loader/render lock inversion. The temporary event trace was removed from authored source after gathering evidence.

- Dispatcher `0x18A850` delivers native events to the live rule handler `0x9EE00`. Observed start-of-play event 58 and batter-ready event 113 after the gameplay HUD appeared.
- Pitch motion begins at event 100; release is event 71. A clock must stop at motion, not release. Pause/resume and dead-ball state must be handled explicitly.
- Events 82 and 75 reach the native ball/strike helpers. Inspection shows that incrementing these counters alone does not establish the complete walk/strikeout and runner-advancement transition. Do not substitute direct count-byte edits for a validated native penalty path.
- Native ten-run option is setting 44 at options storage offset `0x1C0`; native game-ending code tests it. Reuse the native path after validating inning/home-opportunity behavior instead of inventing a second score-ending path.
- Before enabling clocks, validate game-thread time progression, pause/replay suspension, repeated violations, fourth-ball forced advancement, third-strike outs, inning end, substitutions and saved results. The clock also needs a visible gameplay display and playable human/CPU timing.

These findings are implementation guidance, not completed acceptance. A fresh dynasty/save smoke check cannot certify any of these on-field behaviors.

## Native regression evidence (2026-11-07)

Prepared combined-mod and modern-dynasty-disabled exhibitions both reached gameplay and remained responsive for a 40-second no-pitch interval. In the combined/default-settings run, screenshots about 34 seconds apart retained a 0–0 count without an automatic ball. This confirms the missing pitch-clock enforcement in that configuration; it does not certify batter, between-batter or conference timing. The first staged loading attempt stalled and another exceeded its preparation deadline, so loading reliability is not fully certified. Native ten-run ending acceptance remains pending. Detailed evidence and failed attempts are retained in the workspace's `artifacts/all-rules-tests/TEST_REPORT.md`.

## Pitch clock implementation (2026-11-08)

`src/game/pitch_clock.cpp`, active while the 2027 league expansion is active (no separate toggle yet).

- The clock starts at native START_OF_PLAY (58) and stops at PITCH_INITIATED (100), the start of the pitching motion. It is timed on the game object's frame counter (`[[0x6CA090]+8]+0x4C`, 60 per second), checked by the pitching-interface update `0x1FB350` on the game thread. The limit is 30 s for the first pitch to a new batter (after NEW_BATTER, walk, strikeout or a half-inning change) and 20 s otherwise. End of play, substitutions and mound visits stop it.
- **Pitcher violation:** calls `0x17CA90` on the umpire crew `[game+0x6F8]`. That is the game's own online delay-of-game penalty (from `0x1F88E0`, event 180). It runs the umpire's ball handler `0x174EC0`, which broadcasts BALL, walks the batter on ball four through `0x16CA10` with native forced runners, ends the at-bat (`0x16AE30`) or prepares the next pitch (`0x16AD90`), and resumes play. No count or score bytes are edited.
- **Batter violation:** if the batter is not ready (BATTER_READY 113) with 8 s left, the code reruns the native `0x17CA90` sequence with the umpire's called-strike handler `0x174CA0` in place of the ball handler. That handler owns the strikeout (strikes setting `[0x6A44EC]`), outs and statistics. The native batter normally reports ready within a frame, so this path needs `MVP06_TEST_BATTER_VIOLATION` (dev builds only) to test it.
- **HUD:** an ImGui overlay in the top-left corner (UI mod only; drawn from the UI callback in `game_info.cpp`) shows PITCH CLOCK or BETWEEN BATTERS with the time left, turning red at 8 s or less, and BALL or STRIKE plus VIOLATION for 3.5 s after a penalty.

Native evidence (workspace `artifacts/all-rules-tests`, staged runtime `artifacts/pitch-clock/run`, isolated profiles):

| Run | Result |
| --- | --- |
| `clock-idle` | 200 s with no input: 8 pitcher violations. The fourth automatic ball fired the native WALK (67) event and put a runner on first, followed by NEW_BATTER and a 30 s clock |
| `clock-batter2` (forced `MVP06_TEST_BATTER_VIOLATION`) | Each batter violation was enforced at 8 s left. The third strike fired the native STRIKEOUT (74), and three strikeouts ended the half-inning. In the bottom half the CPU pitched with 13 s left, which stopped the clock with no penalty |
| `clock-pause5` | Pause at 20 s left: the clock stayed at 20 s through a 30 s pause, resumed at 19 s on the native resume event (176), then enforced normally |
| `clock-pitch` | Controller pitching: all 11 pitch motions stopped the clock, and there were zero penalties. Natural balls and walks went through normally |
| `clock-hud3` | Presented-frame captures (after the UI pass) show BETWEEN BATTERS 0:25, a red 0:06, and the BALL / VIOLATION notice. Normal screenshots are copied before the UI pass, so they never show host overlays |

Defect found and fixed during testing: BATTER_READY is sent once per plate appearance, but readiness had been reset at every pitch, which caused false automatic strikes from the second pitch on. Readiness now resets only for a new batter (`clock-pause5` and `clock-pitch` had no false strikes).

Limits: every run was force-closed after recording its result (`ForcedExit` in the summaries), not exited normally. Startup stalled twice at 14 s and was retried; this is the existing loading instability, not the clock. Substitutions, mound visits, replays, saved box-score statistics and a played game to completion were not exercised in these runs. The clock pauses for those natively because END_OF_PLAY, SUBSTITUTION and MOUND_VISIT stop it, but that is untested. There is no separate toggle yet; the clock follows the league expansion.

## Ten-run ending — native logic

`0x8A6D0` calls `0x79C30` when regulation is not complete. The mercy check applies only from the 7th inning (`inning+1 >= 7`), only when option `[options+0x1C0] == 1`, and only with a margin of at least 10 runs. After a completed top half, only a home lead ends the game. In the bottom half, a home lead of 10 ends it immediately. At the end of a completed bottom half, either side's 10-run lead ends it. The check never ends a game during a top half. This matches rule 5-9 and its home-team opportunity, so the native path is kept unmodified. The option stays the native user/policy setting (NCAA makes it a conference or mutual-agreement rule, not mandatory). Played-game check (2026-11-09, option on via the dev setting, CPU vs CPU, no injected scores): Texas beat Coppin State 6-2 and Lindenwood (lowest-ranked, all D-) 5-2. `0x79C30` ran every half-inning from the 7th onward and returned 0 every time; both games ended normally at FINAL. This confirms the check is wired and does not end close games. Ten-run endings observed (2026-11-14, `tenrun1`, copy of the May 24 2027 dynasty save, option on, the user's June 4 regional run in native MANAGE/coach mode, no injected scores): the native simulation of other schools' games that day ended three games through `0x79C30` returning 1, each immediately followed by the next game starting at inning 1: 12-2 with 0 outs in the bottom of the 9th, 14-4 with 2 outs in the bottom of the 8th, and 15-1 after a completed top of the 8th. All three match the decoded rule (home lead of 10 ends a bottom half immediately; a completed top half ends only on a home lead). The user's own game ended 5-2 at full length. On-field ending observed (2026-11-14, `tenrun5`, fresh copy of the same save, option on, the user's own June 4 game in native MANAGE/coach mode (the coach screen, not the rendered 3D field), the native engine choosing every pitch, no injected scores): the home side led 10-0 with 2 outs in the bottom of the 8th, `0x79C30` returned 1, and the game ended there. The dynasty autosave then queued the final user result, verified the original serializer, and committed (8,691,976 bytes). Runs 2-4 (losses or margins under 10) went the full length with every check returning 0.

## Still not implemented

- Offensive/defensive conference limits and timing (only native MOUND_VISIT 161 exists; there is no offensive-conference mechanic and no enforcement).
- Double first base (geometry, collision, runner/fielder targeting).

## Defensive conferences

Implemented in `src/game/pitch_clock.cpp` with `src/common/pause_menu_ui.cpp`. The only native conference is the mound visit (event MOUND_VISIT 161), chosen from the pause menu's PITCHING/DEFENSE screen (`IGROSTERPOPT`, row 0). A 161 within 10 s of that screen is the user's charged conference. The allowance is 6 through nine innings plus one per extra inning; the inning comes from the native game-state record read by the ten-run check `0x79C30` (+4, zero-based). Counts reset with each new game object. When the allowance is used, the pause menu clears the native row's enabled flag (shown disabled) and the global pad hook drops A while that row is highlighted, because the native menu still accepts A on a row it did not disable itself.

The game already disables a second mound visit in the same inning natively; the NCAA allowance applies across innings.

Native evidence (`mound` runs, isolated exhibition, TEXAS vs FLORIDA): with the normal allowance, a visit through Start, ROSTER OPTIONS, PITCHING/DEFENSE, MOUND VISIT logged `defensive conference 1 charged (allowance 6)`; the native menu then showed MOUND VISIT disabled for the rest of the inning. With the dev override `MVP06_TEST_CONFERENCE_ALLOWANCE=0`, the row showed disabled, the accept was blocked and no conference was charged. Reaching six charged visits across six innings was not exercised. CPU mound visits are not counted. Runs were force-closed after recording results.
