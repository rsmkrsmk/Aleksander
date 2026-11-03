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
#include "hw_touch.h"
#include <time.h>
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
static uint32_t lastRevChangeMs = 0;          // C1: kiedy ostatnio zmienily sie dane
static bool plainOk = true;                   // C1: rewizja przez zwykle HTTP dziala
static uint32_t plainRetryMs = 0;             // po bledzie HTTP: HTTPS, ponowna proba HTTP po 6 h
static uint32_t revPlain = 0, revTls = 0;     // licznik zapytan (diagnostyka)
static bool deviceApi = true;                 // C2: hosting ma /api/device (po 404 - /api/status)
static int lastErr = 0;
static bool fromCache = false;
static bool syncedThisBoot = false;
static TaskHandle_t netTask = nullptr;
static portMUX_TYPE camMux = portMUX_INITIALIZER_UNLOCKED;
static char camLine[96] = "";
static uint8_t camRating = 0;
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
    FlashWriteGuard g;   // zapis jednym zrywem z PSRAM przy obnizonym zegarze pikseli
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

static long parseRev(const PsBuf &b) {
  if (!b.len) return -1;
  const char *k = strstr(reinterpret_cast<const char *>(b.p), "\"rev\"");
  if (!k) return -1;
  k = strchr(k, ':');
  return k ? strtol(k + 1, nullptr, 10) : -1;
}

