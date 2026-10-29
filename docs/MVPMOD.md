# `.mvpmod` — mod format and loading (spec, format version 1)

This is the contract between MVP Workshop (packs mods), the launcher (enables and orders them) and the runtime (serves
them). It builds on `docs/LOOSE_ASSETS.md`. It replaces the M0 prototype (`MVP06_M0_MOD` + `mod.txt`) and the
`mod.toml` proposal in `MODDING_DESIGN.md` section 4.

## 1. Rules
1. **The base game is never written.** Nothing under `game/` changes when a mod is installed, enabled, disabled or
   removed. Mods are served virtually as files are opened. `manifest.json` verify stays clean with any mods on.
2. **A path is the address.** A mod file at `files/<path>` replaces (or adds) the game file at `game/<path>` in the
   loose layout. That one rule covers plain disc files, archive members (`data/models_big/...`), nested archives and
   single images inside a texture (`.../x.xsh/00_tex0.dds`). The mod needs no list of what it replaces: the runtime
   works it out from the zip's directory.
3. **Only changed files go in a mod.** Anything a mod doesn't contain comes from the game (or an earlier mod).
4. **Changes apply at the next launch.** The mod set is fixed when the game boots.

## 2. The file
A `.mvpmod` is a zip (stored or deflate; nothing else). Names inside are `/`-separated and matched case-insensitively.
```
mod.json          required
icon.png          optional, square, 64-256 px
readme.md         optional
files/...         the game paths it replaces or adds (rule 2)
```
An unzipped folder with the same contents (`mods/<anything>/mod.json`) is also a mod. That is the development form:
Workshop and modders can test without packing.

### `mod.json`
```json
{
  "format": 1,
  "id": "uniforms-2007",
  "name": "2007 Uniform Pack",
  "version": "1.2.0",
  "author": "...",
  "description": "...",
  "game": { "xbeSha1": "<kImageSha1>" },
  "layout": 1,
  "requires": [ { "id": "base-textures", "version": ">=1.0.0" } ],
  "conflicts": [ "other-uniforms" ],
  "remove": [ "data/frontend/frontend_big/unused.fel" ],
  "affectsSaves": false
}
```
| Key | Required | Meaning |
|---|---|---|
| `format` | yes | This spec's version. The runtime refuses a higher one and says so. |
| `id` | yes | `[a-z0-9-]`, unique. Two enabled mods with one id: the second is refused. |
| `name`, `version`, `author`, `description` | name + version | Shown by the launcher. `version` is semver. |
| `game.xbeSha1` | yes | The release it was made for. A mismatch disables the mod, with the reason. |
| `layout` | yes | `LOOSE_ASSETS.md` format number the paths refer to. |
| `requires`, `conflicts` | no | Checked by the launcher and again by the runtime (missing dependency = mod disabled). |
| `remove` | no | Game paths to hide. Only archive members (they drop out of the rebuilt directory) and added files of earlier mods. |
| `affectsSaves` | no | `true` for roster/database mods: the launcher warns before loading a save made with another set. |

