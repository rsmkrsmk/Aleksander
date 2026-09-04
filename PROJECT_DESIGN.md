# Projekt: Rejestr karmienia Aleksandra

## Cel

Program jest przeznaczony dla płytki **Waveshare ESP32-S3-Touch-LCD-4B**. Po uruchomieniu łączy się z lokalną siecią Wi‑Fi, pobiera czas z serwera NTP i prowadzi dziennik opieki nad dzieckiem: karmienia piersią (z minutami osobno dla lewej i prawej), mleko modyfikowane/matki, pieluchy, odciąganie pokarmu, witaminę D, pomiary wagi oraz **sen** (drzemki i sen nocny). Dane zapisywane są w pliku CSV w systemie plików **LittleFS** we wewnętrznej pamięci Flash ESP32. Równolegle urządzenie serwuje **panel WWW** (`http://karmienie.local` lub adres IP), który jest funkcjonalną kopią interfejsu na ekranie.

Moduł **SEN** (wzorowany na aplikacji Napper) rozszerza dziennik o predykcję opartą na *oknach czuwania* (wake windows) wg wieku: urządzenie podpowiada przewidywane okno kolejnej drzemki, sygnalizuje jego stan (za wcześnie / okno drzemki / przekroczenie) i prowadzi dobowy bilans snu względem orientacyjnego celu dla wieku.

## Ekrany programu

