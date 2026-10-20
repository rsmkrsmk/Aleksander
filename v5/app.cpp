// =============================================================================
//  app.cpp — start urzadzenia i petla UI (rdzen 1)
//  Kolejnosc startu: diagnostyka -> I2C -> panel -> dotyk -> ekran startowy ->
//  Wi-Fi -> zegar -> cache danych -> pogoda (RTC) -> dane z WWW -> tablica.
// =============================================================================
#include "app.h"
#include "app_config.h"
#include "hw_display.h"
#include "hw_touch.h"
#include "net.h"
#include "host_sync.h"
#include "weather.h"
#include "telegram.h"
#include "stats.h"
#include "diag.h"
#include "ui.h"
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <lvgl.h>
#include <esp_heap_caps.h>

static Store *store = nullptr;
static UiModel model;
static SleepState sleepState;
static uint32_t chartsHash = 0;
static uint32_t lastLvTick = 0;
static uint32_t lastVsync = 0;
static uint32_t lastLiveMs = 0, lastChartsMs = 0;
static bool chartsDue = true;
static bool night = false;
static bool dimmed = false;
static char diagText[sizeof(UiDiag::text)];

// ------------------------------------------------------------------ pomocnicze
static bool nightNow() {
  if (!timeOk()) return false;
  const time_t n = time(nullptr);
  struct tm t;
  localtime_r(&n, &t);
  return t.tm_hour >= NIGHT_START_HOUR || t.tm_hour < NIGHT_END_HOUR;
}

static void lvTick() {
  const uint32_t n = millis();
  lv_tick_inc(n - lastLvTick);
  lastLvTick = n;
}

// Jeden krok renderu zsynchronizowany z VSYNC (max 40 ms czekania), z odczytem dotyku.
static uint32_t renderStep() {
  uint32_t workUs = 0;
  const uint32_t w0 = millis();
  do {
    delay(1);
    const uint32_t a = micros();
    lvTick();
    touchPoll();
    workUs += micros() - a;
  } while (displayVsyncCount() == lastVsync && millis() - w0 < 40);
  if (displayVsyncCount() != lastVsync) displayResyncIfPending();   // tuz po VSYNC
  lastVsync = displayVsyncCount();
  const uint32_t a = micros();
  lv_timer_handler();
  return workUs + (micros() - a);
}

static void bootPump(uint32_t ms) {
  const uint32_t t0 = millis();
  do { renderStep(); } while (millis() - t0 < ms);
}

static void computeModel(bool charts) {
  const WeatherData w = weatherSnapshot();
  StatsInput in;
  in.store = store;
  in.now = time(nullptr);
  in.timeOk = timeOk();
  in.wifiOk = wifiOk();
  in.night = night;
  in.weather = &w;
  statsCompute(in, model, charts, &sleepState);
}

static void ago(char *dst, size_t n, uint32_t ms) {
  if (ms == 0) { snprintf(dst, n, "nigdy"); return; }
  const uint32_t s = (millis() - ms) / 1000;
  if (s < 60) snprintf(dst, n, "%u s temu", (unsigned)s);
  else if (s < 3600) snprintf(dst, n, "%u min temu", (unsigned)(s / 60));
  else snprintf(dst, n, "%u h temu", (unsigned)(s / 3600));
}

