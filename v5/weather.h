// weather.h — pogoda z Open-Meteo (HTTP, bez klucza), pamiec podreczna w RAM RTC.
#pragma once
#include "stats.h"

void weatherInit();                  // odtworzenie z RTC (po restarcie programowym)
bool weatherFetch();                 // blokujace (zadanie sieciowe); true = nowe dane
WeatherData weatherSnapshot();       // bezpieczna kopia z dowolnego zadania
bool weatherDue();                   // czy czas na kolejne pobranie
uint32_t weatherAgeMs();             // wiek danych (0xFFFFFFFF = brak)

// Parser odpowiedzi (publiczny dla testow na PC).
bool weatherParse(const char *body, int hourNow, WeatherData &out);
