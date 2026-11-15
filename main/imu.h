#pragma once
#include <stdbool.h>

// Bring up the onboard QMI8658 6-axis IMU over I2C (accelerometer only).
// Returns false if the chip isn't found.
bool imu_init(void);

// Read the accelerometer in units of g (gravity). Returns false on I2C error.
bool imu_read_accel_g(float *x, float *y, float *z);
