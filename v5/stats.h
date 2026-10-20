// =============================================================================
//  stats.h — reguly domenowe: dane (Store) + czas -> UiModel
//  Czysty C++ (bez Arduino/LVGL) — ten sam kod dziala w symulatorze na PC.
// =============================================================================
#pragma once
#include <time.h>
#include "store.h"
#include "ui_model.h"

struct WeatherData {
  bool valid = false;
  int tempNow = 0, feelsLike = 0, codeNow = 0, tempMinDay = 0, tempMaxDay = 0;
  struct { uint8_t hour, minute; int temp; bool has; } next[3] = {};
};

// Stan snu potrzebny powiadomieniom o oknie drzemki.
struct SleepState {
  bool sleeping = false;
  time_t sleepStart = 0;   // poczatek trwajacego snu
  time_t lastWake = 0;     // koniec ostatniej sesji (poczatek okna czuwania)
  time_t napStart = 0, napEnd = 0;  // okno drzemki (gdy czuwa)
};

struct StatsInput {
  const Store *store = nullptr;   // nullptr = brak danych
  time_t now = 0;
  bool timeOk = false, wifiOk = false, night = false;
  const WeatherData *weather = nullptr;
};

// Wypelnia model. charts=false liczy tylko czesc "na zywo" (zegar, licznik, dzis,
// drzemka, pogoda); charts=true dodatkowo wykresy (5 dni, przerwy, rytm, waga, noce, kapiele).
void statsCompute(const StatsInput &in, UiModel &m, bool charts, SleepState *sleepOut);

// Skrot danych wykresow — zmiana => przebudowa biezacej karty panelu.
uint32_t statsChartsHash(const UiModel &m);

// Wiek w dniach (data lokalna minus data urodzenia); -1 gdy brak czasu.
long statsAgeDays(time_t now, bool timeOk);
// Okno czuwania (min) dla wieku.
void statsWakeWindow(long ageDays, int &minMin, int &maxMin);
