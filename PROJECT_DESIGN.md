# Projekt: Rejestr karmienia Aleksandra

## Cel

Program jest przeznaczony dla płytki **Waveshare ESP32-S3-Touch-LCD-4B**. Po uruchomieniu łączy się z lokalną siecią Wi‑Fi, pobiera czas z serwera NTP i prowadzi dziennik opieki nad dzieckiem: karmienia piersią (z minutami osobno dla lewej i prawej), mleko modyfikowane/matki, pieluchy, odciąganie pokarmu oraz witaminę D. Dane zapisywane są w pliku CSV w systemie plików **LittleFS** we wewnętrznej pamięci Flash ESP32. Równolegle urządzenie serwuje **panel WWW** (`http://karmienie.local` lub adres IP), który jest funkcjonalną kopią interfejsu na ekranie.

## Ekrany programu

| Ekran | Zawartość | Działanie użytkownika |
|---|---|---|
| Ekran główny | Pasek górny: tytuł, diody statusu `W`(Wi‑Fi) `P`(Pamięć) `C`(Czas), zegar. Lewa karta: wskazówka dnia; prawa: wiek Aleksandra. Pasek „OSTATNIE KARMIENIE: X temu" (zielony <3 h, żółty 3–4 h, czerwony ≥4 h). Karty ostatniego karmienia i butelki. Przyciski: KARMIENIE, PIELUCHA, ODCIAG POKARMU, KALENDARZ, PODSUMOWANIE | Nawigacja jednym dotknięciem |
| Formularz KARMIENIE | Data/godzina ±5 min, minuty piersi LEWA/PRAWA (−5/+5, 0–90), rozwinięcie SZCZEGÓŁY: rodzaj mleka (MATKI/MODYFIKOWANE) + suwak 10–120 ml (domyślnie 30) | Pełny wpis karmienia |
| PIELUCHA | Duże przyciski MOKRA / BRUDNA — zapis bieżącego czasu jednym dotknięciem | Szybki zapis |
| ODCIĄGANIE MLEKA | Suwak 10–120 ml + ZAPISZ/ANULUJ | Zapas mleka |
| KALENDARZ | Karty 3 dni z dwulinijkowym podsumowaniem; SZCZEGÓŁY otwiera dziennik dnia | Przegląd |
| DZIENNIK DNIA | Lista wszystkich wpisów danego dnia + „+ KARMIENIE" dla tego dnia | Historia/dodawanie wsteczne |
| PODSUMOWANIE | Dziś + wczoraj (+ „starsze dni" po 7/klik): nagłówek dnia, dwie linie statystyk i każde karmienie osobno; pasek COFNIJ / WIT.D | Szczegółowa historia |
| WYGASZACZ | Po 2 min bez dotknięcia ekran główny ukrywa przyciski i pokazuje tipCard/ageCard u góry, poniżej duży zegar z datą i ostatnim karmieniem (kolor wg czasu), a niżej kartę pogody: ikona (prymitywy LVGL — 12-promienne słońce, chmura z cieniem, 6 kropli, 6 płatków śniegu, zygzak burzy), temperatura odczuwalna, opis, min/max, 3 kolejne godziny oraz porada ubioru dla noworodka. Pogoda z Open-Meteo (HTTP, bez API key). Cache w LittleFS. Dotknięcie przywraca pełny widok | Tryb bezczynny |

Podsumowanie dnia (format wspólny dla urządzenia i WWW):

```
KARM.: n | MLEKO: x ml | PIERS: L a/P b
PIELUCHY: m/k | ODCIAG.: p ml | WIT.D: TAK/BRAK
```

## Model danych

Plik `/karmienia.csv` (LittleFS). Nagłówek:

```
data,godzina,typ,ml,piers_lewa_min,piers_prawa_min
```

Starsze wpisy z 4 kolumnami pozostają czytelne (minuty = 0).

| Typ wpisu | ml | Minuty L/P | Znaczenie |
|---|---|---|---|
| `KARMIENIE` | 0 | ✔ | karmienie piersią |
| `MLEKO_MATKI` / `MLEKO_MODYFIKOWANE` / `MLEKO` (stare) | ✔ | 0 | butelka |
| `PIELUCHA_MOKRA` / `PIELUCHA_BRUDNA` | 0 | 0 | zmiana pieluchy |
| `ODCIAGANIE` | ✔ | 0 | zapas pokarmu |
| `WITAMINA_D` | 0 | 0 | dawka dzienna (raz na dobę) |
| `WAGA` | ✔ (gramy w kolumnie `ml`) | 0 | pomiar masy ciała |
| `SEN_START` / `SEN_STOP` | 0 | 0 | początek / koniec snu (pasmo doby) |

> Uwaga: dla typu `WAGA` kolumna `ml` przechowuje **gramy** (np. `3700`), a nie mililitry. Zakres 2000–15000 g.

Przykład: `2026-08-22,14:35,KARMIENIE,0,12,8`.

## Cofnięcie wpisu

COFNIJ (PODSUMOWANIE / panel WWW) usuwa ostatni wiersz w sposób bezpieczny: treść bez ostatniego wiersza trafia do pliku tymczasowego, a podmiana wykonywana jest atomowo przez `rename`. Dwuetapowe potwierdzenie chroni przed przypadkowym kliknięciem.

## Statystyki i wydajność

Statystyki dni (dziś + 7 wstecz) utrzymywane są w pamięci podręcznej budowanej **jednym przebiegiem** pliku; zapis nowego wpisu unieważnia cache. Widok PODSUMOWANIE buduje wszystkie widoczne dni także jednym przebiegiem — historia „starsze dni" nie zamraża interfejsu.

## Komunikacja i niezawodność

| Obszar | Rozwiązanie |
|---|---|
| Wi‑Fi | Poświadczenia w `secrets.h`; automatyczne ponawianie (`setAutoReconnect` + restart próby co 30 s); diagnostyka wolnego RAM wewnętrznego w logu |
| Czas | NTP przy starcie i co 6 h; strefa Europe/Warsaw; zegar wymagany do zapisów (`Czas` czerwony gdy brak) |
| Backup | Automatyczna kopia dzienna `/karmienia_backup.csv`; pobieranie pełnego CSV przez WWW (`/export.csv`) oraz **import/przywracanie pliku CSV** (`IMPORTUJ DANE` → `/api/import`, z kopią bezpieczeństwa przed nadpisaniem) |
| Pogoda | Open-Meteo API (HTTP port 80, bez TLS), `current=temperature_2m,apparent_temperature,weather_code`, `hourly=temperature_2m,weather_code`, `daily=temperature_2m_max,temperature_2m_min`. Własny parser JSON (`jsonNumberAfter`, `jsonArrayNumberAt`). Cache binarny z magic number (`/pogoda.cache`). Odświeżanie co 20 min, osobne zadanie FreeRTOS (stack 4096) |
| Motyw nocny | 21:00–7:00 ciemna paleta i łagodniejsze podświetlenie; po bezczynności dodatkowe przyciemnienie (pomijane nocą) |
| Telegram | Powiadomienia o każdym wpisie do drugiego rodzica (token w `config.h`, puste = wyłączone); wysyłka nieblokująca |
| OTA | ArduinoOTA (hasło w `config.h`; puste = wyłączone); podczas transferu UI wstrzymane |
| mDNS | Panel dostępny pod `http://karmienie.local` |
| Awaria FS | Błąd montowania naprawiany formatem po ostrzeżeniu (historia z wolumenu jest wtedy nieodczytywalna) |

## Parametry sprzętowe potwierdzone dla płytki

| Element | Ustawienie |
|---|---|
| Ekran | 480 × 480 ST7701 RGB przez natywne `esp_lcd`, dwa framebuffer'y w PSRAM i synchronizacja VSYNC |
| Dotyk | GT911 @ I2C 0x14 (SDA=47, SCL=48), SensorLib `TouchDrv.hpp` |
| Podświetlenie | GPIO4, PWM active-low (AP3032); motyw nocny ~65% jasności |
| UI | LVGL 9.3 |
| Dane | LittleFS (partycja spiffs), backup + eksport CSV |

## Źródła

[1] [Waveshare — ESP32-S3-Touch-LCD-4B Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B)

[2] [Waveshare — oficjalny schemat ESP32-S3-Touch-LCD-4B](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B/ESP32-S3-Touch-LCD-4B.pdf)
