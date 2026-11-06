// The window the Voodoo's screen is shown in: the game's (as grSstWinOpen names it, or its
// active window), or one of gl2d9's own when it names none (a console program, as the 3dfx
// tests). Covering the game's monitor (borderless, or after a real mode switch) or as an
// ordinary window, and focus changes. The mouse is left alone: on a Voodoo 1/2 the desktop
// kept its own resolution, and games read the mouse in its coordinates.
#pragma once
#include "common.h"

namespace gl2d9 {

// Sets the window up for the screen (g_board.screenW x screenH); g_board.hwnd is the window.
bool WindowOpen(HWND requested);
void WindowOpened();  // after the board is open (see WindowOpen)
void WindowClose();  // the window and the display as they were
// Each frame (grBufferSwap): the cursor hidden over the picture, and the messages of gl2d9's
// own window handled (the game doesn't pump any).
void WindowPump();
// A hidden window of the calling thread's, for a Direct3D device that can't be made on the
// game's window (Windows' Direct3D 9 refuses a window another thread made: NFS 4 opens Glide
// from its rendering thread). Destroyed by the same thread.
HWND WindowMakeHelper();
void WindowDestroyHelper(HWND h);
// The client area the window has, or is about to have once covered or made a window (that
// may be done on its own thread a moment later): the size to present at.
SIZE WindowClientSize();
// Where the screen goes in a client area of cw x ch (as config stretch says).
RECT ImageRect(int cw, int ch);
// Whether the game's monitor has a display mode of w x h (the resolutions offered to games).
bool MonitorHasMode(int w, int h);
// Resolution lists games carry themselves, filtered in their memory to the monitor's modes
// (Unreal engine games' GlideDrv.dll: "512x384 640x480 ... 1600x1200", its GetRes answer).
void FilterGameResolutionLists();

}  // namespace gl2d9
