# Changelog

## 1.1.2
Vsync paced by gl2d9 under Windows' desktop compositor (dd2d9 1.5.0's).

- **Vsync under the desktop compositor: frames paced by gl2d9, Present doesn't wait.**
  gl2d9 presents into a window, which Windows' compositor (DWM) shows at its next pass;
  with vsync, Direct3D 9 made each Present wait for a vblank of its own, on the game's own
  thread (`grBufferSwap`), and Intel's driver spins the CPU for it. With dd2d9, the same
  wait made N.I.C.E. 2 choppy on a laptop's UHD 620: Present 7–8 ms a frame, the game's own
  work slowed from 3–5 to 7–10 ms on the shared power budget, frames now and then a refresh
  late; paced instead, every frame of 10 s one refresh apart. Where composition is on
  (Windows 8 and later; Windows 7 with Aero) and `vsync` isn't `off`, Present no longer waits,
  and the swap, once presented, waits for its slot on a schedule one refresh period apart
  (the compositor's own period), as many slots as Direct3D 9's vsync and the swap-interval
  wait gave (`vsync=on`: one; `auto`: the game's interval, at least one). A frame late by less
  than a period doesn't move the next one; the schedule leans toward the compositor's
  vblanks, an eighth of the way a slot. Without the compositor's clock while presenting at
  once (Windows 7's composition turned off since), the display's rate gives the period. Not
  under Wine nor without composition, nor where the device could only be made with the
  default interval: Direct3D 9's vsync, as before. Asked again at each device, reset and
  swap chain; the log says which (`vsync: frames paced by gl2d9 to the desktop compositor's
  refresh`, or `vsync: Direct3D 9's`). Under Wine (Direct3D 9's vsync there) the test set's
  pictures and output are 1.1.1's, but for those that differ from one run to the next anyway.

## 1.1.1
Alt+Tab fixes (Need for Speed III), and a game drawing on its own thread no longer risks a
hang on closing or keeps its cursor over the picture.

- **Alt+Tab no longer bounces back into the game.** 1.1.0 minimized the window inside
  `WM_ACTIVATEAPP`, which Windows sends in the middle of its own switch; the minimize
  (`SW_MINIMIZE`, which activates the next window) made a second switch within it that gave
  the game the focus back, its window minimized, the game holding the mouse. The window is
  now minimized once the switch is done, without activating anything, and only if the focus
  is still away by then.
- **A game that takes the foreground as it paints stays away.** NFS III calls
  `SetForegroundWindow` in its `WM_PAINT` while fullscreen, and switching away paints the
  window: the desktop flashed and the game came back. While the focus is away gl2d9 now
  answers `WM_PAINT` itself.
- **No hang on closing, for a game drawing on another thread than its window's.** The
  window's style was put back from the drawing thread with Glide's lock held, each change
  waiting for the window's thread, which may have been waiting for the drawing thread. It is
  now put back on the window's own thread; opened again before that, gl2d9 keeps the style
  the game had.
- **The cursor is hidden over the picture for such games too** (Unreal's arrow): the cursor
  is set per thread, and the drawing thread's `SetCursor` did nothing; the window's thread is
  asked.
- **Reopened while another program is in front, the window waits.** A game opening Glide
  again while switched away from (NFS III between its menus and a race) was covered in front
  of the program in front, topmost; it now covers the screen (and switches the display mode)
  once the game has the focus.
- The log names what had the foreground at each focus change.
- `tests/glide2/focus`: `paintfg` (takes the foreground in `WM_PAINT`, as NFS III) and
  `reopen` (closes and opens Glide again from the drawing thread, then checks the window's
  style and procedure).
- Tidying: the export handlers written out (`catch (...) { ... }`), and left off the exports
  that call nothing (stubs, constants, plain setters: nothing there can fail); the export
  macros in `common.h`; `.clang-format` describes the code's style, and the code follows it.

## 1.1.0
Recorders and overlays capture the game, Alt+Tab is reliable for games drawing on their own
thread (Unreal), and fixes for stutter and device resets. The screenshot key is gone.

- **Recorders and overlays see the game.** The back buffer was the size of the largest
  monitor, and only the window's part of it was presented (a source rectangle), sometimes
  from a hidden helper window. OBS's game capture, Steam's overlay and RTSS hook `Present`
  and take the whole render target, and swap chain 0's window: in a window, OBS recorded a
  1920x1080 frame with the 640x480 game in its corner. The back buffer is now the game
  window's client area, presented whole into it, as any windowed Direct3D 9 game's; resized
  with the window (a `Reset`, the picture kept). Where Windows refuses the device on the
  game's window, the helper window's device presents through a swap chain made on the
  game's, sized the same way.
- **The screenshot key is gone** (`screenshotkey`: it was for capture tools that showed the
  picture black). An ini file that sets it still loads.
- **Alt+Tab with a game that draws on its own thread** (Unreal, or any game with OBS running).
  1.0.2 retried a focus change that found Glide's lock taken, 500 times, all within about a
  millisecond, while the drawing thread holds the lock for a whole `Present` (longer with
  OBS copying the frame there): Alt+Tab did nothing, or the game came back black. The focus
  change is now done on the window's own thread without the lock; only repainting needs it,
  and is skipped while the game draws (its next frame shows). New test `tests/glide2/focus`:
  6 Alt+Tabs, from another thread too; 1.0.2 missed all 6 there.
- **The window is placed on its own thread.** A game opening Glide on another thread had its
  window covered from there, asynchronously: the game was sent size and move messages for
  gl2d9's own moves (an Unreal game could react to them), and the restyling waited for the
  window's thread with Glide's lock held. Done on the window's thread now, and the device
  is made at the size the window is about to have (not reset to it straight away).
- **A refused `Reset` is logged and doesn't spin.** It was retried on every draw, silently,
  the picture black. Now: its error logged, tried again every quarter second, and after two
  seconds of refusals (something else holding video memory on the device, such as a capture
  hook) the device is made anew. A removed or hung device (`Present`, or a 9Ex device's
  state) is made anew too. A swap that shows nothing waits a refresh, without the lock.
- **The texture cache's budget no longer stutters.** Over budget, every new texture sorted
  the whole cache, and dropped nothing when all the rest were in use that frame: a cost that
  grew with the cache, each frame (Unreal's light maps, new at new addresses). The budget is
  enforced once a frame at most, and the spare textures count and go first.
- **A call that runs out of memory fails instead of aborting the game.** A C++ exception
  from gl2d9 (memory run out, in a 32-bit game's address space: Unreal's) can't pass into
  the game, which aborted ("requested the Runtime to terminate"). Every Glide call, and
  gl2d9's window procedure, now catches it: logged (`failed: std::bad_alloc`), what the
  call left half done dropped (the batch; spare and unused textures freed), and the call
  returned as failed. A half-done `grSstWinOpen` is closed again, a failed `grLfbLock` or
  `grLfbUnlock` leaves the buffer unlocked, and a texture whose decoding was cut short is
  decoded again at its next use. The crash log no longer allocates (a crash with the memory
  run out was logged by code that could fail itself).
- **The frame limit's last half millisecond** lets the game's other threads run (with
  `cpus=1` they share its one processor).
- **Fixes for arguments a game gets wrong:** a texture whose level address wraps past 4 GB no
  longer reads before the texture memory; absurd vertex, polygon and RLE region sizes are
  refused instead of allocating gigabytes; a triangle far off the screen in stipple rotate
  mode no longer has a row made for every scanline up to it (memory, then a crash).
- **The shader cache recovers:** a damaged record (cut short, or refused by the device) is
  dropped and compiled again, and a record cut short no longer hides the ones after it.
- `cpus=on` is accepted (one processor); `anisotropy=1` is refused (0, or 2 to 16).
- On Wine, a game that doesn't pump its messages while drawing has its covered window shown
  at the full size (it showed at the size the window had before).

## 1.0.2
Fixes for frame buffer locks, focus changes, vsync and device resets, and some work taken off
the hot paths. The pictures are the same as 1.0.1's.

- **Frame buffer writes aren't written twice.** A write lock's changes are written at the
  unlock, and earlier when the game reads (a read lock, `grLfbReadRegion`) or writes a region
  meanwhile. Those changes were then still told apart from the lock's copy, so the unlock
  wrote them again, over whatever had been drawn since (and with the pixel pipeline on,
  blended or stippled them twice). New test `tests/glide3/lfblock`: a pixel written, read
  back, a triangle drawn over it, then the unlock left the pixel instead of the triangle.
- **A write lock in a 32-bit mode has memory of its own.** The read and write locks shared
  one block (as the Voodoo's one address range). A read lock taken while an 8888 or
  depth-mode write lock was held filled that block with 16-bit pixels at a 16-bit stride,
  over the game's 32-bit ones, and the write lock's copy came out half the size, read past
  its end at the unlock. 16-bit write locks still share the block.
- `grLfbWriteRegion` refuses source formats a colour buffer can't take (3dfx's does); such a
  write came out black.
- **Focus changes aren't lost.** A game drawing on another thread holds Glide's lock most of
  each frame; a `WM_ACTIVATEAPP` that found it taken did nothing. A missed focus loss left
  the game topmost over the desktop; a missed focus return left it minimized, with the mode
  not switched back. It is now tried again shortly after.
- **The frame limit waits without Glide's lock** (`fpslimit=`, and the swap interval below),
  so the window's messages and other threads don't wait with it.
- **`vsync=auto` follows `grBufferSwap`'s interval**, as documented: `grBufferSwap(2)` and
  up shows a frame every 2 (or more) refreshes. They ran at the full refresh rate.
  `grBufferSwap(0)` still waits for one refresh.
- **Minimized, a game no longer takes a whole core.** The swap doesn't wait for the display
  then; a refresh's time is waited instead.
- **A failed device reset isn't the end.** If `Reset` failed once (the device lost again
  meanwhile), or the render targets couldn't all be made, the screen stayed black until the
  game opened Glide again (or a half-made set crashed). The targets are made again as soon
  as the device allows. Before `Reset`, the device's own references to its targets and
  textures are dropped too (they made `Reset` fail).
- With `voodoolook=1`, a `Present` that couldn't get the back buffer left its own target
  set for the next drawing.
- Glide utility (`gu*`) textures: reopening Glide forgets which mipmaps the NCC tables hold,
  as 3dfx's `grSstWinOpen` does (a YIQ texture of the new session could reuse an old
  session's table). Glide 2.1's frame buffer pointers are forgotten with the board.
- The frame limiter's timer is closed with its thread; where Windows has no high-resolution
  timer (before Windows 10 1803), the system timer is set to 1 ms so `fpslimit=` keeps its
  rate (it gave an erratic 32–64 fps at 60), and a refused timer no longer busy-waits.
- Faster: Glide 3 vertices decoded with the draw's texture scales worked out once per call,
  not per vertex; each triangle's vertices brought back to the screen once (they were twice:
  for the culling test and for drawing); full-frame frame buffer writes (movies, menus)
  converted four pixels at a time (SSE2); a palette download hashed once, not per TMU; the
  timer frequency for `grSstStatus` and the like asked once; on Direct3D 9Ex, one-level
  textures and the fog table discarded when rewritten instead of waited for.
- Tested: the Glide 2 and Glide 3 SDK test programs and `tests/`, against 1.0.1 (run twice,
  to tell timing from changes): the same pictures and printed results, but for tests that
  animate by frame count or clock. The new `lfblock` fails on 1.0.1 and passes.

## 1.0.1
- **No crash when a game opens Glide again on its window** after something else subclassed
  that window (dd2d9, for the game's DirectDraw menus, or the game itself). gl2d9 can't
  unhook from under the other subclass, so it stays in the window's chain; opening again,
  it subclassed a second time, and the window's messages went round gl2d9, the other, gl2d9
  again, until the stack overflowed, with nothing in the log. NFS High Stakes crashed going
  from its 640x480 menus to a 1600x1200 race, and NFS Porsche Unleashed coming back from
  Alt+Tab (both close and reopen Glide on the same window). gl2d9 now remembers a window it
  is still hooked into and takes that hook back. Should window procedures ever call each
  other round a loop anyway, the loop is logged once and broken, and a stack overflow is
  logged in one line. New test `tests/glide3/reopen`.
- **Frame buffer writes wider than 1024 pixels.** A lock always gave the Voodoo's 1024-pixel
  rows (a 2048-byte stride); the Glide 3 modes wider than that (1280 and up) didn't fit, and
  each row written ran into the next one: NFS Porsche Unleashed's loading screens at
  1600x1200 came out in pieces (the middle and bits of the edges). Those modes get
  2048-pixel rows now; up to 1024 wide nothing changes. New test `tests/glide3/lfbwide`.
- **Stipple rotate mode, exact.** In this mode the Voodoo steps through the 32-bit pattern one
  bit per pixel it reaches, row by row and triangle to triangle, starting over when the
  pattern is written; 1.0.0 approximated it with a diagonal, as a GPU draws pixels in
  parallel. The CPU now walks each triangle's rows with the Voodoo's own rules (MAME's: vertices
  snapped to 1/16 pixel, its rounding, the clip window) and hands the shader a table: each
  row's span and where the pattern is at its first pixel. Pixels written through the pixel
  pipeline by the frame buffer step it too. The pattern mode was already exact. New test
  `tests/glide3/stiprot` (the result against a model of the chip, both origins).
- **`install-gl2d9.sh`** for Linux: installs the three DLLs into a Wine or Proton prefix's
  32-bit system folder (keeping any other Glide DLLs there as `*.pre-gl2d9`) and creates
  `C:\windows\gl2d9.ini`; `uninstall` removes them and puts back what it kept.
- Tested: the Glide 2 and Glide 3 SDK test programs and `tests/` (pixel-identical to 1.0.0
  with the same settings), High Stakes' sequence (640x480, close, shutdown, 1600x1200 on a
  1920x1080 screen, with another subclass on the window) and a full-width frame buffer write
  at 1600x1200 under Wine.

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
