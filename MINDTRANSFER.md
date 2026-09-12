# MINDTRANSFER — kompletny opis aplikacji „Leśny Dziennik Aleksandra”

> **Cel dokumentu.** Ten plik jest jednorazowym „transferem umysłu” dla innego modelu AI, który ma
> pracować nad tą aplikacją bez wcześniejszej wiedzy o niej. Opisuje **wszystkie** warstwy
> (urządzenie sprzętowe, firmware, stronę WWW serwowaną z urządzenia, panel na hostingu),
> **typy danych, algorytmy, endpointy, stałe i każdą istotną relację** między elementami.
> Numery linii odnoszą się do stanu gałęzi `v3` w chwili pisania (mogą się nieznacznie przesunąć po edycjach).
> Język: polski. Nazwy symboli podane dosłownie (angielskie/oryginalne).

---

## 0. Streszczenie w jednym akapicie

Aplikacja to **dziennik opieki nad niemowlęciem** (karmienia, mleko/butelka, pieluchy, odciąganie,
witamina D, waga, sen). Fizyczne urządzenie to **ESP32‑S3 Waveshare Touch LCD 4B** (ekran dotykowy
480×480, panel RGB ST7701, dotyk GT911) z interfejsem **LVGL 9.x**. Firmware (`OfficialWaveshareHelloWorld.ino`)
przechowuje dane w pliku **CSV na LittleFS** (Flash), serwuje **własną stronę WWW** (HTML/JS zaszyty w
`web_panel.h`), wysyła powiadomienia na **Telegram** i po każdej zmianie danych **wypycha cały CSV na
hosting** (`POST /api/upload-data`). Na hostingu działa bliźniaczy **panel PHP** (`web-php/`) z tym samym
kontraktem API i tą samą logiką domenową; hosting jest **lustrem** danych, a **urządzenie — źródłem
prawdy**. Wszystkie trzy interfejsy (ekran LVGL, strona WWW urządzenia, panel hostingu) mówią tym samym
**formatem CSV** i tym samym **kontraktem JSON API**.

```
   ┌───────────────────────────── URZĄDZENIE (ESP32-S3) ─────────────────────────────┐
   │  Ekran LVGL (dotyk)   Strona WWW urządzenia (web_panel.h)   Serwer HTTP :80      │
   │            \                    |                              /                 │
   │             └──────────── plik CSV /karmienia.csv (LittleFS) ──┘   ← ŹRÓDŁO PRAWDY│
   │                                   │  POST /api/upload-data (cały CSV, po zmianie) │
   └───────────────────────────────────┼──────────────────────────────────────────────┘
                                        ▼  (HTTPS multipart)
   ┌───────────────────────────── HOSTING (PHP) ────────────────────────────────────┐
   │  ui/ (index.html + app.js)  →  engine/api.php  →  Repository (CSV lub MySQL)     │
   │                                             └→  Domain.php (logika, statystyki)  │
   │  Panel = LUSTRO danych (kopia .bakap przy każdym wgraniu)                        │
   └──────────────────────────────────────────────────────────────────────────────────┘
        (opcjonalnie) Telegram ← powiadomienia i backup CSV z urządzenia
```

---

## 1. Repozytorium — mapa plików

| Plik / katalog | Rola |
|---|---|
| `OfficialWaveshareHelloWorld.ino` | **Firmware** (jeden translation unit, ~5900 linii): sprzęt, LVGL UI, CSV, serwer HTTP, host‑sync, Telegram, OTA, mDNS, NTP, sen. |
| `config.h` | Wszystkie stałe konfiguracyjne firmware (zakresy, progi, sen/Napper, sieć, host‑sync, Telegram). **Zawiera jawny token Telegrama — dług bezpieczeństwa.** |
| `secrets.h` | `WIFI_SSID`, `WIFI_PASSWORD` (`constexpr char[]`). |
| `web_panel.h` | **Strona WWW serwowana przez urządzenie** — cały HTML+CSS+JS w jednym `PROGMEM` raw string `WEB_APP_HTML = R"WEBPANEL(...)WEBPANEL"`. |
| `lv_conf.h` | Konfiguracja LVGL 9.x (wymagana do kompilacji; patrz README/sekcja błędu `lv_conf.h`). |
| `development_tips.h` | `DEVELOPMENT_TIPS[601]` (`PROGMEM`) — wskazówki rozwojowe dzień 0–600. `DEVELOPMENT_TIP_COUNT=601`. |
| `dressing_guide.h` | `DRESSING_BANDS[]` + `dressingAdviceFor(tempC)` — porada ubioru wg temperatury (wygaszacz). |
| `partitions.csv` | Układ partycji Flash 16 MB: `factory` 5 MB (app), `spiffs` ~11 MB (LittleFS). Bez OTA‑partycji (OTA nadpisuje `factory`). |
| `PROJECT_DESIGN.md`, `README.md`, `VALIDATION.md` | Dokumentacja projektu (README zawiera historię zmian). |
| `web-php/` | **Panel hostingu (PHP).** |
| `web-php/engine/` | Backend: `api.php` (routing+endpointy), `bootstrap.php` (ładowanie+fabryka), `config.php` (klasa `Config`), `Repository.php` (interfejs), `CsvRepository.php`, `MysqlRepository.php`, `Domain.php` (logika), `schema.sql` (tabele MySQL). |
| `web-php/ui/` | Frontend: `index.html` (struktura+CSS), `app.js` (~1000 linii logiki), `index.php` (front controller), `paths.php` (ścieżki engine/data). |
| `web-php/data/` | `karmienia.csv` (dane panelu), `karmienia_backup.csv`, `.htaccess` (blokada dostępu WWW). |

Gałąź robocza: **`v3`** (kopia 1:1 gałęzi `feat/web-panel-design-iteracje`). Merge do `main` — tylko na wyraźne polecenie użytkownika.

---

## 2. Model danych — wspólny format CSV (fundament całej aplikacji)

Wszystkie warstwy operują na jednym formacie. Plik na urządzeniu: `/karmienia.csv` (LittleFS);
na hostingu: `web-php/data/karmienia.csv`.

**Nagłówek (dokładnie):**
```
data,godzina,typ,ml,piers_lewa_min,piers_prawa_min
```

**Wiersz:** `RRRR-MM-DD,GG:MM,TYP,ML[,piers_lewa_min,piers_prawa_min]`
- Kolumny 5–6 (minuty piersi) są **opcjonalne**: starsze/inne wpisy mają **4 kolumny**, karmienia piersią **6**.
- `ml` znaczy różne rzeczy zależnie od typu (patrz tabela).

### 2.1. Typy wpisów (`TYP`) — pełna lista

| TYP | Znaczenie | `ml` | Kol. 5/6 (piersi) |
|---|---|---|---|
| `KARMIENIE` | karmienie piersią / zdarzenie karmienia | zawsze `0` | minuty lewa/prawa (≥0) |
| `MLEKO_MATKI` | butelka: mleko matki | ilość ml | – |
| `MLEKO_MODYFIKOWANE` | butelka: mleko modyfikowane | ilość ml | – |
| `MLEKO_MIESZANE` | butelka: mieszane (matki + modyfikowane) | ilość ml | – |
| `MLEKO` | **stary** typ mleka (zgodność wsteczna) | ilość ml | – |
| `PIELUCHA_MOKRA` | pielucha mokra | `0` | – |
| `PIELUCHA_BRUDNA` | pielucha brudna | `0` | – |
| `ODCIAGANIE` | odciąganie pokarmu | ilość ml | – |
| `WITAMINA_D` | podana witamina D (idempotentna w obrębie dnia) | `0` | – |
| `WAGA` | pomiar wagi | **gramy** (nie ml!) | – |
| `SEN_START` | zaśnięcie | `0` | – |
| `SEN_STOP` | pobudka | `0` | – |

