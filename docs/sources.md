# Sources – borrowed code and data

Everything taken from elsewhere: origin (repo, commit, file), licence, where it is used.

| What | Origin | Licence | Used in |
|---|---|---|---|
| Template code (main.c, recomp_manual.c, tools/*.ps1, CMakeLists.txt) | toolkit option-c `templates/game` (after sp00nznet/xboxrecomp `templates/new-game`, commit 193e299) | MIT / toolkit licence | `src/`, `tools/`, `CMakeLists.txt` |

**Removed 2026-11-04:** 726 US function entry points from [SeveerJ/Orta_PC_Recomp](https://github.com/SeveerJ/Orta_PC_Recomp)
c5c7eed `orta/seeds/seeds_manual.json` (were `config/variants/us.toml` and, carried over with `tools.xbe_transfer`,
678 seeds in `config/variants/pal.toml`). This table called them MIT, but only that repo's xboxrecomp part is MIT;
its own files declare no licence, so by the rule "unclear = reference only" they were taken out. The PAL lift runs
on the lifter's own analysis plus seeds from this port's own scan of the PAL XBE (`config/variants/pal.toml`).

Reference only (read, not copied): SeveerJ/Orta_PC_Recomp `orta/src/recomp_manual.c` (e.g. which XAPI functions
the pad reads go through - the PAL addresses were then found in the PAL XBE), `xboxrecomp-main/src/kernel/kernel_bridge.c`
(Saturn bonus game launch via the launch data page), `docs/KNOWN_ISSUES.md`.
