# Weryfikacja projektu — Rejestr karmienia Aleksandra

## Zakres kontroli

Wersja 1.3.0 została sprawdzona strukturalnie pod kątem składni nawiasów, komentarzy i literałów oraz obecności kluczowych elementów aplikacji. Projekt zawiera gotowy ekran LVGL, sterowanie dotykiem GT911, uruchamianie LCD ST7701 z wymaganą sekwencją ekspandera TCA9554, Wi‑Fi, synchronizację NTP, obliczanie wieku, LittleFS, dopisywanie danych w formacie CSV, pogodę Open-Meteo oraz wygaszacz ekranu.

| Obszar | Wynik |
|---|---|
| Panel LCD ST7701 | Inicjalizacja zawiera oficjalną sekwencję TCA9554 na pinach ekspandera 5 i 6 przed uruchomieniem natywnego panelu `esp_lcd` (single framebuffer PSRAM + bounce DMA, VSYNC). |
| PSRAM | Program sprawdza `ESP.getPsramSize()` przed startem sterownika RGB i wymaga ustawienia `OPI PSRAM`. |
| Bounce buffer | 30 linii, `bb_invalidate_cache=1`, PCLK 6 MHz. |
| Ekran dotykowy | Odczyt GT911 po I2C jest zarejestrowany jako urządzenie wskaźnikowe LVGL. |
| Wi‑Fi | Połączenie w trybie stacji, próby ponownego połączenia co 30 s. |
| Czas | NTP z trzema serwerami oraz regułą polskiej strefy CET/CEST. |
| Formularze | Karmienie piersią (minuty L/P, kroki ±5), butelka (mleko matki/modyfikowane, suwak 10–120 ml), pieluchy, odciąganie, witamina D. |
| Historia | LittleFS tworzy `/karmienia.csv` i dopisuje rekordy `data,godzina,typ,ml,piers_lewa_min,piers_prawa_min`. |
| Ekran główny | Aktualny czas, wiek Aleksandra od 08.08.2026, ostatnie karmienie/butelka, pasek licznika z alarmem, diody W/P/C. |
| Wygaszacz | Po 2 min: tipCard + ageCard widoczne, karta zegara z datą i ostatnim karmieniem, karta pogody Open-Meteo z ikoną, temp odczuwalną, opisem, min/max, 3h prognozą i poradą ubioru. |
| Panel WWW | Kopia UI w PROGMEM, `/api/status`, `/api/entries`, `/api/entry` (obsługuje KARMIENIE + MLEKO + ODCIAGANIE + PIELUCHY + WIT.D), `/api/undo`, `/api/event`, `/api/import`, `/export.csv`. |
| OOM | `undoLastEntry()` sprawdza `file.size()` vs `freeHeap/2` przed wczytaniem. |
| Pogoda | Open-Meteo HTTP (port 80, bez TLS), własny JSON parser, cache binarny z magic number, osobny task FreeRTOS (stack 4096). |

## Wynik kontroli lokalnej

Nie wykryto brakujących funkcji wymaganych przez projekt ani niespójności strukturalnych w szkicu. Wyeliminowano martwy plik `forest_friends.h` (~15 KB flash) oraz wiszący komentarz.

## Warunki wgrania w Arduino IDE

> Wybierz płytkę `ESP32S3 Dev Module`, ustaw `PSRAM: OPI PSRAM`, `Flash Size: 16MB (128Mb)` oraz partycję 16 MB zawierającą `SPIFFS`, np. `16M Flash (3MB APP/9.9MB SPIFFS)`. Przed kompilacją skopiuj dostarczony plik `lv_conf.h` obok katalogu biblioteki `lvgl` oraz wpisz własne Wi‑Fi w `secrets.h`.

## Ograniczenie

W środowisku przygotowania plików nie był dostępny lokalny kompilator Arduino CLI ani fizyczna płytka, dlatego nie wykonano kompilacji binarnej ani testu palcem. Ekran LCD został jednak uprzednio potwierdzony przez użytkownika jako działający po poprawnym ustawieniu PSRAM w izolowanym teście.
