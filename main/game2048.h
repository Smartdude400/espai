#pragma once
#include <stdbool.h>

// 2048, controlled by tilt gestures (tip the board past ~20 degrees to slide,
// level it out, tilt again for the next move).

// Create the 2048 screen with an n x n board (clamped 3..6).
void g2048_start(int n);

// Tear the 2048 screen down.
void g2048_stop(void);

// True while 2048 is on screen.
bool g2048_active(void);

// Feed accelerometer readings (in g). Called from the IMU task ~30x/s.
void g2048_tilt(float ax, float ay);

// Manual move (console keys): 0=up 1=down 2=left 3=right.
void g2048_move(int dir);

// Reset the board.
void g2048_restart(void);
