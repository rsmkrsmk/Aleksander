# Weryfikacja projektu — Rejestr karmienia Aleksandra

## Zakres kontroli

Wersja **v4** (architektura: hosting źródłem prawdy, urządzenie wyświetlaczem) została sprawdzona strukturalnie pod kątem składni nawiasów, komentarzy i literałów oraz obecności kluczowych elementów aplikacji. Projekt zawiera gotowy ekran LVGL, sterowanie dotykiem GT911, uruchamianie LCD ST7701 z wymaganą sekwencją ekspandera TCA9554, Wi‑Fi, synchronizację NTP, obliczanie wieku, mirror CSV na urządzeniu, pogodę Open-Meteo oraz wygaszacz ekranu.

| Obszar | Wynik |
|---|---|
| Panel LCD ST7701 | Inicjalizacja zawiera oficjalną sekwencję TCA9554 na pinach ekspandera 5 i 6 przed uruchomieniem natywnego panelu `esp_lcd` (single framebuffer PSRAM + bounce DMA, VSYNC). |
| PSRAM | Program sprawdza `ESP.getPsramSize()` przed startem sterownika RGB i wymaga ustawienia `OPI PSRAM`. |
| Bounce buffer | 30 linii, `bb_invalidate_cache=1`, PCLK 6 MHz. |
| Ekran dotykowy | Odczyt GT911 po I2C jest zarejestrowany jako urządzenie wskaźnikowe LVGL. |
| Wi‑Fi | Połączenie w trybie stacji, próby ponownego połączenia co 30 s. |
| Czas | NTP z trzema serwerami oraz regułą polskiej strefy CET/CEST. |
| Formularze | Karmienie piersią (minuty L/P, kroki ±5), butelka (mleko matki/modyfikowane, suwak 10–120 ml), pieluchy, odciąganie, witamina D. |
| Mirror danych | Lokalny `/karmienia.csv` na urządzeniu jest mirror danych z hostingu; aktualizowany przez polling rewizji (co 10 s) i zapis przez API. |
| Synchronizacja | Urządzenie sprawdza `GET /api/revision` co 10 s; przy zmianie pobiera `GET /api/export.csv` i aktualizuje mirror + ekran. Zapis z urządzenia: lokalny + push pełnego CSV (`/api/upload-data`). |
| Ekran główny | Aktualny czas, wiek Aleksandra od 08.08.2026, ostatnie karmienie/butelka, pasek licznika z alarmem, diody W/P/C. |
| Wygaszacz | Po 2 min: tipCard + ageCard widoczne, karta zegara z datą i ostatnim karmieniem, karta pogody Open-Meteo z ikoną, temp odczuwalną, opisem, min/max, 3h prognozą i poradą ubioru. |
| Panel WWW | Kopia UI w PROGMEM (na urządzeniu — serwer WWW **wyłączony** w v4); strony hostingu (`ui/` + `indexesp.html`) działają przez API hostingu. |
| mDNS / OTA | **Wyłączone** (zakomentowane) w v4 — kod zachowany. Wgrywanie firmware przez USB. |
| Pogoda | Open-Meteo HTTP (port 80, bez TLS), własny JSON parser, cache binarny z magic number, osobny task FreeRTOS (stack 4096). |
| Ostatnie karmienie | `loadLatestEntries` wybiera najpóźniejszy PRZESZŁY wpis z fallbackiem na najpóźniejszy ogółem (gdy zegar cofnięty) — „ostatnie karmienie" nigdy nie znika. |

## Wynik kontroli lokalnej

Nie wykryto brakujących funkcji wymaganych przez projekt ani niespójności strukturalnych w szkicu.

## Warunki wgrania w Arduino IDE

> Wybierz płytkę `ESP32S3 Dev Module`, ustaw `PSRAM: OPI PSRAM`, `Flash Size: 16MB (128Mb)` oraz partycję 16 MB zawierającą `SPIFFS`, np. `16M Flash (3MB APP/9.9MB SPIFFS)`. Przed kompilacją skopiuj dostarczony plik `lv_conf.h` obok katalogu biblioteki `lvgl` oraz wpisz własne Wi‑Fi w `secrets.h`.

## Ograniczenie

W środowisku przygotowania plików nie był dostępny lokalny kompilator Arduino CLI ani fizyczna płytka, dlatego nie wykonano kompilacji binarnej ani testu palcem. Ekran LCD został jednak uprzednio potwierdzony przez użytkownika jako działający po poprawnym ustawieniu PSRAM w izolowanym teście.
