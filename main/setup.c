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
  const char *zones[] = {"UTC", "Los Angeles / Pacific", "New York / Eastern",
                         "Keep current timezone"};
  int choice = pos_ui_menu("Timezone", zones, 4);
  if (choice < 0)
    return false;
  if (choice == 0)
    strcpy(next.timezone, "UTC0");
  if (choice == 1)
    strcpy(next.timezone, "PST8PDT,M3.2.0,M11.1.0");
  if (choice == 2)
    strcpy(next.timezone, "EST5EDT,M3.2.0,M11.1.0");
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
