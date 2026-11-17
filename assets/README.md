# assets/ - the port's icon and pipeline list

The icon (approved 2026-11-04): an original drawing - a stylized wing of three
feathers in front of a ring on a dark rounded square. No game logo, cover art, title lettering or SEGA / Smilebit
branding (toolkit porting guide, "Icon").

| File | Use |
|---|---|
| `icon.ico` | the exe's icon in Explorer: 16, 32, 48 and 256 px (PNG entries), embedded through `orta.rc` (`llvm-rc`) |
| `icon_64.bmp` | the window / taskbar icon: 64x64, 32-bit with alpha (BITMAPV4, BI_BITFIELDS); CMake embeds its bytes (`port_icon.h`), `main.c` and the launcher hand them to the toolkit (`xbox_set_window_icon`) |
| `pipelist_orta_pal.bin` | the shipped pipeline list (v1.2.0): keys of the game's shader programs and pipelines only - no game data, the programs are referenced by file + offset on your disc. The release puts it into `data\`; the toolkit builds them at the start (`nv2a_vk_set_pipeline_list`), so effects do not stutter the first time they appear |

Both are drawn from the same shapes at each size (not scaled from one image) by `tools/icon_draw.ps1` and packed by
`tools/icon_pack.ps1 -Assets assets`.
