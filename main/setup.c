#include "pos_timezone.h"
#include "ui.h"
#include <string.h>

/* Edits are local until every screen is accepted and the save succeeds. */
bool pos_ui_setup(const char *root, pos_settings_t *settings, bool first) {
  pos_settings_t next = *settings;
  for (;;) {
    if (!pos_ui_text(first ? "Welcome to POS" : "Settings", "Your display name",
                     next.display_name, sizeof(next.display_name), false))
      return false;
    if (pos_settings_display_name_valid(next.display_name))
      break;
    pos_ui_notice(
        "Display name not valid",
        "Enter 1-48 printable characters. The name cannot be empty. "
        "Ctrl+U clears this field; your other setup choices are kept.");
  }
  for (;;) {
    if (!pos_ui_text("Device identity", "Device name", next.device_name,
                     sizeof(next.device_name), false))
      return false;
    if (pos_settings_device_name_valid(next.device_name))
      break;
    pos_ui_notice(
        "Device name not valid",
        "Use 1-24 letters, numbers or hyphens. Spaces are not allowed. "
        "Ctrl+U clears this field; your display name is kept.");
  }
  if (!pos_ui_timezone(next.timezone))
    return false;
  int choice;
  const char *levels[] = {"Dim (25%)", "Normal (70%)", "Bright (100%)"};
  choice = pos_ui_menu("Display brightness", levels, 3);
  if (choice < 0)
    return false;
  next.brightness = choice == 0 ? 25 : choice == 1 ? 70 : 100;
  next.setup_complete = true;
  if (!pos_settings_valid(&next) || pos_settings_save(root, &next)) {
    pos_ui_notice("Settings not saved",
                  "Storage may be full or unreadable. Existing saved settings "
                  "are preserved; your active settings have not changed.");
    return false;
  }
  *settings = next;
  return true;
}

bool pos_ui_timezone(char timezone[64]) {
  const char *items[40];
  for (size_t i = 0; i < pos_timezone_count; ++i)
    items[i] = pos_timezones[i].name;
  items[pos_timezone_count] = "Custom POSIX timezone";
  int choice = pos_ui_menu("Timezone", items, pos_timezone_count + 1);
  if (choice < 0)
    return false;
  if ((size_t)choice < pos_timezone_count) {
    if (pos_timezones[choice].rule)
      strcpy(timezone, pos_timezones[choice].rule);
    return true;
  }
  char text[64];
  strcpy(text, timezone);
  while (pos_ui_text("Custom timezone", "POSIX rule, e.g. UTC0", text,
                     sizeof(text), false)) {
    if (pos_timezone_valid(text)) {
      strcpy(timezone, text);
      return true;
    }
    pos_ui_notice("Invalid timezone",
                  "Use a POSIX timezone rule, not an IANA name. Example: "
                  "PST8PDT,M3.2.0,M11.1.0 or IST-5:30. Escape cancels.");
  }
  return false;
}
