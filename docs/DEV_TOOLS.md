# Developer tools

BASE and DEV use identical source. `MVP06_DEV_TOOLS` selects developer capabilities at build time.

| Build | Tools |
| --- | --- |
| Fresh paired DEV configuration / DEV build.bat | On |
| BASE / standalone default / BASE build.bat | Off |
| debug or dev preset | On |
| player preset | Off |
| release.bat, including an existing developer-enabled cache | Off |

For explicit builds, configure with `cmake --preset release -DMVP06_DEV_TOOLS=ON` or `OFF`, then build that directory. CMake caches explicit choices; the dev and player presets use separate output directories. Debug enables the tools explicitly. release.bat always configures OFF and rebuilds the executable before packaging. A later normal DEV build.bat restores ON.

The option gates filesystem texture replacement and dumping, F3 thread/frame diagnostics, DRED/debug-layer diagnostics, verbose logs, environment-driven test input and previews, FE/logo traces, XBE performance/frame capture, and renderer self-tests. nav.ps1 and shots.ps1 remain development scripts; player builds ignore their test environment switches. Player builds ignore custom_textures and dump_textures settings even when an existing settings file contains true.

Workshop live DDS reload, UI/native mods, normal screenshots, crash reporting and Workshop target navigation remain player features. The option does not restore the retired MVP27 PNG/hash-swap pipeline.

Release packaging copies an explicit runtime/compiler/bundled-mod file list. Development scripts, research, backups and overhaul working folders are excluded; user save backups created by the runtime remain available. Mod source projects are distributed separately from the player package.
