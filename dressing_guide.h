#pragma once
#include <Arduino.h>
#include <climits>

// Zalecany ubior noworodka na spacer wg temperatury (-20 .. +40 C).
// Zrodla: porady polskich poloznych/producenow odziezy niemowlęcej
// (escallante.pl, codladziecka.pl, honsiumisiu.pl, lipsko24.pl).
// Zasada ogolna: noworodek potrzebuje o JEDNA warstwe wiecej niz dorosly,
// a komfort sprawdzamy dotykajac karku (cieply/suchy = OK).

struct DressingBand {
  int minTemp;        // najnizsza temperatura pasujaca do tego pasa
  const char *advice; // jedno zdanie, bez polskich znakow (font LVGL)
};

// Bandy od NAJCIEPLEJSZEJ; pierwszy pasujacy minTemp <= aktualna temperatura.
static const DressingBand DRESSING_BANDS[] = {
    {35, "Upal >30C: samo body z krotkim rekawem, nozki gole, cienka czapeczka, krem z filtrem"},
    {27, "Goraco: body z krotkim rekawem, golenie gole lub cienkie spodenki, lekka czapeczka"},
    {22, "Cieplo: body z dlugim rekawem, cienki pajacyk, golenie gole, bawelniana czapeczka"},
    {18, "Lekko chlodno: body dlugi rekaw, pajacyk, skarpetki, lekka czapka"},
    {13, "Chlodno: body, pajacyk, sweterek, czapka, kocyk do wozka"},
    {6, "Zimno: trzy warstwy, cieple skarpetki, kombinezon przejsciowy, czapka dzianinowa"},
    {0, "Mroz: body, polar, kombinezon zimowy, czapka zakrywajaca uszy, rozek lub spiworek"},
    {-12, "Duzy mroz do -12C: maksimum warstw, spiworek zimowy, czapka z polarem, krotko na dworze"},
    {INT_MIN, "Ponizej -12C: odloz spacer — wychlodzenie grozi szybciej niz korzysci ze świezego powietrza"},
};
constexpr size_t DRESSING_BAND_COUNT = sizeof(DRESSING_BANDS) / sizeof(DRESSING_BANDS[0]);

inline const char *dressingAdviceFor(int tempC) {
  for (size_t i = 0; i < DRESSING_BAND_COUNT; ++i) {
    if (tempC >= DRESSING_BANDS[i].minTemp) return DRESSING_BANDS[i].advice;
  }
  return DRESSING_BANDS[DRESSING_BAND_COUNT - 1].advice;
}
