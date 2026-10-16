#include "pos_health.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_private/panic_internal.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pos_diagnostics.h"
#include "pos_fs.h"
#include "xtensa_context.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Panic context: RTC-only fixed-size writes. The normal SDK handler still
 * prints registers/backtrace and resets. No filesystem, allocation, locks or
 * NVS here. */
typedef struct {
  uint32_t magic, core, exception, pc, sp, a0, cause, address, check;
  char version[32], build[65];
} panic_record_t;
static RTC_NOINIT_ATTR volatile panic_record_t panic_record;
static DRAM_ATTR char current_version[32], current_build[65];
static uint32_t IRAM_ATTR checksum(const volatile panic_record_t *r) {
  uint32_t value =
      r->core ^ r->exception ^ r->pc ^ r->sp ^ r->a0 ^ r->cause ^ r->address;
  for (unsigned i = 0; i < 32; ++i)
    value ^= (uint32_t)(unsigned char)r->version[i] << ((i % 4) * 8);
  for (unsigned i = 0; i < 65; ++i)
    value ^= (uint32_t)(unsigned char)r->build[i] << ((i % 4) * 8);
  return ~value;
}
void __real_esp_panic_handler(panic_info_t *info);
void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t *info) {
  panic_record.magic = 0;
  if (info) {
    panic_record.core = info->core;
    panic_record.exception = info->exception;
    panic_record.pc = (uint32_t)info->addr;
    panic_record.sp = panic_record.a0 = panic_record.cause =
        panic_record.address = 0;
    if (info->frame) {
      const XtExcFrame *frame = info->frame;
      panic_record.pc = frame->pc;
      panic_record.sp = frame->a1;
      panic_record.a0 = frame->a0;
      panic_record.cause = frame->exccause;
      panic_record.address = frame->excvaddr;
    }
    for (unsigned i = 0; i < 32; ++i)
      panic_record.version[i] = current_version[i];
    for (unsigned i = 0; i < 65; ++i)
      panic_record.build[i] = current_build[i];
    panic_record.check = checksum(&panic_record);
    panic_record.magic = 0x50434c31;
  }
  __real_esp_panic_handler(info);
}
static bool safe, tracking, ready, healthy;
static uint32_t failures;
static uint64_t ready_ms, confirm_attempt_ms;
static int (*confirm_hook)(void);
void pos_health_set_confirm_hook(int (*hook)(void)) { confirm_hook = hook; }
static int reset_reason;
static int commit_value(const char *name, uint32_t value) {
  nvs_handle_t h;
  if (nvs_open("pos_health", NVS_READWRITE, &h) != ESP_OK)
    return -1;
  int result = nvs_set_u32(h, name, value) != ESP_OK || nvs_commit(h) != ESP_OK;
  nvs_close(h);
  return result ? -1 : 0;
}
void pos_health_begin(bool supervise) {
  reset_reason = esp_reset_reason();
  snprintf(current_version, sizeof(current_version), "%s",
           esp_app_get_description()->version);
  for (unsigned i = 0; i < 32; ++i)
    snprintf(current_build + 2 * i, 3, "%02x",
             esp_app_get_description()->app_elf_sha256[i]);

  tracking = supervise;
  if (nvs_flash_init() != ESP_OK)
    return;
  nvs_handle_t h;
  if (nvs_open("pos_health", NVS_READWRITE, &h) != ESP_OK)
    return;
  uint32_t pending = 0, forced = 0;
  nvs_get_u32(h, "failures", &failures);
  nvs_get_u32(h, "pending", &pending);
  nvs_get_u32(h, "safe", &forced);
  pos_boot_policy_t policy = {
      .failures = failures, .pending = pending, .forced = forced};
  safe = pos_boot_policy_begin(
      &policy, supervise,
      reset_reason == ESP_RST_PANIC || reset_reason == ESP_RST_INT_WDT ||
          reset_reason == ESP_RST_TASK_WDT || reset_reason == ESP_RST_WDT);
  failures = policy.failures;
  if (supervise)
    nvs_set_u32(h, "failures", failures);
  if (supervise)
    nvs_set_u32(h, "pending", 1);
  nvs_commit(h);
  nvs_close(h);
}
void pos_health_ready(void) {
  ready = true;
  ready_ms = esp_timer_get_time() / 1000;
}
bool pos_health_safe(void) { return safe; }
void pos_health_clear(void) {
  if (!commit_value("pending", 0) && !commit_value("failures", 0)) {
    failures = 0;
    healthy = true;
  }
}
int pos_health_resume(void) {
  if (commit_value("safe", 0))
    return -1;
  safe = false;
  pos_health_clear();
  if (safe || failures)
    return -1;
  healthy = false;
  ready_ms = esp_timer_get_time() / 1000;
  if (tracking && commit_value("pending", 1))
    return -1;
  return 0;
}
int pos_health_request_safe(bool value) {
  if (commit_value("safe", value ? 1 : 0))
    return -1;
  return 0;
}
void pos_health_tick(void) {
  uint64_t now = esp_timer_get_time() / 1000;
  if (tracking && ready && !healthy && !safe && now - ready_ms >= 60000 &&
      (!confirm_attempt_ms || now - confirm_attempt_ms >= 5000)) {
    confirm_attempt_ms = now;
    if (!confirm_hook || !confirm_hook())
      pos_health_clear();
  }
}

int pos_health_export(const char *root) {
  if (reset_reason != ESP_RST_PANIC && reset_reason != ESP_RST_INT_WDT &&
      reset_reason != ESP_RST_TASK_WDT && reset_reason != ESP_RST_WDT &&
      reset_reason != ESP_RST_BROWNOUT && panic_record.magic != 0x50434c31)
    return 0;
  panic_record_t p = panic_record;
  bool valid = p.magic == 0x50434c31 && p.check == checksum(&p);
  pos_crash_record_t record = {.reset_reason = reset_reason,
                               .failed_boots = failures,
                               .panic_snapshot = valid,
                               .utc = time(NULL)};
  if (record.utc < 1577836800)
    record.utc = 0;
  snprintf(record.boot_version, sizeof(record.boot_version), "%s",
           esp_app_get_description()->version);
  if (valid) {
    if (!memchr(p.version, 0, sizeof(p.version)) ||
        !memchr(p.build, 0, sizeof(p.build)))
      return -1;
    memcpy(record.panic_version, p.version, sizeof(p.version));
    memcpy(record.panic_build, p.build, sizeof(p.build));
    record.core = p.core;
    record.exception = p.exception;
    record.pc = p.pc;
    record.sp = p.sp;
    record.a0 = p.a0;
    record.cause = p.cause;
    record.address = p.address;
  }
  char destination[POS_FS_PATH];
  if (pos_crash_record_write(root, &record, destination))
    return -1;
  panic_record.magic = 0;
  reset_reason = ESP_RST_UNKNOWN;
  return 0;
}
