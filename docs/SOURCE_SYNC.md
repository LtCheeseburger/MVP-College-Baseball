# Shared BASE / DEV source

BASE (`MVP06-Base/MVP06-XBERecomp`) is the Git authority. DEV (`MVP06-XBERecomp`) is a plain folder with identical authored source, including the SDK, launcher, tools, docs and Workshop dependency source. The approved source-difference allowlist is empty.

Make shared edits in BASE, review any existing destination differences, then mirror the exact paths to DEV. Commits belong in BASE. Do not copy Git metadata, game assets, mods, generated game code, build outputs or user data between trees.

From either tree:

```powershell
pwsh -NoProfile -File tools/sync-check.ps1
```

The checker normalizes text line endings and compares other files byte-for-byte. It excludes reviewed generated/local paths and rejects unexpected links. Do not add source drift to the allowlist merely to pass a release. In the paired workspace, release.bat checks equality before packaging; a standalone checkout can build without a DEV folder.

The same executable code supplies native behavior with the UI mod disabled and the overhaul with it enabled. UI enabled-by-default is an installation setting, not a source-tree difference. Native logos are a separate optional mod. Developer capabilities use the shared MVP06_DEV_TOOLS build option; see [DEV_TOOLS.md](DEV_TOOLS.md).

Phase 5 was reverified on 2026-11-03: zero source differences, both Release builds successful, and all twelve production overhaul/VFS integration scenarios passing. Evidence is in workspace artifacts/phase5/REPORT.md. Source equality and automated launch checks do not certify full manual gameplay, input, or dynasty/save/load acceptance.
