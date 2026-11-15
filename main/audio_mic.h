#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Initialise the INMP441 I2S receiver (16 kHz, mono). Call once at boot.
bool mic_init(void);

// Read up to `max_samples` 16-bit PCM samples into `dst`.
// Returns the number of samples actually read.
size_t mic_read(int16_t *dst, size_t max_samples);

// Convenience: read for `ms` milliseconds and return the peak absolute
// amplitude seen (0..32767). Useful to verify the mic is wired correctly.
int mic_measure_level(int ms);
