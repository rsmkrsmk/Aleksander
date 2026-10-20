// hw_display.cpp — sterownik obrazu (przeniesiony z v4.1 bez zmian zachowania).
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
// Na czas zapisu flasha: 1 MHz => pelny bufor bounce starcza na ~44 ms.
constexpr uint32_t PCLK_FLASH_SAFE_HZ = 1000000;

// --- stan resynchronizacji i detektora przesuniecia ---------------------------
static volatile bool resyncPending = false;
static volatile uint8_t resyncReason = 0;
static volatile uint8_t lastResyncReason = 0;
static volatile uint32_t resyncCount = 0;
static volatile uint32_t vsyncCount = 0;
static volatile uint32_t lastVsyncUs = 0;
static volatile uint32_t gapIgnoreUntilUs = 0;
static volatile int flashDepth = 0;
// Restart DMA co VSYNC (jak CONFIG_LCD_RGB_RESTART_IN_VSYNC). WYLACZONY: sterownik sam restartuje,
// gdy zabraknie przerwan bounce, a restart w KAZDEJ klatce z nieIRAM-owego przerwania potrafi
// sie spoznic i trwale przesunac obraz (komentarz w esp_lcd_panel_rgb.c). Do testow: polecenie 'r'.
static volatile bool restartEveryVsync = false;
static volatile uint32_t flashWrites = 0;
// Faza: czas od "klatka skopiowana do buforow bounce" do VSYNC. Staly przy poprawnym
// obrazie; przesuniecie o N x 80 linii zmienia go o N x ~7,4 ms i tak zostaje.
static volatile bool phaseAvailable = false;
static volatile uint32_t frameDoneUs = 0;
static volatile int32_t phaseUs = -1, phaseRefUs = -1;
static volatile uint8_t learnLeft = 0, learnCnt = 0, badFrames = 0;
static volatile int32_t learnSum = 0;
constexpr int32_t PHASE_TOL_US = 3000;

static inline uint32_t nowUs() { return static_cast<uint32_t>(esp_timer_get_time()); }

static bool onTransferDone(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *ctx) {
  BaseType_t woken = pdFALSE;
  if (ctx) xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(ctx), &woken);
  return woken == pdTRUE;
}

static bool onFrameDone(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  frameDoneUs = nowUs();
  return false;
}

// Nazwa pola callbacku zalezy od wersji ESP-IDF w rdzeniu (on_frame_buf_complete w nowszych,
// on_bounce_frame_finish w starszych) — wybor w czasie kompilacji; brak pola = brak detektora.
struct CbCompat {
  template <typename T>
  static auto set(T &cb, int) -> decltype(cb.on_frame_buf_complete = nullptr, bool()) {
    cb.on_frame_buf_complete = onFrameDone; return true;
  }
  template <typename T>
  static auto set(T &cb, long) -> decltype(cb.on_bounce_frame_finish = nullptr, bool()) {
    cb.on_bounce_frame_finish = onFrameDone; return true;
  }
  template <typename T>
  static bool set(T &, ...) { return false; }
};

// VSYNC: licznik klatek, wykrycie spoznionego VSYNC (przestoj przerwan = mozliwe
// przesuniecie), pomiar fazy, restart DMA co klatke (odpowiednik CONFIG_LCD_RGB_RESTART_IN_VSYNC).
static bool onVsync(esp_lcd_panel_handle_t p, const esp_lcd_rgb_panel_event_data_t *, void *) {
  vsyncCount = vsyncCount + 1;
  const uint32_t t = nowUs();
  const uint32_t prev = lastVsyncUs;
  lastVsyncUs = t;
  const bool judge = flashDepth == 0 && static_cast<int32_t>(t - gapIgnoreUntilUs) > 0;
  if (judge && prev != 0 && t - prev > FRAME_US + 15000) {
    resyncReason = resyncReason | RESYNC_LATE_VSYNC;
    resyncPending = true;
  }
  const uint32_t done = frameDoneUs;
  if (judge && phaseAvailable && done != 0) {
    const int32_t ph = static_cast<int32_t>(t - done);
    if (ph >= 0 && ph < static_cast<int32_t>(FRAME_US) + 5000) {
      phaseUs = ph;
      if (learnLeft > 0) {
        learnSum = learnSum + ph;
        learnCnt = learnCnt + 1;
        learnLeft = learnLeft - 1;
        if (learnLeft == 0 && phaseRefUs < 0) phaseRefUs = learnSum / learnCnt;
      } else if (phaseRefUs >= 0) {
        const int32_t d = ph - phaseRefUs;
        if (d > PHASE_TOL_US || d < -PHASE_TOL_US) {
          badFrames = badFrames + 1;
          if (badFrames >= 3) {
            badFrames = 0;
            resyncReason = resyncReason | RESYNC_PHASE;
            resyncPending = true;
          }
        } else {
          badFrames = 0;
        }
      }
    }
  }
  if (p && restartEveryVsync) esp_lcd_rgb_panel_restart(p);
  return false;
}

