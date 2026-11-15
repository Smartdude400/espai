#pragma once
#include <stdbool.h>

// Tilt-a-ball maze. A random maze is generated; tilt the board to roll the
// amber ball from the top-left to the green goal. Reaching it generates a
// fresh maze automatically.

// Create the maze screen with n cells per side (clamped 4..14).
void maze_start(int n);

// Tear the maze screen down (back to whatever was underneath).
void maze_stop(void);

// True while the maze is on screen.
bool maze_active(void);

// Feed accelerometer readings (in g). Called from the IMU task ~30x/s.
void maze_tilt(float ax, float ay);
