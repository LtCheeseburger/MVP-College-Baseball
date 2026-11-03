# Expanded engine and player-side patches

PR #1 is integrated with room for 309 schools and 31 conferences. Databases within the stock limits keep the stock engine paths. A larger database selects the expanded paths once at boot; exceeding the supported capacities refuses startup. League data, the database builder and the 309-team dynasty acceptance run belong to Phase 8.

The guest physical range is 128 MB. The stock game keeps the existing heap extension and contiguous reserve. Expanded databases add 48 MB of game heap for larger database/player tables. Player, team registry and node-map capacities scale from kSchools; the team buffers remain 320 rows. Nine-bit schedule slots preserve the 20-byte record and its runs/errors fields, including the no-team sentinel. This does not establish compatibility between different installed league versions; the league save stamp remains separate Phase 8 work.

`config/patches/expansion.json` contains 225 authored edits, instruction/function hashes, expected owners and occurrence counts. The 104 patched function bodies are generated from the player's XBE after translation and compiled into the player's MVP06.dll. Each original DLL entry dispatches to its expanded copy only while expansion::Active(); host hooks and fitted poll settings retain their normal selection. No translated function body is compiled into MVP06.exe.

The same C++ applier runs in the developer CMake build and in the first-start compiler. It validates all owners, SHA-256 hashes and edit counts before publishing any modified partition. Empty manifests leave code unchanged. A changed revision or interrupted publication requires fresh translation. Required manifests are listed in config/game.toml; missing files refuse preparation. Sorted manifest contents and the expansion header enter runtime build stamps, so changed edits invalidate cached game code. Release packaging carries the manifests and the small authored constants header with the compiler, and omits the generated DLL/work directory.

To update the edit specification after deliberately reviewing a translation change:

```powershell
python tools/expansion/prepare_manifest.py generated/game tools/expansion/expansion_spec.json config/patches/expansion.json
```

Use an unpatched translation when binding a changed specification. This developer tool emits hashes and authored replacement snippets, never function bodies. Review the resulting manifest before rebuilding. Players need no Python.

The launcher exposes frame_aspect (hundredths; 237 for 21:9, 356 for 32:9, 0 follows the display), ultrawide_menus, walkup_volume and fitted_poll. Menus default to 16:9; the converted UI startup splash uses the whole frame. The fitted poll is optional with stock data and automatic with expanded data.

Custom walk-up songs use walkup beside MVP06.exe: _all.<extension> is the fallback. The imported PR supports WAV, MP3, M4A/AAC, WMA, FLAC and OGG, with trimming rules in walkup/index.csv. Songs enter the DirectSound mix before its final clip. Setting walkup_volume to 0 preserves the stock ditties. player_names.csv beside the executable supplies roster-keyed names (key in column 1, first/last in columns 4/5), with stock random names retained when absent. These user files are optional and are not bundled with the port.

Automated checks live in tools/tests/walkup_check.cpp, poll_check.cpp, expansion_check.cpp and translated_patch_check.cpp. Phase 7 evidence is recorded in the workspace artifacts/phase7/REPORT.md. Full manual gameplay and dynasty/save acceptance remain separate.
