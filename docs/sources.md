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

Android (2026-11-11): `packaging/android` follows SDL3's `android-project` template (libsdl-org/SDL, zlib licence:
manifest layout, the SDLActivity subclass pattern); SDL's Java glue is compiled straight from the SDL3 source
(`deps\src\SDL3\android-project\app\src\main\java`), not copied into this repo. Nothing is contributed back to SDL.

Android Vulkan fallback (2026-11-11, maintainer's request): `packaging/android/app/jni/vkshim` loads, on Adreno GPUs
whose system driver lacks Vulkan 1.3, the Mesa **Turnip** driver (MIT) from the prebuilt **purple-turnip T30** package
(github.com/MrPurple666/purple-turnip, release `vturnip_mrpurple_T30-toasted.adpkg`, SHA-256 in
`deps\android\purple-turnip-T30\SOURCE.txt`) through **libadrenotools** (github.com/bylaws/libadrenotools 8fae8ce,
BSD-2-Clause, compiled from source into the APK; its linkernsbypass submodule likewise). Neither is copied into this
repo. **Open before any public Android release:** ship Mesa's and libadrenotools' licence texts in the APK, and clarify
the purple-turnip repository's own licence (it declares none; the driver itself is Mesa = MIT).

Reference only (read, not copied): SeveerJ/Orta_PC_Recomp `orta/src/recomp_manual.c` (e.g. which XAPI functions
the pad reads go through - the PAL addresses were then found in the PAL XBE), `xboxrecomp-main/src/kernel/kernel_bridge.c`
(Saturn bonus game launch via the launch data page), `docs/KNOWN_ISSUES.md`.
