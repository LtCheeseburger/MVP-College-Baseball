# .mvpproject format 1

A project is a folder named `<id>.mvpproject`. Share the folder or a ZIP of its contents. It contains editable sources and explicit build recipes; it is not a runtime mod. Workshop editing UI will consume this same format later. The first implementation is the Windows/.NET 9 `tools/MvpProject` CLI, with no third-party image packages.

```text
project.json          format, mod metadata, game release, build settings
recipes.json          format and ordered recipes
sources/              editable masters (including full-resolution PNGs)
overrides/            exact-byte inputs: tables, fonts, video, DDS, xsh.meta
source-inventory.json import-time provenance and SHA-256 hashes
build/                generated folder, .mvpmod, .sha256, build-manifest.json (ignored)
```

`project.json`:

```json
{
  "format": 1,
  "mod": {
    "format": 1, "id": "mvp27-ui", "name": "MVP 27 UI", "version": "1.0.0",
    "game": { "xbeSha1": "2f57040bab2c8b2eb33ebc7aa93209ad30dd103f" },
    "layout": 1, "affectsSaves": false
  },
  "gameRelease": "MVP 06 NCAA Baseball (Xbox)",
  "build": { "archive": "stored-zip64", "folder": "build/mvp27-ui", "defaultEnabled": true }
}
```

All `mod` fields are copied into mod.json, including optional dependencies/conflicts. Change the mod version whenever shipped data changes; `data/overhaul/overhaul.json` separately records `dataVersion`. `build.archive` and `build.folder` describe the format-1 fixed outputs; the builder derives their names from the validated mod id. `defaultEnabled` is the Setup policy for new installs, not part of runtime mod metadata. Existing toggle choices are preserved.

`recipes.json`:

```json
{
  "format": 1,
  "recipes": [
    { "op": "copy", "source": "overrides/team-colors.tsv", "target": "files/data/overhaul/startup/team-colors.tsv" },
    { "op": "image", "source": "sources/team.png", "target": "files/data/overhaul/startup/base-logos/001.png",
      "width": 512, "height": 512, "fit": "contain", "format": "png", "mipLevels": 1, "padding": 0 }
  ]
}
```

Paths are project-relative, slash-separated, case-insensitively unique on output. Absolute paths, empty components, backslashes, `..`, and links within source/build paths are rejected. Inputs must be under sources/ or overrides/. Outputs must be under files/, or icon.png/readme.md at mod root. Unknown operations and unsupported image settings fail instead of silently changing data.

- `copy` preserves exact bytes. Use this for existing DDS and xsh.meta and for PNG masters that need no resizing.
- `image` resamples a master into an explicitly sized PNG/BGRA canvas with high-quality WPF rendering. `fit` is stretch (default) or contain (transparent letterbox). Limits are 1–8192 pixels per side. This version emits one mip and zero padding. Preserve atlas layout by scaling the whole canvas proportionally; UV tables remain normalized. Fixed atlas masters are copied unchanged in the UI project.
- `xsh-logo` converts a school PNG into the native logo's i/j DXT3 DDS slots and rebuilds its indexed a slot with a 256-color palette. It requires an original complete XSH template under overrides/, explicit mainSize/wideWidth/wideHeight, mainFormat/wideFormat `DXT3`, indexedFormat `PAL8`, paletteColors 256, and mipLevels `full` or 1. Dimensions must be powers of two, 4–512. Main art is square; wide art preserves its aspect ratio with transparent padding. Indexed dimensions/swizzle and unrelated slots stay intact. The manifest hashes every template member as well as the source/output files. Atlas packing remains a future operation.

## Build and test

From either shared source tree:

```powershell
pwsh -NoProfile -File tools/build-ui-mod.ps1
# Or choose a project directly:
dotnet run --project tools/MvpProject -c Release -- '<path>/mvp27-ui.mvpproject'
# Runtime developer folder (no repacking required after editing a built override):
& ./out/build/release/MVP06.exe --mod '<path>/mvp27-ui.mvpproject/build/mvp27-ui'
# Runtime archive:
& ./out/build/release/MVP06.exe --mod '<path>/mvp27-ui.mvpproject/build/mvp27-ui.mvpmod'
```

The wrapper locates workspace projects/mvp27-ui.mvpproject, builds it, and stages the bundle under out/build/release/bundled-mods. A standalone release checkout may use an already-built bundle plus checksum/defaults in that staging folder. Missing projects and missing/invalid staged bundles stop packaging.

The builder writes a fresh staging folder, rejects duplicate destinations and stale linked output paths, then replaces its own generated output. File ordering, ZIP timestamps, compression and metadata are fixed. It always emits stored ZIP64, even for a small mod, so runtime support is exercised. `build-manifest.json` records each recipe's source/output SHA-256, size and archive hash; .sha256 checks the final artifact. Rebuilding unchanged masters produces identical archive bytes.

## UI source migration

The UI project lives in workspace projects/mvp27-ui.mvpproject. Full-resolution installed masters are preserved in sources/runtime; current font/video/table/native texture inputs are in overrides. The import script tools/import-ui-project.ps1 can create a fresh project from an existing developer mod; it refuses to overwrite an existing project. It does not modify game files.

Legacy prepare_assets.py, install_game_logos.py and atlas/composition authoring material are archived as references under sources/authoring. The release build no longer runs these scripts against mutable game folders. Their current UI outputs are editable master inputs with explicit copy/resize recipes: uniform previews and team colors are frozen editable inputs, logo previews are rebuilt at 512px, and existing authored atlas masters retain their layouts. Recreating historical feed scraping or native 4K logo GPU mappings is deliberately not a build dependency. No layered PSD/XCF source was present to reconstruct.

The old -wide logo variants remain only in project sources for recovery; neither they nor replacements.tsv ship. Host UI previews stay in the UI mod. Native logos live in the separate projects/mvp27-logos.mvpproject: 127 schools, 256px main/128x64 wide DDS with complete mip chains, and native PAL8 a slots. Northern Iowa stays original. The HD pack requires port 0.2.3-beta or newer: its frontend cache grows from the original 60,000-byte slots to the largest active XSH, and the game heap has room for the larger archive/cache. No league dependency is declared before the 309-team league exists. Full-screen backgrounds are capped to the 2560×1440 virtual canvas without upscaling. Font license notices are included from their upstream distributions.

`tools/export-project-source.ps1 -Project <folder.mvpproject> -Output <outside-project.zip>` includes editable masters, recipes, templates, the builder and Workshop Core source dependency, plus build.ps1. It omits build/bin/obj output. On Windows with .NET 9, the exported project builds without this checkout or a game install.

## Installation, updates and repair

Release zip: Setup.exe plus Port Files, including bundled-mods/mvp27-ui.mvpmod and defaults.json. These are hashed managed port files. Setup copies the verified bundle to mods/mvp27-ui.mvpmod and initializes MVP 27 UI enabled for a new install. Existing mods.json entries are retained, including disabled choices. A developer folder using the same id is preserved instead of installing a duplicate archive.

The cached bundle is retained for Repair. Both quiet and interactive Repair restore a missing/changed managed mod from the verified cache. If the cache itself is corrupt, re-extract the release or update; the original disc cannot supply UI artwork. Updates replace the cache through the existing port transaction, then publish the runtime mod and any new default toggle within the same rollback list. A publication error rolls the transaction back. Player toggles and unrelated mods are never listed as port files. Full game/save acceptance remains the separate regression checklist.