static void buildDiagText() {
  const HostDiag h = hostDiag();
  const DisplayDiag d = displayDiag();
  const WeatherData w = weatherSnapshot();
  char a1[24], a2[24], a3[24], wAge[24];
  ago(a1, sizeof a1, h.lastRevOkMs);
  ago(a2, sizeof a2, h.lastDownloadOkMs);
  ago(a3, sizeof a3, h.lastCacheWriteMs);
  const uint32_t wa = weatherAgeMs();
  if (wa == 0xFFFFFFFFUL) snprintf(wAge, sizeof wAge, "brak pobrania");
  else snprintf(wAge, sizeof wAge, "%u min temu", (unsigned)(wa / 60000));
  const uint32_t up = millis() / 1000;
  char ip[20] = "-";
  if (wifiOk()) snprintf(ip, sizeof ip, "%s", WiFi.localIP().toString().c_str());
  snprintf(diagText, sizeof diagText,
           "DANE Z PANELU WWW (tylko odczyt)\n"
           "  wpisow: %u (%s), %u B\n"
           "  rewizja: lokalna %ld, zdalna %ld\n"
           "  sprawdzenie rewizji: %s\n"
           "  ostatnie pobranie: %s\n"
           "  nieudane pobrania z rzedu: %u\n"
           "  ostatni blad: %s\n"
           "  powiadomienia o drzemce (z WWW): %s\n"
           "CACHE NA FLASHU\n"
           "  zapisow: %u, ostatni: %s%s\n"
           "OBRAZ\n"
           "  detektor przesuniecia: %s\n"
           "  twarde resynchronizacje: %u (ostatni powod %u)\n"
           "  faza %ld us, wzorzec %ld us\n"
           "  operacje zapisu flasha: %u\n"
           "SIEC\n"
           "  Wi-Fi: %s, IP %s, %d dBm\n"
           "  czas NTP: %s\n"
           "  pogoda: %s (%s)\n"
           "URZADZENIE\n"
           "  wersja %s (%s)\n"
           "  RAM wewn.: %u KB (min %u KB)\n"
           "  PSRAM wolny: %u KB\n"
           "  stos zadania sieci: %u B\n"
           "  CPU: %d %%, praca %ud %uh %umin\n"
           "  uruchomien: %u, restartow watchdoga: %u\n"
           "  ostatni reset: %s",
           store ? (unsigned)store->n : 0u, h.fromCache ? "z cache" : (store ? "z WWW" : "brak"), (unsigned)h.csvBytes,
           h.revLocal, h.revRemote, a1, a2, (unsigned)h.downloadFails, hostErrorText(h.lastError),
           hostSleepTelegramEnabled() ? "wlaczone" : "wylaczone",
           (unsigned)h.cacheWrites, a3, h.cachePending ? " (czeka zmiana)" : "",
           d.phaseDetector ? "aktywny" : "niedostepny", (unsigned)d.resyncCount, (unsigned)d.lastReason,
           (long)d.phaseUs, (long)d.phaseRefUs, (unsigned)d.flashWrites,
           wifiOk() ? "polaczono" : "ROZLACZONO", ip, wifiOk() ? (int)WiFi.RSSI() : 0,
           timeOk() ? "OK" : "brak", w.valid ? "OK" : "brak", wAge,
           FW_VERSION, __DATE__,
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024), (unsigned)(diagMinFreeInternal() / 1024),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024), (unsigned)h.netStackFree,
           diagCpuPct(), (unsigned)(up / 86400), (unsigned)((up % 86400) / 3600), (unsigned)((up % 3600) / 60),
           (unsigned)diagBootCount(), (unsigned)diagWdtResets(), diagResetReasonText());
}

static void applyBacklight() {
  displaySetBacklight(dimmed ? BACKLIGHT_DIM_DUTY : (night ? BACKLIGHT_NIGHT_DUTY : BACKLIGHT_FULL_DUTY));
}

static void showScreen(UiScreen s) {
  computeModel(true);
  chartsHash = statsChartsHash(model);
  if (s == UiScreen::Saver) uiShowSaver(model);
  else if (s == UiScreen::Dash) uiShowDash(model);
  else { buildDiagText(); uiShowDiag(model, diagText); }
  lastLiveMs = lastChartsMs = millis();
  chartsDue = false;
}

static bool swallowTouch() { return uiScreen() == UiScreen::Saver; }
static void onSaverTap() { uiRequest(UI_REQ_TO_DASH); }

// ------------------------------------------------------------------ start
static void bootProgress(int pct) {
  uiBootSetProgress(pct);
  bootPump(1);
}