### 2.2. KLUCZOWA RELACJA: karmienie ↔ mleko (dwa wiersze o tej samej godzinie)

Karmienie z butelką to **fizycznie wiersz KARMIENIE + wiersz(e) mleka o identycznej dacie i godzinie**:
```
2026-09-02,08:00,KARMIENIE,0,10,5           ← karmienie (piersi 10/5 min)
2026-09-02,08:00,MLEKO_MATKI,90             ← mleko matki
2026-09-02,08:00,MLEKO_MODYFIKOWANE,50      ← mleko modyfikowane (mieszane = dwa wiersze)
```
- **Parowanie odbywa się WYŁĄCZNIE po zgodności `data`+`godzina`** (nie ma żadnego ID relacji). Do jednego
  karmienia może należeć **0, 1 lub 2 wiersze mleka** — dlatego logika parowania/edycji zbiera WSZYSTKIE
  wiersze `MLEKO_*` o danym czasie (`.filter`, nie `.find`).
- **MLEKO MIESZANE = DWA osobne wiersze** `MLEKO_MATKI,X` + `MLEKO_MODYFIKOWANE,Y` (osobne ilości) o tej samej
  godzinie co karmienie. To pozwala podać różne ilości mleka matki i modyfikowanego. Zapis pojedynczego rodzaju
  = jeden wiersz `MLEKO_MATKI` LUB `MLEKO_MODYFIKOWANE`.
- **`MLEKO_MIESZANE` (stary, jednowierszowy typ)** NIE jest już zapisywany przez API HTTP (panel + strona WWW
  urządzenia), ale POZOSTAJE w pełni obsługiwany przy **odczycie/statystykach/wykresach** dla danych
  historycznych (`isMilkType`, `milkTypeLabel`, `buildDayStats`→`mixedMilkMl`). Przy edycji karmienia ze starym
  `MLEKO_MIESZANE` prefill rozbija tę wartość na oba rodzaje. **Ekran LVGL urządzenia** (poza zakresem tej zmiany)
  nadal może zapisać `MLEKO_MIESZANE` jednowierszowo — statystyki to poprawnie liczą.
- `appendEntry`/zapis przez API dodaje wiersze mleka **tuż za** wierszem KARMIENIE, ale logika parowania nie
  zakłada sąsiedztwa — dopasowuje po czasie.
- **Kontrakt przewodowy (wire)** wyboru mleka: `milkMotherMl` + `milkModifiedMl` (osobne ilości; 0 = brak danego
  rodzaju; obie >0 = mieszane). Wsteczna zgodność: starsze `milkMother`/`milkModified`(+`milkType`) + pojedyncze
  `milkMl` — przy obu zaznaczonych `milkMl` dzielone równo (reszta do matki). Reguła identyczna w firmware
  (`milkAmountsFromArgs` w `handleApiEntry`/`handleApiUpdateFeeding`) i PHP (`milkAmountsFromParams` w `api.php`).

### 2.3. `lineIndex` — identyfikator wiersza (uwaga na stabilność!)

- **CSV:** `lineIndex` = **fizyczna pozycja wiersza po nagłówku**, liczona dla **KAŻDEJ** linii (także pustej/
  niepoprawnej — te są pomijane w wyniku, ale indeks rośnie). Tak liczy firmware (`handleApiEntries`,
  `deleteEntryByIndex`, `updateFeeding`) i `CsvRepository::allEntries`. **Niestabilny** po insert/delete.
- **MySQL:** `lineIndex` = kolumna `id` (autoinkrement). **Stabilny.**
- **Konsekwencja dla UI:** po każdej edycji/usunięciu trzeba **odświeżyć listę** (`/api/entries`), bo indeksy
  CSV mogły się przesunąć. Frontend robi to (`openDay` po zapisie).

### 2.4. Struktury reprezentujące wpis

- **Firmware** — `struct CsvEntry { String date; String time; String type; int ml; int piersLeft; int piersRight; }`.
- **Firmware** — `struct DaySummary { int feedingCount, milkCount, milkMl, motherMilkMl, modifiedMilkMl, mixedMilkMl,
  piersLeftMin, piersRightMin, diaperWet, diaperDirty, pumpingMl; bool vitaminD; int weightG, sleepDayMin, sleepNightMin, napCount; }`.
- **PHP** — wpis jako tablica asocjacyjna `['date','time','type','ml','piersLeft','piersRight','lineIndex']`;
  `DaySummary` jako tablica z tymi samymi kluczami (`Domain::emptyDaySummary()`).

---

## 3. WARSTWA 1 — Urządzenie (sprzęt + firmware)

Plik: `OfficialWaveshareHelloWorld.ino`. Środowisko: Arduino, ESP32 core ≥3.2.0 (ESP‑IDF 5.x), LVGL 9.x,
`Arduino_GFX` (tylko `Arduino_XCA9554SWSPI` + tabela init ST7701), SensorLib `TouchDrvGT911`.

> **Pułapka kolejności deklaracji (Arduino).** Arduino wstawia autoprototypy tuż nad PIERWSZĄ funkcją pliku.
> Dlatego typy `CsvEntry`, `WeatherState`, `DaySummary`, `WeatherKind`, `WakeWindow` MUSZĄ być zdefiniowane
> WYŻEJ niż pierwsza funkcja (`snapshotWeather`). Zmieniając kolejność, łatwo zepsuć kompilację.

### 3.1. Sprzęt i piny

| Element | Szczegóły |
|---|---|
| Ekran | Panel RGB **ST7701** 480×480, `esp_lcd_rgb_panel`, pclk 6 MHz; piny: hsync 46, vsync 3, de 17, pclk 9, 16 linii danych. |
| Framebuffery | **2 × w PSRAM** (`num_fbs=2`, `fb_in_psram=1`, `double_fb=1`); LVGL renderuje w trybie **DIRECT** wprost do nich. |
| Bounce DMA | `config.bounce_buffer_size_px = SCREEN_WIDTH * 40` → 2×40×480×2 B = **75 KB RAM WEWNĘTRZNEGO**. (Było 80 linii = 150 KB → zostawało ~20 KB heapu i urządzenie restartowało się przy 1. żądaniu HTTP.) Liczba linii musi dzielić 230400 (dozwolone 40/48/60/80/96/120). |
| Dryf obrazu | `on_vsync = rgbVsyncCallback` woła `esp_lcd_rgb_panel_restart()` przy KAŻDYM VSYNC (~60/s) — programowy odpowiednik `CONFIG_LCD_RGB_RESTART_IN_VSYNC`; koryguje dryf w ~16 ms. `requestRgbResync()` — punktowo po zapisie LittleFS i transmisjach TLS. |
| Ekspander | **TCA9554/XCA9554** `Arduino_XCA9554SWSPI(7,0,2,1,&Wire,0x20)`; `initialiseDisplayPanel()` = sekwencja pinów 5/6 (reset/zasilanie ST7701). |
| Podświetlenie | PWM GPIO4, 5 kHz, 8‑bit. Sterownik **AP3032 aktywny NISKIM**: `BACKLIGHT_FULL_DUTY=0` (=100%), `BACKLIGHT_DIM_DUTY=119`, `BACKLIGHT_NIGHT_DUTY=90`. |
| Dotyk | **GT911** (SensorLib `TouchDrvGT911`), I2C SDA 47 / SCL 48, `Wire.setClock(400000)` (Fast Mode). LVGL w trybie `LV_INDEV_MODE_EVENT` — brak własnego timera; odczyt wyłącznie ręczny przez `lv_indev_read()` w `loop()`. |
| I2C | SDA 47, SCL 48; ekspander 0x20, GT911 (SensorLib addr L). |

