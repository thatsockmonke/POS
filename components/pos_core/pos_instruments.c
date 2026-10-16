#include "pos_instruments.h"
#include "vendor/bmi270/config.h"
#include <stdio.h>
static int fail(pos_error_t *e, const char *s) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", s);
  return -1;
}
static int write8(const pos_imu_bus_t *b, uint8_t reg, uint8_t v) {
  return b->write(b->ctx, reg, &v, 1);
}
int pos_imu_initialize(const pos_imu_bus_t *b, pos_error_t *e) {
  uint8_t id = 0;
  if (!b || !b->read || !b->write || !b->delay_ms ||
      b->read(b->ctx, 0, &id, 1) || id != 0x24)
    return fail(e, "BMI270 not detected.");
  if (write8(b, 0x7e, 0xb6))
    goto failed;
  b->delay_ms(b->ctx, 3);
  if (write8(b, 0x7c, 0))
    goto failed;
  b->delay_ms(b->ctx, 2);
  if (write8(b, 0x59, 0))
    goto failed;
  for (size_t at = 0; at < sizeof(bmi270_config_file); at += 32) {
    uint8_t address[2] = {(at >> 1) & 15, at >> 5};
    size_t n = sizeof(bmi270_config_file) - at;
    if (n > 32)
      n = 32;
    if (b->write(b->ctx, 0x5b, address, 2) ||
        b->write(b->ctx, 0x5e, bmi270_config_file + at, n))
      goto failed;
  }
  if (write8(b, 0x59, 1))
    goto failed;
  b->delay_ms(b->ctx, 25);
  if (b->read(b->ctx, 0x21, &id, 1) || (id & 15) != 1)
    goto failed;
  if (write8(b, 0x40, 0xa8) || write8(b, 0x41, 1) || write8(b, 0x42, 0xe8) ||
      write8(b, 0x43, 1) || write8(b, 0x7d, 0x0e))
    goto failed;
  b->delay_ms(b->ctx, 80);
  if (b->read(b->ctx, 0x02, &id, 1) || id)
    goto failed;
  return 0;
failed:
  (void)write8(b, 0x7d, 0);
  return fail(e, "BMI270 configuration failed; sensor disabled.");
}
static int16_t signed16(const uint8_t *p) {
  return (int16_t)(p[0] | (uint16_t)p[1] << 8);
}
int pos_imu_sample(const pos_imu_bus_t *b, pos_imu_sample_t *s,
                   pos_error_t *e) {
  uint8_t status, data[12], temp[2];
  if (b->read(b->ctx, 0x03, &status, 1))
    return fail(e, "IMU status read failed.");
  if ((status & 0xc0) != 0xc0)
    return 1;
  if (b->read(b->ctx, 0x0c, data, sizeof(data)) ||
      b->read(b->ctx, 0x22, temp, 2))
    return fail(e, "IMU sample interrupted.");
  for (unsigned i = 0; i < 3; ++i) {
    s->acceleration_g[i] = signed16(data + i * 2) / 8192.0f;
    s->angular_dps[i] = signed16(data + 6 + i * 2) / 32.768f;
  }
  s->temperature_c = 23.0f + signed16(temp) / 512.0f;
  return 0;
}
int pos_imu_suspend(const pos_imu_bus_t *b) { return write8(b, 0x7d, 0); }
void pos_ir_nec(uint8_t a, uint8_t c, uint16_t pairs[34][2]) {
  uint32_t bits = (uint32_t)a | (uint32_t)(uint8_t)~a << 8 | (uint32_t)c << 16 |
                  (uint32_t)(uint8_t)~c << 24;
  pairs[0][0] = 9000;
  pairs[0][1] = 4500;
  for (unsigned i = 0; i < 32; ++i) {
    pairs[i + 1][0] = 560;
    pairs[i + 1][1] = bits & (1UL << i) ? 1690 : 560;
  }
  pairs[33][0] = 560;
  pairs[33][1] = 1000;
}