## 3. Where mods live and which are on
- `mods\` beside `MVP06.exe` (`app::ExeDir()`, whatever `portable.txt` says; changed 2026-10-28 from
  `Documents\MVP06 Recomp\mods`). Every `*.mvpmod` and every sub-folder holding `mod.json` there is a mod. Save
  records (`save_mods\`) stay in the user folder with the saves.
- `mods\mods.json`, written by the launcher (and by hand if needed):
  ```json
  { "format": 1, "order": [ { "id": "uniforms-2007", "enabled": true }, { "id": "fix-pack", "enabled": false } ] }
  ```
  Later in `order` wins. A mod found on disk but missing here is added at the end, **disabled**. A listed mod no longer
  on disk is skipped and logged.
- Command line (testing, Workshop "Run with this mod"): `--mod <file or folder>` adds a mod on top of the list for
  this run only (repeatable); `--no-mods` starts with none (safe mode, also offered by the launcher after a crash).

### What a mod may ship, per kind of game path
| Mod path | Effect |
|---|---|
| a plain disc file (`data/movies/intro.vp6`, `data/hist.dat`) | replaces it |
| a member inside an `_big/` folder, at any nesting depth | replaces that member; a new name is **added** to the archive |
| `.../x.xsh/NN_tag.dds` (or `.bin`) | replaces that one image; the rest of the texture comes from the game |
| a whole new `.../y.xsh/` folder **with** `xsh.meta` | a new texture (new logo, uniform). A mod that ships `xsh.meta` for an existing texture replaces that folder completely (its own image count and layout) |
| a new `.../z_big/` folder **with** `_big.json` | a new nested archive. `_big.json` for an *existing* archive is refused (order and RefPack flags belong to the game) |

Members a mod replaces keep the original's RefPack flag: mods ship them **unpacked** and the runtime packs them as it
does today. Added members are served unpacked unless their name is in the mod's own `_big.json` `refpack` (for new
archives) or in `mod.json` `"refpack": [paths]`. Large unpacked additions to archives the game loads whole
(`datafile.big`) can exhaust its 40 MB heap, so Workshop packs them by default.

## 4. What the runtime does
At boot (`kernel::modvfs::Init`, before the first file is opened):
1. Read `mods.json`, open each enabled mod (zip directory only, no extraction), check `mod.json` (section 2).
2. Build one index: lower-case game path → (mod, zip entry). Later mods overwrite earlier ones. Each overwrite is a
   **conflict** and is logged once: `mods: data/x_big/y.fel from fix-pack (over uniforms-2007)`. A packed
   archive (`*.big`) or a whole `.xsh` file where the game has the folder is refused with a log line: ship members and
   DDS images instead.
3. Log the active set: `mods: 2 on - uniforms-2007 1.2.0, fix-pack 0.3.0`.

### One merged tree for every reader
Today three readers walk the disk directly: `files.cpp` (`Resolve`, `NtCreateFile`/`NtOpenFile`,
`NtQueryFullAttributesFile`, `NtQueryDirectoryFile`, `ReadGameFile`), `LooseBig::Build` (recursive walk of `x_big/`)
and `xsh::Build`/`xsh::Size` (reads `xsh.meta` and the image files of a folder). Mods must reach all three, or
a replaced file shows up in one and not the other (for example the right size from `NtQueryFullAttributesFile` but the
old bytes from the archive).

So M1 adds a small layer in `modvfs`, and the three readers go through it instead of `std::filesystem`:
```cpp
// game/ on disk + mod index - removed paths; paths are relative to game/, any case
struct Node { bool dir; uint64_t size; int64_t time; };
bool Stat(std::string_view path, Node& out);
std::vector<std::string> List(std::string_view dir);        // merged and sorted, disk spelling first
bool Load(std::string_view path, std::vector<uint8_t>& out); // a zip entry or the disk file
```
- `LooseBig::Build` gets its `found` list from `List` (recursively) and its bytes from `Load`. Disc `order` first,
  then added names, exactly as it treats loose extras today; `remove`d names are left out.
- `xsh::Build`/`Size` take the folder path and read through `Load`, so a mod image in one slot is used with the game's
  `xsh.meta` and other images.
- `files.cpp`: a path the mod index holds as a plain file is opened as a `BytesView`/zip-backed `Virtual` before the
  host lookup; `Stat` answers attribute queries; `List` answers directory queries, so added files can be found.
- The `built` cache in `OpenLoose` stays (the mod set can't change while the game runs).
- With no mods enabled the layer is a pass-through to the disk, so the unmodded path is the Phase 2/3 code as tested.
- Modified times: a modded file reports the zip entry's time, so "is this newer" checks see a change.

**Loose installs only.** The game is installed extracted down to the lowest level (members, DDS images), and mods
are written against that layout. Packed `.big` files are not a target: mods are never applied to them.

Failures never reach the game as broken data: a bad zip or `mod.json` disables that mod; an unreadable entry falls
back to the next source; a bad DDS goes through the existing `textures: ... shown blank` rule.

### `AddPatch` with mods
The game's own patches (`main_menu_layout.cpp` on `fes_mainmenu.fel`, feflow) run on the winning bytes, as
`LOOSE_ASSETS.md` 6 says. A mod that replaces `fes_mainmenu.fel` therefore still gets the Quit Game row. If the
patch can't apply to the mod's version it logs and leaves the mod's bytes alone (today's behaviour).

**Cache:** none in v1. If rebuilding modded archives slows loading (to be measured, like `LOOSE_ASSETS.md` 7.4), add
`%LOCALAPPDATA%\MVP06 Recomp\cache\` keyed by the archive name plus the hashes of its modded members.

## 5. Saves
Each save written while mods are on gets a sidecar `mods.json` in its save folder (ids + versions of the active set).
When the launcher (or game) opens a save whose set differs and any of the mods involved has `affectsSaves`, it warns.
Nothing is blocked.

## 6. Making a mod (Workshop)
- **Export:** diff an edited loose tree (or Workshop's open archives) against `manifest.json` hashes; every changed or
  new file goes into `files/`, nothing else. Workshop fills `game.xbeSha1` and `layout` from the manifest.
- **Validate:** every path either exists in the manifest, is a new member under an `_big/` folder, or is a new file
  (allowed, warned). DDS images are checked against their slot like the runtime does.
- **Test:** writes the mod as a folder and starts the game with `--mod <folder>`.

## 7. Not in v1
- Code/DLL mods (security; see `MODDING_DESIGN.md` 3.3).
- Partial edits inside a file (text/record patches, e.g. one row of `roster.bin`). Mods replace whole members. A
  later format may add `patches/` driven by Workshop's record editors.
- In-game mod manager (the launcher owns the list; `MODDING_DESIGN.md` section 5 options stay open).

## 8. M1 status (2026-10-28)
Implemented in `sdk/runtime/kernel/`: `mods.{h,cpp}` (Init, mods.json, zip reader, mod.json checks, index, merged
tree), `inflate.{h,cpp}` (deflate for zip entries), `modvfs.cpp` (LooseBig walks and reads through the tree; whole mod
files; `MVP06_M0_MOD` removed), `xsh.cpp` (images through the tree), `files.cpp` (opens,
attribute and directory queries, `ReadGameFile`), `app/main.cpp` (`--mod`, `--no-mods`).

Tested outside the game (a test program on the real loose install, BASE `out\build\release\assets`): with no mods and
with an identity zip mod, `frontend.big` and `awards.big` are byte-identical; a folder mod replaces a packed member
(packed again), adds one (unpacked), removes one, changes one texture slot (other members and textures unchanged) and
replaces `FEENG.LOC`; a wrong-release mod is left off; the later of two mods wins.

**Workshop (M2, 2026-10-28):** mods are made by opening the loose install in MVP Workshop and editing there.
`MVPWorkshop.Core/Editing/MvpModExporter.cs` packs the session's edits: only files that differ from the install, at
their loose paths; members unpacked; a texture contributes only its changed DDS images (the whole folder with
`xsh.meta` when its layout changed); `.big` refused; `mod.json` filled in (xbe SHA-1 from the install's
`default.xbe`, layout 1). App: on a loose install **Export Mod** (Ctrl+E) asks for name/id/version/author/description/
affects-saves and saves a `.mvpmod`; **Test in Game** writes the edits as `%TEMP%\MVP-Workshop	est-mod` and starts
`MVP06.exe --mod` beside the install. Disc and packed folders keep the old mod-output export. Tested headless: an edited
member and one DDS slot exported as exactly two files, and the runtime loaded the `.mvpmod` with the member's bytes.
Not yet clicked through in the app. Adding new files (members, textures) isn't possible from Workshop yet.

**Launcher (M3, 2026-10-28):** MVP Workshop `src/MVPSetup` Mods tab (`ModList.cs` + `LauncherWindow.ModsTab`):
lists the mods folder, turn on/off, order (lower wins), add `.mvpmod`, remove, shows files overridden between mods,
missing `requires`, declared conflicts and refused mods (same checks as the runtime), "Play without mods" (`--no-mods`).
Writes `mods.json` on every change. The runtime read a `mods.json` in that format correctly (offline test). The tab
itself is built but not yet clicked through.

**In game (2026-10-28):** `--mod docs/m1/invert-splash` showed the modded splash (one DDS slot in a loose archive). **Not tested yet:** menus/match with mods, `mods.json` reading; `requires`/`conflicts`. (Packed-archive support removed 2026-10-28: loose installs only.)
In-game test mod: `docs/m1/invert-award` (scrambles award texture `aw00`):
`MVP06.exe --mod "<repo>\docs\m1\invert-award"`. The log should show `mods: 1 on - invert-award 1.0.0`.
`docs/m0` and `run_m0.bat` are obsolete.

## 9. Milestones
| | Work | Done when |
|---|---|---|
| M1 | Runtime: miniz (vendored), `Init`, index, merged tree (`Stat`/`List`/`Load`) used by `files.cpp`, `LooseBig` and `xsh`, `--mod`/`--no-mods`, retire `MVP06_M0_MOD` | with no mods: menus, a match and a dynasty load as before. With a folder mod and the same mod zipped: a menu string, one texture slot, an added archive member and a replaced plain file all show in game; `verify` still clean |
| M2 | Workshop: export, validate, test-run | an edited archive exports to a `.mvpmod` holding only the changed members |
| M3 | Launcher: mods list (enable, order, conflicts, missing deps, wrong release), `--no-mods` safe start | user toggles two overlapping mods and sees the winner change |
| M4 | Save sidecar + warning | loading a dynasty with a different `affectsSaves` set warns |