void appSetup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Lesny Dziennik v" FW_VERSION " — tylko wyswietlacz.");
  diagInit();
  tzInit();
  Wire.begin(47, 48);
  Wire.setClock(400000);   // szybszy odczyt dotyku
  Wire.setTimeOut(100);
  if (ESP.getPsramSize() == 0) {
    Serial.println("BLAD: brak PSRAM — ustaw Narzedzia > PSRAM > OPI PSRAM.");
    for (;;) delay(1000);
  }
  if (!displayInit()) {
    Serial.println("BLAD: panel RGB nie wystartowal.");
    for (;;) delay(1000);
  }
  displayBacklightInit();
  touchInit();
  touchSetSwallow(swallowTouch, onSaverTap);
  lastLvTick = millis();

  uiBootBegin();
  bootPump(300);

  // 1. Wi-Fi
  uiBootSetStep(0, 0);
  bootPump(50);
  wifiStart();
  const uint32_t w0 = millis();
  while (!wifiOk() && millis() - w0 < WIFI_CONNECT_TIMEOUT_MS) bootPump(100);
  uiBootSetStep(0, wifiOk() ? 1 : 2);
  displayRequestResync(RESYNC_BOOT);   // inicjalizacja Wi-Fi moze dotknac flasha (kalibracja PHY)
  displayResyncAfterVsync();

  // 2. Zegar
  uiBootSetStep(1, 0);
  if (wifiOk()) {
    ntpStart();
    uiBootSetText("Synchronizacja zegara…");
    const uint32_t t0 = millis();
    while (!timeOk() && millis() - t0 < 12000) bootPump(100);
  }
  uiBootSetStep(1, timeOk() ? 1 : 2);

  // 3. Pamiec (cache ostatnich danych)
  uiBootSetStep(2, 0);
  bootPump(50);
  hostCacheMount();
  store = hostLoadCache();
  displayResyncAfterVsync();
  uiBootSetStep(2, store ? 1 : 2);

  // 4. Pogoda (z RAM RTC; pobranie w tle)
  uiBootSetStep(3, 0);
  weatherInit();
  uiBootSetStep(3, weatherSnapshot().valid ? 1 : 2);

  // 5. Dane z panelu WWW
  uiBootSetStep(4, 0);
  uiBootSetText("Odbieranie danych z WWW…");
  uiBootSetProgress(0);
  bootPump(50);
  Store *fresh = wifiOk() ? hostSyncBlocking(HOST_BOOT_SYNC_MS, bootProgress) : nullptr;
  if (fresh) { storeFree(store); store = fresh; }
  uiBootSetStep(4, fresh ? 1 : 2);
  uiBootSetProgress(100);
  if (fresh && timeOk()) uiBootSetText("Gotowe!");
  else if (!fresh && store) uiBootSetText("Brak połączenia z WWW – dane z pamięci");
  else if (!fresh) uiBootSetText("Brak danych WWW – start mimo to");
  else uiBootSetText("Brak czasu – start mimo to");
  bootPump(900);
  displayRequestResync(RESYNC_BOOT);   // start zawsze konczymy czystym strumieniem obrazu
  displayResyncAfterVsync();

  night = nightNow();
  applyBacklight();
  showScreen(UiScreen::Saver);
  uiBootEnd();

  if (telegramConfigured()) {
    char msg[400];
    const WeatherData w = weatherSnapshot();
    char when[24] = "brak NTP";
    if (timeOk()) {
      const time_t n = time(nullptr);
      struct tm t;
      localtime_r(&n, &t);
      strftime(when, sizeof when, "%d.%m.%Y %H:%M", &t);
    }
    snprintf(msg, sizeof msg,
             "Leśny Dziennik v" FW_VERSION " uruchomiony\nCzas: %s\nIP: %s\nRAM: %u KB, PSRAM: %u KB\nDane: %u wpisów (%s)\nPogoda: %s\nReset: %s",
             when, wifiOk() ? WiFi.localIP().toString().c_str() : "brak Wi-Fi",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(ESP.getFreePsram() / 1024),
             store ? (unsigned)store->n : 0u, fresh ? "WWW" : (store ? "cache" : "brak"),
             w.valid ? "OK" : "brak", diagResetReasonText());
    telegramQueue(msg);
  }
  diagWatchdogStart();   // przed startem zadania sieciowego — ono tez sie subskrybuje
  hostTaskStart();
}

// ------------------------------------------------------------------ petla
void appLoop() {
  const uint32_t t0 = micros();
  diagFeedWatchdog();
  uint32_t work = renderStep();
  const uint32_t a = micros();
  const uint32_t now = millis();

  // Nowe dane z hostingu (zadanie sieciowe) — przejmujemy wlasnosc.
  if (Store *s = hostTakeNewStore()) {
    storeFree(store);
    store = s;
    chartsDue = true;
    displayRequestResync(RESYNC_DATA);   // jak w v4.1: po nowych danych wyprostuj obraz jeszcze raz
  }

  // Zadania z UI (poza callbackami LVGL).
  const uint8_t req = uiTakeRequests();
  if (req & UI_REQ_TO_DIAG) showScreen(UiScreen::Diag);
  else if (req & UI_REQ_TO_SAVER) showScreen(UiScreen::Saver);
  else if (req & UI_REQ_TO_DASH) showScreen(UiScreen::Dash);

  // Motyw nocny (paleta + jasnosc) — przebudowa biezacego ekranu.
  static uint32_t lastNightCheck = 0;
  if (now - lastNightCheck > 30000) {
    lastNightCheck = now;
    const bool n = nightNow();
    if (n != night) {
      night = n;
      dimmed = false;
      applyBacklight();
      showScreen(uiScreen() == UiScreen::Boot ? UiScreen::Saver : uiScreen());
    }
  }

  // Przyciemnienie (tylko w dzien) i powroty po bezczynnosci.
  const uint32_t idle = now - touchLastActivityMs();
  const bool wantDim = !night && idle >= DIM_TIMEOUT_MS;
  if (wantDim != dimmed) { dimmed = wantDim; applyBacklight(); }
  if (uiScreen() == UiScreen::Dash && idle > SAVER_TIMEOUT_MS) showScreen(UiScreen::Saver);
  if (uiScreen() == UiScreen::Diag && idle > DIAG_TIMEOUT_MS) showScreen(UiScreen::Dash);

  // Model: czesc "na zywo" co 1 s, wykresy po zmianie danych i co 60 s.
  if (now - lastLiveMs >= 1000) {
    lastLiveMs = now;
    const bool charts = chartsDue || now - lastChartsMs >= 60000;
    computeModel(charts);
    bool changed = false;
    if (charts) {
      lastChartsMs = now;
      chartsDue = false;
      const uint32_t h = statsChartsHash(model);
      changed = h != chartsHash;
      chartsHash = h;
    }
    if (uiScreen() == UiScreen::Diag) buildDiagText();
    uiRefresh(model, changed, diagText);
    telegramCheckSleep(sleepState, hostSleepTelegramEnabled(), timeOk(), time(nullptr));
  }
  work += micros() - a;
  (void)t0;
  diagCpuAdd(work);
}
