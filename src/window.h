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
void WindowClose();  // the window and the display as they were
// Each frame (grBufferSwap): the cursor hidden over the picture, and the messages of gl2d9's
// own window handled (the game doesn't pump any).
void WindowPump();
// Where the screen goes in a client area of cw x ch (as config stretch says).
RECT ImageRect(int cw, int ch);
// Whether the game's monitor has a display mode of w x h (the resolutions offered to games).
bool MonitorHasMode(int w, int h);

}  // namespace gl2d9