### 3.2. `setup()` — kolejność startu (skrótowo, wg kroków bootu)

1. `Serial.begin(115200)`; diagnostyka RTC: `esp_reset_reason()`, liczniki `RTC_NOINIT_ATTR` (`bootCount`,
   `watchdogResetCount`, `rtcMagic`=`0xA1EC5AAD`) przeżywają miękki restart.
2. I2C Fast Mode; inwentaryzacja partycji + log heap/PSRAM; **halt gdy brak PSRAM**.
3. `initialiseDisplayPanel()` → `initialiseNativeRgbPanel()` → `initialiseBacklight()`; init GT911.
4. `lv_init()`, `lv_display_create(480,480)`, bufory DIRECT (fb0/fb1), `lv_indev_create` (EVENT).
5. `createReusableScreenRoots()` (11 ekranów), `showBootScreen()`.
6. Kroki: **(0)** `connectWiFi()` (blok. do 15 s) → **(1)** `beginNtp()`+`waitForNtp(2500)` → **(2)**
   `initialiseStorage()`, `loadSettings()`, `loadLatestEntries()` → **(3)** mutexy + cache pogody →
   **(4)** `startWebServer()`, `initOptionalServices()`.
7. **Zadania FreeRTOS** (oba na **rdzeniu 0**): `weatherTask` (4096) i `telegramTask` (8192).
   `loop()`/LVGL działa na **rdzeniu 1** (Arduino loopTask).
8. `appendBackupIfDue()`, `updateNightMode()`, timery LVGL (`agingTickCb` 30 s, `counterAlarmTickCb` 500 ms).
9. Oczekiwanie na NTP do `BOOT_TIME_WAIT_MS=12000`; `createHomeScreen()`; `enterScreensaver()`.
10. **`initWatchdog()` — NA KOŃCU** (po blokujących krokach).

### 3.3. `loop()` — pętla główna (rdzeń 1) — bardzo ważna dla wydajności

Kolejność w każdej iteracji:
1. Jeśli `otaInProgress` → tylko `ArduinoOTA.handle()` + `return`.
2. `feedWatchdog()` (reset WDT + telemetria min. heapu), `updateNightMode()`, `resyncNtpIfDue()`,
   `checkSleepNotifications()`.
3. Powrót na home po `HOME_RETURN_TIMEOUT_MS=30 s`; wygaszacz po `SCREENSAVER_TIMEOUT_MS=120 s`.
4. Pogoda: warunki odświeżenia → `requestWeatherFetch()` (notyfikuje `weatherTask`); wynik wpinany do LVGL
   TYLKO w `loop()` (gdy `weatherDataReady`).
5. `updateScreenDimming()`.
6. Serwer WWW: stop przy utracie Wi‑Fi, `startWebServer()` przy odzyskaniu, `webServer.handleClient()`.
7. `lv_tick_inc(...)` + `lv_timer_handler()` (pełny render + dotyk).
8. **Pętla gęstego próbkowania dotyku** — 4×: `delay(2)`, tick, `lv_indev_read(touchDriver)` (sam dotyk bez
   renderu) **oraz `webServer.handleClient()` między próbkami, gdy klient jest połączony**. To rozwiązuje
   „ospały serwer”: serwer Arduino potrzebuje **kilku** wywołań `handleClient()` na jedno żądanie (accept →
   nagłówki → wysyłka); jedno wywołanie na iterację (render + ~8 ms) było za rzadkie.
9. Reakcja na zmianę Wi‑Fi, reconnect co 30 s, pomiar `cpuLoadPct` (okno 5 s).

> **Model współbieżności do zapamiętania:** serwer WWW i LVGL są na **tym samym** zadaniu (rdzeń 1) i dzielą
> **deficytowy RAM wewnętrzny** z Wi‑Fi/TLS. Blokujące TLS (Telegram, host‑sync) wyniesiono na **rdzeń 0**
> (`telegramTask`). Stan współdzielony chroniony mutexami: `telegramMutex` (kolejka Telegrama, `backupState`,
> `backupFileName`), `weatherMutex` (`weatherState`). Flagi `volatile`: `weatherFetchPending`,
> `weatherDataReady`, `weatherBusyFlag`, `hostSyncPending`.

### 3.4. Watchdog i ochrona pamięci

- `WATCHDOG_TIMEOUT_S=30`. `initWatchdog()` (IDF≥5: `esp_task_wdt_config_t{trigger_panic=true}`),
  `esp_task_wdt_add(nullptr)` pilnuje TYLKO loopTask. `feedWatchdog()` resetuje WDT tylko gdy bieżący task
  jest subskrybentem (telegramTask/weatherTask NIE są — inaczej „task not found”).
- **`httpBailIfLowMemory()`**: gdy wolny RAM wewnętrzny < `HTTP_MIN_FREE_INTERNAL_B = 24 KB`, ciężkie handlery
  (`/`, `/api/status`, `/api/entries`, `/api/weight-series`) zwracają **503** zamiast ryzykować panic.
- `minFreeHeapEver` — najniższy zaobserwowany wolny heap wewnętrzny (telemetria w `/api/status`).

### 3.5. Algorytmy CSV (firmware)

- **`parseCsvLine(line, entry)`** — ręczne cięcie po przecinkach (`indexOf`), wymaga ≥3 przecinków; kol. 5/6 opcjonalne.
- **`csvDateTimeToEpoch(date, time)`** — `struct tm` + `mktime` z `tm_isdst=-1` (czas lokalny, poprawna zmiana CET/CEST).
- **`loadLatestEntries()`** — JEDEN przebieg pliku; wylicza: „ostatnie karmienie/mleko” **po NAJPÓŹNIEJSZYM
  znaczniku czasu** (`if (stamp >= lastFeedingTime)`), NIE po pozycji w pliku (bo edycja in‑place łamie
  chronologię pliku). Liczy też `todayFeedingCount`, `avgFeedingGapMin`, `longestFeedingGapMin`, `lastWeightG`,
  stan snu (`SEN_START`→`sleepInProgress`/`sleepStartedTime`; `SEN_STOP`→`lastWakeTime` tylko dla sparowanego
  START), oraz `nextFeedingEta = lastFeedingTime + COUNTER_BLINK_MIN*60` (4 h).
- **`appendEntry(type, when, ml, piersLeft=-1, piersRight=-1)`** — `FILE_APPEND`, `snprintf` (6 kol. gdy piersi
  ≥0, inaczej 4), **weryfikacja pełnego zapisu** (`written==rowLen`), `requestRgbResync()`. Po sukcesie:
  aktualizuje „ostatnie” **tylko gdy `when >= last*Time`**, `invalidateDayStats()`, `appendBackupIfDue()`,
  `archiveDataFileIfHuge()`, `queueTelegram(telegramTextFor(...))`, `requestHostSync()`.
