// diag.cpp
#include "diag.h"
#include "app_config.h"
#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>

RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static uint32_t rtcBoots;
RTC_NOINIT_ATTR static uint32_t rtcWdt;
RTC_NOINIT_ATTR static int32_t rtcBackupDay;
constexpr uint32_t RTC_MAGIC = 0xA1EC5A05UL;

static int resetReason = 0;
static bool wdtReady = false;
static uint32_t minFree = 0xFFFFFFFFUL;
static uint64_t busy = 0;
static uint32_t windowStart = 0;
static int cpuPct = 0;

void diagInit() {
  resetReason = static_cast<int>(esp_reset_reason());
  if (rtcMagic != RTC_MAGIC) {
    rtcMagic = RTC_MAGIC;
    rtcBoots = 0;
    rtcWdt = 0;
    rtcBackupDay = -1;
  }
  ++rtcBoots;
  if (resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_INT_WDT || resetReason == ESP_RST_WDT) ++rtcWdt;
  Serial.printf("DIAG: start #%u, reset: %s, restarty WDT: %u\n", (unsigned)rtcBoots, diagResetReasonText(), (unsigned)rtcWdt);
}

void diagWatchdogStart() {
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = WATCHDOG_TIMEOUT_S * 1000;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;
  esp_err_t err = esp_task_wdt_reconfigure(&cfg);
  if (err == ESP_ERR_INVALID_STATE) err = esp_task_wdt_init(&cfg);
  wdtReady = err == ESP_OK;
  if (wdtReady) diagWatchdogAddCurrentTask();
  Serial.printf("DIAG: watchdog %s (%us)\n", wdtReady ? "aktywny" : "NIEAKTYWNY", (unsigned)WATCHDOG_TIMEOUT_S);
}

void diagWatchdogAddCurrentTask() {
  if (!wdtReady) return;
  if (esp_task_wdt_status(nullptr) != ESP_OK) esp_task_wdt_add(nullptr);
  esp_task_wdt_reset();
}

void diagFeedWatchdog() {
  if (wdtReady && esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
  const uint32_t f = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (f < minFree) minFree = f;
}

void diagCpuAdd(uint32_t us) {
  busy += us;
  const uint32_t now = millis();
  if (windowStart == 0) windowStart = now;
  if (now - windowStart >= 5000) {
    cpuPct = static_cast<int>(busy * 100 / (static_cast<uint64_t>(now - windowStart) * 1000));
    if (cpuPct > 100) cpuPct = 100;
    busy = 0;
    windowStart = now;
  }
}

int diagCpuPct() { return cpuPct; }
uint32_t diagMinFreeInternal() { return minFree == 0xFFFFFFFFUL ? 0 : minFree; }
uint32_t diagBootCount() { return rtcBoots; }
uint32_t diagWdtResets() { return rtcWdt; }
int32_t diagLastBackupDay() { return rtcBackupDay; }
void diagSetLastBackupDay(int32_t d) { rtcBackupDay = d; }

const char *diagResetReasonText() {
  switch (resetReason) {
    case ESP_RST_POWERON: return "wlaczenie zasilania";
    case ESP_RST_SW: return "restart programowy";
    case ESP_RST_PANIC: return "blad (panic)";
    case ESP_RST_INT_WDT: return "watchdog przerwan";
    case ESP_RST_TASK_WDT: return "watchdog zadania";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "spadek napiecia";
    case ESP_RST_EXT: return "reset zewnetrzny";
    case ESP_RST_USB: return "USB";
    default: return "inny";
  }
}