| Ekran | Zawartość | Działanie użytkownika |
|---|---|---|
| Ekran główny | Pasek górny: tytuł, diody statusu `W`(Wi‑Fi) `P`(Pamięć) `C`(Czas), zegar. Lewa karta: OSTATNIE KARMIENIE; prawa: wiek Aleksandra. Pasek „OSTATNIE KARMIENIE: X temu" (zielony <3 h, żółty 3–4 h, czerwony ≥4 h). Karta OSTATNIA BUTELKA (pełna szerokość). Duży przycisk KARMIENIE, pod nim nawigacja 3×2: SEN, INNE, WAGA (rząd 1) oraz KALENDARZ, PODSUMOWANIE (rząd 2). Wskazówka rozwojowa nie jest już na ekranie głównym (pozostaje w WWW i na wygaszaczu) | Nawigacja jednym dotknięciem |
| Formularz KARMIENIE | Data/godzina ±5 min, minuty piersi LEWA/PRAWA (−5/+5, 0–90), rozwinięcie SZCZEGÓŁY: rodzaj mleka (MATKI/MODYFIKOWANE) + suwak 10–120 ml (domyślnie 30) | Pełny wpis karmienia |
| SEN | Karta stanu (Śpi od… / Czuwa od…), przewidywane okno drzemki wg wieku z kolorowym paskiem stanu (zielony = za wcześnie, żółty = okno drzemki, czerwony = przekroczone), okno czuwania wg wieku. Duży przycisk ZASNĄŁ / OBUDZIŁ SIĘ (zapis SEN_START/SEN_STOP). Bilans dnia: drzemki (cel), sen dzień/noc vs cel, razem | Zapis snu + podgląd rytmu |
| INNE | Szybkie akcje: PIELUCHA, ODCIAG POKARMU, DIAGNOSTYKA | Nawigacja |
| PIELUCHA | Duże przyciski MOKRA / BRUDNA — zapis bieżącego czasu jednym dotknięciem | Szybki zapis |
| ODCIĄGANIE MLEKA | Suwak 10–120 ml + ZAPISZ/ANULUJ | Zapas mleka |
| WAGA | Suwak 2000–15000 g + kroki ±10 g + ZAPISZ (zapis typu `WAGA`) | Pomiar masy |
| KALENDARZ | Karty 3 dni z dwulinijkowym podsumowaniem; SZCZEGÓŁY otwiera dziennik dnia | Przegląd |
| DZIENNIK DNIA | Lista wszystkich wpisów danego dnia + „+ KARMIENIE" dla tego dnia | Historia/dodawanie wsteczne |
| PODSUMOWANIE | Dziś + wczoraj (+ „starsze dni" po 7/klik): nagłówek dnia, dwie linie statystyk i każde karmienie osobno; pasek COFNIJ / WIT.D | Szczegółowa historia |
| DIAGNOSTYKA | Stan urządzenia (Wi‑Fi/IP/RSSI, HTTP, NTP, pamięć, RAM/PSRAM, CPU, praca, watchdog, resety) oraz przełącznik „Sen: powiadomienia Telegram" | Podgląd + ustawienie |
| WYGASZACZ | Po 2 min bez dotknięcia ekran główny ukrywa przyciski. Karta zegara (dwukolumnowa): po lewej duży zegar z dniem tygodnia i datą pod spodem; po prawej ostatnie karmienie (kolor wg czasu, w nawiasie godzina następnego) i osobny wiersz o oknie drzemki. Niżej karta statystyk dnia i karta pogody: ikona (prymitywy LVGL — 12-promienne słońce, chmura z cieniem, 6 kropli, 6 płatków śniegu, zygzak burzy), temperatura odczuwalna, opis, min/max, 3 kolejne godziny oraz porada ubioru dla noworodka. Pogoda z Open-Meteo (HTTP, bez API key). Cache w LittleFS. Dotknięcie przywraca pełny widok | Tryb bezczynny |

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

## Moduł SEN (wake windows)

Model oparty na *oknach czuwania* i *zapotrzebowaniu na sen* wg wieku (dane wzorowane na aplikacji Napper / Stanford Medicine Children's Health). Wiek liczony metrykalnie od daty urodzenia (`config.h`). Wszystkie wartości progowe znajdują się w `config.h` (tabele `WAKE_WIN_*`, `SLEEP_NEED_*`, `NAP_TARGET_*`) i są interpolowane liniowo względem wieku w dniach.

| Element | Zasada |
|---|---|
| Okno czuwania | Od ostatniego przebudzenia (`SEN_STOP`): `min`–`max` minut wg wieku (u noworodka ~35–60 min, rośnie z wiekiem do 4–6 h) |
| Predykcja drzemki | `okno = ostatnie_przebudzenie + [min, max]`; stan: za wcześnie / okno drzemki teraz / przekroczone (ryzyko przemęczenia) |
| Bilans dnia | Sumy snu **dzień/noc** i liczba **drzemek**, liczone razem z resztą statystyk **jednym przebiegiem** pliku. Sen dzieli się na noc (21:00–07:00) i dzień; sen przez północ rozdzielany proporcjonalnie |
| Cel snu | Orientacyjny cel dobowy (noc/dzień) i liczba drzemek wg wieku — do porównania z rzeczywistym bilansem |
| Powiadomienia | Telegram raz na okno czuwania: przy wejściu w okno drzemki i przy przekroczeniu; zabezpieczenie przed fałszywym alarmem po restarcie (okno zamknięte dawno jest pomijane). Włącznik trwały w `/ustawienia.cfg` |

Sen zapisywany jest jako pary `SEN_START` → `SEN_STOP` w tym samym pliku CSV. Predykcja i bilans korzystają wyłącznie z historii tych wpisów — nie wymagają dodatkowych czujników.

## Statystyki i wydajność

Statystyki dni (dziś + 7 wstecz) — w tym bilans snu (drzemki, sen dzień/noc) — utrzymywane są w pamięci podręcznej budowanej **jednym przebiegiem** pliku; zapis nowego wpisu unieważnia cache. Widok PODSUMOWANIE buduje wszystkie widoczne dni także jednym przebiegiem — historia „starsze dni" nie zamraża interfejsu. Gdy plik danych przekroczy 256 KB, tworzona jest jednorazowa kopia archiwalna (dane nie są usuwane), a fakt sygnalizowany w DIAGNOSTYCE.

## Komunikacja i niezawodność

| Obszar | Rozwiązanie |
|---|---|
| Wi‑Fi | Poświadczenia w `secrets.h`; automatyczne ponawianie (`setAutoReconnect` + restart próby co 30 s); diagnostyka wolnego RAM wewnętrznego w logu |
| Czas | NTP przy starcie i co 6 h; strefa Europe/Warsaw; zegar wymagany do zapisów (`Czas` czerwony gdy brak) |
| Backup | Automatyczna kopia dzienna `/karmienia_backup.csv`; pobieranie pełnego CSV przez WWW (`/export.csv`) oraz **import/przywracanie pliku CSV** (`IMPORTUJ DANE` → `/api/import`, z kopią bezpieczeństwa przed nadpisaniem). Miękka rotacja: kopia archiwalna po przekroczeniu 256 KB |
| Sen (wake windows) | Predykcja okna drzemki i bilans snu wg wieku (moduł SEN, patrz sekcja wyżej); powiadomienia Telegram o oknie snu (opcjonalne, przełącznik w DIAGNOSTYCE, trwałe w `/ustawienia.cfg`) |
| Pogoda | Open-Meteo API (HTTP port 80, bez TLS), `current=temperature_2m,apparent_temperature,weather_code`, `hourly=temperature_2m,weather_code`, `daily=temperature_2m_max,temperature_2m_min`. Własny parser JSON (`jsonNumberAfter`, `jsonArrayNumberAt`). Cache binarny z magic number (`/pogoda.cache`). Odświeżanie co 20 min, osobne zadanie FreeRTOS (stack 4096) |
| Motyw nocny | 21:00–7:00 ciemna paleta i łagodniejsze podświetlenie; po bezczynności dodatkowe przyciemnienie (pomijane nocą) |
| Telegram | Powiadomienia o każdym wpisie do drugiego rodzica oraz (opcjonalnie) o oknie snu (token w `config.h`, puste = wyłączone). Wysyłka w osobnym zadaniu FreeRTOS (rdzeń 0) — nie blokuje interfejsu ani dotyku; kolejka i stan chronione mutexem |
| OTA | ArduinoOTA (hasło w `config.h`; puste = wyłączone); podczas transferu UI wstrzymane |
| mDNS | Panel dostępny pod `http://karmienie.local` |
| Awaria FS | Błąd montowania naprawiany formatem po ostrzeżeniu (historia z wolumenu jest wtedy nieodczytywalna) |

## Parametry sprzętowe potwierdzone dla płytki

| Element | Ustawienie |
|---|---|
| Ekran | 480 × 480 ST7701 RGB przez natywne `esp_lcd`, dwa framebuffer'y w PSRAM i synchronizacja VSYNC; bufor bounce DMA + cykliczny restart DMA przy VSYNC (zapobiega poziomym artefaktom i dryfowi obrazu) |
| Dotyk | GT911 @ I2C 0x14 (SDA=47, SCL=48), SensorLib `TouchDrv.hpp`; I²C 400 kHz (Fast Mode), urządzenie wejściowe LVGL w trybie zdarzeniowym odpytywane częściej niż pełny render (wysoka responsywność) |
| Podświetlenie | GPIO4, PWM active-low (AP3032); motyw nocny ~65% jasności |
| UI | LVGL 9.3 |
| Dane | LittleFS (partycja spiffs), backup + eksport CSV |

## Źródła

[1] [Waveshare — ESP32-S3-Touch-LCD-4B Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B)

[2] [Waveshare — oficjalny schemat ESP32-S3-Touch-LCD-4B](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B/ESP32-S3-Touch-LCD-4B.pdf)
