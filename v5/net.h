// =============================================================================
//  net.h — Wi-Fi, czas (NTP) i wspolny klient HTTP(S) z buforem w PSRAM.
// =============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <Client.h>

// Rosnacy bufor w PSRAM (odpowiedzi HTTP, CSV z hostingu).
struct PsBuf {
  uint8_t *p = nullptr;
  size_t len = 0, cap = 0, limit = 0;   // limit = maks. rozmiar (0 = bez limitu)
  bool overflow = false;
  bool append(const uint8_t *src, size_t n);
  void clear() { len = 0; overflow = false; }
  void release();
  void swap(PsBuf &o);
};

void wifiStart();                    // tryb STA, bez zapisu do NVS
bool wifiWaitConnected(uint32_t timeoutMs);
bool wifiOk();
void wifiTick();                     // ponawianie polaczenia co WIFI_RETRY_MS
void ntpStart();                     // configTzTime (nieblokujace)
bool timeOk();                       // rok >= 2025
void tzInit();                       // strefa czasowa dla mktime/localtime (przed parsowaniem cache)

// Jedna wymiana HTTP/1.1 na juz polaczonym kliencie: wysyla kawalki zapytania (z kontrola
// zapisu), odbiera status, naglowki i cialo (Content-Length / chunked / do zamkniecia).
struct HttpPiece { const uint8_t *data; size_t len; };
struct HttpResult {
  int status = 0;
  bool complete = false;     // cialo odebrane w calosci
  long contentLength = -1;
};
using HttpProgress = void (*)(size_t received, long total);
HttpResult httpExchange(Client &c, const HttpPiece *pieces, int nPieces, PsBuf *body, uint32_t timeoutMs,
                        HttpProgress progress = nullptr);

// GET https://HOST_NAME<path> do bufora. Zwraca status HTTP (0 = blad sieci/TLS).
int hostGet(const char *path, PsBuf &body, HttpProgress progress = nullptr, bool *complete = nullptr);

uint32_t netLastTlsEndMs();          // koniec ostatniej sesji TLS (odstep miedzy sesjami)
void netMarkTls();
