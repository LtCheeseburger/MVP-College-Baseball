# Shared BASE / DEV source

- BASE (`MVP06-Base/MVP06-XBERecomp`) is the Git authority; DEV (`MVP06-XBERecomp`) stays a plain folder. Keep their authored source identical.
- Make new shared changes in BASE first, then copy the exact reviewed paths to DEV in the same task. Review any pre-existing difference before overwriting it; preserve unrelated user work.
- Run `pwsh -NoProfile -File tools/sync-check.ps1` after every sync and before finishing a source-editing task. It works from either tree. Resolve unexplained differences; never add source differences to the allowlist merely to make the check pass.
- Keep `AGENTS.md`, tools, docs, SDK source, launcher source, and dependency source synchronized too. Do not copy `.git` metadata or turn DEV into a checkout.
- Game assets, mods, generated game code, saves/settings, logs, build output, Workshop bin/obj, and unpacked releases are local artifacts; do not synchronize them as source. BASE's overhaul remains off without the enabled UI mod.
- Validate the affected builds. `release.bat` checks source equality when the paired workspace is present; standalone checkouts may build without a DEV folder.
- Source equality does not certify the still-pending full gameplay/manual acceptance items in MVP27_PLAN.md. Do not mark those passed without evidence.
