#pragma once

// Start the background gallery loader task. Call once at boot (after the UI).
void gallery_start(void);

// Switch the gallery to a new article: clears the old images, fetches the new
// article's image list, shows the first image, and then lazily loads more as
// the user scrolls toward the bottom. Non-blocking (handled by the task).
void gallery_request(const char *title);
