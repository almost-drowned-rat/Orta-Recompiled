<p align="center">
  <img src="assets/icon_256.png" alt="Orta Recompiled" width="160">
</p>

# Orta Recompiled

A static recompilation of *Panzer Dragoon Orta* (Xbox, Europe / PAL) into native PC code, with a Vulkan renderer and a
set of modern extras: higher resolutions, the game's own widescreen mode, antialiasing, button remapping and a full
settings menu inside the game.

> This project contains **no game data, no game code and no keys**. You need your own copy of the game (PAL disc).

> **AI disclosure:**  AI assistance yes (Claude Code by Anthropic).

> **v0.9.0:** I expect bugs I have not found myself yet: please report every bug you find as an issue, I will try to fix each one
> as fast as I can.

---

## A benchmark for option-c

This port is first and foremost a **benchmark for option-c**, a branch of my fork of the xboxrecomp toolkit
([xboxrecomp-option-c](https://github.com/almost-drowned-rat/xboxrecomp-option-c)).

**What option-c is:** a change of the toolkit's code model at the root. The lifter (which translates the Xbox code to
C) used to keep the guest CPU's registers in globals and access the game's state through blanket `volatile` - correct,
but the compiler could keep almost nothing in host registers. option-c gives every guest thread its own register set,
passed to each translated function as a `__restrict` context, without blanket `volatile` (after the model of
[XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp), MIT). The result is C code the compiler can really
optimise. It was only merged after a gate of conformance tests against real x86, a behaviour comparison on a real game
and micro-benchmarks ([docs/option_c.md](https://github.com/almost-drowned-rat/xboxrecomp-option-c/blob/main/docs/option_c.md)).
My earlier port, THPS 2X Recompiled, still runs on the older code model; Panzer Dragoon Orta is the first complete
game on option-c.

**Comparison** with SeveerJ's [Orta_PC_Recomp](https://github.com/SeveerJ/Orta_PC_Recomp) v0.7.1, an independently
developed recompilation of the same game (same machine: laptop with an RTX 3050 Ti, attract demo, 16:9, 480 lines,
frame cap 60, matching scenes, CPU in cores busy):

| | Orta Recompiled | Orta_PC_Recomp v0.7.1 |
|---|---|---|
| CPU, menus | 0.22 - 0.26 | 0.10 - 0.33 |
| CPU, movies | 0.21 - 0.28 | 0.62 - 1.27 |
| CPU, gameplay | 0.39 - 0.45 | 0.71 - 0.90 |
| GPU load | 5.8 % | 14.5 % |
| RAM | 375 MB | 297 MB |

These are numbers from one machine and one scene set, not a ranking - both ports are young and will change. The two
projects also serve different players: Orta_PC_Recomp runs on DirectX 11, so it also supports older hardware, and it
targets the USA version; this port is the PAL version and needs a GPU with Vulkan 1.3.

---

## Features

### Native port, no emulator
- The original Xbox code is translated to C ahead of time and compiled for the PC – no CPU emulation at run time.
- **Vulkan 1.3** renderer, **SDL3** for window, input and audio, plain **C** code.
- The game's own sound engine runs on an emulated audio chip, with the game's own effects mix.

### Rearranged menus
- **Main menu:** CONTINUE, NEW GAME, PANDORA'S BOX, OPTIONS, **EXIT GAME** (asks before it quits). The practice
  missions moved into OPTIONS.
- **Pause menu:** CONTINUE, RETRY, **EXIT GAME** (asks with the game's own dialog), SAVE & QUIT, QUIT GAME - aiming
  and vibration are set in OPTIONS only.
- New lines are in English in every language; the game's own lines keep their language.

### Rearranged options menu
Everything is set inside the game's own **OPTIONS** menu – no external tool, no overlay. **Y puts the selected line
back to its default** - only that line. Every change is kept when you leave the menu, also after a restart.

| Entry | What it does |
|---|---|
| **PRACTICE** | The game's practice missions (flying, aiming, dragon forms). |
| **SOUND** | The game's own volumes, and **SPEAKERS**: stereo or 5.1 (the game's own surround mix; applies after a restart, needs a 5.1 output device). |
| **CONTROLS** | AIMING (normal / inverted), VIBRATION and **BUTTON MAPPING** (keyboard or controller, then remap its buttons). |
| **RECOMP OPTIONS** | All port settings (see below). |

### RECOMP OPTIONS
Changes are saved immediately. Settings that need a restart are marked, and leaving the menu asks **RESTART NEEDED**.

| Setting | Values |
|---|---|
| **Display mode** | Borderless (default), Fullscreen (exclusive), Windowed – window size and position are remembered |
| **Render resolution** | From the original 480 lines up to 5x (e.g. 1080p, 1440p, 4K) – independent of the window size |
| **Aspect ratio** | 4:3, 16:9, 16:10 or 21:9 – 16:9 is the game's own widescreen mode; 16:10 and 21:9 widen the view further, the HUD moves to the screen edges; never stretched |
| **Field of view** | Original, or wider by +5 up to +30 |
| **Anisotropic filter** | Off up to 16x |
| **Antialiasing** | Off, FXAA (default), SMAA, MSAA 2x, MSAA 4x |
| **Frame rate** | V-Sync, **frame pacing** (DISPLAY = in step with your monitor, TIMER = a steady 60 Hz timer) and an FPS counter (FPS, frame time or both); the game runs at its original 60 fps |
| **Boot animation** | Plays your own intro video before the game, for example the original Xbox boot animation ;) |
| **Language** | System (follows Windows), English, German, French, Spanish, Italian |
| **Brightness and contrast** | The game's own brightness and contrast, now visible on the PC screen |

### Widescreen (16:9, 16:10, 21:9)
- Panzer Dragoon Orta has its own 16:9 mode; ASPECT RATIO 16:9 switches the game to it - menus included.
- 16:10 and 21:9 build on it: more of the world at the sides, the HUD (health, gauges, radar) at the screen edges,
  text lines kept whole.
- The mouse cursor hides itself over the game window after 2 seconds without movement.
- The game arranges some screens differently at 4:3 and 16:9 (e.g. the options background: a dense cloud layer at
  4:3, a large relief with thin clouds at 16:9) - both are the game's own layouts.

### Button remapping
- Keyboard and every gamepad (Xbox, PlayStation, Switch, generic) can be remapped per device; identical pads share
  one mapping.
- The menus always stay usable with the default layout, so a bad mapping can never lock you out.

### Everything in one folder
- The port runs from one folder that can be moved or copied anywhere – even to a USB stick.
- **Nothing is written outside it:** no registry, no AppData, no shortcuts, no temp files left behind.

### Installer built in
- A small setup window asks for two things: your disc image and the folder to install into. **Go** builds the game
  there, **Play** starts it. Your disc image is only read - it is never changed or deleted.
- The image is checked, extracted and verified; a wrong game or an unsupported version is refused with a clear
  message.
- No GPU with Vulkan 1.3? The setup and the game say so at once, with an apology, instead of crashing.

---

## Getting started

### Requirements
- Windows 10/11, 64-bit (Linux via Proton / Wine is not tested yet)
- A GPU whose driver offers **Vulkan 1.3** with the extension `VK_KHR_push_descriptor` (NVIDIA GTX 900 and newer,
  AMD RX 400 and newer, Intel HD 500 / UHD 600 and newer, Intel Arc). If something is missing, the game says so at
  the start and names your GPU, its Vulkan version and what it lacks.
- Your own copy of *Panzer Dragoon Orta* (Europe / PAL)
- About **7 GB** of free disk space during the first start (the image and its extracted files), about **4 GB**
  afterwards. **No internet needed:** the compiler ships inside the release zip.

### Supported version

| Version | Title ID | `default.xbe` SHA-1 |
|---|---|---|
| Europe (PAL) | 4947002B | `57213FF35A19807DEF664F9018CC2923815DFA1F` |

Other versions (USA, Japan) are refused with: *"This Panzer Dragoon Orta version is not supported. Supported
version(s): Europe. Please provide a supported version."*

### First start
1. Download `Orta-Recompiled-v<version>-win64.zip` (about 210 MB, the compiler included) from the release page and
   unpack it (keep the `tools\` folder next to the exe).
2. Start `Panzer Dragoon Orta Recomp.exe`. The setup window asks for your disc image (`.iso`, XISO or full redump
   image) and the folder to install into (it suggests one). Press **Go**.
3. The setup copies itself into that folder (with the folders below, each with a short `README.txt`) and sets
   everything up there, the progress in the same window:
   - the image is checked, extracted and verified,
   - the compiler ([llvm-mingw](https://github.com/mstorsjo/llvm-mingw), a fixed release, in the zip's
     `tools\download\`) is checked by its SHA-256 and unpacked – only if that file is missing is it downloaded,
   - the game's code is translated and compiled on your PC (a few minutes, only once - about 4 on a 6-core
     laptop).
4. Press **Play**. Later, start `Panzer Dragoon Orta Recomp.exe` in the install folder - it goes straight into the
   game.
5. Optional: add a boot video (see next section).

Already have an extracted disc? Put the files (or one image) into `game\` next to the exe and start it there - it
installs in place, as before.

### Updating from an earlier version
Unpack the new zip over the old folder. The game is built once more at the next start (a few minutes); your
`saves\` and `media\` stay.

> **Windows warning:** the exe is not signed. **SmartScreen** shows "Windows protected your PC" – click
> **More info → Run anyway**. **Smart App Control** (Windows 11) blocks unsigned apps completely; the port can only
> run with Smart App Control turned off.

---

## Where your files go

```
Panzer Dragoon Orta Recomp/
├── Panzer Dragoon Orta Recomp.exe   start this (sets everything up on the first start, then starts the game)
├── ORTA.exe                         the port – built on your PC on the first start
├── game/                            YOUR GAME: put the disc image or extracted files here
├── media/                           YOUR MEDIA (read only – the port never changes it)
│   └── boot.mpg                     optional boot animation
├── saves/                           your saves and settings – never deleted
│   ├── UDATA/4947002B/              the game's saves
│   └── settings.ini                 all port settings
├── data/                            caches and logs – safe to delete at any time
└── tools/                           translator, compiler and licences (used on the first start and for a repair)
```

### Game → `game\`
- One disc image (`.iso`) or the extracted disc folder. `default.xbe` must sit directly in `game\`.
- The image is deleted only after every extracted file has been verified. On any error it stays.

### Boot animation → `media\boot.mpg`
- Your own intro video, played before the game starts. It cannot be skipped – keep it short. It can be switched
  off in RECOMP OPTIONS (the change applies at the next start).
- Format: MPEG-1 video with MP2 audio. Convert any video once with the free [ffmpeg](https://ffmpeg.org), keeping it
  at full quality (1080p, 60 fps):
  ```
  ffmpeg -i boot.mp4 -c:v mpeg1video -q:v 1 -qmin 1 -bf 2 -g 30 -pix_fmt yuv420p -c:a mp2 -b:a 256k boot.mpg
  ```
- A `boot.mp4` or `boot.mkv` alone is not played; BOOT ANIMATION then says "CONVERT BOOT VIDEO TO BOOT.MPG  SEE
  README". The port never converts or changes your files.
- Never shipped with the port – you supply your own video.

---

## Controls

Gamepads work out of the box via SDL3, with the game's own vibration (OPTIONS → CONTROLS → VIBRATION). Keyboard
default (physical key positions):

| Key | Xbox pad |
|---|---|
| W A S D | left stick |
| Arrow keys | D-pad |
| Enter / Backspace | Start / Back |
| I / J / K / L | Y / X / A / B |
| U / O | Black / White |
| Z / P | left / right trigger |

Remap in **OPTIONS → CONTROLS → BUTTON MAPPING**.
**Emergency exit:** if a mapping leaves you stuck, start once with the environment variable `ORTA_BIND_RESET=1`, or
delete the `bind.` lines from `saves\settings.ini`.

---

## Advanced

- **Settings file:** `saves\settings.ini` (plain `key=value`), shared by the menus.
- **Overrides:** every setting can be forced for one run with an environment variable `ORTA_<SETTING>` (e.g.
  `ORTA_ASPECT_RATIO=16:9`); an override is not written to the file.
- **Logs:** `data\orta.log` (the previous run is kept as `orta.previous.log`); the first-start setup logs to
  `data\launcher.log`. Please attach `data\orta.log` to a bug report.
- **Frame rate or stutter reports:** start with the environment variable `RECOMP_FRAME_TIME=1` and attach
  `data\frametime.csv` too.
- **Repair:** `"Panzer Dragoon Orta Recomp.exe" --repair` unpacks, translates and compiles everything again;
  `saves\` and `media\` are never touched.

---

## Known issues

- The dragon's red special effect does not tint the whole picture: its edge shows a staircase of blocks - under
  investigation.
- Episodes 5 and later are not tested through yet.
- 120 fps comes later (the game logic runs at its original 60).
- The Sega Saturn *Panzer Dragoon* in Pandora's Box is not part of this port (selecting it shows a message).
- The game has no custom soundtracks.
- The exe is not signed (see *Windows warning* above).

---

## Roadmap

- **v0.6.0-beta:** open beta - playable from the start (tested through the first episodes), the reworked options
  menu.
- **v0.6.1-beta:** setup window, rearranged main and pause menus with EXIT GAME, vibration, options kept on leaving.
- **v0.9.0:** 16:10 / 21:9, field of view, frame pacing, 5.1 sound; water and Pandora's Box background fixed; the
  freeze at the end of episode 3 fixed; less memory.
- **Next:** bug fixes from your reports, then v1.0; the USA version.
- **Later:** 120 fps (the game's logic decoupled from the frame rate).

### Non-goals
- Online play (the game has none).
- The Sega Saturn bonus game.

---

## Building from source

The release exe is built from this repository and the toolkit
[xboxrecomp-option-c](https://github.com/almost-drowned-rat/xboxrecomp-option-c); Windows 10/11 x64.
Folder layout (`tools\paths.ps1`; every path **without spaces**, the compiler and Ninja use them directly):

```
<root>\projects\orta\<this repo>      generated files go to <root>\projects\orta\private\
<root>\tools\xboxrecomp_orta          the toolkit; its dependencies go to <root>\tools\private\
<root>\roms\xbox\                     your disc image (optional, lift.ps1 -Iso takes any path)
```

1. `powershell -ExecutionPolicy Bypass -File tools\setup.ps1` – installs Python 3.12, CMake, Ninja, LLVM-MinGW (UCRT)
   and the Vulkan SDK with winget, and `capstone` for Python.
2. `tools\deps.ps1` – builds glslang and SDL3 from source with the same compiler.
3. `tools\lift.ps1 -Iso "<path to your PAL .iso>"` – extracts the disc into the install folder and generates the C
   code (into a `private\` folder next to the repo, never into it).
4. `tools\build.ps1` – builds into the portable install folder `Panzer Dragoon Orta Recomp\`.
5. Optional: `tools\release.ps1 -Version <name>` – the single release exe (the launcher with its payload).

---

## Legal

This repository and its releases contain no game data, no XBE, no generated game code and no keys. You need your
own legally obtained copy of the PAL disc.

This project is not affiliated with or endorsed by SEGA or Smilebit. *Panzer Dragoon* and all related names and
trademarks belong to their respective owners.

---

## Credits & third-party code

| Project | Used for | Licence |
|---|---|---|
| [sp00nznet/xboxrecomp](https://github.com/sp00nznet/xboxrecomp) | toolkit base: XBE lifter, kernel/runtime, game template | MIT |
| [TGP482/XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) | the code model of option-c (per-thread register context) | MIT |
| [antoxa2584x/burnout3-sw](https://github.com/antoxa2584x/burnout3-sw) | Vulkan NV2A renderer, back-end interface, pushbuffer executor | MIT |
| [xemu-project/xemu](https://github.com/xemu-project/xemu) | MCPX APU core, ADPCM decoder, NV2A definitions, fixed-function vertex shader | LGPL-2.1-or-later (these files) |
| [comoria08/defjam_recomp](https://github.com/comoria08/defjam_recomp) | lift pipeline and seeds workflow | MIT |
| takedawn-xboxrecomp | XDVDFS disc reader | MIT |
| [iryoku/smaa](https://github.com/iryoku/smaa) | SMAA antialiasing | MIT |
| [miniz](https://github.com/richgel999/miniz) | unpacking the compiler | MIT |
| [phoboslab/pl_mpeg](https://github.com/phoboslab/pl_mpeg) | boot animation (MPEG-1 video, MP2 audio) | MIT |
| [SDL3](https://github.com/libsdl-org/SDL), [glslang](https://github.com/KhronosGroup/glslang) | window, input, audio; shader compiler | zlib; BSD/MIT |
| [Python](https://www.python.org) 3.12 (embeddable), [capstone](https://github.com/capstone-engine/capstone) | the translator on the first start (shipped in `tools\`) | PSF; BSD |
| [mstorsjo/llvm-mingw](https://github.com/mstorsjo/llvm-mingw) | the compiler (shipped in the release zip) and its runtime | Apache-2.0 with LLVM exception; MinGW-w64 runtime licences |

Reference (read, no code or data taken): [SeveerJ/Orta_PC_Recomp](https://github.com/SeveerJ/Orta_PC_Recomp) - the
other Orta recompilation, a great help while tracking down rendering and timing issues. Details: `docs/sources.md`.

Full licence texts and attributions: the toolkit's `NOTICE` and `LICENSES/`, and in the release `tools\licenses\`.

## Licence

GPL-3.0-or-later – see [LICENSE](LICENSE). Third-party code keeps its own licence.