- **`deleteEntryByIndex(entryIndex, &removed)`** — STRUMIENIOWE przepisanie `src → /karmienia.tmp` z pominięciem
  wiersza o danym indeksie (fizyczna pozycja licząca KAŻDĄ linię), `rename`, `invalidateDayStats()` + `loadLatestEntries()`.
- **`updateFeeding(feedLineIndex, when, motherMl, modifiedMl, &err)`** — edycja karmienia **W MIEJSCU** (JEDEN
  przebieg `src → /karmienia.tmp`, bez drugiego pliku tymczasowego):
  1. Wiersz KARMIENIE pod `feedLineIndex`: podmiana czasu, **minuty piersi zachowane**; TUŻ ZA nim wstawia nowe
     wiersze mleka: `MLEKO_MATKI` (gdy `motherMl>0`) i `MLEKO_MODYFIKOWANE` (gdy `modifiedMl>0`) — mieszane = oba,
     brak mleka = obie 0.
  2. WSZYSTKIE dotychczasowe wiersze `MLEKO_*` o **STARYM** czasie karmienia (w tym stare jednowierszowe
     `MLEKO_MIESZANE`) są **pomijane** (usuwane) — odtwarzane z powyższych ilości.
  3. `rename`, `invalidateDayStats()` + `loadLatestEntries()`. **Kolejność wierszy zachowana.**
  Argumenty `motherMl`/`modifiedMl` pochodzą z `milkAmountsFromArgs()` (parsuje `milkMotherMl`/`milkModifiedMl`
  albo wstecznie `milkMother`/`milkModified`+`milkMl`).
- **`refreshDayStats()`** — jeden skan całego pliku do `statsData[0..7]` (`STATS_DAY_COUNT=8`). Sen parowany
  `SEN_START→SEN_STOP`: `accrueNapCount` (drzemka tylko gdy start NIE w porze nocnej), `accrueSleepInterval`
  (dzieli sen na segmenty noc/dzień krokami do granicy godzinowej, guard 4000; otwarty sen bez STOP doliczany do
  `time(nullptr)`). `statsIndexForDay`/`dayStats` z inwalidacją przy zmianie doby. **3 kategorie mleka** liczone
  osobno (`motherMilkMl`/`modifiedMilkMl`/`mixedMilkMl`); `milkMl` = suma.
- **`archiveDataFileIfHuge()`** — po przekroczeniu `DATA_FILE_ROTATE_BYTES=256 KB` jednorazowa kopia
  `/karmienia_arch_YYYY-MM-DD.csv` + flaga `dataFileHuge` (SYGNAŁ; danych NIE usuwa).

### 3.6. Sen (algorytm „Napper”) — wspólny z panelem

Tabele progów w `config.h` (interpolacja liniowa `interpTable`, klamrowanie brzegów):
- **Okno czuwania** (min–max, minuty) wg wieku w dniach: `WAKE_WIN_AGE_DAYS = {0,28,84,150,210,330,420}`,
  `WAKE_WIN_MIN_MINUTES = {35,60,75,120,150,180,240}`, `WAKE_WIN_MAX_MINUTES = {60,90,120,180,210,240,360}`.
- **Zapotrzebowanie na sen** (minuty noc/dzień): `SLEEP_NEED_AGE_DAYS = {0,30,91,182,274,365}`,
  `SLEEP_NEED_NIGHT_MIN = {510,510,570,600,660,660}`, `SLEEP_NEED_DAY_MIN = {480,420,300,240,180,120}`.
- **Liczba drzemek** wg wieku: `NAP_TARGET_AGE_DAYS = {0,120,210,365,550}`, `NAP_TARGET_NAPS = {5,4,3,2,1}`.
- **Stany** (liczone w `handleApiStatus`): `sleepInProgress` ⇒ `"spi"`; inaczej `napStart=lastWakeTime+min`,
  `napEnd=lastWakeTime+max` → `"czuwa"` (przed napStart) / `"okno"` (napStart..napEnd) / `"przekroczone"` (po).
- **Doba snu:** `SLEEP_NIGHT_START_HOUR=21`, `SLEEP_NIGHT_END_HOUR=7` (sen w [21..24) lub [0..7) = nocny).
- **Powiadomienia** (`checkSleepNotifications`, gdy `sleepTelegramEnabled`): raz na okno — „okno drzemki” i
  „przekroczone okno”; reset flag przy nowym `lastWakeTime`; ochrona przed retro‑alarmem po restarcie
  (`STALE_MARGIN_SEC=30 min` po `napEnd`).

### 3.7. Wiek, wskazówki, ubiór, waga, pogoda

- **Wiek:** `calculateAgeDays()` = `mktime(urodziny 12:00)` vs teraz 12:00, `lround(diff/86400)`.
  `calculateAgeText()` = „X dni / Y tyg. i Z dni / N mies.”. Data urodzenia w `config.h`
  (`BIRTH_DAY=8, BIRTH_MONTH=8, BIRTH_YEAR=2026`).
- **Wskazówka rozwojowa:** `developmentTipForToday()` → `DEVELOPMENT_TIPS[calculateAgeDays()]` (0–600; powyżej — tekst domyślny).
- **Ubiór:** `dressingAdviceFor(tempC)` — pierwszy pas `DRESSING_BANDS` z `minTemp <= tempC` (od najcieplejszego).
- **Waga:** `WAGA` zapisuje **gramy** w kolumnie `ml`. `handleApiWeightSeries` → punkty `{day(=dzień życia),date,g}`;
  krzywe WHO/pasmo rysuje panel WWW. `BIRTH_WEIGHT_G=3080`.
- **Pogoda:** `fetchWeatherNow()` — HTTP (port 80, bez klucza) `api.open-meteo.com/v1/forecast`
  (current+hourly+daily, 2 dni, `timezone=Europe/Warsaw`); strumieniowy odczyt z `WEATHER_BODY_LIMIT=12288`;
  parsery `jsonNumberAfter`/`jsonArrayNumberAt`; zapis do `weatherState` pod `weatherMutex`. Cache `/pogoda.cache`
  (magic `0x57454131` "WEA1" + binarny `WeatherState`). **Uwaga:** komentarz w `config.h` mówi „wttr.in”, ale kod
  używa Open‑Meteo — rozbieżność do sprostowania.

### 3.8. Serwer HTTP urządzenia (`WebServer webServer(80)`, `startWebServer`)

Trasy i handlery (kontrakt JSON — patrz sekcja 6, wspólna z hostingiem):

