Panzer Dragoon Orta Recompiled v{VERSION} (Windows 10/11, 64-bit)

A static recompilation of Panzer Dragoon Orta (Xbox, Europe / PAL) into native PC code with a Vulkan renderer.
This download contains no game data, no game code and no keys. You need your own copy of the game (PAL disc).

AI disclosure:  AI assistance yes (Claude Code by Anthropic).

The port is playable from the first episode to the end credits - I played it through myself. If you still find a bug,
please report it as an issue on GitHub, I will try to fix every reported bug as fast as I can.

Start:
  1. Unpack this zip (keep the tools\ folder next to the exe) and start 'orta_launcher.exe'.
  2. The setup window asks for two things: your disc image (.iso) and the folder to install into.
     Press Go.
  3. No internet needed: the compiler (llvm-mingw, in tools\bundle\, checked by its SHA-256) is copied
     along and unpacked, then the game's code is translated and compiled on your PC - a few GB of disk
     space and some minutes. Then the game starts. Later, start 'orta_launcher.exe' in the install folder -
     it goes straight into the game (the game itself is data\orta.exe). Nothing is ever downloaded: if
     tools\bundle\ is missing or damaged, download this release zip again.
  Your disc image is only read; it is never changed or deleted. Quitting during the installation asks
  first and stops cleanly.
  Updating from v1.2.x: run the new setup into your old install folder. The old
  'Panzer Dragoon Orta Recomp.exe' there is removed; your saves and settings stay.

Requirements: a GPU with Vulkan 1.3; your own Panzer Dragoon Orta (Europe) disc
(default.xbe SHA-1 57213FF35A19807DEF664F9018CC2923815DFA1F). Other versions (USA, Japan) are not supported.

Settings: in the game's own options menu, RECOMP OPTIONS: POST PROCESSING (brightness, contrast,
antialiasing, texture filter, bloom, upscaling with AMD FSR 1.0), DISPLAY (display mode, render resolution,
aspect ratio, field of view), PERFORMANCE (V-Sync, FPS / frame time / game speed on screen), RENDERING
(shader compile, shader notice), language, boot animation; CONTROLS > BUTTON MAPPING for keyboard and
controller. F12 saves a screenshot into screenshots\.

Windows warning: the exe is not signed. SmartScreen shows 'Windows protected your PC' - click
'More info' -> 'Run anyway'. Smart App Control (Windows 11) blocks unsigned apps completely; the port can
only run with Smart App Control turned off.

Everything stays in its folder: no registry, no AppData, no shortcuts. Saves: saves\, logs: data\.
Full documentation and source: https://github.com/almost-drowned-rat/Orta-Recompiled
Licences: LICENSES\ (the port GPL-3.0-or-later; the toolkit MIT; the audio emulation data\xbox_apu.dll
LGPL-2.1-or-later - its complete source is the LGPL-source-and-notices zip on the release page and, after
the setup, tools\licenses\source\; llvm-mingw Apache-2.0 with LLVM exception; third-party parts under their
own licences).

Not affiliated with or endorsed by SEGA or Smilebit. Panzer Dragoon and all related names and trademarks belong
to their respective owners. The Sega Saturn Panzer Dragoon in Pandora's Box is not part of this port.
