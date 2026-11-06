# gl2d9's tests

Small Glide programs, each drawing one thing whose right result is known (and printing what
it reads back, where it reads something, at the fourth frame). Each draws 100000 frames and
exits; a script runs them under a timeout and takes a screenshot.

Build (needs 3dfx's open-sourced Glide tree, for its headers; gl2d9 itself built first is not
needed, the import libraries are made from `src/*.def`):

    GLIDE_SRC=/path/to/glide tests/build.sh

The programs go to `tests/bin/`. Put the matching gl2d9 DLL next to them.

Glide 2 (`glide2x.dll`; `g21win` loads `glide.dll`):

| Program | What it checks | Right result |
|---|---|---|
| `lfbz` | depth read back; depth written through the frame buffer; a pipeline write blended | prints `8000 8000` and `ffff`; only the right half of the green quad; a purple square |
| `lfbt` | frame buffer writes in 565 (or the format numbered on the command line) | the pattern the program writes, over red |
| `aat` | Glide's anti-aliased triangles, lines and points | smooth edges on the left triangle, hard on the right |
| `mpr` | `guMP` multipass; RLE16 region writes encoded with `guEncodeRLE16` | red, green, black and white squares; a blue and yellow image |
| `mt3`, `mt4` | two-TMU multitexturing as Unreal does it (8-bit palette texture times a light map) | a blue and white checker fading left to right |
| `g21win` | Glide 2.1's `grSstOpen` with no window given: the game's own window is used | the picture in the game's window |
| `wbz` | W buffer: depth read back against MAME's formula; coplanar decals with depth bias at three distances | prints `read` equal to `MAME`; green decals on the left, none on the right |
| `pfl` | `PREFOG_COLOR` blending through a frame buffer write and a triangle | two purple squares; prints `801f` for both |
| `focus` | its own window drawn to from the window's thread, or (`focus thread`) from another as Unreal does, through focus changes (Alt+Tab, minimize) done from outside | prints the frames each second; the picture comes back after each focus change, minimized while away |
| `ovl` | overlays (`overlaybias`): an additive light on a road cut into other triangles, its depth a little off, Z and W buffer | the light patches clean, no road showing through |

Glide 3 (`glide3x.dll`):

| Program | What it checks | Right result |
|---|---|---|
| `feat3` | pattern stipple; a mirrored texture; `PREFOG_COLOR` blending | red stripes swapping every two rows; a triangle wave; dark green (dark blue is wrong) |
| `lfbwide` | frame buffer writes at 1600x1200, wider than the Voodoo's 1024-pixel rows (NFS Porsche Unleashed's loading screens) | prints reds `1 17 29`; a smooth red ramp over the whole width |
| `stiprot` | stipple rotate mode against a model of the Voodoo's rasterizer, clip window, both origins | prints a handful differing (edge pixels Direct3D misses), not thousands, twice |
| `reopen` | reopening on the game's window after another subclass took it (NFS High Stakes with dd2d9), at 1600x1200 | prints `open 1600` and `done`, a green triangle |
| `lfblock` | frame buffer locks held while other things happen: a read lock taken during an 8888 write lock; a pixel written, `grLfbReadRegion`, then a triangle drawn over it before the unlock | prints `001f` and `07e0`; a blue square, a green triangle |

The 3dfx SDK's own test programs (`glide2x/cvg/glide/tests`, `glide3x/cvg/glide3/tests` in the
Glide tree) are the rest of the regression set.
