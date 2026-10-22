#include "pos_boot.h"
#include "esp_timer.h"
#include "pos_device.h"

void pos_boot_animation(QueueHandle_t input, const bool *display_ok) {
  if (!*display_ok || !input)
    return;
  pos_view_t view;
  uint32_t hold_ms;
  int64_t start = esp_timer_get_time();
  for (unsigned frame = 0;; ++frame) {
    /* Keep the intro's clock moving even if drawing takes longer than a
     * frame. Never omit the final logo or hold it longer to catch up. */
    unsigned elapsed = (esp_timer_get_time() - start) /
                       (POS_BOOT_FRAME_MS * 1000);
    if (frame && elapsed > frame)
      frame = elapsed < POS_BOOT_HOLD_FRAME ? elapsed : POS_BOOT_HOLD_FRAME;
    if (!pos_boot_frame(frame, &view, &hold_ms))
      break;
    pos_device_show(&view);
    if (!*display_ok)
      break;
    /* Light the LCD only after its first black frame has been transferred. */
    if (!frame)
      pos_device_brightness(70);
    int64_t deadline = frame == POS_BOOT_HOLD_FRAME
                           ? esp_timer_get_time() + hold_ms * 1000
                           : start + (frame + 1) * POS_BOOT_FRAME_MS * 1000;
    do {
      int64_t remaining = deadline - esp_timer_get_time();
      TickType_t ticks =
          remaining > 0 ? pdMS_TO_TICKS((remaining + 999) / 1000) : 0;
      if (remaining > 0 && !ticks)
        ticks = 1;
      pos_event_t event;
      /* Use the input queue directly: a skip must not trigger key sounds,
       * global Home, or an action in the next setup/recovery/launcher screen. */
      if (xQueueReceive(input, &event, ticks) == pdTRUE && event.pressed)
        goto finished;
    } while (esp_timer_get_time() < deadline);
    if (frame == POS_BOOT_HOLD_FRAME)
      break;
  }
finished:
  xQueueReset(input);
}