| Trasa | Metoda | Handler | Uwagi |
|---|---|---|---|
| `/` | GET | `handleWebRoot` | streamuje `WEB_APP_HTML` z PROGMEM po 1024 B; `httpBailIfLowMemory()`. |
| `/api/status` | GET | `handleApiStatus` | duży JSON (reserve 2560 B), pełny snapshot + telemetria sprzętu. |
| `/api/entries?date=` | GET | `handleApiEntries` | **strumieniowo** (`CONTENT_LENGTH_UNKNOWN`), `lineIndex` fizyczny. |
| `/api/weight-series` | GET | `handleApiWeightSeries` | strumieniowo; tylko `WAGA`. |
| `/api/entry` | POST | `handleApiEntry` | dodanie wpisu (mleko/waga/zdarzenie/karmienie+extraMilk). |
| `/api/delete-entry` | POST | `handleApiDeleteEntry` | `line`=lineIndex; po sukcesie `requestHostSync()`. |
| `/api/update-feeding` | POST | `handleApiUpdateFeeding` | edycja karmienia in‑place; `requestHostSync()`. |
| `/api/event` | POST | `handleApiEvent` | pieluchy/wit.D/odciąganie/waga/sen; opcjonalne `when`. |
| `/api/send-backup` | POST | `handleApiSendBackup` | planuje wysyłkę backupu CSV na Telegram. |
| `/api/import` | POST | `handleApiImport` | body text/csv ≤512 KB; backup + sanityzacja strumieniowa + `rename`. |
| `/api/setting` | POST | `handleApiSetting` | `key=sleepTelegram`, `value`0/1. |
| `/export.csv` | GET | `handleExportCsv` | streaming pliku, `attachment`. |
| (404) | – | `handleWebNotFound` | JSON dla `/api/`, tekst inaczej. |

Helpery: `sendJson(status, payload)` (`Cache-Control: no-store`), `jsonEscape` (`\ " \n \r \t`),
`webDateTime` (`%Y-%m-%dT%H:%M`), **`parseWebDateTime`** (ścisła walidacja 16‑znakowego ISO `YYYY-MM-DDTHH:MM`,
`mktime` isdst=-1, **odrzuca daty < 2025‑01‑01** czyli ts<1735689600, weryfikuje round‑trip przez `localtime_r`).

### 3.9. Synchronizacja z hostingiem (`FEATURE_HOST_SYNC`, domyślnie 1)

- **`requestHostSync()`** — ustawia `hostSyncPending=true` + `wakeTelegramTask()` (bezpieczne z każdego rdzenia).
  Wołane z `appendEntry`, `handleApiDeleteEntry`, `handleApiUpdateFeeding`, importu.
- **`uploadCsvToHost()`** (w `telegramTask`, rdzeń 0) — parsuje `PANEL_UPLOAD_URL` (musi być `https://`),
  `WiFiClientSecure` `setInsecure()`, POST **multipart/form-data** pole `file` (filename `karmienia.csv`),
  streaming pliku po 512 B (`requestRgbResync` co 8 chunków). Nagłówek `X-Upload-Token` dodawany **tylko gdy
  `PANEL_UPLOAD_TOKEN` niepusty** (obecnie PUSTY = bez tokena — dług). Sukces przy kodzie 200/201.
- **Debounce/łączenie serii:** wysyłka gdy `millis()>=hostSyncNextMs` i `(millis()-lastTelegramTlsMs)>=TLS_COOLDOWN_MS(3000)`;
  po sukcesie `hostSyncNextMs=millis()+HOST_SYNC_MIN_INTERVAL_MS(15000)`; po błędzie retry `HOST_SYNC_RETRY_MS(30000)`.
  Cooldown TLS zapobiega błędowi „esp-aes: Failed to allocate memory” przy drugim handshake (fragmentacja heapu po
  wcześniejszym TLS Telegrama).
- Endpoint docelowy: `https://phpmapy1.webd.pro/api/upload-data`.

### 3.10. Integracje: Telegram / OTA / mDNS / NTP

- **Telegram** (`telegramTask`, rdzeń 0): `queueTelegram` (kolejka 1‑elementowa pod mutexem),
  `telegramTextFor` (teksty per typ), `pumpTelegramQueue` (priorytet backup > wiadomość; TLS bez trzymania
  mutexa; retry 30 s; po `TELEGRAM_MAX_FAILS=5` porzuca wpis). Backup CSV: `sendBackupViaTelegram` (`sendDocument`
  multipart, streaming). `appendBackupIfDue` — raz dziennie kopia `/karmienia_backup.csv` + planowana wysyłka.
- **OTA** (`FEATURE_OTA`): tylko gdy `OTA_PASSWORD` niepuste; hostname „karmienie”; `otaInProgress` wstrzymuje UI.
  Brak partycji OTA — OTA nadpisuje `factory`.
- **mDNS** (`FEATURE_MDNS`): `karmienie.local` (http/tcp/80).
- **NTP:** `beginNtp` (`configTzTime(TIMEZONE_RULE, pool.ntp.org, time.google.com, time.cloudflare.com)`),
  `resyncNtpIfDue` co `NTP_RESYNC_INTERVAL_MS=6 h` (nieblokująco).

### 3.11. Ustawienia trwałe

- Plik `/ustawienia.cfg` (`SETTINGS_FILE`), format `klucz=wartość`. Obecnie jeden klucz: `sleepTelegram` (0/1).
  `saveSettings`/`loadSettings`.

---

## 4. WARSTWA 2 — Strona WWW serwowana przez urządzenie (`web_panel.h`)

- Cały interfejs (HTML+CSS+JS) jest zaszyty jako **jeden `PROGMEM` raw string**:
  `const char WEB_APP_HTML[] PROGMEM = R"WEBPANEL( ... )WEBPANEL";`. Serwowany przez `handleWebRoot` (streaming 1 KB).
- To **samodzielny SPA** działający offline (bez CDN). Konsumuje ten sam kontrakt API co panel hostingu:
  `GET /api/status` (polling co 10 s), `GET /api/entries?date=`, `POST /api/entry|delete-entry|update-feeding|event|setting`,
  `GET /api/weight-series`, `GET /export.csv`, `POST /api/import`.
- **Stan JS** (`state`): m.in. `data`, `activeDay`, `detailLabel`, `milkMother`, `milkModified`, `bottleOpen`,
  `pumpMode`, oraz tryb edycji: **`editFeedLine`** (lineIndex edytowanego karmienia lub `null`), **`editMilkHad`**.
- **Kluczowe funkcje JS** (bliźniacze do panelu, patrz sekcja 5.5):
  - render ekranu głównego (zegar, karty OSTATNIE KARMIENIE / OSTATNIA BUTELKA, pasek licznika, przebieg dnia),
  - `openDay(date,label)` — renderuje dziennik dnia; przy KARMIENIE dodaje przycisk edycji (✎), który znajduje
    parę mleka (`entries.find(x => x.time===e.time && x.type.startsWith('MLEKO'))`) i woła `openEditFeeding(feed, milk)`,
  - `openForm` / `openEditFeeding` / `openPumping`, `toggleMilkKind` / `refreshMilkKindButtons`, `updateBottle`,
  - obsługa `#milkRemoveBtn` (data‑action `milk-remove`) — zwija butelkę i czyści rodzaje (⇒ przy zapisie `milkRemove=1`),
  - submit formularza: gdy `state.editFeedLine != null` → `POST /api/update-feeding` (`feedLine`, `when`, oraz
    `milkMl`/`milkMother`/`milkModified` albo `milkRemove=1`), po zapisie `openDay(...)` (odświeża lineIndexy).
- **Świadoma decyzja:** edycji karmienia **nie ma na fizycznym ekranie LVGL** — tylko w interfejsach WWW (strona
  urządzenia + panel hostingu).

---

## 5. WARSTWA 3 — Panel hostingu (`web-php/`)

Architektura warstwowa: **UI (`ui/`) → API JSON (`engine/api.php`) → Repository (CSV lub MySQL) → Domain
(`engine/Domain.php`)**. Ten sam format CSV i te same reguły domenowe co firmware.

