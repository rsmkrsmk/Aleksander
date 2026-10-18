# Nowy interfejs urządzenia („Leśna bajka”)

Urządzenie jest teraz **tablicą informacyjną** — dane wpisuje się w panelu WWW, a ekran pokazuje stan dnia
i te same wykresy co strona WWW. Funkcjonalność (licznik karmienia, sygnał po 3 h / 4 h, okno drzemki,
pogoda, tryb nocny, diagnostyka, powiadomienia) jest bez zmian.

## Ekrany
| Ekran | Opis |
|---|---|
| Start | statyczna leśna scena z animowanym **jeżem** (mruganie, rumieńce, podskok), kroki startu (Wi‑Fi, NTP, pamięć, pogoda, dane z WWW) i pasek postępu; odświeżany tylko między krokami |
| Tablica (wygaszacz) | duży zegar, pogoda z animowaną ikoną, pierścień czasu od karmienia, statystyki dnia (karmień dziś, mleka dziś, średnia przerwa), ubiór, wskazówka dnia |
| Panel (dotknij tablicy) | 6 kart: **Dziś · Mleko · Odstępy · Waga · Sen · Kąpiel** — przełączane dotykiem w dolny pasek lub przeciągnięciem w bok; wykresy rosną z animacją |

* Logo (lewy górny róg panelu) → powrót na tablicę. Długie przytrzymanie zegara → diagnostyka.
* Brak dotyku przez 30 s (poza panelem) → panel; 2 min → tablica.
* Przyciski edycji (karmienie, pieluchy, waga…) zostały usunięte z urządzenia — wpisy dodaje się w panelu WWW.
  Stare ekrany formularzy są nadal w kodzie (nieosiągalne z interfejsu).

## Pliki
* `ui_modern.h` — cały interfejs (motyw dzień/noc, boot, tablica, panel), niezależny od reszty szkicu.
* `ui_fonts.h`, `ui_assets.h` — czcionki (Nunito/Fredoka, polskie znaki) i ikony/sprite'y (generowane przez `tools/lvfont.py`, `tools/genassets.py`).
* `v3.ino` — wypełnia `UiModel` (`uiFillLive` co sekundę; `uiFillCharts` jednym przebiegiem CSV po zmianie danych / co 2 min)
  i wywołuje funkcje `ui*`.
* `tools/sim/` — symulator PC (patrz tamtejszy README).

## Kompilacja
Nowe pliki `ui_modern.h`, `ui_fonts.h`, `ui_assets.h` muszą leżeć obok `v3.ino`. `lv_conf.h` bez zmian.
Koszt: ok. 0,6 MB w flashu (partycja aplikacji 5 MB — mieści się), animacje tylko na małych obszarach, aby nie
obciążać RGB‑panelu 480×480.

## Wykresy (jak na stronie WWW)
Mleko 5 dni (matki/modyfikowane/mieszane) · przerwy dzisiaj + rytm 3 dni · przyrost wagi (trend tygodniowy,
regresja z okna ±7 dni) · sen nocny wg karmień (początek karmienia wieczornego +90 min … następne karmienie −10 min)
· kalendarz kąpieli (co 3 dni).

## Strona „Waga” (v4.1)
Dwa wykresy w jednym miejscu, przełączane same co ~3,5 s (wskaźnik dwóch kresek pod tytułem): waga na tle normy WHO
(pasmo P3–P97, mediana, pasmo przyrostu od wypisu) oraz przyrost tygodniowy (g/dzień). Pod nimi stałe kafelki
„Od urodzenia” i „Ostatnie 2 tygodnie” (g/dzień + norma). Szczegóły i uwagi o stabilności obrazu, PSRAM i wątkach:
sekcja „v4.1” w `README.md`.
