// host_sync.cpp — odczyt danych z hostingu + zadanie sieciowe (rdzen 0).
// Urzadzenie NIC nie zapisuje na hosting. Jedyny zapis flasha to cache ostatnich danych:
// tylko gdy tresc sie zmienila i najwyzej raz na CACHE_MIN_INTERVAL_MS (decyzja D2).
#include "host_sync.h"
#include "app_config.h"
#include "net.h"
#include "diag.h"
#include "weather.h"
#include "telegram.h"
#include "hw_display.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static PsBuf csv;                  // ostatnie POPRAWNE dane (kopia na Telegram i do cache)
static PsBuf dl;                   // bufor pobierania
static Store *volatile pendingStore = nullptr;
static bool fsReady = false;
static uint32_t cacheCrc = 0;      // CRC tresci zapisanej w cache (0 = brak)
static bool cacheDirty = false;
static uint32_t cacheWrites = 0, lastCacheWriteMs = 0;
static volatile bool sleepTg = false;
static long revLocal = -1, revRemote = -1;
static uint32_t lastRevOk = 0, lastDlOk = 0, dlFails = 0, lastPoll = 0, lastStatus = 0;
static int lastErr = 0;
static bool fromCache = false;
static bool syncedThisBoot = false;
static TaskHandle_t netTask = nullptr;
static HostProgressFn progressFn = nullptr;

const char *hostErrorText(int e) {
  switch (e) {
    case 0: return "brak";
    case 1: return "brak Wi-Fi";
    case 2: return "siec/TLS";
    case 3: return "HTTP";
    case 4: return "niepoprawne dane";
    default: return "?";
  }
}

static void publish(Store *s) {
  Store *old = __atomic_exchange_n(const_cast<Store **>(&pendingStore), s, __ATOMIC_SEQ_CST);
  if (old) storeFree(old);   // petla UI nie zdazyla odebrac — starszy niepotrzebny
}

Store *hostTakeNewStore() { return __atomic_exchange_n(const_cast<Store **>(&pendingStore), static_cast<Store *>(nullptr), __ATOMIC_SEQ_CST); }
bool hostSleepTelegramEnabled() { return sleepTg; }

// ------------------------------------------------------------------ cache (flash)
bool hostCacheMount() {
  FlashWriteGuard g;   // montaz (i ewentualny format) moze pisac do flasha
  fsReady = LittleFS.begin(true);
  Serial.printf("Cache: LittleFS %s\n", fsReady ? "OK" : "BLAD");
  return fsReady;
}

Store *hostLoadCache() {
  if (!fsReady) return nullptr;
  File f = LittleFS.open(CACHE_FILE, FILE_READ);
  if (!f) return nullptr;
  const size_t sz = f.size();
  csv.limit = HOST_CSV_MAX_BYTES;
  csv.clear();
  uint8_t tmp[1024];
  while (f.available()) {
    const size_t n = f.read(tmp, sizeof tmp);
    if (n == 0 || !csv.append(tmp, n)) break;
  }
  f.close();
  if (csv.len != sz || !csvLooksValid(csv.p, csv.len)) {
    csv.clear();
    Serial.println("Cache: plik uszkodzony — pomijam.");
    return nullptr;
  }
  Store *s = storeParse(csv.p, csv.len);
  if (s) {
    cacheCrc = s->crc;
    fromCache = true;
    Serial.printf("Cache: %u wpisow (%u B).\n", (unsigned)s->n, (unsigned)csv.len);
  }
  return s;
}

static void cacheWriteIfDue(bool force) {
  if (!fsReady || !cacheDirty || !csv.len) return;
  if (!force && lastCacheWriteMs != 0 && millis() - lastCacheWriteMs < CACHE_MIN_INTERVAL_MS) return;
  bool ok = false;
  {
    FlashWriteGuard g;   // zapis jednym zrywem z PSRAM, obnizony zegar pikseli, potem resynchronizacja
    File f = LittleFS.open("/dane.tmp", FILE_WRITE);
    if (f) {
      ok = f.write(csv.p, csv.len) == csv.len;
      f.close();
    }
    if (ok) ok = LittleFS.rename("/dane.tmp", CACHE_FILE);
    if (!ok) LittleFS.remove("/dane.tmp");
  }
  lastCacheWriteMs = millis();
  if (ok) {
    cacheDirty = false;
    cacheCrc = crc32Of(csv.p, csv.len);
    ++cacheWrites;
  }
  Serial.printf("Cache: zapis %s (%u B).\n", ok ? "OK" : "nieudany", (unsigned)csv.len);
}

// ------------------------------------------------------------------ hosting
static void bootProgress(size_t got, long total) {
  if (!progressFn) return;
  static uint32_t last = 0;
  if (millis() - last < 150) return;
  last = millis();
  progressFn(total > 0 ? static_cast<int>(got * 99 / total) : static_cast<int>((millis() / 60) % 100));
}

static long fetchRevision() {
  PsBuf b;
  b.limit = 2048;
  const int st = hostGet(HOST_PATH_REVISION, b);
  long rev = -1;
  if (st == 200 && b.len) {
    const char *k = strstr(reinterpret_cast<const char *>(b.p), "\"rev\"");
    if (k) {
      k = strchr(k, ':');
      if (k) rev = strtol(k + 1, nullptr, 10);
    }
  }
  b.release();
  if (rev >= 0) {
    revRemote = rev;
    lastRevOk = millis();
    lastErr = 0;
  } else {
    lastErr = st == 0 ? (wifiOk() ? 2 : 1) : (st == 200 ? 4 : 3);
    Serial.printf("Host: rewizja — blad (HTTP %d).\n", st);
  }
  return rev;
}

