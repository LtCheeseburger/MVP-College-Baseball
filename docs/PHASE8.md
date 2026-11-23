> Updated 2026-11-07: supplied CSVs and official online additions now produce 309 sourced squads, 7,515 real player entries and no synthetic squads in preview.4. See [ROSTER_IMPORT.md](ROSTER_IMPORT.md) for current data, tests and limitations. The notes below describe the original placeholder preview.

# Phase 8 development status

There is no finished player dataset. The workspace league project builds `mvp27-league` version
`0.1.0-preview.1`, clearly labeled as a preview with synthetic players. It is required and affects saves.
This is not a finished 2027 roster or a published release. Full dynasty/season/save/reload/rollover acceptance
is still required before completing Phase 8.

`MVPWorkshop.Core.Formats.LeagueDatabase` reads the canonical kit identities and writes the original Xbox
database layout: 309 active schools, 311 stable roster rows (including null and retired ID 102), 30 conferences,
7,725 owned placeholder players, 3,138 pitchers, team metadata and player names. It checks duplicate identities,
capacity, player keys and name lengths. Missing real player data refuses a normal production build. Preview
placeholders require an explicit build opt-in and a version containing `-preview`; diagnostic IDs end in `-test`.

The authored baseline schedule has 8,588 games, 56 per school except 40 for Ivy schools, without same-day
double bookings. It uses the native 2006 calendar and is not an official 2027 schedule. Phase 9's annual
schedule layer remains separate. Expanded schools keep audited native uniform and stadium stand-ins.

`logotable.csv` holds 69 generic create-a-school entries. Those textures move to assets 400–468; school
logo IDs belong in roster records and `teaminfo.csv`. Native XSH directory tags and image-name attachments
are both retagged. New BIG members explicitly request RefPack compression because the native frontend
always decompresses them. The original 200-pack texture directory is relocated into separate storage with
512 records and its existing twenty image-cache slots. This avoids changing the surrounding manager layout.
The league project includes the 182 new schools' native artwork; optional `mvp27-logos` supplies all 309.

The launcher installs verified caches for the required league, optional native logos (default off) and optional
UI (default on). Repair preserves optional preferences and editable mod folders. Required leagues cannot be
disabled or removed through the launcher; `--no-mods` keeps required packages. The runtime refuses startup
when the required save-affecting league is absent. Only developer builds support the explicit diagnostic
fixture alias, selected with `MVP06_TEST_LEAGUE_FIXTURE=1`.

Save compatibility compares save-affecting mod IDs and versions. Missing, malformed, legacy unstamped or
mismatched stamps refuse access with an explanation, preserving existing save bytes. New slots receive a stamp
under `saves/.mvp27-league-stamps`, outside mounted Xbox partitions. Managed backups, restores and last-good
snapshots carry matching stamps. Cosmetics do not invalidate saves. Manual file-only save transfers must also
carry their stamp; unstamped imports are refused.

Tests: `tools/tests/Phase8`, `save_compatibility_check.cpp`, `required_mod_check.cpp`, `expansion_check.cpp`,
`poll_check.cpp`, and the three-package cases in `tools/tests/Phase3`. Runtime evidence and limitations are
recorded in workspace `artifacts/phase8/REPORT.md`. BASE and DEV remain identical authored source; build
capabilities stay BASE off / DEV on.