static long fetchRevision() {
  PsBuf b;
  b.limit = 2048;
  long rev = -1;
  int st = 0;
  // C1: najpierw zwykle HTTP (bez uscisku TLS); gdy hosting go nie obsluguje (przekierowanie na HTTPS,
  // blad) - HTTPS jak dawniej, a HTTP sprobujemy znow za 6 h.
  if (!plainOk && millis() - plainRetryMs > 6UL * 3600UL * 1000UL) plainOk = true;
  if (plainOk) {
    st = hostGetPlain(HOST_PATH_REVISION, b);
    ++revPlain;
    if (st == 200) rev = parseRev(b);
    if (rev < 0) {
      plainOk = false; plainRetryMs = millis();
      Serial.printf("Host: rewizja przez HTTP niedostepna (HTTP %d) - uzywam HTTPS.\n", st);
    }
  }
  if (rev < 0) {
    st = hostGet(HOST_PATH_REVISION, b);
    ++revTls;
    if (st == 200) rev = parseRev(b);
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

// --- proste odczyty z fragmentu JSON (obiekt camNight jest maly i plaski) ---
static bool jsonNum(const char *b, const char *e, const char *key, float *out) {
  char k[32]; snprintf(k, sizeof k, "\"%s\":", key);
  const char *p = strstr(b, k);
  if (!p || p >= e) return false;
  p += strlen(k);
  if (strncmp(p, "null", 4) == 0) return false;
  *out = strtof(p, nullptr);
  return true;
}
static bool jsonStr(const char *b, const char *e, const char *key, char *out, size_t n) {
  char k[32]; snprintf(k, sizeof k, "\"%s\":\"", key);
  const char *p = strstr(b, k);
  if (!p || p >= e) return false;
  p += strlen(k);
  size_t i = 0;
  while (p < e && *p != '"' && i + 1 < n) out[i++] = *p++;
  out[i] = 0;
  return true;
}

// "camNight":{...} -> "Noc 1.11: zasnął po 9 min · spokój 87% · blok 1 h 03" (data wieczoru)
static void parseCamNight(const char *json) {
  char line[96] = ""; uint8_t rating = 0;
  const char *b = strstr(json, "\"camNight\":{");
  if (b) {
    b += 11;
    const char *e = strchr(b, '}');
    if (e) {
      char night[12] = "", st[16] = "", rt[16] = "";
      float settle = -1, quiet = -1, block = -1;
      jsonStr(b, e, "night", night, sizeof night);
      jsonStr(b, e, "settleStatus", st, sizeof st);
      jsonStr(b, e, "rating", rt, sizeof rt);
      jsonNum(b, e, "settleMin", &settle);
      jsonNum(b, e, "quietPct", &quiet);
      jsonNum(b, e, "longestBlock", &block);
      rating = !strcmp(rt, "spokojna") ? 1 : !strcmp(rt, "umiarkowana") ? 2 : !strcmp(rt, "niespokojna") ? 3 : 4;
      int d = 0, mo = 0, y = 0;
      if (sscanf(night, "%d-%d-%d", &y, &mo, &d) == 3) {
        char s1[40];
        if (!strcmp(st, "ok") && settle >= 0) snprintf(s1, sizeof s1, "zasnął po %d min", (int)settle);
        else if (!strcmp(st, "aktywny")) snprintf(s1, sizeof s1, "sen aktywny");
        else snprintf(s1, sizeof s1, "zasypianie: brak");
        const int bl = block > 0 ? (int)block : 0;
        snprintf(line, sizeof line, "Noc %d.%02d: %s · spokój %d%% · blok %d h %02d",
                 d, mo, s1, quiet >= 0 ? (int)(quiet + 0.5f) : 0, bl / 60, bl % 60);
      }
    }
  }
  portENTER_CRITICAL(&camMux);
  strncpy(camLine, line, sizeof camLine - 1); camLine[sizeof camLine - 1] = 0;
  camRating = line[0] ? rating : 0;
  portEXIT_CRITICAL(&camMux);
}

// "camDev":{...} -> UiCam (pasek nocy, kafelki, 7 nocy). Obiekt plaski, teksty bez klamer.
static UiCam camDev;
static void parseCamDev(const char *json) {
  static UiCam c;
  memset(&c, 0, sizeof c);
  const char *b = strstr(json, "\"camDev\":{");
  const char *e = b ? strchr(b + 10, '}') : nullptr;
  if (b && e) {
    b += 9;
    char night[12] = "", st[16] = "", wk[160] = "";
    float v;
    jsonStr(b, e, "night", night, sizeof night);
    int y = 0, mo = 0, d = 0;
    if (sscanf(night, "%d-%d-%d", &y, &mo, &d) == 3) {
      c.ok = true;
      snprintf(c.night, sizeof c.night, "%d.%02d", d, mo);
      c.rating = jsonNum(b, e, "rating", &v) ? (uint8_t)v : 4;
      jsonStr(b, e, "settleStatus", st, sizeof st);
      c.settleMin = jsonNum(b, e, "settleMin", &v) ? (int)v : -1;
      c.status = (!strcmp(st, "ok") && c.settleMin >= 0) ? 0 : !strcmp(st, "aktywny") ? 1 : 2;
      jsonStr(b, e, "placed", c.placed, sizeof c.placed);
      jsonStr(b, e, "settled", c.settled, sizeof c.settled);
      jsonStr(b, e, "sleepFrom", c.sleepFrom, sizeof c.sleepFrom);
      jsonStr(b, e, "sleepTo", c.sleepTo, sizeof c.sleepTo);
      c.quietPct = jsonNum(b, e, "quietPct", &v) ? (int)v : 0;
      c.longestBlock = jsonNum(b, e, "longestBlock", &v) ? (int)v : 0;
      c.wakeCount = jsonNum(b, e, "wakeCount", &v) ? (int)v : 0;
      c.cryCount = jsonNum(b, e, "cryCount", &v) ? (int)v : -1;
      jsonStr(b, e, "strip", c.strip, sizeof c.strip);
      jsonStr(b, e, "cry", c.cry, sizeof c.cry);
      jsonStr(b, e, "cmp", c.cmp, sizeof c.cmp);
      jsonStr(b, e, "wk", wk, sizeof wk);
      char *sv = nullptr;
      for (char *t = strtok_r(wk, ";", &sv); t && c.wkN < UI_CAM_WK; t = strtok_r(nullptr, ";", &sv)) {
        int s = 0, q = 0, r = 0; char lb[8] = "";
        if (sscanf(t, "%7[^:]:%d:%d:%d", lb, &s, &q, &r) != 4) continue;
        snprintf(c.wkLabel[c.wkN], sizeof c.wkLabel[0], "%s", lb);
        c.wkSettle[c.wkN] = s; c.wkQuiet10[c.wkN] = q; c.wkRating[c.wkN] = (uint8_t)r;
        ++c.wkN;
      }
    }
  }
  portENTER_CRITICAL(&camMux);
  memcpy(&camDev, &c, sizeof c);
  portEXIT_CRITICAL(&camMux);
}

void hostCamDev(UiCam &out) {
  portENTER_CRITICAL(&camMux);
  memcpy(&out, &camDev, sizeof out);
  portEXIT_CRITICAL(&camMux);
}

bool hostCamNight(char *line, size_t n, uint8_t *rating) {
  portENTER_CRITICAL(&camMux);
  strncpy(line, camLine, n - 1); line[n - 1] = 0;
  if (rating) *rating = camRating;
  portEXIT_CRITICAL(&camMux);
  return line[0] != 0;
}

static void fetchStatusSettings() {
  PsBuf b;
  b.limit = 64 * 1024;
  // C2: lekkie /api/device (te same klucze); starszy hosting bez niego - pelne /api/status
  int st = hostGet(deviceApi ? HOST_PATH_DEVICE : HOST_PATH_STATUS, b);
  if (deviceApi && st == 404) {
    deviceApi = false;
    Serial.println("Host: brak /api/device na hostingu - uzywam /api/status.");
    delay(TLS_GAP_MS);
    st = hostGet(HOST_PATH_STATUS, b);
  }
  if (st == 200 && b.len) {
    const char *k = strstr(reinterpret_cast<const char *>(b.p), "\"sleepTelegram\"");
    if (k) {
      k = strchr(k, ':');
      if (k) {
        while (*++k == ' ') {}
        sleepTg = strncmp(k, "true", 4) == 0 || *k == '1';
      }
    }
    parseCamNight(reinterpret_cast<const char *>(b.p));
    parseCamDev(reinterpret_cast<const char *>(b.p));
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

// C1: 10 s gdy ktos korzysta z ekranu albo dane niedawno sie zmienily; inaczej 30 s (dzien) / 60 s (noc).
static uint32_t pollInterval() {
  const uint32_t now = millis();
  if (now - touchLastActivityMs() < 5UL * 60UL * 1000UL) return HOST_POLL_MS;
  if (lastRevChangeMs && now - lastRevChangeMs < 10UL * 60UL * 1000UL) return HOST_POLL_MS;
  const time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  const bool night = timeOk() && (lt.tm_hour >= NIGHT_START_HOUR || lt.tm_hour < NIGHT_END_HOUR);
  return night ? HOST_POLL_NIGHT_MS : HOST_POLL_IDLE_MS;
}

static void netLoop(void *) {
  diagWatchdogAddCurrentTask();
  for (;;) {
    diagFeedWatchdog();
    wifiTick();
    if (wifiOk()) {
      if (tlsGapOk() && telegramPending()) telegramSendPending();
      if (tlsGapOk() && millis() - lastPoll >= pollInterval()) {
        lastPoll = millis();
        const long rev = fetchRevision();
        if (rev >= 0 && rev != revLocal) {
          lastRevChangeMs = millis();
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
  d.revPlain = revPlain;
  d.revTls = revTls;
  d.plainOk = plainOk;
  d.deviceApi = deviceApi;
  d.pollMs = pollInterval();
  return d;
}
