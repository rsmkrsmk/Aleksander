// hw_display.cpp — sterownik obrazu: ST7701 + esp_lcd RGB (bounce + 2 FB w PSRAM), LVGL DIRECT.
#include "hw_display.h"
// Pola sram_trans_align / on_bounce_frame_finish sa w nowszych rdzeniach oznaczone jako
// przestarzale, ale potrzebne dla zgodnosci ze starszymi (3.2.x) — bez ostrzezen.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "app_config.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <sdkconfig.h>

// Ekspander TCA9554 (zasilanie/reset ST7701 i jego komendy inicjalizujace).
static Arduino_XCA9554SWSPI *expander = nullptr;
static esp_lcd_panel_handle_t panel = nullptr;
static SemaphoreHandle_t transferSem = nullptr;
static void *fb0 = nullptr, *fb1 = nullptr;
static lv_display_t *lvDisp = nullptr;

constexpr uint8_t BACKLIGHT_PIN = 4;

// Zegar pikseli: ramka 552 x 518 px; 6 MHz = ~47,7 ms (~21 FPS). Wyzszy zegar przekracza
// pasmo PSRAM przy renderowaniu LVGL (sprawdzone) — nie zmieniac bez testow.
constexpr uint32_t PCLK_HZ = 6000000;
constexpr uint32_t FRAME_US = static_cast<uint32_t>(552ULL * 518ULL * 1000000ULL / PCLK_HZ);
// Tryb bounce (2 x 80 linii w RAM wewnetrznym) + 2 framebuffery w PSRAM. Tryb EDMA bez
// bounce byl testowany 19.10.2026: linie, mruganie, przesuniecia. NIE wracac do niego.
constexpr uint32_t BOUNCE_LINES = 80;
constexpr uint32_t BOUNCE_CHUNKS = SCREEN_H / BOUNCE_LINES;   // 6 porcji (przerwan EOF) na klatke
// Na czas zapisu flasha: 1 MHz => porcja bounce trwa ~44 ms, wiec krotki zapis jej nie zaglodzi.
constexpr uint32_t PCLK_FLASH_SAFE_HZ = 1000000;

// =============================================================================
//  PRZYCZYNA PRZESUNIECIA OBRAZU (ustalona 26.10.2026 na zrodlach ESP-IDF 5.5.5)
//  Rdzen Arduino 3.3.x jest skompilowany z CONFIG_LCD_RGB_RESTART_IN_VSYNC=1. W tym trybie
//  sterownik restartuje DMA w kazdym VSYNC zawsze od bufora bounce nr 0, ale licznika
//  przerwan EOF (bb_eof_count) NIGDY nie zeruje — a to z jego parzystosci wybiera, ktory
//  bufor uzupelnic po przerwaniu. Gdy dwa przerwania EOF zleja sie w jedno (przerwanie
//  spoznione > 1 porcji: zapis flasha, start Wi-Fi, dluga sekcja krytyczna), parzystosc
//  odwraca sie NA STALE: obraz zaczyna sie od 2. porcji (przesuniecie o 80 linii w gore
//  z zawinieciem), a uzupelnianie bufora, ktory DMA wlasnie czyta, daje poziome artefakty.
//  Twarda resynchronizacja (esp_lcd_panel_init) licznika nie zeruje, wiec nie pomagala;
//  detektor fazy go nie widzial, bo moment zawiniecia klatki sie nie zmienia.
//  Model maszyny stanow sterownika: tools/sim5/rgb_bounce_model.py.
//
//  POPRAWKA: w callbacku VSYNC (wywolywanym w tym samym przerwaniu tuz PRZED restartem DMA)
//  zerujemy licznik EOF — dokladnie to, co sterownik robi sam w trybie bez RESTART_IN_VSYNC.
//  Parzystosc jest wtedy poprawna na poczatku kazdej klatki; ewentualne zaklocenie psuje
//  najwyzej jedna klatke. Pole licznika jest prywatne, wiec jego adres ustalamy w czasie
//  dzialania po kotwicach w strukturze sterownika (nasze wskazniki callbackow i user_ctx)
//  z kontrola sensownosci; gdy sie nie uda — log i dzialanie jak dotad.
// =============================================================================
static volatile uint32_t *drvEofCount = nullptr;   // esp_rgb_panel_t::bb_eof_count
static portMUX_TYPE *drvLock = nullptr;            // esp_rgb_panel_t::spinlock
static bool fixActive = false;

