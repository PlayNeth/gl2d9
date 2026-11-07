# gl2d9

3dfx **Glide 2.1, 2.x and 3.x** on Direct3D 9, for running **Voodoo-era games** on modern Windows and on Linux.

gl2d9 is a reimplementation of 3dfx's Glide: `glide.dll`, `glide2x.dll` and `glide3x.dll`, drawn through Direct3D 9. It emulates the Voodoo the way games programmed it, from the pixel pipeline and its depth buffer to texture memory byte for byte, and adds borderless display, scaling, higher internal resolutions and texture filtering. It runs on Windows, and on Linux under Wine, with Wine's own Direct3D or with DXVK.

---

## Contents

- [Requirements](#requirements)
- [Installing](#installing)
- [Uninstalling](#uninstalling)
- [What it does](#what-it-does)
- [Using gl2d9 with dd2d9](#using-gl2d9-with-dd2d9)
- [Settings](#settings)
- [Files gl2d9 uses](#files-gl2d9-uses)
- [Troubleshooting](#troubleshooting)
- [Building from source](#building-from-source)
- [For developers](#for-developers)
- [License](#license)

---

## Requirements

- A **game made for 3dfx Voodoo cards** that uses Glide 2.1 (`glide.dll`), 2.x (`glide2x.dll`) or 3.x (`glide3x.dll`).
- **Windows** with Direct3D 9, or **Linux** with Wine or Proton.
- **Optional, for games that also use DirectDraw** (menus, videos, a software renderer): [dd2d9](https://github.com/PlayNeth/dd2d9). On Windows some of those games need it; see [Using gl2d9 with dd2d9](#using-gl2d9-with-dd2d9).

## Installing

### Windows

1. Copy the DLL the game loads (`glide.dll`, `glide2x.dll` or `glide3x.dll`) next to the game's exe. Games that look for Glide in the Windows folder find it next to the exe first.
2. To change a setting, also copy `gl2d9.ini` there. Without one, every setting is its default.
3. Start the game.

### Linux (Wine / Proton)

Either install it into the prefix the game uses, from the folder you unzipped:

```sh
WINEPREFIX=~/Games/foo ./install-gl2d9.sh install
```

That copies all three DLLs into the prefix's 32-bit system folder (`syswow64` in a 64-bit prefix, `system32` in a 32-bit one), so every Glide game in the prefix uses gl2d9, and creates `C:\windows\gl2d9.ini` if there's none. Wine has no Glide of its own, so no DLL override is needed. Glide DLLs already there (nGlide or dgVoodoo, say) are kept as `*.pre-gl2d9`. A Glide DLL next to a game's exe is still loaded first: remove it for that game to use gl2d9. For Proton, the prefix is the game's `compatdata/<appid>/pfx` folder.

Or, for one game: put the DLL next to the game's exe, as on Windows.

### Checking it works

Set `log=1` in `gl2d9.ini` and start the game. `gl2d9.log` appears next to the game's exe, with the version, the settings in effect and the Direct3D device it got.

## Uninstalling

- **Windows:** delete the Glide DLL you copied from the game's folder.
- **Wine / Proton:** `WINEPREFIX=~/Games/foo ./install-gl2d9.sh uninstall` removes gl2d9's DLLs from the prefix and puts back any it kept. It keeps `gl2d9.ini`.
- You can also delete `gl2d9.ini`, `gl2d9.log` and `gl2d9.shaders`.

---

## What it does

### The Voodoo, emulated

- **The pixel pipeline as generated pixel shaders:** color and alpha combine, texture combine on two TMUs, fog (table, iterated alpha, Z or W), chroma key, alpha test and blending, including `PREFOG_COLOR`, which Direct3D 9 can't express. One shader per combination of states, cached on disk (`gl2d9.shaders`), so a game doesn't stutter compiling them the next time.
- **Depth as the Voodoo stored it:** the shader writes the 16-bit value the chip would, Z's integer part or W in the Voodoo's floating format, with the depth bias in the chip's own units. Coplanar passes tie the way they did on the card.
- **Texture memory emulated byte for byte:** downloads go into each TMU's memory as on the card. Textures are decoded from it when drawn (SSE2 for the 16-bit formats) and cached until the bytes under them change: every format, NCC tables, palettes, multibase and split (two-TMU trilinear) textures, mirroring.
- **The linear frame buffer both ways:** reads come from the GPU at the game's resolution and are reused while nothing has been drawn. Writes take every format (565, 555, 1555, 888, 8888, depth and alpha), directly or through the pixel pipeline, and RLE16 region writes.
- **Glide's own anti-aliasing** (`grAADrawTriangle` and the rest, Glide 3's `GR_AA_ORDERED`), `guMP` multipass, stipple (pattern and rotate modes, both exact: rotate's pixel order is worked out on the CPU the way the Voodoo rasterizes), `gu3df` texture files and the Glide 2 utility library.

### Display

- **Borderless by default:** the picture covers the primary monitor, scaled with its aspect kept. `display=window` shows it in a window, and `display=switch` changes the screen mode, as a Voodoo card did.
- **Scaling:** keep the aspect, fill the screen or scale by whole numbers, with linear, point or sharp-bilinear filtering.
- **Higher resolution:** `scale` draws the game up to 8 times larger on the GPU (2: 640×480 at 1280×960).
- **`voodoolook`:** the picture dithered to 16 bits with the Voodoo's 4×4 dither.
- **V-sync as the game asks,** through `grBufferSwap`'s interval, unless `vsync` forces it.
- **Recorders and overlays work** (OBS's game capture, Steam's overlay, RTSS): the picture is presented as an ordinary windowed Direct3D 9 game's, its back buffer the window's size.

### Options beyond the original

- **Texture filtering:** linear filtering of point-filtered textures, and anisotropic filtering; cut-outs keep theirs.
- **`overlaybias`:** light maps, headlights and decals pulled forward one depth step, so they don't z-fight with the surface under them (NFS III's headlights).
- **A frame limiter** for games that run too fast.
- **One processor by default** (`cpus=1`), as the PCs of the time had: early NFS games' timing breaks on more.

### Compatibility

- **Where Windows refuses a Direct3D 9 device** (a game with Windows' own DirectDraw loaded, NFS4), gl2d9 tries its own window, a smaller back buffer and then Direct3D 9Ex, and logs a diagnosis if all fail.
- **Texture memory sized for the game:** 4 MB per TMU for Glide 2 (a Voodoo2), 16 MB for Glide 3. NFS III crashed once 4 MB filled.
- **Unreal-engine resolution lists** (`GlideDrv.dll`) are trimmed to the modes the monitor has.
- **The mouse cursor is hidden over the picture,** also for games that capture the mouse.
- **A crash in the game** is logged with its location and stack (`log=1`).

### Not implemented

- **Multisample anti-aliasing.** `scale=2` or more gives a similar result: the picture is drawn larger and shrunk to the screen.

---

## Using gl2d9 with dd2d9

Many Glide games also use DirectDraw, for menus, videos or a software renderer (Need for Speed III, High Stakes, NFS4). With [dd2d9](https://github.com/PlayNeth/dd2d9) as their `ddraw.dll`, each DLL handles only its own API, and what each reports to the game is separate, as on a PC of the time with a 2D card and a Voodoo: DirectDraw's video memory and desktop on one side, the Voodoo's texture memory, TMUs and resolutions on the other. Some games need dd2d9 next to gl2d9 on Windows: with Windows' own DirectDraw loaded, Windows refuses gl2d9 an ordinary Direct3D 9 device.

Recommended settings:

| | dd2d9 | gl2d9 | Why |
|---|---|---|---|
| `display` | `borderless` | `borderless` | Nothing changes the screen mode: no reset or black flash when the game goes from DirectDraw menus to the Glide race, and instant Alt+Tab. For real mode changes, `switch` in dd2d9 only: only one of them may change the mode. |
| `stretch`, `filter` | the same in both | the same in both | Menus and race scaled alike, with no jump when the game hands over. |
| `cpus` | `1` (its default) | `1` (its default) | Both pick the same processor. |
| `vsync`, `d3d9` | `auto`, `system` | `auto`, `system` | The defaults. |
| `fpslimit` | `0` | as needed | In the DLL that draws the part that runs too fast, usually the Glide 3D. |
| `texfilter`, `anisotropy`, `overlaybias` | (for its own Direct3D) | as wanted | They act on whichever draws the 3D: for a Glide game, gl2d9. |
| `log` | `0` | `0` | For a bug report, `1` in both: a handover problem shows as one log stopping where the other starts. |

---

## Settings

gl2d9 reads `gl2d9.ini` from these places, later ones winning:

1. the Windows folder;
2. next to the DLL;
3. next to the game's exe;
4. the file named by the `GL2D9_INI` environment variable.

A `[gl2d9]` section applies to every game, and a `[game.exe]` section overrides it for that game. [`gl2d9.ini.example`](gl2d9.ini.example) explains every key.

| Setting | Values (default first) |
|---|---|
| `display` | `borderless` (the primary monitor covered, the picture scaled), `window`, `switch` (a real mode change to the game's resolution; borderless where the monitor lacks it) |
| `stretch` | `aspect`, `fill`, `integer` |
| `filter` | `linear`, `point`, `sharp` (sharp bilinear: flat pixels, blended over one screen pixel at their edges, at any scale) |
| `scale` | `1`; up to `8`: the game's resolution drawn that many times larger on the GPU |
| `vsync` | `auto` (the game decides, through `grBufferSwap`), `on`, `off`. Under Windows' desktop compositor gl2d9 paces the frames to the screen's refresh itself; elsewhere (Wine, no compositor) Direct3D 9's vsync does |
| `voodoolook` | `off`; `on`: the picture dithered to 16 bits with the Voodoo's 4×4 dither |
| `texfilter` | `game`; `linear`: point-filtered textures drawn bilinear (cut-outs keep theirs) |
| `anisotropy` | `0` (off); `2`, `4`, `8` or `16`, on smoothly filtered textures (not on cut-outs) |
| `overlaybias` | `1`: light maps, headlights and decals pulled forward one 16-bit depth step and a pixel's slope, without writing depth; `0` off; a number for more steps |
| `tmumemory` | `auto` (4 MB per TMU for Glide 2, 16 MB for Glide 3); `1`–`32` MB |
| `tmus` | `2` or `1`: texture units reported |
| `fbmemory` | `4`; `1`–`16`: frame buffer memory reported, MB |
| `fpslimit` | `0` (off); frames per second at most, paced at the buffer swap |
| `cpus` | `1`: the game runs on one processor; `0`: all |
| `filterresolutions` | `on`: Unreal-engine resolution lists trimmed to the monitor's modes; `off` |
| `shadercache` | `on`: compiled shaders kept in `gl2d9.shaders` next to the DLL; `off` |
| `d3d9` | `system` (Windows' own `d3d9.dll`), `game` (the one in the game's folder, such as DXVK's) |
| `log` | `0`; `1` (info: settings, device, errors, a crash's location); `2` (every Glide call: slow) |
| `logfile` | a path; by default `gl2d9.log` next to the game's exe |

**Environment variables:** `GL2D9_INI` names an extra config file. `GL2D9_LOG` set to `0`, `1` or `2` sets the log level; any other value is taken as a log file path.

## Files gl2d9 uses

| File | |
|---|---|
| `glide.dll`, `glide2x.dll`, `glide3x.dll` | gl2d9 itself: the one the game loads. |
| `gl2d9.ini` | Settings (see [Settings](#settings)). |
| `install-gl2d9.sh` | Linux: installs gl2d9 into a Wine or Proton prefix, or removes it. |
| `gl2d9.log` | Next to the game's exe, with `log=1` or more. Send it with any bug report. |
| `gl2d9.shaders` | The compiled shaders, next to the DLL (`shadercache`). Safe to delete. |

## Troubleshooting

- **Nothing changes, and `log=1` writes no `gl2d9.log`:** the game isn't loading gl2d9. Check that the right DLL for the game is next to its exe (or, under Wine, run `install-gl2d9.sh install` for the game's prefix).
- **The game doesn't start on Windows, and the log says Windows refused a Direct3D 9 device:** the game has Windows' own DirectDraw loaded. Put [dd2d9](https://github.com/PlayNeth/dd2d9) next to it as `ddraw.dll`.
- **The game runs too fast, or its timing is off:** keep `cpus=1`, or set `fpslimit`.
- **Hatched light maps, headlights or decals on the road:** raise `overlaybias`.
- **The game crashes once it has loaded many textures:** check `tmumemory`; `auto` gives Glide 2 games a Voodoo2's 4 MB per TMU.
- **An Unreal-engine game offers resolutions the monitor can't show:** keep `filterresolutions=on`.
- **Recordings come out black or cut off:** use OBS's Game Capture (or Window Capture with the Windows 10 capture method) on the game's window; gl2d9 1.1.0 and later present it as any windowed Direct3D 9 game does.
- **An Unreal-engine game stutters:** try `cpus=0` in a section for its exe (`[UnrealTournament.exe]`): with `cpus=1` its threads, and a recorder's, share one processor.
- **Something else:** set `log=1` (or `2` for every Glide call) and please report the problem with `gl2d9.log`.

## Building from source

Requires MinGW-w64 (i686) and Meson.

```sh
meson setup build --cross-file build-win32.txt
ninja -C build
```

The output is `build/glide.dll`, `build/glide2x.dll` and `build/glide3x.dll`, statically linked.

---

## For developers

### Repo layout

| Path | What |
|---|---|
| `src/` | The DLLs. `api2.cpp` and `api3.cpp` (the Glide 2.x / 2.1 and Glide 3 entry points), `core.cpp` (what both share: opening the screen, drawing, clearing and swapping, the hardware as reported, anti-aliasing), `state.cpp` and `voodoo.h` (Glide's state as the Voodoo's registers), `render.cpp` and `shader.cpp` (drawing through Direct3D 9, the generated pixel shaders), `texmem.cpp` (texture memory and decoding), `lfb.cpp` (the linear frame buffer), `gu.cpp` (the `gu` utility library), `window.cpp` (the window the picture is shown in), `util.cpp` (config and logging). |
| `tests/` | Small Glide programs with known right results: frame buffer, depth, W buffer, overlays, anti-aliasing, multipass, PREFOG, stipple, mirroring. See [`tests/README.md`](tests/README.md). |

### Tests

`tests/` builds against 3dfx's open-sourced Glide tree for its headers (`GLIDE_SRC`); see [`tests/README.md`](tests/README.md). The Glide 2 and Glide 3 SDKs' own test programs are compared screenshot by screenshot between builds. [`CHANGELOG.md`](CHANGELOG.md) explains every change and what showed the need for it.

---

## License

gl2d9 is under the MIT license, see [`LICENSE`](LICENSE), except the parts derived from 3dfx's open-sourced Glide source code: `src/gu.cpp`, `src/gu.h`, `src/glidedefs.h` and the antialiasing in `src/core.cpp`, which are under the 3DFX GLIDE Source Code General Public License, see [`LICENSE-3DFX`](LICENSE-3DFX). Each of those files says so in its header. The complete source code of every released DLL is in this repository. Glide is a trademark of its owner; gl2d9 is not affiliated with or endorsed by 3dfx Interactive or its successors.
