# Changelog

## 1.0.0
The first release: Glide 2.1 (`glide.dll`), 2.x (`glide2x.dll`) and 3.x
(`glide3x.dll`) on Direct3D 9, for Windows and for Linux under Wine (its own
Direct3D or DXVK).

Rendering
- The Voodoo's pixel pipeline as generated pixel shaders: color and alpha combine,
  two TMUs' texture combine, fog (table, iterated alpha, Z, W), chroma key, alpha
  test and blending, `PREFOG_COLOR` included (in the shader, against a copy of the
  target, for triangles and frame buffer writes alike). Compiled shaders are kept
  in `gl2d9.shaders` (`shadercache`).
- Depth as the Voodoo stored it: the shader writes the 16-bit value the chip would
  (Z's integer part; W in the Voodoo's floating format, MAME's formula), with the
  depth bias in the chip's units, so coplanar passes tie as on the card and the
  bias works at any distance. Compare-to-bias modes.
- `overlaybias` (from dd2d9): light maps, headlights and decals pulled forward one
  depth step and a pixel's slope, without writing depth. Polygons cut differently
  from the surface under them no longer z-fight with it (NFS III's headlights).
- Texture memory emulated byte for byte on each TMU, textures decoded from it when
  drawn and cached until their bytes change: every format, NCC tables (broadcast to
  both TMUs), palettes, multibase and split (two-TMU trilinear)
  textures, clamping and mirroring, LOD bias. SSE2 decoding for the 16-bit formats.
- Glide's anti-aliasing (`grAADrawTriangle`, points, lines, polygons; Glide 3's
  `GR_AA_ORDERED`), stipple (pattern mode exact, rotate mode approximated),
  `guMP` multipass, the `gu` utility library and `gu3df` files.

Linear frame buffer
- Reads at the game's resolution (shrunk on the GPU when `scale` is above 1), and
  reused while nothing is drawn: a game locking several times a frame doesn't wait
  for the GPU each time. Depth and alpha buffer reads.
- Writes in every format (565, 555, 1555, 888, 8888, depth, alpha), direct or
  through the pixel pipeline (fog, blending, chroma key, alpha and depth tests),
  constant depth and alpha, RLE16 region writes.

Display
- `display`: `borderless` (the default: the primary monitor covered, the picture
  scaled), `window`, `switch` (a real mode change). `stretch`, `filter` (`linear`,
  `point`, `sharp`: sharp bilinear at any scale), `scale` (1–8 times the resolution
  on the GPU), `vsync` (the game's swap interval by default).
- `voodoolook`: the picture dithered to 16 bits with the Voodoo's 4x4 dither.
- Screenshots (`screenshotkey`, F12), as Windows' capture tools may see the picture
  black: 24-bit BMPs in `screenshots\` next to the game.
- The mouse cursor hidden over the picture, also for games that capture the mouse.
- Glide 2.1's `grSstOpen` (no window given): the game's own main window.

Compatibility
- Where Windows refuses a Direct3D 9 device (a game with Windows' own DirectDraw
  loaded, NFS4), gl2d9 tries its own window, a smaller back buffer and then
  Direct3D 9Ex; if all fail, it logs a diagnosis. The refusals are logged in one line.
- `tmumemory=auto`: 4 MB per TMU for Glide 2 (a Voodoo2), 16 MB for Glide 3 (NFS III
  crashed once 4 MB filled: its texture allocator returned nothing).
- `filterresolutions`: Unreal-engine games' resolution lists (`GlideDrv.dll`)
  trimmed to the monitor's modes.
- From dd2d9: `cpus` (1 by default: one processor, as early NFS games and others of
  their time need), `fpslimit`, `texfilter`, `anisotropy` (neither on cut-outs).
- A crash in the game is logged with its location and stack (`log=1`).

Tested
- Need for Speed III and 4 (Windows, with dd2d9), Unreal Tournament (Windows),
  SimCopter 1.02 (Glide 2.1; matches nGlide), the Glide 2 and Glide 3 SDK test
  programs, and gl2d9's own tests (frame buffer, depth, anti-aliasing, multipass,
  PREFOG, W buffer, overlays).
