# Phase 8 roster import — 2026-11-07

The roster is built into `mvp27-league.mvpmod`; the installed mod includes original-layout player tables, roster records, names, ratings and the source audit. Authoring CSVs and downloaded HTML are not required at runtime.

Current preview: `0.1.0-preview.4`, 309 sourced squads, 7,691 selected real player entries, zero synthetic squads. The authoring record preserves all 11,597 source entries and 3,906 reserves. 8 schools have fewer than 25 supplied players; those squads retain their available real players.

Official online additions:
- Mercyhurst: 26 published players, 2027 roster, https://hurstathletics.com/sports/baseball/roster/2026-27
- New Haven: 35 published players, explicit 2026 fallback, https://newhavenchargers.com/sports/baseball/roster
- West Florida: 40 published players, 2027 route with heading “2026 Fall Baseball Roster”, https://goargos.com/sports/baseball/roster/2027
- West Georgia: 44 published players, 2027 roster, https://uwgathletics.com/sports/baseball/roster/2027
- Southern Illinois: 34 published players, explicit 2026 fallback (no published 2027 roster), https://siusalukis.com/sports/baseball/roster — re-captured 2026-11-22, replaces a 13-row partial list
- Bethune-Cookman: 35 published players, explicit 2026 fallback, https://bcuathletics.com/sports/baseball/roster — re-captured 2026-11-22, replaces a 13-row partial list
- 2026-11-22 re-check: 47 schools re-imported from official pages with fuller lists (41 labeled 2026 fallbacks; 6 on 2027 rosters: Alcorn State, Arkansas-Pine Bluff, Bucknell, Cal Poly, UC Davis, Mercyhurst). Survey, snapshots and per-school results: workspace `artifacts/roster-recheck-2026-11-22`. Dual published jerseys (3) are kept as `published_jersey` with the engine jersey left blank.
- 2026-11-23 second pass: 16 more schools from pages the survey could not read (WMT/Nuxt season rosters, Sidearm 2026 pages behind coach-only 2027 defaults, Arkansas table) or that were missing/renamed in the source list: Clemson, Miami, Stanford, Little Rock, Jacksonville State, Iona (2027); Notre Dame, Texas A&M, Alabama, Arizona, Oregon, Kennesaw State, Arkansas, Grambling, LIU, UAlbany (2026). Remaining short squads: Saint Peter's 16, Pacific 18, San Francisco 20, Radford 21, VMI 23 and Princeton 24 (2027) match their official pages; Texas Southern 21 and Prairie View A&M 23 keep earlier 2026 data because their current pages list fewer (17 and 20).

Preserved facts include full names, positions, jersey, class, handedness and any published measurements, hometown and bio links. Engine name abbreviations and authored field assignments are audited separately. Unsupported measurements/redshirt/hometown conversions retain native defaults instead of guessed values. Imported skin-tone values use the native validated subset; no headshot-based inference is performed.

Supplied ratings retain their labels (stats, prestige or prospect ranking). The 100 players selected for the four online additions use explicitly estimated native role ratings, not fabricated statistics. Left/right splits use the supplied shared ratings; unprovided repertoire/fielding values retain native role templates. Twelve cross-school statistics IDs (24 source rows, 12 selected players) are quarantined: their statistics-derived ratings are not applied. Original supplied ratings and links remain in `suppliedRatings`; affected players use labeled native role estimates. This conservative join correction does not verify all other statistical links. This is not a completed, independently verified final 2027 dataset.

Reimport through `tools/MvpProject`:

    dotnet run --project tools/MvpProject -c Release -- import-roster <original-csv-folder> <league-sources> <stock-loose-game> <output-folder> [online-rosters.json]

Preserve `player-registry.json` in the output folder across imports. It assigns keys once to active players and reserves, retains removed entries, and reuses existing keys. Name/identity corrections and transfers require reviewed registry changes rather than deleting/recreating the registry. No source files in the user's roster folder are modified.

The three native rotation bytes following each roster's player list are remapped to the new player indices. Merely copying the old record tail leaves invalid references when a squad is shorter or reordered. Native batting-order words also stay with the same lineup template rather than borrowing batting orders from another school. Tests verify both structures.

Full dynasty season/save/reload/rollover acceptance and source/ratings review remain open. Existing preview versions are preserved under workspace `artifacts/phase8/preview1` and `preview2`; league version guards preserve and refuse incompatible old saves.

Preview 4 preserves source classes 1–5 as `modernClass` and emits `data/league/graduate-classes.csv` for 341 selected graduates. The required native table stays stock-compatible (graduate clamped to senior); the matching modern-dynasty runtime overlays only those class bytes in memory when enabled. Registry keys are unchanged. The quarantine changes five selected squad entries through existing role selection; no new identities are minted. Use a fresh dynasty with the matching save-affecting preview versions.