// Pobiera CSV; przy sukcesie podmienia `csv` i zwraca nowy Store (nullptr = blad).
static Store *downloadCsv() {
  dl.limit = HOST_CSV_MAX_BYTES;
  bool complete = false;
  const int st = hostGet(HOST_PATH_CSV, dl, bootProgress, &complete);
  if (st != 200 || !complete || !csvLooksValid(dl.p, dl.len)) {
    ++dlFails;
    lastErr = st == 0 ? 2 : (st == 200 ? 4 : 3);
    Serial.printf("Host: CSV odrzucony (HTTP %d, %u B, komplet %d).\n", st, (unsigned)dl.len, complete);
    return nullptr;
  }
  Store *s = storeParse(dl.p, dl.len);
  if (!s) { ++dlFails; lastErr = 4; return nullptr; }
  csv.swap(dl);
  dl.clear();
  dlFails = 0;
  lastErr = 0;
  lastDlOk = millis();
  fromCache = false;
  syncedThisBoot = true;
  if (s->crc != cacheCrc) cacheDirty = true;
  Serial.printf("Host: CSV pobrany (%u B, %u wpisow, %u bledow).\n", (unsigned)csv.len, (unsigned)s->n, (unsigned)s->badLines);
  return s;
}

static void fetchStatusSettings() {
  PsBuf b;
  b.limit = 64 * 1024;
  const int st = hostGet(HOST_PATH_STATUS, b);
  if (st == 200 && b.len) {
    const char *k = strstr(reinterpret_cast<const char *>(b.p), "\"sleepTelegram\"");
    if (k) {
      k = strchr(k, ':');
      if (k) {
        while (*++k == ' ') {}
        sleepTg = strncmp(k, "true", 4) == 0 || *k == '1';
      }
    }
  }
  b.release();
  lastStatus = millis();
}

Store *hostSyncBlocking(uint32_t timeoutMs, HostProgressFn progress) {
  progressFn = progress;
  const uint32_t t0 = millis();
  Store *got = nullptr;
  while (!got && millis() - t0 < timeoutMs && wifiOk()) {
    const long rev = fetchRevision();
    if (rev >= 0) {
      got = downloadCsv();
      if (got) revLocal = rev;
    }
    if (!got) { diagFeedWatchdog(); delay(1000); }
  }
  progressFn = nullptr;
  lastPoll = millis();
  if (got) fetchStatusSettings();
  if (got) cacheWriteIfDue(true);   // pierwszy zapis od razu (o ile tresc sie zmienila)
  return got;
}

// ------------------------------------------------------------------ kopia na Telegram
static void backupIfDue() {
  if (!telegramConfigured() || !syncedThisBoot || !csv.len || !timeOk()) return;
  const time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  const int32_t day = daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  if (day == diagLastBackupDay()) return;
  static uint32_t lastTry = 0;
  if (lastTry && millis() - lastTry < 10UL * 60UL * 1000UL) return;   // ponowka co 10 min
  lastTry = millis();
  char name[40];
  snprintf(name, sizeof name, "karmienia_%04d-%02d-%02d.csv", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  if (telegramSendDocument(csv.p, csv.len, name)) diagSetLastBackupDay(day);
}

// ------------------------------------------------------------------ zadanie sieciowe
static bool tlsGapOk() { return millis() - netLastTlsEndMs() >= TLS_GAP_MS; }

static void netLoop(void *) {
  diagWatchdogAddCurrentTask();
  for (;;) {
    diagFeedWatchdog();
    wifiTick();
    if (wifiOk()) {
      if (tlsGapOk() && telegramPending()) telegramSendPending();
      if (tlsGapOk() && millis() - lastPoll >= HOST_POLL_MS) {
        lastPoll = millis();
        const long rev = fetchRevision();
        if (rev >= 0 && rev != revLocal) {
          delay(TLS_GAP_MS);
          Store *s = downloadCsv();
          if (s) { revLocal = rev; publish(s); }
        }
      }
      if (tlsGapOk() && (lastStatus == 0 || millis() - lastStatus >= HOST_STATUS_REFRESH_MS)) fetchStatusSettings();
      if (weatherDue()) weatherFetch();
      if (tlsGapOk()) backupIfDue();
    }
    cacheWriteIfDue(false);
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void hostTaskStart() {
  if (xTaskCreatePinnedToCore(netLoop, "net", 12288, nullptr, 1, &netTask, 0) != pdPASS) {
    Serial.println("Host: nie mozna uruchomic zadania sieciowego!");
    netTask = nullptr;
  }
}

HostDiag hostDiag() {
  HostDiag d;
  d.revLocal = revLocal;
  d.revRemote = revRemote;
  d.lastRevOkMs = lastRevOk;
  d.lastDownloadOkMs = lastDlOk;
  d.downloadFails = dlFails;
  d.lastError = lastErr;
  d.csvBytes = csv.len;
  d.fromCache = fromCache;
  d.cacheWrites = cacheWrites;
  d.lastCacheWriteMs = lastCacheWriteMs;
  d.cachePending = cacheDirty;
  d.netStackFree = netTask ? uxTaskGetStackHighWaterMark(netTask) : 0;
  return d;
}
