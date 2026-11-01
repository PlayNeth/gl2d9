# gl2d9

3dfx Glide 2.1, 2.x and 3.x, implemented on Direct3D 9: `glide.dll`, `glide2x.dll`
and `glide3x.dll` for games made for Voodoo cards. Each one runs on Windows natively
and on Linux under Wine, either with Wine's own Direct3D or with DXVK. See
`CHANGELOG.md` for what changed and why.

## How it works
- **The Voodoo's pixel pipeline as generated pixel shaders.** Color and alpha
  combine, texture combine on two TMUs, fog (table, iterated alpha, Z or W),
  chroma key, alpha test and blending, including `PREFOG_COLOR`, which Direct3D 9
  can't express. One shader per register combination, cached on disk
  (`gl2d9.shaders`) so a game doesn't stutter compiling them the next time.
- **Depth as the Voodoo stored it.** The shader writes the 16-bit value the chip
  would: Z's integer part, or W in the Voodoo's floating format (MAME's formula),
  with the depth bias added in the chip's own units. Coplanar passes tie the way
  they did on the card. `overlaybias` (from dd2d9) pulls light maps, headlights and
  decals forward one step so they don't z-fight with the surface under them.
- **Texture memory is emulated byte for byte.** Downloads go into each TMU's memory
  as on the card. Textures are decoded from that memory when drawn (SSE2 for the
  16-bit formats) and cached until the bytes under them change: NCC tables,
  palettes, multibase and split (two-TMU trilinear) textures, mirroring.
- **The linear frame buffer works both ways.** Reads come from the GPU at the
  game's resolution and are reused while nothing has been drawn. Writes take every
  format (565, 555, 1555, 888, 8888, depth and alpha), either directly or through
  the pixel pipeline (fog, blending, chroma key, alpha and depth tests). RLE16
  region writes are supported too.
- **Glide's own anti-aliasing** (`grAADrawTriangle` and the rest, Glide 3's
  `GR_AA_ORDERED`), `guMP` multipass, stipple patterns, `gu3df` texture files and
  the Glide 2 utility library.
- **The game decides vsync** through `grBufferSwap`'s interval, unless `vsync`
  forces it.
- **Borderless by default.** The picture covers the primary monitor, scaled with
  the aspect kept. `display=switch` changes the screen mode instead, as a Voodoo
  card did.
- **Windows' quirks handled.** Where Windows refuses a Direct3D 9 device (a game
  that has Windows' own DirectDraw loaded), gl2d9 tries its own window, a smaller
  back buffer and Direct3D 9Ex. Unreal-engine games' resolution lists
  (`GlideDrv.dll`) are trimmed to the modes the monitor has. The mouse cursor is
  hidden over the picture.

## Using
Put the DLL the game loads (`glide.dll`, `glide2x.dll` or `glide3x.dll`) next to
the game's executable, and `gl2d9.ini.example` renamed `gl2d9.ini` if you want to
change a setting (without one, every setting is its default). Games that look for Glide in the
Windows folder find it next to the executable first.

