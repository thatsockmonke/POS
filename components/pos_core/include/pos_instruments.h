#pragma once
#include "pos_install.h"
#include <stdbool.h>
typedef struct {
  void *ctx;
  int (*read)(void *, uint8_t reg, uint8_t *bytes, size_t size);
  int (*write)(void *, uint8_t reg, const uint8_t *bytes, size_t size);
  void (*delay_ms)(void *, unsigned milliseconds);
} pos_imu_bus_t;
typedef struct {
  float acceleration_g[3], angular_dps[3], temperature_c;
} pos_imu_sample_t;
/* BMI270 sensor axes, +/-4 g and +/-1000 dps at 100 Hz. No magnetometer. */
int pos_imu_initialize(const pos_imu_bus_t *, pos_error_t *);
/* 1 = waiting for a fresh accel+gyro sample, 0 = sample, -1 = bus failure. */
int pos_imu_sample(const pos_imu_bus_t *, pos_imu_sample_t *, pos_error_t *);
int pos_imu_suspend(const pos_imu_bus_t *);
/* Standard 8-bit NEC address/command, complements, least-significant-bit first.
 * 34 mark/space pairs; active-high envelope with 38 kHz carrier on marks. */
void pos_ir_nec(uint8_t address, uint8_t command, uint16_t pairs[34][2]);
