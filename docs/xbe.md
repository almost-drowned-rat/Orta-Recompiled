# XBE analysis – Panzer Dragoon Orta (Europe)

Source: `tools\m0_analyze.ps1` (toolkit option-c `tools.xiso` / `tools.xbe_parser`), raw output in `docs/xbe-raw.txt`.
Generated data (XBE copy, file list, JSON) stays in `private\orta\` and is never committed.

## Identity

| Field | Value |
|---|---|
| Image | `Panzer Dragoon Orta (Europe).iso` (read only) |
| Title | Panzer Dragoon ORTA |
| Title ID | **0x4947002B** (`IG-043`, Sega / Smilebit) |
| Region flag | Rest of World (PAL) |
| default.xbe SHA-1 | **57213FF35A19807DEF664F9018CC2923815DFA1F** (4,292,608 bytes) – the only supported version for now |
| Build / cert date | 2003-01-17 / 2003-01-29 |
| Media | DVD_X2, disc 0, version 1 |
| Base / image size | 0x00010000 / 0x004C2700 |
| Entry point | 0x00232A9B |
| Kernel thunks | 0x003094C0 |

default.xbe SHA-256: `0776e8607d820c4e359a8059cad3aabe30629abdd13dbc33553151e43587c26d` (`config/game.toml`).

## Versions (maintainer rule 2026-11-01: one dev exe holds every version, a player uses one)

| id | version | image | state |
|---|---|---|---|
| `pal` | Europe (PAL), 6 text languages | `Panzer Dragoon Orta (Europe).iso` | the supported version |
| `us` | USA (NTSC), **title ID 0x53450007**, XDK **4928**, built 2002-11-18, default.xbe SHA-1 B9E64BC1554EFCA9FCBBDA4AA4A0C11CF5684241 | `Panzer Dragoon Orta (USA).iso` (full redump) | not supported (Orta is PAL only); its variant was removed 2026-11-04 |
| `jp` | Japan (NTSC-J), first release Dec 2002 | missing | – |

## XDK libraries

All **1.0.5028**: XAPILIB, CalcSig, D3D8, D3DX8, DSOUND, XBOXKRNL, LIBCMT, LIBCPMT, XGRAPHC.
HLE signatures must match XDK 5028 (D3D8 / DSOUND / XAPI). No XNET / XONLINE library → **no network,
no System Link, no Xbox Live** (nothing to grey out beyond what the menu offers).

## Sections (16)

`.text` (2.6 MB), `MMATRIX`, `D3D`, `D3DX`, `DSOUND`, `WMADEC`, **`PSGSFD00`, `PSGSFD_I`, `PSGSFD_B`, `PSGSFD_P`**
(CRI Sofdec movie player), `XGRPH`, `XPP`, `.rdata`, `APPDATA` (1.6 MB, writable), `DOLBY` (Dolby Digital encoder),
`$$XTIMAGE`. Kernel import `XeLoadSection` / `XeUnloadSection` → the title loads sections on demand (check which
ones and when in M1; every executable section must be lifted regardless).

## Kernel imports

116 imports. Notable: `AvSetDisplayMode` / `AvSendTVEncoderOption` / `HalBootSMCVideoMode` (video mode, PAL50/60
decision), `XeLoadSection`, `LaunchDataPage` + `HalReturnToFirmware` (title relaunch – relevant for the Saturn
bonus game, see below), `IdexChannelObject`, `MmClaimGpuInstanceMemory`, `KeConnectInterrupt` (GPU vblank ISR),
`PsCreateSystemThreadEx`, `XcSHA*` (save signatures). Full list in `docs/xbe-raw.txt`.

## Disc content (3,004 files)

- **Movies: CRI Sofdec `.sfd`** (MPEG-1 video + ADX audio; some with a separate `.aix` audio track): `movie/` (13,
  opening, endings, intros) and `p1media/` (Panzer Dragoon 1 extras). Decoded by the title's own lifted Sofdec
  code (PSGSFD sections) – no host video decoder needed (unlike XMV/Bink); check the cost in M2.
- **Music:** `.wav` (408, incl. long `_LOOP` tracks) + `.wma` (18, decoded by the title's own WMADEC section).
- **No custom-soundtrack support** in the game (maintainer, 2026-11-01): `ORTA_CUSTOM_SOUNDTRACKS` stays OFF, no
  SOUNDTRACKS menu lines.
- **Languages:** menu/movie text in `menudata/TextData/*_{EN,FR,GE,IT,SP,JP}.msg` → LANGUAGE line in RECOMP
  OPTIONS with the languages the game itself offers (check whether JP is selectable on this PAL disc in M3).
- **Pandora's Box bonus content:** `PD1.xbe` (8.5 MB, a second XBE = the Sega Saturn *Panzer Dragoon* emulation,
  path string `d:\pd1.xbe` in default.xbe), `p1media/` (its movies + `bgm*.wma`), `pd1_dra.*`.

## Saturn bonus game (maintainer decision, 2026-11-01)

`PD1.xbe` is **not** lifted or run - the Saturn emulator is struck (maintainer, 2026-11-01); everything else stays.
`p1media/` is shared: `default.xbe`'s movie list plays `P1Media\all_op.sfd` and `P1Media\endingmv.sfd` in Pandora's
Box (string table at 0x3012xx); the rest of it (`*.CGZ`, `*.BGZ`, `bgm*.wma`, `SPECL1MV.sfd`, `PDTL.BMZ`,
`TNRoman.bin`) is read only by `PD1.xbe`. `pd1_dra.*` (disc root) are Orta's own models. The installer copies the
whole disc. When the game launches it (expected: `XLaunchNewImage("d:\pd1.xbe", ...)` in
XAPI → `LaunchDataPage` + `HalReturnToFirmware`), the port shows a message that this feature is not planned
and returns to the Pandora's Box menu. The interception stays one small, named hook so a later separate Saturn
recomp could be started there instead. Exact call site: M3.

## Frame rate / video mode

To check in M1/M2: target rate (30/60), PAL50 vs PAL60 (the dashboard's PAL60 setting via
`ExQueryNonVolatileSetting` / `AvSetDisplayMode`). PAL60 rule: if the title supports PAL60 it never runs at 50 Hz.

## Licences of borrowed material

None borrowed yet. SeveerJ/Orta_PC_Recomp: MIT (xboxrecomp parts), LGPL-2.1-or-later (xemu-derived parts) – only
used as a reference so far; anything taken goes into `docs/sources.md` with repo, commit and file.
