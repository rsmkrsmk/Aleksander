// net.cpp — Wi-Fi, NTP i klient HTTP(S) bez Arduino String w sciezkach okresowych.
#include "net.h"
#include "app_config.h"
#include "secrets.h"
#include "diag.h"
#include <Arduino.h>
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <esp_heap_caps.h>
#include <string.h>

// ------------------------------------------------------------------ PsBuf
bool PsBuf::append(const uint8_t *src, size_t n) {
  if (limit && len + n > limit) { overflow = true; return false; }
  if (len + n + 1 > cap) {
    size_t nc = cap ? cap : 16 * 1024;
    while (nc < len + n + 1) nc *= 2;
    if (limit && nc > limit + 1) nc = limit + 1;
    uint8_t *g = static_cast<uint8_t *>(heap_caps_realloc(p, nc, MALLOC_CAP_SPIRAM));
    if (!g) g = static_cast<uint8_t *>(realloc(p, nc));
    if (!g) { overflow = true; return false; }
    p = g;
    cap = nc;
  }
  memcpy(p + len, src, n);
  len += n;
  p[len] = 0;   // zawsze zakonczone zerem (wygodne parsowanie tekstu)
  return true;
}
void PsBuf::release() {
  if (p) free(p);
  p = nullptr;
  len = cap = 0;
  overflow = false;
}
void PsBuf::swap(PsBuf &o) {
  PsBuf t = *this;
  *this = o;
  o = t;
}