Screenshots: F12 (`screenshotkey`) saves the frame shown to `screenshots\` next to
the game. Windows' own capture and recording tools may show the picture black.

## Using gl2d9 with dd2d9
Many Glide games also use DirectDraw, for menus, videos or a software renderer
(Need for Speed III, High Stakes, NFS4). With dd2d9
as their `ddraw.dll`, each DLL handles only its own API, and what each reports to
the game is separate, as on a PC of the time with a 2D card and a Voodoo:
DirectDraw's video memory and desktop on one side, the Voodoo's texture memory,
TMUs and resolutions on the other. Some games need dd2d9 next to gl2d9 on Windows:
with Windows' own DirectDraw loaded, Windows refuses gl2d9 an ordinary Direct3D 9
device.

Recommended:

| | dd2d9 | gl2d9 | Why |
|---|---|---|---|
| `display` | `borderless` | `borderless` | Nothing changes the screen mode: no reset or black flash when the game goes from DirectDraw menus to the Glide race, instant alt-tab. For real mode changes, `switch` in dd2d9 only: one of them only may change the mode. |
| `stretch`, `filter` | the same in both | the same in both | Menus and race scaled alike, no jump when the game hands over. |
| `cpus` | `1` (its default) | `1` (its default) | Both pick the same processor. |
| `vsync`, `d3d9` | `auto`, `system` | `auto`, `system` | The defaults. |
| `fpslimit` | `0` | as needed | In the DLL that draws the part that runs too fast, usually the Glide 3D. |
| `texfilter`, `anisotropy`, `overlaybias` | (for its own Direct3D) | as wanted | They act on whichever draws the 3D: for a Glide game, gl2d9. |
| `log` | `0` | `0` | For a bug report, `1` in both: a handover problem shows as one log stopping where the other starts. |

## Configuration
`gl2d9.ini`, read from the Windows folder, then next to the DLL, then next to the
game's executable, then the file `GL2D9_INI` names (each overriding the one before).
A `[gl2d9]` section applies to every game; a `[game.exe]` section overrides it for
that game only. `GL2D9_LOG` set to `0`, `1` or `2` sets the log level; any other
value is a log file path. The first value listed is the default.

| Key | Values |
|---|---|
| `display` | `borderless` (the primary monitor covered, the picture scaled), `window`, `switch` (a real mode change to the game's resolution; borderless where the monitor lacks it) |
| `stretch` | `aspect`, `fill`, `integer` |
| `filter` | `linear`, `point`, `sharp` (sharp bilinear: flat pixels, blended over one screen pixel at their edges, at any scale) |
| `scale` | `1`; up to `8`: the game's resolution drawn that many times larger on the GPU (2: 640x480 at 1280x960) |
| `vsync` | `auto` (the game decides, through `grBufferSwap`), `on`, `off` |
| `voodoolook` | `off`; `on`: the picture shown dithered to 16 bits with the Voodoo's 4x4 ordered dither |
| `texfilter` | `game`; `linear`: point-filtered textures drawn bilinear (cut-outs, colour-keyed or with alpha, keep theirs) |
| `anisotropy` | `0` (off); `2`, `4`, `8`, `16`: anisotropic filtering on smoothly filtered textures (not on cut-outs) |
| `overlaybias` | `1`: light maps, headlights and decals (passes that add, multiply or don't write depth) pulled forward one 16-bit depth step and a pixel's slope, without writing depth (no z-fighting); `0` off; a number for more steps |
| `tmumemory` | `auto` (4 MB per TMU for Glide 2, as a Voodoo2; 16 MB for Glide 3); `1`–`32` MB |
| `tmus` | `2` or `1`: texture units reported |
| `fbmemory` | `4`; `1`–`16`: frame buffer memory reported, MB |
| `fpslimit` | `0` (off); frames per second at most, paced at the buffer swap |
| `cpus` | `1`: the game runs on one processor, as on the PCs of the time (early NFS games' timing breaks on more); `0`: all |
| `screenshotkey` | `f12`; `f1`–`f12`, `printscreen`, `scrolllock`, `pause`, `none` |
| `filterresolutions` | `on`: resolution lists games carry themselves (Unreal engine's `GlideDrv.dll`) trimmed to the monitor's modes; `off` |
| `shadercache` | `on`: compiled shaders kept in `gl2d9.shaders` next to the DLL; `off` |
| `d3d9` | `system` (Windows' own `d3d9.dll`), `game` (the one the game's folder has, such as DXVK's) |
| `log` | `0`, `1` (info: settings, device, errors, a crash's location), `2` (every Glide call: slow) |
| `logfile` | path; default `gl2d9.log` next to the game's executable |

## Not implemented
- Stipple's rotate mode is approximated (the chip steps through the pattern per
  pixel drawn, in drawing order; a GPU draws in parallel). The pattern mode is
  exact.
- Multisample anti-aliasing.

## Building
Meson and MinGW (i686):

    meson setup build --cross-file build-win32.txt
    ninja -C build

The output is `build/glide.dll`, `build/glide2x.dll` and `build/glide3x.dll`.

## Tests
`tests/` holds small Glide programs with known right results (frame buffer, depth, W buffer,
overlays, anti-aliasing, multipass, PREFOG, stipple, mirroring); see `tests/README.md`.

## License
gl2d9 is under the MIT license, see [`LICENSE`](LICENSE), except the parts derived from 3dfx's
open-sourced Glide source code: `src/gu.cpp`, `src/gu.h`, `src/glidedefs.h` and the
antialiasing in `src/core.cpp`, which are under the 3DFX GLIDE Source Code General Public
License, see [`LICENSE-3DFX`](LICENSE-3DFX). Each of those files says so in its header. The
complete source code of every released DLL is in this repository. Glide is a trademark of its
owner; gl2d9 is not affiliated with or endorsed by 3dfx Interactive or its successors.