### 5.1. `bootstrap.php` — ładowanie i fabryka

- `require_once`: `config.php`, `Domain.php`, `Repository.php`, `CsvRepository.php`, `MysqlRepository.php`
  (brak autoloadera — jawne wymagania).
- **`make_repository(): Repository`** — gdy `Config::storageDriver()==='mysql'` próbuje `new MysqlRepository()`;
  przy **dowolnym `Throwable`** loguje `error_log` i **wraca do `new CsvRepository()`** (fallback — panel nigdy nie
  pada). W przeciwnym razie CSV.

### 5.2. `config.php` — `final class Config`

- `date_default_timezone_set('Europe/Warsaw')`.
- Stałe **identyczne z firmware**: `BIRTH_*`, `BIRTH_WEIGHT_G=3080`, `ML_MIN/MAX/DEFAULT_ML`,
  `MILK_ML_MIN/MAX/STEP/DEFAULT`, `WEIGHT_MIN_G/MAX_G/DEFAULT_WEIGHT_G`, `COUNTER_WARN_MIN=180`,
  `COUNTER_BLINK_MIN=240`, tablice snu/Napper (`WAKE_WIN_*`, `SLEEP_NEED_*`, `NAP_TARGET_*`),
  `SLEEP_NIGHT_START_HOUR=21`/`END=7`, `NIGHT_START_HOUR=21`/`END=7`,
  `CSV_HEADER='data,godzina,typ,ml,piers_lewa_min,piers_prawa_min'`.
- Metody (konfiguracja przez zmienne środowiskowe): `storageDriver()` (`STORAGE_DRIVER`, domyślnie `csv`),
  `corsOrigin()` (`CORS_ORIGIN`, domyślnie pusty = same‑origin), `mysql()` (`DB_*`, tabele `entries`/`settings`,
  `utf8mb4`), ścieżki `dataDir()/dataFile()/backupFile()/settingsFile()`, `timestampedBackupFile()`
  (`Y-m-d-H-i-s.bakap` obok pliku danych), **`uploadToken()`** (`UPLOAD_TOKEN`, domyślnie pusty ⇒ upload otwarty).

### 5.3. `interface Repository` (Repository.php) — kontrakt magazynu

```php
allEntries(): array                       // [{date,time,type,ml,piersLeft,piersRight,lineIndex}, ...] chronologicznie
append(type, DateTimeImmutable $when, ml, piersLeft=-1, piersRight=-1): bool
deleteByIndex(int $lineIndex): array       // ['ok'=>bool, 'removed'=>string]
updateFeeding(int $feedLineIndex, DateTimeImmutable $when, int $motherMl, int $modifiedMl): array // ['ok','message'] — mieszane = obie >0 (dwa wiersze); obie 0 = usuń mleko
importCsv(string $rawText): array          // ['ok','imported','skipped']
replaceRawCsv(string $rawText): array      // ['ok','backup','lines','message']
rawCsv(): string
loadSettings(): array; saveSettings(array): void
```

### 5.4. Implementacje Repository

**`CsvRepository`** (magazyn append‑only na pliku, domyślny):
- `allEntries()` — czyta plik, zdejmuje nagłówek, `lineIndex=$idx` (fizyczny, dla KAŻDEJ linii).
- `append()` — `Domain::toCsvRow(...)` z `FILE_APPEND|LOCK_EX`.
- `deleteByIndex()` — przepisuje plik pomijając wiersz o indeksie; zwraca opis (`describeCsvEntry`).
- **`updateFeeding($feedLineIndex,$when,$motherMl,$modifiedMl)`** (edycja **W MIEJSCU**): usuwa WSZYSTKIE sparowane
  wiersze `MLEKO_*` o starym czasie (w tym stare `MLEKO_MIESZANE`), przepisuje KARMIENIE na nowy czas (piersi
  zachowane) i wstawia nowe wiersze `MLEKO_MATKI`/`MLEKO_MODYFIKOWANE` (wg ilości >0) tuż za karmieniem. Poniższy
  akapit opisuje starszą (jednowierszową) wersję — obecnie zastąpioną modelem dwóch wierszy:
- (hist.) dawniej `updateFeeding()` lokalizowało KARMIENIE po `feedLineIndex`; znajdowało sparowane
  `MLEKO_*` po **starym** `date`+`time`; buduje nowy wiersz karmienia (piersi zachowane) i mleka; przepisuje plik
  zachowując pozycje — mleko podmienione/usunięte, a przy braku pary wstawione **tuż za** karmieniem.
- `replaceRawCsv()` — waliduje (nagłówek + ≥1 poprawny wiersz), robi kopię `.bakap`, podmienia dane (znormalizowane).
- `loadSettings/saveSettings` — `ustawienia.cfg`.

**`MysqlRepository`** (źródło prawdy = baza; CSV = **zawsze** równoległy backup):
- Konstruktor: PDO (`ERRMODE_EXCEPTION`, `FETCH_ASSOC`, `EMULATE_PREPARES=false`) + wewnętrzny `CsvRepository`.
- `allEntries()` — `SELECT ... ORDER BY entry_date, entry_time, id`; `id`→`lineIndex`, czas obcinany do `HH:MM`.
- **Po KAŻDYM zapisie** (`append/deleteByIndex/updateFeeding/importCsv`) → **`rewriteCsvBackup()`** (zrzut całej bazy
  do CSV — wierne lustro, kolejność wg `date,time,id`).
- **`updateFeeding()`** — transakcja: `UPDATE` czasu karmienia po `id`; mleko: `null`⇒`DELETE`, istnieje⇒`UPDATE`
  (typ/ml/czas), brak⇒`INSERT`; `commit`/`rollBack`.
- `schema.sql`: tabela **`entries`** (`id BIGINT UNSIGNED AI PK, entry_date DATE, entry_time TIME, type VARCHAR(32),
  ml INT, piers_left INT, piers_right INT, created_at TIMESTAMP`, indeksy `idx_date`, `idx_date_time`); tabela
  **`settings`** (`key VARCHAR PK, value VARCHAR`). InnoDB/utf8mb4.

**Kiedy który:** decyduje `Config::storageDriver()` (domyślnie CSV). MySQL wymaga env + tabel; awaria ⇒ fallback do CSV.

### 5.5. `Domain.php` — `final class Domain` (czysta logika)

Operuje na tablicach wpisów (bez magazynu). `STATS_DAY_COUNT=8`. Kluczowe metody:
- `isMilkType($t)`, `milkTypeLabel($t)`.
- `parseCsvLine`, `toCsvRow` (6 kol. gdy piersi ≥0), `describeCsvEntry`, `formatEntryForUi`.
- Czas: `dateIso`, `webDateTime` (`Y-m-d\TH:i`), `formatDateTime` (`d.m.Y H:i`), `csvDateTimeToDate`,
  **`parseWebDateTime`** (ścisła walidacja, `checkdate`, odrzuca daty <2025 — jak firmware).
- Wiek: `calculateAgeDays`, `calculateAgeText`, `developmentTipForToday`, `dayOfLifeForDate`.
- Napper: `interpTable`, `wakeWindowMinutes`, `sleepNeedMinutes`, `napTargetCount`, `sleepHourIsNight`.
- `loadLatestEntries($entries,$now)` — ostatnie karmienie/mleko/waga, stan snu, `avgFeedingGapMin`,
  `longestFeedingGapMin`, `todayFeedingCount`, `nextFeedingEta` (=last+`COUNTER_BLINK_MIN`).
