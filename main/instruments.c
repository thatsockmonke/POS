#include "pos_device.h"
#include "pos_peripherals.h"
#include "ui.h"
#include <errno.h>
#include <stdlib.h>
static void motion(void) {
  pos_error_t e = {0};
  pos_imu_sample_t sample = {0};
  if (pos_motion_start(&e)) {
    pos_ui_notice("IMU unavailable", e.message);
    return;
  }
  pos_view_t *v = calloc(1, sizeof(*v));
  if (!v) {
    pos_motion_stop();
    return;
  }
  while (!pos_home_pending()) {
    pos_event_t event;
    if (pos_device_event(&event, 100) && event.pressed &&
        (event.key == POS_KEY_ESCAPE || event.key == POS_KEY_HOME))
      break;
    int result = pos_motion_read(&sample, &e);
    if (result < 0) {
      pos_ui_notice("IMU stopped", e.message);
      break;
    }
    if (result)
      continue;
    pos_view_clear(v, "IMU / BMI270");
    pos_view_line(v, 2, "Acceleration (g)");
    pos_view_line(v, 4, "X:%+.2f Y:%+.2f", (double)sample.acceleration_g[0],
                  (double)sample.acceleration_g[1]);
    pos_view_line(v, 5, "Z:%+.2f", (double)sample.acceleration_g[2]);
    pos_view_line(v, 7, "Gyroscope (degrees/s)");
    pos_view_line(v, 9, "X:%+.1f Y:%+.1f", (double)sample.angular_dps[0],
                  (double)sample.angular_dps[1]);
    pos_view_line(v, 10, "Z:%+.1f T:%.1f C", (double)sample.angular_dps[2],
                  (double)sample.temperature_c);
    pos_view_line(v, 12, "Sensor axes; no compass");
    pos_view_line(v, 15, "Esc:back Fn+Space:close");
    pos_device_show(v);
  }
  pos_motion_stop();
  free(v);
}
static bool byte(const char *title, char text[4], uint8_t *out) {
  if (!pos_ui_text(title, "Hex byte (00-FF)", text, 4, false))
    return false;
  char *end;
  errno = 0;
  unsigned long n = strtoul(text, &end, 16);
  if (errno || !*text || *end || n > 255) {
    pos_ui_notice("Invalid byte",
                  "Enter two hexadecimal digits, 00 through FF.");
    return false;
  }
  *out = (uint8_t)n;
  return true;
}
void pos_ui_instruments(void) {
  const char *items[] = {"Live IMU readings", "Send infrared / NEC",
                         "Hardware information", "Back"};
  while (!pos_home_pending()) {
    int choice = pos_ui_menu("IR & motion", items, 4);
    if (choice < 0 || choice == 3)
      return;
    if (choice == 0)
      motion();
    if (choice == 1) {
      uint8_t address, command;
      char a[4] = "00", c[4] = "00";
      pos_error_t e = {0};
      if (byte("NEC address", a, &address) &&
          byte("NEC command", c, &command)) {
        const char *send[] = {"Cancel", "Send one NEC frame"};
        if (pos_ui_menu("Point emitter at your receiver", send, 2) == 1)
          pos_ui_notice("Infrared",
                        pos_infrared_send(address, command, &e)
                            ? e.message
                            : "One 38 kHz NEC frame transmitted. This does not "
                              "confirm the receiver acted on it.");
      }
    }
    if (choice == 2)
      pos_ui_notice(
          "IR & motion hardware",
          "ADV: BMI270 at I2C 69 (68 fallback), +/-4 g and +/-1000 degrees/s, "
          "100 Hz sensor rate. Values use raw sensor axes; no compass or "
          "calibrated heading. IR emitter: GPIO44, 38 kHz NEC transmit. The "
          "ADV has no built-in IR receiver. Services start only when used.");
  }
}
