// =============================================================================
//  hw_display.h — panel RGB ST7701 480x480 (esp_lcd, bounce + 2 FB w PSRAM),
//  LVGL DIRECT, podswietlenie, ochrona obrazu przy (rzadkim) zapisie flasha.
// =============================================================================
#pragma once
#include <stdint.h>

constexpr uint16_t SCREEN_W = 480;
constexpr uint16_t SCREEN_H = 480;

// Powody twardej resynchronizacji (bity, do logu/diagnostyki).
enum : uint8_t { RESYNC_FLASH = 1, RESYNC_LATE_VSYNC = 2, RESYNC_PHASE = 4, RESYNC_DATA = 8, RESYNC_BOOT = 16, RESYNC_PERIODIC = 32, RESYNC_SCREEN = 64 };

bool displayInit();                 // ekspander, ST7701, esp_lcd, lv_init + lv_display (DIRECT)
void displayBacklightInit();
void displaySetBacklight(uint8_t duty);   // 0 = 100% (AP3032 aktywne niskim)

uint32_t displayVsyncCount();       // licznik klatek (VSYNC)
void displayRequestResync(uint8_t reason);
void displayResyncIfPending();      // wolac TYLKO z petli UI, tuz po VSYNC
void displayResyncAfterVsync();     // czeka na VSYNC (max 70 ms) i wykonuje zalegla resynchronizacje

struct DisplayDiag {
  bool phaseDetector;
  uint32_t resyncCount;
  uint8_t lastReason;
  int32_t phaseUs, phaseRefUs;
  uint32_t flashWrites;
};
DisplayDiag displayDiag();

// RAII: obniza zegar pikseli na czas zapisu do flasha, po zapisie zleca resynchronizacje.
// Zagniezdzenia i uzycie z roznych rdzeni sa liczone atomowo.
struct FlashWriteGuard {
  FlashWriteGuard();
  ~FlashWriteGuard();
  FlashWriteGuard(const FlashWriteGuard &) = delete;
  FlashWriteGuard &operator=(const FlashWriteGuard &) = delete;
 private:
  bool active_ = false;
};
