# BASE/DEV source comparison

BASE owns Git history; DEV stays a plain folder. Run `pwsh -NoProfile -File tools/sync-check.ps1` from either tree after each sync and before finishing a source change. The script locates both folders automatically; `-Base`, `-Dev`, `-Allowlist`, and `-Report` allow explicit paths.

Make shared changes in BASE first and mirror the exact reviewed paths to DEV. For existing divergence, inspect and back up the destination before copying; never overwrite unexplained changes blindly. Keep SDK, launcher, dependency source, docs, tools, and AGENTS.md identical as well. Assets and build output stay local. `release.bat` refuses packaging when the paired workspace exists and the checker fails.

`sync-allowlist.json` remains empty. An approved exception requires exact path, status, both comparison hashes and a reason. Never blanket-allow runtime files or add exceptions to hide drift. Prefer shared feature gates when developer-only functionality is needed.

The comparison is bidirectional. Strict UTF-8 authored text gets in-memory line-ending normalization; binary files compare as bytes. Explicit exclusions cover game/build/log/generated roots, launcher bin/obj, Workshop Core bin/obj and historical docs/m0 game fixtures. A root named MVP-27-College-Baseball-v<version> is excluded only when it contains Setup.exe and Port Files/port-manifest.json, identifying an unpacked release. Repository source, icons and example mods remain included. Unexpected links outside exclusions fail.

Source equality was established on 2026-11-02; see workspace artifacts/source-sync/REPORT.md for inventory, backups and validation. No full gameplay acceptance is implied. Future changes follow the same BASE-first, mirror, check workflow in AGENTS.md.
