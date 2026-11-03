# MVP 27 UI switch (Phase 2)

The overhaul code is compiled into both BASE and DEV executables. It activates once per launch only when `mvp27-ui` is enabled and the mounted mod set supplies `data/overhaul/overhaul.json`. A loose marker in the base game is insufficient. `--no-mods` overrides both `mods.json` and all `--mod` arguments.

The existing developer folder in DEV is `out/build/release/mods/mvp27-ui/`. BASE shares the code but has no UI mod installed by this source sync. It contains the marker, startup images, fonts, strings, video and loading-bar texture. Its `affectsSaves` is false. Phase 3 now provides workspace projects/mvp27-ui.mvpproject/build/mvp27-ui.mvpmod and its developer folder. See docs/MVPPROJECT.md for building, installation and default toggles.

From DEV, run the current build with:

```powershell
# One launch with the developer UI folder enabled.
& ./out/build/release/MVP06.exe --mod ./out/build/release/mods/mvp27-ui

# Safe mode, including when --mod is supplied by another tool.
& ./out/build/release/MVP06.exe --no-mods
```

Alternatively, enable **MVP 27 UI** in the launcher. Mods found on disk without an enabled entry in `mods/mods.json` remain off. Restart after changing the UI toggle; this switch and UI caches intentionally do not change during a session.

Fonts and text load through `overhaul::Load`/`Text`. The generic SDK UI-image decoder also reads through `kernel::mods::Load`; it must retain that support instead of reverting to filename-only WIC loading. Media Foundation reads a unique temporary copy of mounted video bytes, which the decoder releases after completion or shutdown. Native files are never deleted by this cleanup. The legacy 4K logo replacement-map path is absent; Workshop's independent live-DDS support remains available.

All overhaul callbacks and hooks return to native behavior while disabled. Workshop startup routing is a separate opt-in feature and continues to work in safe mode. The 1024-entry UI heap and guest-texture access remain compiled in.

## Checks

Run the headless integration checks with PowerShell 7 and the configured Visual Studio/Clang toolchain:

```powershell
& ./tools/tests/check-overhaul.ps1
```

The script compiles the production VFS/overhaul sources into a separate test executable and writes fixtures/results under workspace `artifacts/phase2/`. It checks folder and packed mods, marker/id requirements, safe mode, VFS precedence, video-copy isolation/cleanup, native-file preservation, and separate-process on/off/on activation.

Game startup tests and evidence are in workspace `artifacts/phase2/REPORT.md`. These use isolated user folders and the existing BASE game as a read-only source. Full exhibition, controller/keyboard interaction, PC Options, and dynasty save/load acceptance still require manual testing. Phase 3 release packages include the UI mod and current code; older Phase 1 zips remain historical artifacts.

## Native logos (Phase 4)

The optional mvp27-logos.mvpmod is separate from the UI. Its editable project supplies native 256px/128x64 DXT3 logos with mipmaps and native indexed palette art for 127 stock schools; Northern Iowa stays original. Port 0.2.3-beta adds capacity for these assets in the frontend's twenty-slot cache and balances game-heap growth against a 16 MB contiguous reserve. The tested 64 MB layout uses a 46 MB game heap with the HD pack; safe mode retains the stock policy. Host UI previews remain 512px PNGs. No legacy GPU hash mapping or runtime 4K/-wide PNG output ships. See workspace artifacts/phase4/REPORT.md for validation and the remaining manual gameplay/save checklist.
