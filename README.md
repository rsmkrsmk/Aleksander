# Leśny Dziennik Aleksandra — v5 (urządzenie = tylko wyświetlacz)

Dziennik opieki nad niemowlęciem na płytce **Waveshare ESP32-S3-Touch-LCD-4B** (480×480, dotyk GT911) z panelem WWW na hostingu (`web-php/`).

W wersji 5:
- Wpisy dodaje się **wyłącznie na stronie WWW**.
- Urządzenie pobiera dane z hostingu i je **wyświetla**: tablica, panel z 6 kartami, diagnostyka, pogoda, powiadomienia Telegram.
- Urządzenie **niczego nie zapisuje na hosting**.

Gałąź `v5` to firmware napisane od nowa na podstawie analizy `ANALIZA_V5.md`. Poprzednia wersja (v4.1, jeden plik `v3.ino`) jest na gałęzi `v4.1-claude-pazdziernik`.

---

## Zawartość

| Ścieżka | Rola |
|---|---|
| `v5/` | **firmware v5** — folder szkicu Arduino (otwierasz `v5/v5.ino`) |
| `web-php/` | panel WWW (PHP): źródło danych, formularze, API (`/api/revision`, `/api/export.csv`, `/api/status`) |
| `tools/sim/`, `tools/sim5/` | symulator PC interfejsu LVGL i testy obliczeń v5 |
| `tools/genassets.py`, `tools/lvfont.py` | generatory grafik (`ui_assets.h`) i fontów (`ui_fonts.h`) |
| `ANALIZA_V5.md` | specyfikacja: co robi urządzenie, wzory, decyzje projektowe D1–D6 |
| `ulepszenia19-10-26.md` | przegląd kodu z 19.10.2026 (bezpieczeństwo, hosting, pomysły) |
| `docs/archiwum/` | dokumenty z wersji v3/v4 (historyczne) |

### Moduły firmware (`v5/`)

| Plik | Co robi |
|---|---|
| `v5.ino` | tylko `setup()`/`loop()` → `app.cpp` |
| `app.cpp` | kolejność startu (ekran startowy, 5 kroków), pętla UI zsynchronizowana z VSYNC, przełączanie ekranów, przyciemnianie, tryb nocny, diagnostyka |
| `hw_display.cpp` | ST7701 przez ekspander TCA9554, `esp_lcd` RGB (bounce 80 linii + 2 FB w PSRAM), LVGL DIRECT, podświetlenie, twarda resynchronizacja obrazu, detektor przesunięcia, `FlashWriteGuard` |
| `hw_touch.cpp` | GT911 (SensorLib) jako wskaźnik LVGL; dotyk tablicy tylko otwiera panel |
| `net.cpp` | Wi-Fi (bez zapisów NVS), NTP, wspólny klient HTTP(S) z buforem w PSRAM (Content-Length / chunked) |
| `host_sync.cpp` | **zadanie sieciowe (rdzeń 0)**: rewizja co 10 s → pobranie CSV → parsowanie → przekazanie do UI; pogoda; Telegram; kopia dzienna; cache na flashu |
| `store.cpp` | parser CSV → tablica wpisów w PSRAM, posortowana po czasie |
| `stats.cpp` | **wszystkie obliczenia** do ekranu (karmienia, przerwy, rytm, mleko, sen, okno drzemki, waga, noce, kąpiele, wiek, wskazówka) — czysty C++, testowany na PC |
| `weather.cpp`, `weather_parse.cpp` | Open-Meteo (HTTP), parser, pamięć RTC |
| `telegram.cpp` | wiadomość startowa, okno drzemki, kopia CSV (`sendDocument`) |
| `diag.cpp` | watchdog zadań, liczniki uruchomień (RTC), CPU, minimum RAM |
| `ui.cpp` + `ui_modern.h` | ekrany: start (jeż), tablica, panel (Dziś, Mleko, Odstępy, Waga, Sen, Kąpiel), diagnostyka |
| `ui_model.h` | dane do wyświetlenia (`UiModel`) |
| `ui_fonts.h`, `ui_assets.h`, `development_tips.h` | fonty z polskimi znakami, grafiki, wskazówki dnia 0–600 |
| `app_config.h` | stałe (urodziny, progi, czasy, adresy hostingu) |
| `secrets.h.example` | wzór pliku z Wi-Fi i Telegramem (`secrets.h` nie trafia do repozytorium) |
| `partitions.csv` | aplikacja 8 MB + 1 MB LittleFS na cache danych |
| `lv_conf.h` | konfiguracja LVGL (do skopiowania do bibliotek, patrz niżej) |

---

## Kompilacja (Arduino IDE)

1. **Biblioteki i płytka.** Wersje sprawdzone 20.10.2026 kompilacją pełnego szkicu:

   | Składnik | Wersja |
   |---|---|
   | esp32 by Espressif | 3.3.12 |
   | lvgl | 9.3.0 |
   | GFX Library for Arduino | 1.6.9 |
   | SensorLib | 0.5.1 |

   Starsze 3.2.x powinny działać (kod ma zgodność wsteczną).
2. **`lv_conf.h`:** skopiuj `v5/lv_conf.h` do folderu bibliotek Arduino, obok folderu `lvgl`, czyli `Dokumenty/Arduino/libraries/lv_conf.h`. Jeśli masz już plik z v4, możesz go zostawić — jest taki sam.
3. **Sekrety:** w folderze `v5` skopiuj `secrets.h.example` jako `secrets.h` i uzupełnij:
   - `WIFI_SSID`, `WIFI_PASSWORD`,
   - `TELEGRAM_BOT_TOKEN`, `TELEGRAM_CHAT_ID`.

   Stary `secrets.h` z v4 też zadziała, ale **Telegram trzeba dopisać**, bo w v4 był w `config.h`.

   ⚠️ Token bota, który był w publicznym repozytorium, należy unieważnić (@BotFather → `/revoke`) i wpisać nowy.