// ------------------------------------------------------------------ Wi-Fi / czas
void wifiStart() {
  WiFi.persistent(false);   // bez zapisow NVS (zapis flasha = ryzyko przesuniecia obrazu)
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool wifiWaitConnected(uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

bool wifiOk() { return WiFi.status() == WL_CONNECTED; }

void wifiTick() {
  static uint32_t last = 0;
  static bool wasConnected = false;
  const bool ok = wifiOk();
  if (ok && !wasConnected) ntpStart();   // po odzyskaniu polaczenia: SNTP w tle (bez blokowania)
  wasConnected = ok;
  if (ok || millis() - last < WIFI_RETRY_MS) return;
  last = millis();
  Serial.println("Wi-Fi: ponawiam polaczenie.");
  if (!WiFi.reconnect()) {
    WiFi.disconnect();
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

void tzInit() {
  setenv("TZ", TIMEZONE_RULE, 1);
  tzset();
}

void ntpStart() {
  if (!wifiOk()) return;
  configTzTime(TIMEZONE_RULE, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
}

bool timeOk() {
  const time_t n = time(nullptr);
  struct tm t;
  localtime_r(&n, &t);
  return t.tm_year + 1900 >= 2025;
}

// ------------------------------------------------------------------ HTTP
static uint32_t lastTlsEnd = 0;
uint32_t netLastTlsEndMs() { return lastTlsEnd; }
void netMarkTls() { lastTlsEnd = millis(); }

static bool writeAll(Client &c, const uint8_t *d, size_t n, uint32_t timeoutMs) {
  size_t off = 0;
  uint32_t lastProgress = millis();
  while (off < n) {
    if (!c.connected()) return false;
    const size_t w = c.write(d + off, n - off);
    if (w == 0) {
      if (millis() - lastProgress > timeoutMs) return false;
      delay(5);
      continue;
    }
    off += w;
    lastProgress = millis();
    diagFeedWatchdog();
  }
  return true;
}

// Czyta jedna linie (do \n) z limitem czasu; bez \r. Zwraca false przy timeoucie.
static bool readLine(Client &c, char *dst, size_t cap, uint32_t timeoutMs) {
  size_t n = 0;
  uint32_t t0 = millis();
  for (;;) {
    if (c.available()) {
      const int ch = c.read();
      if (ch < 0) continue;
      if (ch == '\n') break;
      if (ch != '\r' && n + 1 < cap) dst[n++] = static_cast<char>(ch);
      t0 = millis();
    } else {
      if (!c.connected()) { dst[n] = 0; return n > 0; }
      if (millis() - t0 > timeoutMs) { dst[n] = 0; return false; }
      delay(2);
    }
  }
  dst[n] = 0;
  return true;
}

// Czyta dokladnie n bajtow ciala (albo do zamkniecia gdy n < 0).
static bool readBody(Client &c, long n, PsBuf *body, uint32_t timeoutMs, size_t &received, long total, HttpProgress pr) {
  uint8_t chunk[1024];
  uint32_t t0 = millis();
  long left = n;
  while (n < 0 || left > 0) {
    const int avail = c.available();
    if (avail > 0) {
      size_t want = sizeof(chunk);
      if (n >= 0 && static_cast<long>(want) > left) want = static_cast<size_t>(left);
      const int r = c.read(chunk, want);
      if (r <= 0) continue;
      if (body) body->append(chunk, static_cast<size_t>(r));
      received += static_cast<size_t>(r);
      if (n >= 0) left -= r;
      t0 = millis();
      if (pr) pr(received, total);
      diagFeedWatchdog();
    } else {
      if (!c.connected()) {
        delay(50);                       // dane moga jeszcze byc w buforze
        if (!c.available()) return n < 0;
        continue;
      }
      if (millis() - t0 > timeoutMs) return false;
      delay(3);
    }
  }
  return true;
}

HttpResult httpExchange(Client &c, const HttpPiece *pieces, int nPieces, PsBuf *body, uint32_t timeoutMs, HttpProgress pr) {
  HttpResult res;
  for (int i = 0; i < nPieces; ++i) {
    if (!writeAll(c, pieces[i].data, pieces[i].len, timeoutMs)) return res;
  }
  char line[256];
  if (!readLine(c, line, sizeof line, timeoutMs)) return res;
  if (strncmp(line, "HTTP/1.", 7) != 0 || strlen(line) < 12) return res;
  res.status = atoi(line + 9);
  bool chunked = false;
  for (;;) {
    if (!readLine(c, line, sizeof line, timeoutMs)) return res;
    if (line[0] == 0) break;   // koniec naglowkow
    if (strncasecmp(line, "Content-Length:", 15) == 0) res.contentLength = atol(line + 15);
    else if (strncasecmp(line, "Transfer-Encoding:", 18) == 0 && strstr(line + 18, "chunked")) chunked = true;
  }
  size_t received = 0;
  if (chunked) {
    for (;;) {
      if (!readLine(c, line, sizeof line, timeoutMs)) return res;
      const long sz = strtol(line, nullptr, 16);
      if (sz <= 0) { res.complete = true; break; }
      if (!readBody(c, sz, body, timeoutMs, received, -1, pr)) return res;
      if (!readLine(c, line, sizeof line, timeoutMs)) return res;   // \r\n po danych chunku
    }
  } else if (res.contentLength >= 0) {
    res.complete = readBody(c, res.contentLength, body, timeoutMs, received, res.contentLength, pr);
  } else {
    res.complete = readBody(c, -1, body, timeoutMs, received, -1, pr);
  }
  if (body && body->overflow) res.complete = false;
  return res;
}

int hostGet(const char *path, PsBuf &body, HttpProgress pr, bool *complete) {
  if (complete) *complete = false;
  if (!wifiOk()) return 0;
  NetworkClientSecure c;
#ifdef HOST_ROOT_CA
  c.setCACert(HOST_ROOT_CA);
#else
  c.setInsecure();
#endif
  c.setHandshakeTimeout(8);
  c.setTimeout(10000);   // ms (Stream); handshake w sekundach
  diagFeedWatchdog();
  if (!c.connect(HOST_NAME, HOST_PORT)) {
    netMarkTls();
    return 0;
  }
  char req[256];
  const int n = snprintf(req, sizeof req,
                         "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: LesnyDziennik/" FW_VERSION
                         "\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
                         path, HOST_NAME);
  HttpPiece pc = {reinterpret_cast<const uint8_t *>(req), static_cast<size_t>(n)};
  body.clear();
  const HttpResult r = httpExchange(c, &pc, 1, &body, 15000, pr);
  c.stop();
  netMarkTls();
  if (complete) *complete = r.complete;
  return r.status;
}
