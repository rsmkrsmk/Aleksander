// =============================================================================
//  host_sync.h — dane z hostingu (TYLKO odczyt) + zadanie sieciowe na rdzeniu 0
//  (polling rewizji, pobranie CSV, pogoda, Telegram, rzadki zapis cache na flash).
// =============================================================================
#pragma once
#include <stdint.h>
#include "store.h"
#include "ui_model.h"

// --- start (wolane z setup(), przed hostTaskStart) ---------------------------
bool hostCacheMount();                       // LittleFS (mala partycja na cache)
Store *hostLoadCache();                      // ostatnie zapisane dane (lub nullptr)
using HostProgressFn = void (*)(int pct);    // 0..100 (pasek na ekranie startowym)
Store *hostSyncBlocking(uint32_t timeoutMs, HostProgressFn progress);   // pierwsze pobranie
void hostTaskStart();                        // zadanie sieciowe (rdzen 0)

// --- petla UI ------------------------------------------------------------------
Store *hostTakeNewStore();                   // nowe dane (wlasnosc przechodzi na wolajacego) lub nullptr
bool hostSleepTelegramEnabled();             // ustawienie z panelu WWW (/api/status)
// Ostatnia noc z kamery (obiekt "camNight" w /api/status): gotowa linia tekstu na karte Sen
// i ocena (0 brak, 1 spokojna, 2 umiarkowana, 3 niespokojna, 4 malo danych). false = brak danych.
bool hostCamNight(char *line, size_t n, uint8_t *rating);
// Pelny skrot nocy do widoku "Noc z kamery" (obiekt "camDev"); out.ok = false gdy brak.
void hostCamDev(UiCam &out);

struct HostDiag {
  long revLocal, revRemote;
  uint32_t lastRevOkMs, lastDownloadOkMs;    // millis() (0 = nigdy)
  uint32_t downloadFails;
  int lastError;                             // 0 ok, 1 brak Wi-Fi, 2 TLS/siec, 3 HTTP, 4 dane
  uint32_t csvBytes;
  bool fromCache;                            // dane pochodza z cache (brak udanego pobrania)
  uint32_t cacheWrites;
  uint32_t lastCacheWriteMs;
  bool cachePending;
  uint32_t netStackFree;                     // zapas stosu zadania sieciowego (B)
};
HostDiag hostDiag();
const char *hostErrorText(int e);
