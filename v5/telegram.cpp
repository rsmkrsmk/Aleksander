// telegram.cpp — Telegram Bot API (sendMessage, sendDocument) z zadania sieciowego.
#include "telegram.h"
#include "app_config.h"
#include "secrets.h"
#include "net.h"
#include "diag.h"
#include <Arduino.h>
#include <NetworkClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

static SemaphoreHandle_t mtx = nullptr;
static char pending[600];
static uint32_t nextAttemptMs = 0;
static uint8_t fails = 0;

bool telegramConfigured() { return strlen(TELEGRAM_BOT_TOKEN) > 0 && strlen(TELEGRAM_CHAT_ID) > 0; }

static void lock() {
  if (!mtx) mtx = xSemaphoreCreateMutex();
  xSemaphoreTake(mtx, portMAX_DELAY);
}
static void unlock() { xSemaphoreGive(mtx); }

void telegramQueue(const char *text) {
  if (!telegramConfigured() || !text || !*text) return;
  lock();
  strncpy(pending, text, sizeof(pending) - 1);
  pending[sizeof(pending) - 1] = 0;
  fails = 0;
  nextAttemptMs = 0;
  unlock();
}

bool telegramPending() {
  lock();
  const bool p = pending[0] != 0 && static_cast<int32_t>(millis() - nextAttemptMs) >= 0;
  unlock();
  return p;
}

static bool connectTg(NetworkClientSecure &c) {
#ifdef TELEGRAM_ROOT_CA
  c.setCACert(TELEGRAM_ROOT_CA);
#else
  c.setInsecure();
#endif
  c.setHandshakeTimeout(8);
  c.setTimeout(10000);
  diagFeedWatchdog();
  return c.connect("api.telegram.org", 443);
}

static size_t urlEncode(const char *in, char *out, size_t cap) {
  static const char hex[] = "0123456789ABCDEF";
  size_t n = 0;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(in); *p && n + 4 < cap; ++p) {
    const unsigned char ch = *p;
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      out[n++] = static_cast<char>(ch);
    } else {
      out[n++] = '%';
      out[n++] = hex[ch >> 4];
      out[n++] = hex[ch & 15];
    }
  }
  out[n] = 0;
  return n;
}

bool telegramSendPending() {
  char text[sizeof(pending)];
  lock();
  strcpy(text, pending);
  unlock();
  if (!text[0] || !wifiOk()) return false;
  static char body[sizeof(pending) * 3 + 64];
  int bl = snprintf(body, sizeof body, "chat_id=%s&text=", TELEGRAM_CHAT_ID);
  bl += static_cast<int>(urlEncode(text, body + bl, sizeof(body) - bl));
  NetworkClientSecure c;
  int status = 0;
  if (connectTg(c)) {
    char head[256];
    const int hl = snprintf(head, sizeof head,
                            "POST /bot%s/sendMessage HTTP/1.1\r\nHost: api.telegram.org\r\nUser-Agent: LesnyDziennik\r\n"
                            "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                            TELEGRAM_BOT_TOKEN, bl);
    HttpPiece pcs[2] = {{reinterpret_cast<const uint8_t *>(head), static_cast<size_t>(hl)},
                        {reinterpret_cast<const uint8_t *>(body), static_cast<size_t>(bl)}};
    status = httpExchange(c, pcs, 2, nullptr, 10000).status;
    c.stop();
  }
  netMarkTls();
  lock();
  if (status == 200) {
    if (strcmp(pending, text) == 0) pending[0] = 0;   // nie kasujemy nowszej wiadomosci
    fails = 0;
    Serial.println("Telegram: wiadomosc wyslana.");
  } else if (++fails >= 5) {
    Serial.println("Telegram: 5 nieudanych prob — porzucam wiadomosc.");
    if (strcmp(pending, text) == 0) pending[0] = 0;
    fails = 0;
    nextAttemptMs = millis() + 60000;
  } else {
    Serial.printf("Telegram: blad (HTTP %d), ponowka za 30 s.\n", status);
    nextAttemptMs = millis() + 30000;
  }
  unlock();
  return status == 200;
}

bool telegramSendDocument(const uint8_t *data, size_t len, const char *fileName) {
  if (!telegramConfigured() || !wifiOk() || !data || !len) return false;
  NetworkClientSecure c;
  if (!connectTg(c)) { netMarkTls(); return false; }
  char boundary[40];
  snprintf(boundary, sizeof boundary, "----lesny%lu", static_cast<unsigned long>(millis()));
  char pre[400];
  const int pl = snprintf(pre, sizeof pre,
                          "--%s\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n%s\r\n"
                          "--%s\r\nContent-Disposition: form-data; name=\"document\"; filename=\"%s\"\r\n"
                          "Content-Type: text/csv\r\n\r\n",
                          boundary, TELEGRAM_CHAT_ID, boundary, fileName);
  char post[64];
  const int ql = snprintf(post, sizeof post, "\r\n--%s--\r\n", boundary);
  char head[300];
  const int hl = snprintf(head, sizeof head,
                          "POST /bot%s/sendDocument HTTP/1.1\r\nHost: api.telegram.org\r\nUser-Agent: LesnyDziennik\r\n"
                          "Content-Type: multipart/form-data; boundary=%s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
                          TELEGRAM_BOT_TOKEN, boundary, static_cast<unsigned>(pl + len + ql));
  HttpPiece pcs[4] = {{reinterpret_cast<const uint8_t *>(head), static_cast<size_t>(hl)},
                      {reinterpret_cast<const uint8_t *>(pre), static_cast<size_t>(pl)},
                      {data, len},
                      {reinterpret_cast<const uint8_t *>(post), static_cast<size_t>(ql)}};
  const HttpResult r = httpExchange(c, pcs, 4, nullptr, 20000);
  c.stop();
  netMarkTls();
  Serial.printf("Telegram: kopia %s — %s (HTTP %d).\n", fileName, r.status == 200 ? "wyslana" : "blad", r.status);
  return r.status == 200;
}

void telegramCheckSleep(const SleepState &s, bool enabled, bool timeValid, time_t now) {
  static time_t anchor = 0;
  static bool sentWindow = false, sentOver = false;
  if (!enabled || !timeValid) return;
  if (s.sleeping || s.lastWake != anchor) {   // nowe okno czuwania lub sen: reset flag
    anchor = s.lastWake;
    sentWindow = sentOver = false;
  }
  if (s.sleeping || s.lastWake <= 0) return;
  // Okno, ktore zamknelo sie dawno (np. po restarcie) — nie powiadamiamy wstecz.
  if (now > s.napEnd + 30 * 60) { sentWindow = sentOver = true; return; }
  char a[8], b[8], msg[160];
  struct tm t;
  if (!sentOver && now > s.napEnd) {
    sentOver = sentWindow = true;
    telegramQueue(CHILD_NAME ": przekroczone okno czuwania — mozliwe przemeczenie. Warto uspic.");
  } else if (!sentWindow && now >= s.napStart) {
    sentWindow = true;
    localtime_r(&s.napStart, &t);
    snprintf(a, sizeof a, "%02d:%02d", t.tm_hour, t.tm_min);
    localtime_r(&s.napEnd, &t);
    snprintf(b, sizeof b, "%02d:%02d", t.tm_hour, t.tm_min);
    snprintf(msg, sizeof msg, CHILD_NAME ": okno drzemki (~%s-%s). Dobry moment na sen.", a, b);
    telegramQueue(msg);
  }
}