// --- statystyki (zmieniane w przerwaniu VSYNC) ---------------------------------
static volatile uint32_t vsyncCount = 0;
static volatile uint32_t lastVsyncUs = 0;
static volatile uint32_t lateVsyncs = 0;      // VSYNC spozniony > 1 ramka + 15 ms (przestoj przerwan)
static volatile uint32_t frameEofBad = 0;     // klatki z liczba EOF != 6 (zgubione/spoznione przerwanie)
static volatile uint32_t parityFixes = 0;     // klatki, w ktorych poprawka przywrocila parzystosc
static volatile uint32_t lastBadEof = 0;
static volatile int flashDepth = 0;
static volatile uint32_t flashWrites = 0;
static volatile bool resyncPending = false;
static volatile uint32_t resyncCount = 0;

static inline uint32_t nowUs() { return static_cast<uint32_t>(esp_timer_get_time()); }

static bool onTransferDone(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *ctx) {
  BaseType_t woken = pdFALSE;
  if (ctx) xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(ctx), &woken);
  return woken == pdTRUE;
}

static bool IRAM_ATTR onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  const uint32_t n = vsyncCount + 1;
  vsyncCount = n;
  const uint32_t t = nowUs();
  const uint32_t prev = lastVsyncUs;
  lastVsyncUs = t;
  if (prev != 0 && flashDepth == 0 && t - prev > FRAME_US + 15000) lateVsyncs = lateVsyncs + 1;
#if CONFIG_LCD_RGB_RESTART_IN_VSYNC
  if (drvEofCount) {
    if (drvLock) portENTER_CRITICAL_ISR(drvLock);
    const uint32_t c = *drvEofCount;
    *drvEofCount = 0;
    if (drvLock) portEXIT_CRITICAL_ISR(drvLock);
    if (n > 2 && c != BOUNCE_CHUNKS) {   // pierwsze klatki po starcie pomijamy
      frameEofBad = frameEofBad + 1;
      lastBadEof = c;
      if (c & 1) parityFixes = parityFixes + 1;
    }
  }
#endif
  return false;
}

// Odnajduje w prywatnej strukturze esp_rgb_panel_t pola bb_eof_count i spinlock.
// Uklad (IDF 5.x): ... bounce_pos_px, bb_eof_count, expect_eof_count, on_color_trans_done,
// on_frame_buf_complete, on_vsync, on_bounce_empty, user_ctx, x_gap, y_gap, spinlock ...
static void locateDriverState() {
#if CONFIG_LCD_RGB_RESTART_IN_VSYNC
  uint32_t *w = reinterpret_cast<uint32_t *>(panel);   // 'base' jest pierwszym polem struktury
  const uint32_t aTx = reinterpret_cast<uint32_t>(&onTransferDone);
  const uint32_t aVs = reinterpret_cast<uint32_t>(&onVsync);
  const uint32_t aCtx = reinterpret_cast<uint32_t>(transferSem);
  for (int i = 4; i < 112; ++i) {   // struktura ma ~0x160 B (pole on_color_trans_done: +0x130 w IDF 5.5.5)
    if (w[i] != aTx || w[i + 2] != aVs || w[i + 4] != aCtx) continue;
    const uint32_t expect = w[i - 1];
    const uint32_t pos = w[i - 3];
    const uint32_t chunkPx = SCREEN_W * BOUNCE_LINES;
    if (expect != BOUNCE_CHUNKS || pos % chunkPx != 0 || pos > chunkPx * BOUNCE_CHUNKS) {
      Serial.printf("LCD: struktura sterownika nie pasuje (expect %u, pos %u) — poprawka EOF wylaczona.\n",
                    (unsigned)expect, (unsigned)pos);
      return;
    }
    drvEofCount = &w[i - 2];
    portMUX_TYPE *lk = reinterpret_cast<portMUX_TYPE *>(&w[i + 7]);
    if (lk->owner == SPINLOCK_FREE) drvLock = lk;
    fixActive = true;
    Serial.printf("LCD: poprawka licznika EOF aktywna (pole +%d B, blokada %s).\n", (i - 2) * 4, drvLock ? "tak" : "nie");
    return;
  }
  Serial.println("LCD: nie znaleziono pol sterownika — poprawka EOF wylaczona.");
#else
  Serial.println("LCD: rdzen bez RESTART_IN_VSYNC — sterownik sam zeruje licznik EOF.");
#endif
}