- `buildDayStats($entries,$now)` — agregacja 8 dni; sen godzina‑po‑godzinie (`accrueSleepInterval`, guard 4000),
  drzemki (`accrueNapCount`, tylko start w dzień); **3 kategorie mleka** osobno.
- `entriesForDate` (dodaje `label` + `lineIndex`), `weightSeries` (`{day,date,g}` z `WAGA`),
  `calendarDayTitle` („DZISIAJ/WCZORAJ/N DNI TEMU - dd.mm.RRRR”), `formatAgoText`.

> **Uwaga o zgodności firmware↔PHP:** `Domain::loadLatestEntries` w PHP przypisuje „ostatnie” per iteracja
> (zakłada chronologię listy). Firmware wybiera po `stamp >= last*Time`. Przy MySQL lista jest posortowana po
> `date,time,id`, więc „ostatnie” = najpóźniejsze; przy CSV panelu warto to zweryfikować, jeśli edycje in‑place
> mieszają kolejność (potencjalne miejsce do ujednolicenia z firmware).

### 5.6. Frontend (`ui/app.js` + `index.html`)

- **State** globalny: `{data, page, view, activeDay, detailLabel, milkMother:true, milkModified:false, bottleOpen,
  pumpMode, summaryExtra, statView:'summary', histPeriod:'week', editFeedLine:null, editMilkHad:false}`.
  `MODALS=['formModal','otherModal','diaperModal']`, `PAGES=['start','diary','stats','weight','sleep']`.
  Tablice WHO (`WHO_P3/MED/P97`) do wykresu wagi.
- **`request(url,options)`** — `fetch`+`json`, rzuca `Error(data.message)` przy `!r.ok`. Polling `refresh()` →
  `GET /api/status` co 10 s. POST‑y: `x-www-form-urlencoded` (poza `upload-data` → **`FormData` multipart**).
- **Nawigacja:** `navTo(page)`, `renderStatView(view)` (summary/charts/rhythm/history), modale `show/openFormModal/closeFormModal/clearPanels`.
- **Render główny `render(data)`** — zegar, pierścień licznika (frac=`min/240`), pasek dnia (`countUp`), motyw
  dzień/noc (`applyTheme`, localStorage `lesny-theme`), status, `renderDayBand`, `renderDiag`.
- **`buildTimeline(entries,onChanged)`** — oś czasu: paruje SEN_START/STOP w pasma, karmienia z odstępem „po Xh Ymin”,
  przy KARMIENIE przycisk edycji (`.tl-edit`) → szuka pary mleka po tym samym czasie → `openEditFeeding`; przycisk
  usuwania → `/api/delete-entry`.
- **`openDay(date,label)`** — `GET /api/entries?date=`, agregacja + hero/bento/łuk doby/timeline.
- **`openForm` / `openEditFeeding(feed,milk)` / `openPumping`** — tryby formularza; edycja: `editFeedLine=feed.lineIndex`,
  ukrywa pole piersi (niezmieniane), rozbija typ mleka na 2 checkboxy, `milkRemoveBtn` gdy mleko było, `bottleOpen=!!milk`.
- **`toggleMilkKind`/`refreshMilkKindButtons`** — 2 niezależne checkboxy (oba ⇒ Mieszane).
- **submit** — edycja⇒`/api/update-feeding` (`feedLine,when` + `milkMl/milkMother/milkModified` albo `milkRemove=1`);
  `pumpMode`⇒`/api/entry` `ODCIAGANIE`; inaczej `/api/entry` `KARMIENIE` (`ml=0` + `lewaMin/prawaMin` + opcjonalne mleko).
- **Wykresy:** `renderChart(cal)` (słupki mleka stackowane mother/mixed/modified + tabela), `renderGaps`,
  `renderAnalysis` (rytm 3 dni), `renderHistory(period)` (week/month/year z `/export.csv`).
- **Waga:** `saveWeight` (`/api/event` WAGA, 2000–15000), `renderWeightChart` (SVG: pasma WHO `expectedWeightBand`,
  pasmo przyrostu `dischargeGainBand`).
- **Sen:** `renderSleep` (spi/czuwa/okno/przekroczone), toggle → `/api/event` `SEN_START`/`SEN_STOP`.
- **Import/Upload:** `importFile` → `/api/import` (text/csv); `uploadDataFile` → `/api/upload-data` **multipart** (obejście WAF).

### 5.7. `index.php` (front controller) + `paths.php`

- **`index.php`** — rozpoznaje trasę regexem (`.../export.csv` lub `.../api/([a-z0-9\-]+)`); dla API `require engine/api.php`
  + `handle_api($route,$method,make_repository())`; statyki serwuje z `ui/`; `/`,`/index.php`,`/index.html` → `index.html`.
- **`paths.php`** — `PANEL_ENGINE_DIR`, `PANEL_DATA_DIR`. Domyślnie układ lokalny; zakomentowany wariant cPanel/webd
  przenosi `engine/` i `data/` **poza** `public_html`.

### 5.8. Bezpieczeństwo panelu (`.htaccess`)

- `data/.htaccess` — `Require all denied` (blokada bezpośredniego dostępu WWW do danych).
- `engine/.htaccess` — blokada `*.php/*.sql`, ale `api.php` `granted` (jedyny punkt wejścia dla urządzenia).
- `ui/.htaccess` — `mod_rewrite` → `index.php`, `Options -Indexes`, `paths.php` `denied`.
- Upload jako **multipart** (nie raw `text/csv`) — obejście WAF (raw body POST bywa blokowane 403). Backend
  akceptuje oba źródła (`$_FILES['file']` lub `php://input`), limit 512 KB.

---

## 6. Kontrakt API (wspólny: firmware ↔ hosting)

Obie warstwy (serwer urządzenia i `engine/api.php`) implementują ten sam zestaw. Format czasu wejściowego:
`when` = `YYYY-MM-DDTHH:MM` (ścisła walidacja). Odpowiedzi: JSON, `Cache-Control: no-store`.