4. **Otwórz `v5/v5.ino`.** Folder musi nazywać się `v5`.
5. **Narzędzia:**

   | Ustawienie | Wartość |
   |---|---|
   | Płytka | **ESP32S3 Dev Module** |
   | PSRAM | **OPI PSRAM** |
   | Flash Size | **16MB (128Mb)** |
   | Partition Scheme | **Custom** (zawsze używany jest `v5/partitions.csv`; przy innym wyborze też, ale limit rozmiaru bywa mniejszy) |
   | USB CDC On Boot | jak dotąd (Enabled, jeśli log Seriala czytasz przez natywny port USB) |
6. **Wgraj.** Zmiana tablicy partycji czyści stare dane LittleFS z v4. To nic nie szkodzi: dane są na hostingu, a cache odtworzy się po pierwszym pobraniu.

Rozmiar (kompilacja 20.10.2026): program ~2,25 MB z 8 MB, zmienne globalne 62 KB RAM.

---

## Jak działa

```
Panel WWW (PHP) ──GET /api/revision co 10 s──► zadanie sieciowe (rdzeń 0)
               ──GET /api/export.csv przy zmianie──► parser → tablica wpisów w PSRAM ──► pętla UI (rdzeń 1)
               ──GET /api/status co 10 min (ustawienie powiadomień o drzemce)            │
Open-Meteo ────HTTP co 20 min──► pogoda (RAM + RTC)                                     ├─ stats.cpp → UiModel
Telegram ◄─── start, okno drzemki, kopia CSV raz dziennie                                └─ LVGL: tablica / panel / diagnostyka
```

**Ekrany i nawigacja**
- **Start:** jeż, 5 kroków — Wi-Fi, zegar, pamięć (cache), pogoda, dane z WWW.
- **Tablica:** zegar, pogoda, drzemka, pierścień od ostatniego karmienia, 3 kafelki dnia, prognoza godzinowa, ubiór, wskazówka dnia.
- **Panel:** dotknięcie tablicy. 6 kart przełączanych paskiem lub gestem; na karcie Waga dotknięcie wykresu zmienia widok.
- **Powroty i diagnostyka:** liść w panelu wraca do tablicy; po 2 min bezczynności panel też wraca do tablicy. Długie przytrzymanie zegara w panelu otwiera diagnostykę.

**Tryb nocny** (21:00–7:00): ciemna paleta i jasność ~65%. W dzień ekran przyciemnia się po 60 s.

**Brak internetu przy starcie:** urządzenie pokazuje ostatnie dane z cache. Cache jest zapisywany tylko przy zmianie danych, najwyżej raz na 30 min — to **jedyny zapis do flasha** w pracy urządzenia.

**Powiadomienia o oknie drzemki** włącza się na **stronie WWW** (przełącznik „Sen: powiadomienia Telegram”). Urządzenie tylko odczytuje to ustawienie.

**Stabilność obrazu**
- Poza rzadkim zapisem cache urządzenie nie dotyka flasha (dane w PSRAM, pogoda w RAM RTC, Wi-Fi bez NVS).
- Zostają twarda resynchronizacja i detektor przesunięcia jako zabezpieczenie. Ich liczniki są w diagnostyce.
- Gdyby przesunięcia mimo to wracały, rozwiązaniem u źródła jest kompilacja w PlatformIO z `CONFIG_SPIRAM_XIP_FROM_PSRAM` (patrz `ANALIZA_V5.md`, 7.4).

### Zmiany względem v4.1

**Usunięte:**
- formularze i zapisy z urządzenia (pielucha, odciąganie), wysyłanie CSV na hosting,
- stare ekrany LVGL,
- serwer WWW, mDNS, OTA,
- lokalna archiwizacja,
- cache pogody na flashu, ustawienia na flashu.

**Poprawione:**
- „Śpi od –” — zdarzenia snu z przyszłości (auto-sen panelu WWW) są pomijane,
- sen liczony z posortowanych zdarzeń, z odrzuceniem sesji > 14 h,
- średnia przerwa z posortowanych czasów,
- ostatnia waga = najpóźniejszy pomiar,
- widok wagi nie wraca sam do wykresu wagi,
- karta panelu przebudowuje się tylko przy zmianie danych,
- watchdog obejmuje też zadanie sieciowe,
- poprawne jednostki timeoutów TLS.

**Wynik obliczeń** porównany z v4.1 na tym samym pliku 40 dni danych (`tools/sim5`): identyczne wykresy tygodni, nocy, kąpieli i wagi.

---

## Testy na PC

```bash
cd tools/sim5
g++ -std=c++17 -O1 -I../../v5 test_stats.cpp ../../v5/store.cpp ../../v5/stats.cpp -o test_stats
python3 ../sim/gen_csv.py                      # syntetyczne dane do /tmp/fw_data.csv
TZ="CET-1CEST,M3.5.0,M10.5.0/3" ./test_stats /tmp/fw_data.csv
```

Zrzuty ekranów robi symulator LVGL (`tools/sim`, scenariusz `tools/sim5/scen_v5.h`, opis w pliku).

---

## Panel WWW

Opis w `web-php/README.md`. Ważne uwagi z przeglądu (`ulepszenia19-10-26.md`):
- API nie ma uwierzytelniania (P0),
- auto-sen przy każdym karmieniu dopisuje zdarzenia w przyszłości (P1),
- zapisy CSV mają wyścigi (P1).