static void flushCb(lv_display_t *d, const lv_area_t *, uint8_t *px) {
  // DIRECT: LVGL renderuje do niewidocznego FB; draw_bitmap na pelnym obszarze przelacza
  // framebuffery bez kopiowania. Tylko na ostatnim fragmencie klatki.
  if (lv_display_flush_is_last(d)) esp_lcd_panel_draw_bitmap(panel, 0, 0, SCREEN_W, SCREEN_H, px);
  lv_display_flush_ready(d);
}

bool displayInit() {
  // Sekwencja zasilania panelu z oficjalnego demo Waveshare (piny 5 i 6 ekspandera).
  expander = new Arduino_XCA9554SWSPI(7, 0, 2, 1, &Wire, 0x20);
  expander->pinMode(5, OUTPUT);
  expander->pinMode(6, OUTPUT);
  expander->digitalWrite(6, LOW);
  delay(200);
  expander->digitalWrite(5, LOW);
  delay(200);
  expander->digitalWrite(5, HIGH);
  delay(200);
  if (!expander->begin()) {
    Serial.println("LCD: nie mozna uruchomic magistrali ekspandera.");
    return false;
  }
  expander->sendCommand(0x01);
  delay(120);
  expander->batchOperation(st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

  esp_lcd_rgb_panel_config_t c = {};
  c.clk_src = LCD_CLK_SRC_DEFAULT;
  c.timings.pclk_hz = PCLK_HZ;
  c.timings.h_res = SCREEN_W;
  c.timings.v_res = SCREEN_H;
  c.timings.hsync_pulse_width = 8;
  c.timings.hsync_back_porch = 50;
  c.timings.hsync_front_porch = 14;
  c.timings.vsync_pulse_width = 8;
  c.timings.vsync_back_porch = 20;
  c.timings.vsync_front_porch = 10;
  c.data_width = 16;
  c.bits_per_pixel = 16;
  c.num_fbs = 2;
  c.bounce_buffer_size_px = SCREEN_W * BOUNCE_LINES;   // musi dzielic 480*480 bez reszty
  c.sram_trans_align = 8;
  c.dma_burst_size = 64;
  c.hsync_gpio_num = 46;
  c.vsync_gpio_num = 3;
  c.de_gpio_num = 17;
  c.pclk_gpio_num = 9;
  c.disp_gpio_num = GPIO_NUM_NC;
  static const int dataPins[16] = {40, 41, 42, 2, 1, 21, 8, 18, 45, 38, 39, 10, 11, 12, 13, 14};
  for (int i = 0; i < 16; ++i) c.data_gpio_nums[i] = dataPins[i];
  c.flags.disp_active_low = 1;
  c.flags.fb_in_psram = 1;
  c.flags.double_fb = 1;
  c.flags.bb_invalidate_cache = 1;
  esp_err_t err = esp_lcd_new_rgb_panel(&c, &panel);
  if (err != ESP_OK) { Serial.printf("LCD: esp_lcd_new_rgb_panel blad 0x%x\n", (unsigned)err); return false; }

  transferSem = xSemaphoreCreateBinary();
  esp_lcd_rgb_panel_event_callbacks_t cb = {};
  cb.on_color_trans_done = onTransferDone;
  cb.on_vsync = onVsync;
  err = esp_lcd_rgb_panel_register_event_callbacks(panel, &cb, transferSem);
  if (err == ESP_OK) locateDriverState();   // przed startem strumienia (esp_lcd_panel_init)
  if (err == ESP_OK) err = esp_lcd_panel_reset(panel);
  if (err == ESP_OK) err = esp_lcd_panel_init(panel);
  if (err == ESP_OK) err = esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &fb0, &fb1);
  if (err != ESP_OK || !fb0 || !fb1) { Serial.printf("LCD: inicjalizacja blad 0x%x\n", (unsigned)err); return false; }

    // Biala pierwsza klatka.
  const size_t fbBytes = SCREEN_W * SCREEN_H * 2;
  memset(fb0, 0xFF, fbBytes);
  memset(fb1, 0xFF, fbBytes);
  esp_lcd_panel_draw_bitmap(panel, 0, 0, SCREEN_W, SCREEN_H, fb0);
  xSemaphoreTake(transferSem, pdMS_TO_TICKS(100));

  lv_init();
  lvDisp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(lvDisp, flushCb);
  lv_display_set_buffers(lvDisp, fb0, fb1, fbBytes, LV_DISPLAY_RENDER_MODE_DIRECT);
  Serial.printf("LCD: 2 FB w PSRAM, bounce %u linii, wolny RAM wewn. %u KB\n", (unsigned)BOUNCE_LINES,
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
  return true;
}

void displayBacklightInit() {
  ledcAttach(BACKLIGHT_PIN, 5000, 8);
  ledcWrite(BACKLIGHT_PIN, BACKLIGHT_FULL_DUTY);
}
void displaySetBacklight(uint8_t duty) { ledcWrite(BACKLIGHT_PIN, duty); }

uint32_t displayVsyncCount() { return vsyncCount; }

void displayRequestResync() { resyncPending = true; }

// Twarda resynchronizacja (tylko na zadanie, polecenie 'h'): pelny restart strumienia od
// poczatku klatki, z wyzerowaniem licznika EOF.
void displayResyncIfPending() {
  if (!panel || !resyncPending || flashDepth > 0) return;
  resyncPending = false;
  esp_lcd_panel_init(panel);
  if (drvEofCount) {
    if (drvLock) portENTER_CRITICAL(drvLock);
    *drvEofCount = 0;
    if (drvLock) portEXIT_CRITICAL(drvLock);
  }
  resyncCount = resyncCount + 1;
  Serial.printf("LCD: twarda resynchronizacja (#%u)\n", (unsigned)resyncCount);
}

DisplayDiag displayDiag() {
  DisplayDiag d;
  d.eofFix = fixActive;
  d.frames = vsyncCount;
  d.badFrames = frameEofBad;
  d.parityFixes = parityFixes;
  d.lastBadEof = lastBadEof;
  d.lateVsyncs = lateVsyncs;
  d.resyncCount = resyncCount;
  d.flashWrites = flashWrites;
  return d;
}

void displayLogEvents() {
  static uint32_t seenBad = 0, lastMs = 0;
  if (frameEofBad == seenBad || millis() - lastMs < 2000) return;
  lastMs = millis();
  Serial.printf("LCD: zaklocone klatki: %u (EOF w ostatniej: %u, przywrocona kolejnosc buforow: %u razy)\n",
                (unsigned)frameEofBad, (unsigned)lastBadEof, (unsigned)parityFixes);
  seenBad = frameEofBad;
}

// esp_lcd_rgb_panel_set_pclk moze nie istniec w starszym rdzeniu — deklaracja "weak".
extern "C" esp_err_t esp_lcd_rgb_panel_set_pclk(esp_lcd_panel_handle_t panel, uint32_t freq_hz) __attribute__((weak));

FlashWriteGuard::FlashWriteGuard() {
  if (!panel) return;
  active_ = true;
  if (__atomic_add_fetch(const_cast<int *>(&flashDepth), 1, __ATOMIC_SEQ_CST) == 1 && esp_lcd_rgb_panel_set_pclk) {
    esp_lcd_rgb_panel_set_pclk(panel, PCLK_FLASH_SAFE_HZ);
    delay(70);   // nowy zegar dziala od najblizszego VSYNC
  }
}

FlashWriteGuard::~FlashWriteGuard() {
  if (!active_) return;
  flashWrites = flashWrites + 1;
  if (__atomic_sub_fetch(const_cast<int *>(&flashDepth), 1, __ATOMIC_SEQ_CST) == 0 && esp_lcd_rgb_panel_set_pclk)
    esp_lcd_rgb_panel_set_pclk(panel, PCLK_HZ);
}
