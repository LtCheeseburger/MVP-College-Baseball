# Mods, saves and the in-game mod manager — research notes

Status: research only, nothing below is implemented except the `game`/`assets` lookup (section 1).

## 1. Finding the game files (done)

`sdk/runtime/app/game_files.cpp` `FindAssets()` walks up to 6 folders from the exe looking for `assets`
(unchanged behaviour, the junction keeps working). New: if there is none it looks for a `game` folder that
contains `default.xbe` the same way, so `out\build\release\MVP06.exe` finds `E:\Recomp Source\MVP06-NCAABB\game`
without the junction. `.iso` files inside the folder are still scanned. Built, not yet launched.

Open: a release zip only has an empty `assets` beside the exe; consider also checking
`%USERPROFILE%\Documents\MVP06 Recomp\assets` later (see 2).

## 2. Where user data lives today, and where it should go

| What | Now | Code |
|---|---|---|
| Emulated hard disk (saves) | `<exe>\hdd\{C,E,X,Y,Z}` (`hdd2`.. for `--instance N`) | `main.cpp:295-298`, `kernel/files.cpp MountDefaults` |
| Settings | `<exe>\mvp06-settings.toml` | `app/settings.cpp:27` |
| Logs, crash dumps, texture dumps | `<exe>\logs`, `CrashDumps`, `textures` | `main.cpp`, `texturedumper.cpp` |
| Recompiled game code | `<exe>\MVP06.dll`, `<exe>\compiler` | `game_code.cpp` |

Save layout is standard Xbox: partition 1 = `E:` -> `hdd\E\UDATA\4541008d\` (title id 0x4541008D, EA).
Today it only holds `TitleMeta.xbx` and `TitleImage.xbx` — no game save has been written yet, so
the save format (XCreateSaveGame vs. raw files in UDATA, memory unit use) is **not yet observed**. Nothing in the
XDK HLE reaches `XCreateSaveGame`; `config/game.toml` only maps `XMountUtilityDrive`. First task: make a franchise/
season save in game and look at what appears under `hdd\E\UDATA\4541008d` and `TDATA`.

Proposed layout (Windows, per user):

```
%USERPROFILE%\Documents\MVP06 Recomp\
    settings.toml
    saves\E\UDATA\4541008d\...     <- mounted as HARDDISK0\PARTITION1  (other partitions C/X/Y/Z stay in AppData)
    mods\<mod-id>\mod.toml ...
    mods\mods.toml                 <- enabled flags + load order
    logs\