static void flushCb(lv_display_t *d, const lv_area_t *, uint8_t *px) {
  // DIRECT: LVGL renderuje cala klatke do niewidocznego FB; draw_bitmap na pelnym obszarze
  // przelacza framebuffery bez kopiowania. Tylko na ostatnim fragmencie klatki.
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
  phaseAvailable = CbCompat::set(cb, 0);
  Serial.printf("LCD: detektor przesuniecia obrazu %s.\n", phaseAvailable ? "aktywny" : "NIEDOSTEPNY w tej wersji rdzenia");
  err = esp_lcd_rgb_panel_register_event_callbacks(panel, &cb, transferSem);
  if (err == ESP_OK) err = esp_lcd_panel_reset(panel);
  if (err == ESP_OK) err = esp_lcd_panel_init(panel);
  if (err == ESP_OK) err = esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &fb0, &fb1);
  if (err != ESP_OK || !fb0 || !fb1) { Serial.printf("LCD: inicjalizacja blad 0x%x\n", (unsigned)err); return false; }

  // Biala pierwsza klatka.
  const size_t fbBytes = SCREEN_W * SCREEN_H * 2;
  memset(fb0, 0xFF, fbBytes);
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

bool displayToggleVsyncRestart() {
  restartEveryVsync = !restartEveryVsync;
  return restartEveryVsync;
}

void displayRequestResync(uint8_t reason) {
  resyncReason = resyncReason | reason;
  resyncPending = true;
}

// Twarda resynchronizacja: esp_lcd_panel_init() na dzialajacym panelu restartuje strumien
// (stop, reset FIFO/DMA, pozycja bounce = 0, ponowne napelnienie obu buforow, start).
// Zwykly restart DMA nie zeruje pozycji bounce, wiec przesuniecie zostawalo na stale.
void displayResyncIfPending() {
  if (!panel || !resyncPending || flashDepth > 0) return;
  static uint32_t lastMs = 0;
  if (lastMs != 0 && millis() - lastMs < 700) return;   // bez serii mrugniec
  lastMs = millis();
  resyncPending = false;
  const uint8_t reason = resyncReason;
  resyncReason = 0;
  const int32_t before = phaseUs;
  esp_lcd_panel_init(panel);   // zatrzymuje i uruchamia strumien od poczatku klatki (pozycja bounce = 0)
  gapIgnoreUntilUs = nowUs() + 300000;
  badFrames = 0;
  // Bezpiecznik: gdy resynchronizacja z powodu fazy nie przywraca wzorca, po 3 probach
  // w 5 s uczymy wzorzec od nowa (zamiast mrugac w kolko).
  static uint32_t lastPhaseMs = 0;
  static uint8_t streak = 0;
  if (reason == RESYNC_PHASE) {
    streak = (millis() - lastPhaseMs < 5000) ? streak + 1 : 1;
    lastPhaseMs = millis();
    if (streak >= 3) { streak = 0; phaseRefUs = -1; Serial.println("LCD: faza nie wraca do wzorca — nowy wzorzec."); }
  }
  if (phaseRefUs < 0) { learnSum = 0; learnCnt = 0; learnLeft = 4; }
  resyncCount = resyncCount + 1;
  lastResyncReason = reason;
  Serial.printf("LCD: twarda resynchronizacja (#%u, powod %u, faza %ld us, wzorzec %ld us)\n",
                (unsigned)resyncCount, (unsigned)reason, (long)before, (long)phaseRefUs);
}

void displayResyncAfterVsync() {
  if (!resyncPending) return;
  const uint32_t v = vsyncCount, t = millis();
  while (vsyncCount == v && millis() - t < 70) delay(1);
  if (vsyncCount != v) displayResyncIfPending();
}

DisplayDiag displayDiag() {
  DisplayDiag d;
  d.phaseDetector = phaseAvailable;
  d.resyncCount = resyncCount;
  d.lastReason = lastResyncReason;
  d.phaseUs = phaseUs;
  d.phaseRefUs = phaseRefUs;
  d.flashWrites = flashWrites;
  return d;
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
  if (__atomic_sub_fetch(const_cast<int *>(&flashDepth), 1, __ATOMIC_SEQ_CST) == 0) {
    if (esp_lcd_rgb_panel_set_pclk) esp_lcd_rgb_panel_set_pclk(panel, PCLK_HZ);
    displayRequestResync(RESYNC_FLASH);
  }
}
