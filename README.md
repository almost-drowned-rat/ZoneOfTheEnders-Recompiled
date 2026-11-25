<p align="center">
  <img src="assets/icon_256.png" alt="Zone of the Enders Recompiled" width="160">
</p>

# Zone of the Enders Recompiled

A static recompilation of *Zone of the Enders* (PlayStation 2, USA, SLUS-20148) into native PC code, with a Vulkan
renderer and a set of modern extras: higher resolutions, widescreen, antialiasing, a settings menu inside the game,
and the Japanese voices with subtitles in English, French, German, Italian, Spanish or Japanese.

> This project contains **no game data, no game code and no keys**. You need your own copy of the game (USA disc).
> The download holds a translator and a compiler: on the first start it translates **your** disc's program to C++
> and compiles it on your PC.

> **AI disclosure:**  AI assistance yes (Claude Code by Anthropic).

**Features at a glance**

- **Native PC code, no emulator** - the PS2's program, its vector units and its sound processor are translated to C++
  ahead of time; the picture is drawn with **Vulkan**.
- **Built from your own disc on your PC** - one first start of about 10 minutes; after it, the `game` folder is all
  you need.
- **Windows and Linux** (`zoe_launcher.exe`, `ZOE-x86_64.AppImage`) - and **Android** as an app of its own:
  [ZOE Recompiled for Android](https://github.com/almost-drowned-rat/ZOE-Recompiled-Android)
  (latest: [v0.5.1](https://github.com/almost-drowned-rat/ZOE-Recompiled-Android/releases/latest)).
- **Higher resolutions** (up to 5x), **widescreen** 16:9 / 16:10 / 21:9 (widened, never stretched), field of view.
- **Antialiasing** (FXAA, SMAA, SSAA 2x / 4x), anisotropic filter, bloom, FSR 1 upscaling.
- **RECOMP OPTIONS** - a settings menu inside the game's own OPTIONS menu, in the game's style.
- **The Japanese voices** with your Japanese disc, and subtitles in **English, French, German, Italian, Spanish or
  Japanese** (the subtitle pack).
- **Controllers with rumble**, keyboard, a performance overlay (FPS, frame time, game speed).
- **Texture packs**, and crash reports to attach to an issue.

Details: [Features](#features).

---

## The first port of ps2recomp-option-p

This port is first and foremost the first game of **ps2recomp-option-p**, my fork of the ps2-port-engine toolkit
([ps2recomp-option-p](https://github.com/almost-drowned-rat/ps2recomp-option-p)).

**What option P does differently:** the game's program is translated to C++ and the translation is *checked* -
run side by side with an interpreter of the PS2's processor on the game's own data, or proven equal to a form that
was. The PS2's vector unit programs and the sound processor's modules are translated ahead of time too. The
translation and the compile run on your own PC from your own disc image, so the download carries no game code.

**Comparison** with an independently developed recompilation of the same game: Yaz-Myst's
[ZOE-Recompiled](https://github.com/Yaz-Myst/ZOE-Recompiled) v1.1.0, its latest version, built from source. I measured
both on 2026-11-21 (this port's v0.8.1) in one session on the same machine (laptop with an RTX 3050 Ti), each at the
PS2's own resolution, windowed, 60 Hz; nothing else running. CPU in cores busy (means of two rounds); GPU in
milliseconds per frame (read for the whole GPU, desktop included):

| | Zone of the Enders Recompiled | ZOE-Recompiled v1.1.0 |
|---|---|---|
| CPU, fight (first battle) | 1.11 | 1.70 |
| CPU, in-engine cutscene (hangar) | 1.04 | 1.63 |
| CPU, title movie | 0.35 | 0.86 |
| GPU, fight | 2.7 ms | 1.8 ms |
| GPU, in-engine cutscene | 3.0 ms | 2.2 ms |
| GPU, title movie | 0.8 ms | 0.7 ms |

Both hold the game's 60 pictures a second in every scene. On a four-core machine (the Steam Deck's count) the CPU
results come out alike: 35-59 % less for this port.

**Only a benchmark, not a competition.** I measure against ZOE-Recompiled because it is the best way to see what
option P brings - not to rank anyone's work. Its renderer is leaner on the GPU than mine; both projects are young
and will change, and these are numbers from one machine and one scene set.

---

## What you need

- **Windows** 10 or 11, 64-bit - or **Linux** (x86-64, glibc 2.35 or newer: Ubuntu 22.04, Debian 12, Fedora 36,
  SteamOS 3.5, Arch and newer) - a processor with AVX2, a graphics card with a **Vulkan 1.3** driver
- A disc image of **Zone of the Enders (USA, SLUS-20148)** that you made from your own disc (ISO or CHD)
- Optional, for the **Japanese voices**: a disc image of the Japanese release (**SLPM-65019**) **and** the subtitle
  pack `ZOE-subtitles.pak` (a download of its own, see *Languages*) - the Japanese voices need both
- About 3 GB of free space (5 GB with the Japanese disc) and ~10 minutes for the first start (the translation, the
  compile and unpacking the discs run once)

## Getting started

1. Download `zoe_launcher.exe` (Windows) or `ZOE-x86_64.AppImage` (Linux) from the [Releases](../../releases) page
   and put it in a folder you can write to (not *Program Files*) - the game keeps its saves and settings in that
   folder. (`ZOE-windows-x86_64.zip` is the same launcher with `recomplauncher.json`, for RecompLauncher.) On Linux,
   make it executable (`chmod +x ZOE-x86_64.AppImage`, or the file's properties); if it does not start because
   your system has no FUSE, start it as `./ZOE-x86_64.AppImage --appimage-extract-and-run`. Optional:
   `ZOE-subtitles.pak` from the same page, my own subtitle translations (see *Languages*).
2. Start it - it is the only program on top of the folder, now and later. A small window asks for your disc image
   (and, if you like, the Japanese one and the subtitle pack) and the folder the game is set up in.
3. Press **Go**. The first start translates and compiles the game (into the `data` folder) and **unpacks your discs
   into its `game` folder** - the US disc whole, of the Japanese disc only what the Japanese voices need. From then on the game
   reads everything from there: your disc images are needed only this once (they are only read, never changed or
   deleted). You can close the window while this runs: it asks first, then stops cleanly, and the next start
   begins again from the start.

**Keep the `game` folder** and you need no disc image again: on a new PC, choose that folder in the setup window's
first line instead of an image - the Japanese disc and the subtitle pack in it are found by themselves.

## Features

### Native port, no emulator
- The PS2's program is translated to C++ ahead of time and compiled for the PC - no CPU emulation at run time.
- The PS2's vector units and the game's sound processor run as translated code too; the GS is drawn with
  **Vulkan**, window, input and sound through **SDL3**.

### RECOMP OPTIONS - in the game's own OPTIONS menu
The OPTIONS screen shows VIBRATION, SOUND, **RECOMP OPTIONS** and EXIT. RECOMP OPTIONS opens pages drawn in the
game's own style:

| Page | Lines |
|---|---|
| POST PROCESSING | antialiasing (FXAA, SMAA, SSAA 2x / 4x), anisotropic filter, bloom + strength, upscaling (bilinear / FSR 1) |
| DISPLAY | display mode (window / fullscreen / borderless), render resolution (standard sizes up to 5x), aspect ratio (4:3, 16:9, 16:10, 21:9 - the picture widened, never stretched), field of view |
| PERFORMANCE | V-sync, FPS, frame time, game speed (each on or off; FPS / frame time / game speed shown in a corner) |
| RENDERING | texture filtering (accurate / fast / auto - auto is fast on Android, accurate elsewhere), shader compile mode, shader notice |
| LANGUAGE | subtitles in cutscenes / in gameplay, voices (English / Japanese), subtitle language |

Up / down chooses a line, left / right changes it, **Triangle** sets a line back to its default.

### Languages and the Japanese voices
- Subtitles in **English, French, German or Italian** - all four are on the US disc already.
- With your Japanese disc image **and** the subtitle pack, **VOICES** offers the original Japanese voice cast, on the
  Japanese lines' timing; the movies with speech come in their Japanese version. Without the pack there are no
  Japanese voices.
- **The subtitle pack** (`ZOE-subtitles.pak`, a download of its own; chosen in the setup window, or in the game's `game` folder)
  holds my own translations, and they follow the voices you choose:
  - with the Japanese voices: a new translation from the Japanese into English, French, German, Italian and Spanish,
    so the subtitles say what the Japanese cast says - and **Japanese** subtitles (the Japanese disc's own lines);
  - with the English voices: **Spanish** and **Japanese**, translated from the English;
  - Spanish weapon and mission descriptions and VR training texts (the menus stay in English); the Japanese
    subtitles are drawn with Noto Sans JP.

### Controls
- A controller works as a DualShock 2, **with rumble**.
- Keyboard (by key position, the same on every layout): W A S D left stick, arrow keys D-pad, Enter / Backspace
  Start / Select, I / J / K / L Triangle / Square / Cross / Circle, U / O L1 / R1, Z / P L2 / R2.
- Alt+Enter switches between window and fullscreen, F1 shows or hides the performance overlay (FPS, frame time, game speed - 100 % = as fast as on the console).

### Texture packs
Put a texture pack in a folder inside `textures` in the game's folder; TEXTURES in RECOMP OPTIONS chooses it.

## Files in the game's folder

| File | What it is |
|---|---|
| `zoe_launcher.exe` | start here (on Linux the AppImage) |
| `game\` (`game/` on Linux) | your discs, unpacked (`disc`, `disc_ja`) and the subtitle pack - keep it as your backup |
| `data\` | the built game and everything the launcher made; `data\logs\` holds what the last run did - please attach it to a bug report |
| `pc_settings.ini` | your settings |
| `memcard\` | your memory card: the game's saves |
| `crash reports\` | after a crash: a zip with the log, your settings and the system's details - attach it to an issue |

A folder of an earlier version (`ZOE.exe`, `zoe_us.exe` on top) is updated in place: put `zoe_launcher.exe` into it
and start it - your saves and settings stay, the old programs are removed.

## Known issues

- This is an early release. If something looks or sounds wrong, please open an issue with the log - I will try to
  fix each one as fast as I can.
- **Linux** is played and tested on CachyOS (KDE Plasma, Wayland) with an AMD Radeon 880M (Mesa RADV). Other
  distributions and NVIDIA / Intel drivers should work the same way but are not tested yet - reports are welcome.

## Building from source

This repository holds the port's own code (`games/zoe_us/port`: the game's hooks, the RECOMP OPTIONS pages, the
Japanese voices), its configuration and the subtitle translations (`games/zoe_us/translation`, built into the pack by
`tools/m10/subs_translation.py`); the toolkit is
[ps2recomp-option-p](https://github.com/almost-drowned-rat/ps2recomp-option-p) (the `engine` submodule). The release
is built with the toolkit's `tools/player_release.py`; the translation itself runs from your disc image. The Linux
AppImage is the same release, made with the toolkit's `tools/linux/release_linux.sh` (its compiler bundle:
`tools/linux/make_toolchain_bundle.sh` - LLVM, glibc 2.35, Python - so the player's first start downloads nothing).

## Credits and licences

- The toolkit, ps2recomp-option-p, is my fork of [ps2-port-engine](https://github.com/danielgomesvieira2000/ps2-port-engine).
- Fonts and libraries: Liberation Sans (SIL Open Font License 1.1), SDL3 (zlib), stb_truetype and stb_image (MIT /
  public domain), pl_mpeg (MIT), miniz (MIT), xxHash (BSD-2-Clause), bcdec (MIT), minimp3 (CC0), AMD FidelityFX FSR 1
  (MIT), SMAA (MIT), libchdr (BSD-3-Clause). Their licence texts are in the toolkit's `LICENSES` folder.
- Noto Sans JP (SIL Open Font License 1.1, `games/zoe_us/port/fonts`), a subset for the Japanese subtitles.
- Test results of the real console: [ps2autotests](https://github.com/unknownbrackets/ps2autotests) (ISC).
- Some addresses of the game's functions were first named by another recompilation of the same game,
  ZOE-Recompiled; only those facts were taken, no code.

This project is licensed under the GNU General Public License v3.0 (see `LICENSE`).

*Zone of the Enders* is a trademark of its owners. This project is not affiliated with, endorsed by or sponsored by
the game's publisher or developer.
