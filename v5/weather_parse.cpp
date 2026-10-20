// weather_parse.cpp — parser odpowiedzi Open-Meteo (czysty C++, testowalny na PC).
#include "weather.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

// Liczba po kluczu (np. "\"temperature_2m\":") w sekcji zaczynajacej sie od from.
static bool numAfter(const char *from, const char *key, float &out) {
  const char *k = strstr(from, key);
  if (!k) return false;
  k += strlen(key);
  while (*k == ' ' || *k == ':') ++k;
  if (!strncmp(k, "null", 4)) return false;
  char *e = nullptr;
  out = strtof(k, &e);
  return e && e != k;
}
// n-ty element tablicy liczbowej po kluczu.
static bool arrayAt(const char *from, const char *key, int idx, float &out) {
  const char *k = strstr(from, key);
  if (!k) return false;
  k = strchr(k, '[');
  if (!k) return false;
  ++k;
  for (int i = 0; i < idx; ++i) {
    const char *c = strchr(k, ',');
    const char *cl = strchr(k, ']');
    if (!c || (cl && cl < c)) return false;
    k = c + 1;
  }
  while (*k == ' ') ++k;
  if (!strncmp(k, "null", 4) || *k == ']') return false;
  char *e = nullptr;
  out = strtof(k, &e);
  return e && e != k;
}

bool weatherParse(const char *body, int hourNow, WeatherData &w) {
  w = WeatherData();
  const char *curS = strstr(body, "\"current\"");
  if (!curS) return false;
  float t, f, code;
  if (!numAfter(curS, "\"temperature_2m\"", t) || !numAfter(curS, "\"weather_code\"", code)) return false;
  w.tempNow = static_cast<int>(lroundf(t));
  w.feelsLike = numAfter(curS, "\"apparent_temperature\"", f) ? static_cast<int>(lroundf(f)) : w.tempNow;
  w.codeNow = static_cast<int>(code);
  const char *hS = strstr(body, "\"hourly\"");
  if (hS && hourNow >= 0) {
    // Sekcja "hourly" zawiera tez "hourly_units" — szukamy obiektu z danymi ("time":[...]).
    const char *data = strstr(hS + 8, "\"hourly\"");
    if (data) hS = data;
    for (int i = 0; i < 3; ++i) {
      const int idx = hourNow + 1 + i;
      float ht, hc;
      if (arrayAt(hS, "\"temperature_2m\"", idx, ht) && arrayAt(hS, "\"weather_code\"", idx, hc)) {
        w.next[i].hour = static_cast<uint8_t>(idx % 24);
        w.next[i].minute = 0;
        w.next[i].temp = static_cast<int>(lroundf(ht));
        w.next[i].has = true;
      }
    }
  }
  const char *dS = strstr(body, "\"daily\"");
  if (dS) {
    const char *data = strstr(dS + 7, "\"daily\"");
    if (data) dS = data;
  }
  float v;
  w.tempMinDay = (dS && arrayAt(dS, "\"temperature_2m_min\"", 0, v)) ? static_cast<int>(lroundf(v)) : w.tempNow;
  w.tempMaxDay = (dS && arrayAt(dS, "\"temperature_2m_max\"", 0, v)) ? static_cast<int>(lroundf(v)) : w.tempNow;
  w.valid = true;
  return true;
}