```

Code change is small: `kernel::Boot(..., data_dir)` already takes the folder; pass the Documents path (via
`SHGetKnownFolderPath(FOLDERID_Documents)`, which follows OneDrive redirection) and add a one-time migration from
`<exe>\hdd\E`. Keep `hdd\C X Y Z` (cache, Z: scratch) in `%LOCALAPPDATA%\MVP06 Recomp` since they are not user data.
Decisions needed: should settings also move, and should a `portable.txt` beside the exe keep everything in the
exe folder (useful for dev builds and for `--instance N` multi-instance tests)? Recommend yes to both.

## 3. How a mod can change the game

Data: `game/data` is mostly `.big` archives (128 of them; `database.big` holds `roster.bin` 88 KB, `location.dat`,
`hist.dat` 293 KB sits loose; `frontend\portrait.big`, `models.big`, `stadiums.big`/`wstad.big`, `ghead.big`,
audio `.big`/`.ast`, `.vp6` movies, `.xsh` shaders). The game reads these through the kernel file layer, which the
port already owns: `Resolve()` in `kernel/files.cpp` maps `D:\...` to the host folder or the mounted ISO.

Two layers, pick by difficulty:

1. **Loose-file overlay (cheap, do first).** In `Resolve()`, before the disc/host lookup, check
   `mods\<id>\data\<same relative path>` for each enabled mod (reverse load order). Replaces whole files:
   movies, `.ast` audio, `hist.dat`, a whole `.big`. Conflict = two mods ship the same path.
   Weakness: replacing a 380 MB `ghead.big` to change one head is unacceptable.
2. **Archive entry overlay (needed for real mods).** Mods ship individual entries (e.g.
   `database.big/roster.bin`, `portrait.big/<school>.xxx`). The game finds entries by offset inside the big file, so
   either (a) the port opens the `.big`, parses its directory (the `survey*.md` tool already walks them — it knows
   the EA BIG layout, RefPack for `feflow.txt`) and serves a *virtual* big file assembled from the original plus
   replaced entries at the `NtReadFile` level, or (b) hook the game's big-file entry lookup. (a) needs no game
   address hunting and keeps conflicts exact (per `archive/entry` key). Entries that grow need the virtual file
   rebuilt with shifted offsets, which the game cannot notice if the directory is also served virtually.
   Cache the merged directory on disk keyed by mod set hash.
3. **Code/hook mods** (native DLL with hooks.cpp-style overrides): out of scope, security issue, skip.

Roster/rating editing falls under 2: the shipped roster is `database.big/roster.bin`, but franchise saves will
carry their own copy — to find out which wins, diff a fresh save against `roster.bin`. An editor outside the game
(a small tool that reads/writes roster.bin and the save) is far cheaper than an in-game one; in-game editor is
a later phase and depends on reversing the roster record layout (88 KB / 128 schools ~ small per-team record,
players probably in another table — unknown).

## 4. Mod package format (proposal)

```
mods\<id>\mod.toml
mods\<id>\icon.png          (64x64 or 128x128)
mods\<id>\data\...          (loose overlay, mirrors game/data)
mods\<id>\archives\<name.big>\<entry>   (entry overlay)
```

```toml
id = "uniforms-2007"        # unique, folder name
name = "2007 Uniform Pack"
version = "1.2.0"
author = "..."
description = "..."
game = "mvp06-ncaa-baseball-xbox"   # sha1 of default.xbe also accepted
requires = ["base-textures >= 1.0"]  # optional
conflicts = ["other-uniforms"]       # explicit
```

Conflict detection is mechanical: build `map<virtual-path-or-archive/entry, [mods]>`; any key with >1 claimant is a
conflict, resolved by load order (last wins) and shown to the user with the winner. Also flag: wrong game sha,
missing dependency, declared conflict, entry that does not exist in the base archive, broken mod.toml.

## 5. In-game Mod Manager menu

Existing pattern: `src/game/pc_options.cpp` turned "Online Options" (feflow.txt SMARTOPTIONS, context 0x5E) into a
native list built by a factory hook (0x112BC0), rows are plain choice lists. That is **text-only**; it cannot show
icons or multi-column name/version rows. Options:

- **A. Native list screen** with one row per mod ("Name  v1.2.0 — On/Off"), conflict count in the title, a detail row.
  Consistent look, gamepad nav for free, no icons. Cheapest, ~same effort as PC options.
- **B. ImGui overlay opened from a native menu row** ("Mod Manager..."). The SDK already ships ImGui with
  gamepad/keyboard nav (`main.cpp`: NavEnableGamepad), so icons (D3D12 texture upload from icon.png), version column,
  conflict panel, drag/ordering are easy. Visually departs from the game's UI but delivers exactly what you described.
- **C. Fully native custom screen** (own fe_ screen class like fes_video_options.fel): authentic look, but needs
  reversing the screen/texture draw API for icons. Highest cost.

Recommendation: B for the manager screen, A-style native row as the entry point (reuse pc_options.cpp), C only if the
look matters later. Changing the enabled set takes effect on next game launch (the game relaunches itself on match
start already, so apply at a clean relaunch, and warn that existing saves may depend on the active mods —
store the active mod ids+versions in a sidecar next to each save and warn on mismatch).

## 6. Save management

Needs the observation in section 2 first. Planned features once the format is known: list saves (name, date, size,
mods used) in the same overlay, import/export a save as a single zip, backup-on-overwrite (keep last N copies),
cloud-sync friendly because it sits in Documents. Risk: game-side save UI (memory-unit/XDK dialogs) is untouched.

## 7. Next steps, in order

1. Launch once with `game` found without the junction (verify the log line `Game: default.xbe in ...\game`).
2. Play to a save point; record what lands in `hdd\E\UDATA\4541008d` / `TDATA` (and which XAPI/kernel calls produced it).
3. Move `hdd\E` + settings to Documents with migration and `portable.txt` override.
4. Loose-file overlay in `Resolve()` + `mods\mods.toml`; test by replacing a small movie or `hist.dat`.
5. Parse BIG directories in the port (reuse survey tool logic) and serve virtual archives.
6. Mod manager (B) with conflict report; entry row via pc_options.cpp.
7. External roster editor tool, then decide on in-game.

## Hardening log

### Item 1 — saves (2026-10-26, code done, not yet verified in game)

`sdk/runtime/kernel/files.cpp` (SDK edit): every NtCreateFile / NtOpenFile / write / rename / delete / delete-on-close /
directory listing / flush on a path containing `\UDATA` or `\TDATA` now logs `save: ...` at debug level (debug build only;
release logs Report/Error only). `NtQueryVolumeInformationFile` logs the first 16 classes asked.
Fix found by reading: `NtSetInformationFile` rename (class 10) called `fs::rename` on a file still held open by `fopen`, which
Windows refuses (save files are written to a temp name then renamed). It now closes the stream, renames, reopens at the same
position, honours ReplaceIfExists=false (returns name collision) and logs failures.
NOT verified: no real save written yet. Test: run `out\build\debug\MVP06.exe`, make a dynasty/season save and a settings
save, quit, relaunch, load; then `grep "save:" logs\*.log` and check `Documents\MVP06 Recomp\saves\E\UDATA\4541008d`.

### Item 5 — per-effect graphics options (2026-10-26, built, not verified in game)

PC Options already had Anti-Aliasing (FXAA), Upscaler, Sharpening, Effects Quality and Texture Filtering rows; defaults were
already FSR off / sharpening 0. Added rows **Depth of Field** and **Glow** (setting ids 0x9x, `pc_options.cpp`) backed by
`app::Settings::dof` / `glow` (`depth_of_field`, `glow` in the settings file, default on). Applied live in
`sdk/runtime/gpu/d3d12.cpp` (SDK edit) right after the pipeline is built in the draw path: DOF off skips any draw that read a
depth buffer as colour (`draw_depth_colour_`, the DOF combine); glow off skips the single-quad draw blending ONE + dst * blend
colour (src 1, dst 0x8001). NOT verified: both skips are inferred from the earlier DOF/glow notes; if DOF-off leaves a
black or stale frame in a match, the combine pass needs a copy-through instead of a skip.
Items 6-10 and the in-game parts of 1-4 are untouched.

### Item 9 — rebindable keyboard (2026-10-26, built, not verified in game)

`src/common/keyboard.cpp`: A, B, X, Y, Start, Back, left/right trigger, White, Black read `game::g_keys` (virtual key codes,
defaults as before). Saved as `key_a` ... `key_black` in the settings file via `GameInfo::options`. `pc_options.cpp` has a
"Key: ..." choice row per button (letters, digits, numpad, Space/Enter/Tab/Shift/Ctrl/Alt, a few symbols) and a
"Reset Keyboard Keys" row. D-pad (arrows), sticks (WASD / IJKL) and thumb clicks stay fixed. Choose-a-key rows rather than
press-a-key capture, because the screen is the game's own list. NOT verified: rows in game (25 rows, ids 0x90-0xA8, the game's
arrays end near 0xB0), the list may need to scroll; controller (SDL pad) rebinding is not done.

### Item 6 — texture filtering (code review only, nothing run)

`StageSampler` (gpu/d3d12.cpp): the Texture Filtering row forces anisotropy only where the game already filters linearly
across mips, the texture has mips and it is not a shadow map; textures the game filters with point or without mips are left
alone, so no new shimmer source. MipLODBias is clamped to D3D12's range (the game sets -100.5 in matches, which clamps to
-16 and sharpens/aliases field textures; a softer clamp such as -1.0 could be tried but was NOT tested, so no option added).
Needs in-game look at the field and uniforms at 1x vs 16x.

### Item 10 — controllers (code review only, nothing run)

`sdk/runtime/core/input.cpp`: SDL3 gamepads on the lowest free port, hot-plug through GAMEPAD_ADDED/REMOVED (a pad takes the
lowest free port, frees it on unplug), triggers 0..32767 -> 0..255, y flipped without overflow, rumble via SDL_RumbleGamepad
with duration 0. No deadzone is applied on the host (the game applies its own). Nothing found wrong by reading; hot-plug, rumble
and PlayStation/Switch layouts are NOT tested (no pads exercised).

Follow-up 2026-10-27 (built, nothing run with pads): a pad that found all four ports taken now gets the first port freed
(it used to wait for a replug); each pad gets its port as SDL player index (lights on PS/Switch pads); the log line names
the SDL pad type. New settings: `rumble` 0-100 (default 100, also PC Options "Controller Vibration", row 0xAF, the last
free id), `deadzone_left`/`deadzone_right` 0-50 % round deadzone rescaled to full range (default 0 = the game's own only),
`nintendo_buttons` = "label" (default: the button printed A is A) or "position" (south is A, as SDL reports).
PlayStation pads stay positional (cross = A); touchpad press is also Back. Button prompts: the A/X icons are plain 32x32
textures (dumps `32x32_8d37d59c1e7f7521`, `32x32_8e8edc38233890d1`), so a fixed PS/Switch icon set works through
textureseplacement; swapping them per connected pad type is not built.

### Items 2, 3, 4, 7, 8 — not started

2 audio, 3 soak and 4 relaunch handover need interactive sessions; 7 widescreen FOV and 8 frame-rate coupling need
disassembly of the game's projection/viewport and tick code, which was not attempted.

### Item 4 — match-start relaunch (code review only, nothing run)

Already built as a handover: the new process opens the old one's event (`app/handover.cpp`), keeps its window hidden, and the
old instance stays up until the new one has drawn its first frames (`FinishHandover` shows the window, sets the event). The
log carries on across `--relaunched` (`logx::Init(..., append)`), so logs are not overwritten, and settings come from the shared
settings file. Not checked in game: black flash length, focus after the swap, window position/mode (borderless vs fullscreen)
and a stray duplicate window if the new process fails to reach its first frame.

### Item 8 — frame rate (partial reading, nothing run)

Host side: `app::Pace()` (app/main.cpp) only sleeps in the swap path to the `fps_cap`. Guest side: `xdk/d3d8.cpp` drives the
game's vertical blank from a 60 Hz frame clock (CDevice VBlankCount/VBlankEvent/VBlankCallback; D3DDevice_Swap increments the
swap count shown at the next blank, and D3DDevice_GetRasterStatus is also on that clock). The game therefore sees 60 blanks a
second regardless of host cap, and a cap above 60 only renders the same guest frame more often (no new content) while a swap
still needs a blank. Conclusion so far: logic is tied to the 60 Hz blank; the 120/144/Unlimited cap rows give no extra
smoothness without interpolation, which was NOT built or tested. Keep 60 as the recommended cap; a true 120 Hz mode would
need render-side interpolation of object transforms between two guest frames, a large change left undone.
Whether 30 fps cap halves game speed or only drops frames was not tested.

### Items 1-3 — what the existing user data shows (2026-10-26)

`Documents` can be OneDrive-redirected (`C:\Users\<you>\OneDrive\Documents\MVP06 Recomp`). Saves, logs and the
settings file therefore sit in a synced folder: OneDrive can hold a file open briefly while it uploads, which would show up as
a failed open/rename/delete in the `save:` trace. If saves fail intermittently, test with `portable.txt` beside the exe.
The only log present (`logs\mvp06.log`, 13:11, release) is a 1.3 s start-and-exit: no audio, no gameplay, no DRED, so nothing
can be concluded for audio or soak. `saves\E\UDATA\4541008d` still holds only TitleMeta.xbx / TitleImage.xbx; `TDATA` empty.
The settings file predates the new keys, so `depth_of_field`, `glow` and `key_*` appear after the next run saves it.

### Item 7 — wider than 16:9 (feasibility, from code reading; nothing built or run)

Today: with `xbox_720p` on and a 16:9 output the game is told the TV is widescreen/720p (`hooks.cpp` XGetVideoFlags,
`d3d8.cpp` DisplayFlags 0x10000), so it builds its own 1280x720 mode and its own 16:9 projection. That is already native 16:9,
not a stretched 4:3. Wider (21:9, 32:9) needs three changes, none verified:
1. Projection: the game's projection reaches `D3DDevice_SetTransform` (d3d8.cpp:764, type 1) and the vertex shader constants.
   Scaling the matrix's x terms by (16/9)/(aspect) widens the view, but only for fixed function draws; programmable vertex
   shaders get the combined matrix in constants, so the hook must catch the constant upload (or the game's camera setup,
   address not found) and must leave shadow/reflection passes alone.
2. Viewport/targets: backbuffer and viewport width grow to the new aspect; every screen-sized intermediate target (glow, DOF,
   FXAA chain) follows from `guest_w_`, but game code that hard codes 1280 (HUD layout, scissor rects) will not.
3. HUD: scoreboard and menus use pretransformed (screen space) vertices; they would need an x offset/scale per draw to stay in
   the 16:9 centre, which means telling HUD draws from world draws (pretransformed flag is available per draw as
   `a.pretransformed`), and full screen quads (wipes, movies) would need stretching.
Verdict: not a contained change; the world-camera widening alone is plausible, the HUD and full-screen passes make it a
multi-day effort with a risk of regressions in the effect passes. Not implemented. 16:9 output is already correct.

### Item 2 — audio (code review only, nothing heard or run)

`sdk/runtime/xdk/dsound.cpp` is a host software mixer for XDK 4831/5849 DirectSound: buffers and streams are read from guest
memory as they play, voices live in `g_voices` (erased on Release at the two destroy paths), invalid ranges return
DSERR_INVALIDCALL, and exports still running original code are logged once (`dsound: exports running their original code`).
The one log on disk shows only the effects image line (`reverb 0 ... not reproduced`), i.e. some DSP effects (reverb among
them) are not emulated; that can make crowd/commentary sound drier than the Xbox but is not a failure. NOT checked: stutter,
dropouts, behaviour across the match-start relaunch (the mixer starts again in the new process) and after alt-tab. Test by
ear plus `grep -i "dsound\|audio" logs\mvp06.log` after a match; the release log only keeps Report/Error lines, so use the
debug build for handle failures.

### Item 1 result — first real save observed (2026-10-26 14:42, release build, user-made)

Dynasty save "Dyna1" landed in `saves\E\UDATA\4541008d\7F1F049CFF1E\`: `data.dat` 2,173,192 bytes, `saveimage.xbx` 4096,
`SaveMeta.xbx` 46 (UTF-16 `Name=Dyna1 (Dynasty)`). That is the standard XDK save folder layout (hex folder name, meta,
image, data), so the game does go through XAPI's save-game layout over the raw UDATA paths and the kernel file layer
handles it: the session ended `Exit (0)`, no error or warning in the log, OneDrive did not get in the way. Release build, so no
`save:` trace was written. NOT yet confirmed: that the save loads after a relaunch (and after the match-start relaunch),
the settings/profile save (nothing under TDATA, no profile file seen), and overwrite/delete of a save.
The settings file now carries `key_*` (defaults), proving the new options persist.

Update (user screenshot, 2026-10-26): after a relaunch the Load Dynasty screen lists "Dyna1 (136 block(s))", so the game
enumerates the UDATA folder, reads SaveMeta.xbx and computes the block count correctly (directory listing, meta read and
file sizes work). The "Trax" overlay was also showing, so menu music was playing in that session (the only audio evidence
so far). Still unconfirmed: accepting the load and the dynasty opening; settings/profile save; overwrite/delete.

Update (user report, 2026-10-26): accepting "Dyna1" after a relaunch opens the dynasty. Item 1 is confirmed for dynasty
save and load across a relaunch. Still open: loading after the match-start relaunch, settings/profile save, overwrite/delete.

Update (user "looks good" + files, 2026-10-26 15:08): `data.dat` and `saveimage.xbx` of Dyna1 were rewritten at 15:08 (same
size, SaveMeta.xbx untouched), so overwriting an existing save works. `TDATA\4541008d` is still empty: no settings/profile
file has been written by anything the user did, so the game may keep those only in memory or write them on a menu not yet
visited. Still open: delete a save, a profile/settings save, loading after the match-start relaunch (user said it "looks good",
not explicitly confirmed).

### Item 1 closed (2026-10-26 15:10-15:18)

A second save folder `EB8970ABAE91` (data.dat 3,336 bytes, SaveMeta 56) appeared at 15:10: a small profile/settings-sized save,
so non-dynasty saves are written too. Dynasty save, reload after relaunch and overwrite (15:08) confirmed; user reports
"all good it works". The release session that followed ran 515 s and ended `Exit (0)` with no error lines; the settings
list in its log shows `depth_of_field = true`, `glow = true` and the `key_*` values. TDATA stays empty (the game uses UDATA for
everything seen). Not exercised: delete, and the debug `save:` trace (user played the release build).

## Final status by item (2026-10-26)
| # | Status | Verified in the real game | User still to test |
|---|---|---|---|
| 1 Saves | done | dynasty + small save written, listed, loaded after relaunch, overwritten; 515 s clean exit | delete a save |
| 2 Audio | review only | menu music (Trax overlay) | crowd/commentary in a match, relaunch, alt-tab |
| 3 Soak | not started | one 515 s session, clean exit | 9 innings, quick games, season sim, DRED/handle growth |
| 4 Relaunch | review only | the dynasty match round trip worked ("looks good") | flash/focus/window details |
| 5 Graphics rows | built | settings persist (`depth_of_field`, `glow` in log) | DOF-off / glow-off look in a match |
| 6 Filtering | review only | - | field/uniform sharpness |
| 7 Widescreen | feasibility written | - | - |
| 8 Frame rate | partial write-up (60 Hz blank clock) | - | 30 fps cap behaviour |
| 9 Rebind keys | built | `key_*` persist | change a key, relaunch, reset row |
| 10 Controllers | review only | - | hot-plug, rumble, PS/Switch pads |

### Save safety (2026-10-27, built; copy logic tested in a host harness, nothing run in game)

`sdk/runtime/kernel/save_safety.{h,cpp}` (SDK edit), hooked in `kernel/files.cpp` (truncating create, first write per
handle, resize, rename, delete, delete-on-close, handle close), `boot.cpp` (Relaunch flushes, Shutdown flushes + exit
backup unless relaunching) and `app/main.cpp` (configure, `--restore-backup`, launch backup when not `--relaunched`).
All under `UserDir()` (`2`.. suffix for `--instance` saves2..):
- `backups\<YYYYMMDD_HHMMSS_mmm>_<launch|exit|before-restore>\` = whole saves folder; skipped when equal (names, sizes,
  times) to the newest; newest `save_backups` kept (default 10, 0 off, max 100).
- `save_history\E\UDATA\4541008d\<slot>\<time>\` = slot as it was before the first change since its last complete save;
  newest `save_history` kept (2-20, default 5).
- `save_lastgood\...\<slot>\` = slot after a complete save: no write handle open, 3 s quiet, `SaveMeta.xbx` present and no
  empty file. A deleted slot keeps its last good copy.
Every copy goes to `<dst>.partial`, is compared entry by entry (count, sizes) with the source, then renamed; replaced
folders are swapped via `.old` and put back on failure; old copies are pruned only after the new one verified; a free space
check (size + 16 MB) runs first. OneDrive cloud-only files are downloaded by the read; a failed download fails the copy.
`--restore-backup <name|lastgood>` backs the current saves up first (no prune until after), then swaps; a message box says
what happened; unknown names list the newest 15. PC Options rows 0xA9 "Save Backups", 0xAA "Versions Kept Per Save".

### PC Options: folders and Reset Settings (2026-10-27, built, not verified in game)

`src/game/pc_options.cpp`, rows 0xAB-0xAE (31 rows; 0xAF is the last id below the game's ~0xB0 array end). "Open Saves /
Logs / Screenshots Folder" create the folder under UserDir() if missing and ShellExecute "explore" it on a detached
thread. "Reset Settings (Restart)" needs two steps (Select -> "Are You Sure? Once More" -> "Reset and Restart"; any other
move disarms, so the left wrap from Select does nothing): it renames mvp06-settings.toml to
mvp06-settings.toml.bak-YYYYMMDD-HHMMSS and calls kernel::Relaunch (the match relaunch path: --relaunched, handover,
no launch.bin so the new instance boots to the title with defaults). Settings the reset loses include key bindings,
720p mode and save backup counts, since all live in that file.

## Main menu Quit Game row (2026-10-27, built, not run)

The Xbox main menu (`fes_mainmenu.fel` in `frontend.big`, group CONSOLEMENU) lists its rows in grid `GG:CELL` (1 x 7);
the list code takes the row count from that record and the MainMenu screen (vtable 0x643630) routes by the selected
text box's name through feflow.txt, except a row named QUIT, which opens the game's yes/no popup (0x517F60, prompt
string 15223) whose answer function on Xbox is empty (0x5012E0). `src/common/main_menu_layout.cpp` registers a
`modvfs::AddPatch` (new SDK call: patch one BIG entry as the archive opens, RefPack unpacked and repacked as literals)
that makes the grid 1 x 8 and adds row X0Y7 (copy of X0Y6 one row step lower, text box QUIT, label string 0xFDF0
"Quit Game" via the pc_options string hook). `src/game/main_menu.cpp` marks the quit popup in its setup (0xE1620) and
on answer 1 (yes, as the PC version's 0x518010 took it) calls `kernel::Shutdown(0)` (saves flushed, exit backup).
Game files are unchanged. The ~80 transition scenes still hold 7-row copies of the menu.
