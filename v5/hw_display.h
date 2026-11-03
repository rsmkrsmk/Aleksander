// =============================================================================
//  hw_display.h — panel RGB ST7701 480x480 (esp_lcd, bounce + 2 FB w PSRAM),
//  LVGL DIRECT, podswietlenie, ochrona obrazu przy (rzadkim) zapisie flasha.
//  Przyczyna i poprawka przesuwania obrazu: opis w hw_display.cpp.
// =============================================================================
#pragma once
#include <stdint.h>

constexpr uint16_t SCREEN_W = 480;
constexpr uint16_t SCREEN_H = 480;

bool displayInit();                 // ekspander, ST7701, esp_lcd, lv_init + lv_display (DIRECT)
void displayBacklightInit();
void displaySetBacklight(uint8_t duty);   // 0 = 100% (AP3032 aktywne niskim)

uint32_t displayVsyncCount();       // licznik klatek (VSYNC)
void displayRequestResync();        // reczna twarda resynchronizacja (polecenie 'h')
void displayResyncIfPending();      // wolac TYLKO z petli UI, tuz po VSYNC
void displayLogEvents();            // log zakloconych klatek (z petli, najwyzej co 2 s)

struct DisplayDiag {
  bool eofFix;                      // poprawka licznika EOF aktywna
  uint32_t frames;                  // klatki od startu
  uint32_t badFrames;               // klatki z zaklocona liczba przerwan EOF
  uint32_t parityFixes;             // ...w tym z odwrocona kolejnoscia buforow (naprawione)
  uint32_t lastBadEof;              // liczba EOF w ostatniej zakloconej klatce
  uint32_t lateVsyncs;              // spoznione VSYNC (przestoj przerwan)
  uint32_t resyncCount;             // reczne twarde resynchronizacje
  uint32_t flashWrites;
  // 5.4.0: mruganie animacji (2112026.md, rozdzial G)
  uint32_t wraps;                   // zawiniecia ramki (przelaczenia bufora przez sterownik)
  uint32_t wrapUs;                  // czas od VSYNC do zawiniecia (oczekiwane ok. 30-32 ms)
  uint32_t renderFrames;            // narysowane klatki LVGL
  uint32_t flushRisk;               // LVGL zaczal pisac do wyswietlanego bufora (ma byc 0)
  uint32_t flushLate;               // klatki pokazane o klatke pozniej (powtorzona klatka, bez mrugania)
  uint32_t lateLastMin;             // ...w ostatniej pelnej minucie
  uint32_t renderMaxUs, renderAvgUs;// czas rysowania klatki w ostatniej pelnej minucie
  uint32_t flushTimeouts;           // czekanie na przelaczenie bufora > 400 ms (poza zapisem flasha 0)
};
DisplayDiag displayDiag();

// RAII: obniza zegar pikseli na czas zapisu do flasha (bufory bounce nie zaglodza sie).
// Zagniezdzenia i uzycie z roznych rdzeni sa liczone atomowo.
struct FlashWriteGuard {
  FlashWriteGuard();
  ~FlashWriteGuard();
  FlashWriteGuard(const FlashWriteGuard &) = delete;
  FlashWriteGuard &operator=(const FlashWriteGuard &) = delete;
 private:
  bool active_ = false;
};