| Endpoint | Metoda | Wejście (kluczowe) | Odpowiedź / uwagi |
|---|---|---|---|
| `/api/status` | GET | – | pełny snapshot: `now/nowIso/ip/age/developmentTip/developmentDay`, `lastFeeding/lastMilk` + `lastFeedingAgo`/`lastFeedingAgeMin`, `avgFeedingGapMin`, `nextFeedingIso`, `longestFeedingGapMin`, blok snu (`sleepState`∈`brak/spi/czuwa/okno/przekroczone`, okna/potrzeby/`napCount/napTarget`, `sleepTelegram`), limity `minMl/maxMl/defaultMl/milkMinMl/milkMaxMl/milkStepMl/milkDefaultMl/birthWeightG/lastWeightG`, `calendar[5]` (per dzień: `date,label,feedingCount,milkMl,motherMilkMl,modifiedMilkMl,mixedMilkMl,piersLeftMin,piersRightMin,diaperWet,diaperDirty,pumpingMl,vitaminD,weightG`), `night`, `mdns`, `undoWindowSec=60`, telemetria (`freeHeap`... na hostingu = 0). |
| `/api/entries?date=YYYY-MM-DD` | GET | `date` | `{date, entries:[{time,type,label,ml,piersLeftMin,piersRightMin,lineIndex}]}`. |
| `/api/weight-series` | GET | – | `{birthWeightG, points:[{day,date,g}]}`. |
| `/export.csv` | GET | – | surowy CSV (`attachment`). |
| `/api/entry` | POST | `type,when,ml` (+`extraMilk`, `milkMotherMl`,`milkModifiedMl`; wstecznie `milkMl,milkMother,milkModified,milkType`; `lewaMin,prawaMin`) | dodanie wpisu; walidacja zakresów; `KARMIENIE` musi mieć `ml=0`, opcjonalne mleko: `milkMotherMl>0`⇒wiersz `MLEKO_MATKI`, `milkModifiedMl>0`⇒`MLEKO_MODYFIKOWANE` (oba ⇒ dwa wiersze = mieszane). 201/400/409/500/503. |
| `/api/delete-entry` | POST | `line` (=lineIndex) | `{message, removed}` / 400. |
| `/api/update-feeding` | POST | `feedLine, when`, oraz `milkMotherMl`+`milkModifiedMl` (osobne ilości; wstecznie `milkMother/milkModified/milkMl`) **albo** `milkRemove=1` | edycja karmienia in‑place (mleko + godzina; piersi bez zmian). Mieszane = obie ilości >0 (dwa wiersze). Obie 0 i brak `milkRemove` ⇒ 400. 200/400. |
| `/api/event` | POST | `type`∈`{PIELUCHA_MOKRA,PIELUCHA_BRUDNA,WITAMINA_D,ODCIAGANIE,WAGA,SEN_START,SEN_STOP}`, opc. `when,ml` | szybkie zdarzenia; WAGA/ODCIAGANIE walidowane; WITAMINA_D idempotentna w dniu. 201. |
| `/api/import` | POST | body `text/csv` ≤512 KB | zastąpienie danych + backup. `{message}` z liczbą imported/skipped. |
| `/api/upload-data` | POST | multipart `file` (lub raw), opc. `X-Upload-Token` | (hosting) kopia `.bakap` + podmiana; token wymagany tylko gdy `UPLOAD_TOKEN` niepusty. |
| `/api/setting` | POST | `key=sleepTelegram, value=0/1` | zapis ustawienia. |
| `/api/send-backup` | POST | – | (urządzenie) planuje wysyłkę backupu na Telegram; (hosting) stub 400. |

---

## 7. Przepływy end‑to‑end (najważniejsze scenariusze)

1. **Dodanie karmienia z butelką (na urządzeniu):** UI → `POST /api/entry` (`KARMIENIE`, `ml=0`, `extraMilk=1`,
   `milkMotherMl`/`milkModifiedMl`, `lewaMin/prawaMin`) → `appendEntry(KARMIENIE...)` + `appendEntry(MLEKO_MATKI...)`
   i/lub `appendEntry(MLEKO_MODYFIKOWANE...)` (mleko mieszane = dwa wiersze mleka, ta sama godzina) →
   `invalidateDayStats` + `loadLatestEntries` + `queueTelegram` + `requestHostSync` → `telegramTask` wysyła CSV na
   hosting (`/api/upload-data`) → hosting robi `.bakap` i podmienia dane.
2. **Edycja karmienia (mleko + godzina):** UI (strona urządzenia lub panel) → `openEditFeeding(feed, milks[])`
   (prefill z WSZYSTKICH sparowanych wierszy mleka; mieszane → dwa pola ilości z popoverem) → submit
   `POST /api/update-feeding` (`milkMotherMl`/`milkModifiedMl` albo `milkRemove=1`) → `updateFeeding()` (in‑place:
   usuwa wszystkie stare wiersze mleka po starym czasie, wstawia nowe wg ilości tuż za karmieniem) →
   `loadLatestEntries` → `requestHostSync`. UI odświeża `openDay` (świeże `lineIndex`).
3. **Sen:** przycisk „Zasnął/Obudził się” → `POST /api/event` `SEN_START`/`SEN_STOP`. `refreshDayStats`/`buildDayStats`
   parują interwały, `handleApiStatus` liczy stan (`spi/czuwa/okno/przekroczone`) z okna czuwania Napper.
4. **Waga:** `POST /api/event` `WAGA` (gramy) → wykres z `GET /api/weight-series` na tle WHO + pasmo przyrostu od wagi wypisowej.

---

## 8. Znane długi / TODO (bezpieczeństwo i spójność)

1. **Token uploadu** — `/api/upload-data` działa **bez tokena** (`PANEL_UPLOAD_TOKEN` i `UPLOAD_TOKEN` puste;
   rozwiązanie testowe). Każdy znający URL może nadpisać dane hostingu. Przed produkcją: ustawić wspólny sekret.
2. **Token Telegrama jawny w repo** — `TELEGRAM_BOT_TOKEN`/`TELEGRAM_CHAT_ID` w `config.h` w **publicznym** repo.
   Zalecane: zrewokować bota u @BotFather i przenieść sekrety poza repo.
3. **Firmware nie jest kompilowany w środowisku pracy** — zmiany firmware wymagają weryfikacji na fizycznym
   urządzeniu (log serial: `LCD: wolny RAM wewnetrzny po init panelu`, panel „RAM %”).
4. **Rozbieżność dokumentacyjna:** `config.h` nazywa źródło pogody „wttr.in”, kod używa **Open‑Meteo**.
5. **`Domain::loadLatestEntries` (PHP)** przypisuje „ostatnie” per iteracja (nie po max `stamp`) — do rozważenia
   ujednolicenie z firmware, jeśli edycje in‑place mieszają kolejność w CSV panelu.

---

## 9. Reguły pracy nad projektem (konwencje ustalone z użytkownikiem)

- **Gałąź robocza:** `v3`. **Nie mergować** do `main` bez wyraźnego polecenia.
- **Zmiany tylko warstwy wizualnej** wykonuj wyłącznie, gdy tak ustalono; nie ruszaj logiki „przy okazji”.
- **Mleko mieszane = DWA osobne wiersze** `MLEKO_MATKI` + `MLEKO_MODYFIKOWANE` (osobne ilości), wybierane w UI przez
  dwa pola z popoverem gdy zaznaczono oba rodzaje. Stary jednowierszowy typ `MLEKO_MIESZANE` NIE jest już
  zapisywany przez API, ale MUSI pozostać obsługiwany przy odczycie/statystykach/wykresach (dane historyczne) —
  nie usuwać go z `isMilkType`/`milkTypeLabel`/agregacji (`mixedMilkMl`). Ekran LVGL urządzenia pozostaje bez zmian.
- **Zakres mleka (butelka):** suwak 20–200, krok 10, domyślnie 60. **Odciąganie** ma osobny, niezmienny zakres (10–120, dom. 30).
- **Edycja karmienia:** in‑place (bez zmiany kolejności wierszy CSV); minuty piersi zachowane; edycji **nie ma** na
  fizycznym ekranie LVGL (świadomie) — tylko strona WWW urządzenia + panel hostingu.
- **Firmware:** nie kompiluje się w tym środowisku — weryfikuje użytkownik na sprzęcie. Po edycjach `.ino`
  sprawdzaj statycznie (m.in. balans nawiasów; uwaga: istnieje **preexisting** nadwyżka `}` o 1 wynikająca z `}`
  w literałach JSON w stringach — to nie jest błąd).
- **Panel PHP:** waliduj `php -l` (engine) i `node --check` (`ui/app.js`) przed commitem.
