# ANALIZA_V5 — co robi obecny kod i jak napisać wersję v5

Dokument jest bazą do przepisania firmware od nowa. Gałąź: `v5`, utworzona z `v4.1-claude-pazdziernik` (commit `a4f432c`).

**Wymagania v5 (ustalone 20.10.2026):**
- Urządzenie **tylko wyświetla** dane. Nie zapisuje niczego na hosting i nie ma żadnych formularzy wpisów.
- Zachowana **cała obecna funkcjonalność widoczna dla użytkownika**: ekran startowy, tablica (wygaszacz), panel z 6 kartami, diagnostyka, pogoda, Telegram (start, okno drzemki, codzienna kopia).
- Usunięcie bezużytecznego kodu i optymalizacja. Najważniejszy cel techniczny: **brak przesuwania obrazu**.
- Panel WWW (`web-php/`) pozostaje źródłem danych. Zmiany po stronie hostingu są opcjonalne (sekcja 9).

Powiązana notatka: `ulepszenia19-10-26.md` (przegląd błędów i ryzyk).

---

## 1. Diagnoza ze zdjęcia z 20.10.2026 (17:34)

1. **Obraz przesunięty pionowo z zawinięciem.** Góra tablicy (zegar) jest ucięta, a na dole ekranu znów widać zegar „17:34”. To klasyczne rozjechanie buforów bounce (wielokrotność 80 linii). Mechanizmy v4.1 (twarda resynchronizacja, detektor fazy) nie przywróciły obrazu, albo detektor jest „NIEDOSTEPNY” w tej wersji rdzenia, albo przesunięcie nastąpiło bez wykrywalnej zmiany fazy.

   Wniosek dla v5: **nie łatać skutków, tylko usunąć przyczynę**.
   - Zero operacji na flashu (zapis **i** odczyt przez LittleFS/NVS) w normalnej pracy.
   - Opcjonalnie budowanie z własnym `sdkconfig` (sekcja 7.4), co rozwiązuje problem u źródła.
2. **„Śpi od –”.** Ostatnim zdarzeniem snu w danych jest `SEN_START` z czasem w przyszłości, dopisany przez auto-sen panelu WWW (T+60 min po karmieniu). Firmware uznaje, że dziecko śpi, a czas snu wychodzi ujemny, więc zaokrągla do 0 i wyświetla „–”.

   W v5: zdarzenia snu z czasem > teraz+1 min są ignorowane, a sesje sortowane po czasie (sekcja 4.7). Docelowo warto też wyłączyć auto-sen w PHP (`ulepszenia19-10-26.md`, pkt 7.2).

---

## 2. Inwentarz obecnego kodu i decyzja dla v5

| Plik | Linie | Co robi | v5 |
|---|---|---|---|
| `v3.ino` | 6432 | wszystko: sprzęt, LVGL, Wi-Fi/NTP, LittleFS i CSV, statystyki, stare ekrany i formularze, Telegram, pogoda, sync z hostingiem, start, pętla | **przepisać** na moduły (sekcja 7) |
| `ui_modern.h` | 1388 | nowy UI: motyw, ekran startowy, tablica, panel (6 kart), wykresy | **zostaje** (drobne poprawki), to najlepsza część kodu |
| `ui_fonts.h` | 8494 (≈700 KB źródła) | fonty `ui_f13/15/18/24/32/48/80` z polskimi znakami (generator `tools/lvfont.py`) | zostaje |
| `ui_assets.h` | 32152 (≈2,3 MB źródła) | ikony duotone `ic_*_{s,m,l}_{fill,line}`, sprite'y `sp_*`, maski `mk_*` (generator `tools/genassets.py`) | zostaje; usunąć nieużywane warianty (np. `_l`, `chart`, `calendar`, `back`, `drop`, `zzz`, `sun_s`, `glow_f1..3`, `hill`) po sprawdzeniu |
| `config.h` | 141 | stałe: urodziny, zakresy, sen (Napper), noc, pogoda, Telegram, hosting | **przepisać**: tylko stałe wyświetlacza; sekrety do `secrets.h` |
| `secrets.h` | 6 | SSID/hasło Wi-Fi (w repo!) | poza repo + `secrets.h.example` |
| `development_tips.h` | 608 | 601 wskazówek „dzień życia” bez polskich znaków | zostaje, ale z polskimi znakami (źródło: `web-php/engine/development_tips.php`) |
| `dressing_guide.h` | 35 | progi ubioru (bez PL znaków) | scalić z `UI_DRESS_PL` z `v3.ino` w jedną tablicę z PL znakami |
| `lv_conf.h` | 1416 | konfiguracja LVGL 9.3 | zostaje (przegląd: wyłączyć nieużywane widgety i fonty Montserrat) |
| `web_panel.h` | 466 (84 KB) | stara strona WWW serwowana z urządzenia | **usunąć** z firmware |
| `web_server_disabled.h` | 872 | wyłączony serwer WWW, mDNS, OTA | **usunąć** |
| `partitions.csv` | — | app 5 MB, LittleFS ~11 MB | zmienić: app większa, mała partycja danych (sekcja 7.6) |
| `tools/sim/*` | — | symulator PC UI + fragmentów firmware | **zostaje i rośnie**: v5 ma być testowalny w całości na PC |

**Szacunek:** z ~7800 linii własnego kodu (`v3.ino` + `ui_modern.h` + nagłówki bez fontów i grafik) zostanie ~3000–3500, czyli mniej więcej o połowę mniej.

---

## 3. Co jest w `v3.ino` — mapa funkcji

Martwe funkcje i UI to około 40% pliku.

| Obszar | Funkcje | Status w v4.1 | v5 |
|---|---|---|---|
| Sprzęt i panel RGB | `initialiseDisplayPanel`, `initialiseNativeRgbPanel`, `displayFlush`, `rgbVsyncCallback`, `rgbFrameDoneCallback`, `RgbCbCompat`, `requestRgbResync`, `rgbHardResync*`, `FlashWriteGuard`, `initialiseBacklight`, `setScreenDimmed`, `updateScreenDimming`, `touchRead` | używane | **przenieść** do `display.cpp`/`touch.cpp`; `FlashWriteGuard` niepotrzebny przy zerowych zapisach |
| Wi-Fi / NTP | `connectWiFi`, `retryWiFiConnection`, `beginNtp`, `waitForNtp`, `syncTimeFromNTP`, `resyncNtpIfDue`, `currentLocalTime` | używane | uprościć (`resyncNtpIfDue` zbędne — SNTP sam resynchronizuje) |
| LittleFS i CSV na flashu | `initialiseStorage`, `LineReader`, `parseCsvLine`, `csvDateTimeToEpoch`, `loadLatestEntries`, `recomputeFeedingRhythm`, `refreshDayStats`, `dayStats`, `accrue*`, `entriesForDay`, `populateDayEntries`, `archiveDataFileIfHuge`, `copyLittleFsFile`, `appendBackupIfDue` | używane (wielokrotne skany pliku) | **zastąpić** jednym parserem do PSRAM i jedną funkcją statystyk (sekcja 7.3) |
| Zapis danych | `appendEntry`, `deleteEntryByIndex`, `updateFeeding`, `applyAutoSleepForFeeding`, `uploadCsvToHost`, `requestHostSync` | ekran „INNE” (pielucha, odciąg) nadal zapisuje | **usunąć całkowicie** |
| Stare ekrany LVGL | formularz, kalendarz, dziennik dnia, podsumowanie z usuwaniem, odciąganie, pielucha, INNE, SEN, WAGA, kafelki `createHome*Tile`, `drawWeatherIcon`, `weatherDescPL`, `applyTheme`/`COLOR_*`, `createCard`/`createButton`/`createLabel`, `reusable screens` | nieosiągalne (poza INNE i DIAGNOSTYKA) | **usunąć**; DIAGNOSTYKĘ napisać w stylu `ui_modern.h` |
| Model nowego UI | `uiFillLive`, `uiFillCharts`, `uiParseRow`, `uiRowEpoch`, `uiEntryLine`, `uiDressPl`, `uiDaysFromCivil`, `updateHomeInformation`, `createHomeScreen`, `uiShowSaver`, `enter/exitScreensaver` | używane | **przepisać** jako czyste funkcje `Store → UiModel` (testowalne na PC) |
| Pogoda | `fetchWeatherNow`, `weatherTask`, `jsonNumberAfter`, `jsonArrayNumberAt`, `weatherKindOf`, `saveWeatherCache`(flash co 30 min!), `loadWeatherCache` | używane | zostaje, ale cache w RAM RTC zamiast flasha i parser JSON pewniejszy |
| Hosting (odczyt) | `fetchRevision`, `downloadCsvFromHost`, `csvBufferLooksValid`, `fileEqualsBuffer`, `csvFileLooksValid`, `ramCsvAppend`, `syncOnBoot`, `applyHostMirrorUpdate` | używane | zostaje logika odczytu; **bez zapisu mirrora na flash** (albo rzadko, decyzja D2) |
| Telegram | `queueTelegram`, `pumpTelegramQueue`, `telegramTask`, `telegramTextFor`, `telegramUrlEncode`, `sendBackupViaTelegram`, `queueTelegramStartup`, `checkSleepNotifications` | używane | zostaje: start, okno drzemki, codzienna kopia (z RAM). Znika wiadomość „po wpisie” (urządzenie już nie zapisuje) |
| Ustawienia | `loadSettings`/`saveSettings` (`/ustawienia.cfg`), przełącznik w DIAGNOSTYCE | używane | ustawienie `sleepTelegram` **czytane z hostingu** (`/api/status` już je zwraca), bez zapisu na urządzeniu (decyzja D3) |
| Diagnostyka/watchdog | `initWatchdog`, `feedWatchdog`, `resetReasonText`, `hostTimeAgo`, `hostErrorText`, liczniki RTC, CPU load | używane | zostaje, uzupełnić (sekcja 4.5) |
| Start | `showBootScreen`, `bootStep`, `bootPumpFor`, `bootPumpLvgl`, `updateBootSyncUi`, `setup` | używane | zostaje (prościej) |
| Pętla | `loop`: noc, NTP, okno snu, wygaszacz, pogoda, VSYNC-sync render, dotyk co 5 ms, Wi-Fi retry, CPU | używane | zostaje idea; mniej obowiązków |

---

## 4. Specyfikacja funkcjonalna do zachowania (1:1)

### 4.1 Sprzęt (Waveshare ESP32-S3-Touch-LCD-4B)

**I2C**
- SDA 47, SCL 48, 400 kHz, timeout 100 ms.
- Ekspander TCA9554 pod adresem `0x20`, przez `Arduino_XCA9554SWSPI(7,0,2,1,&Wire,0x20)`.
- Sekwencja zasilania panelu:
  1. piny 5 i 6 jako OUTPUT,
  2. pin 6 LOW i 200 ms,
  3. pin 5 LOW i 200 ms,
  4. pin 5 HIGH i 200 ms.
- Potem przez ekspander: `sendCommand(0x01)`, 120 ms, `batchOperation(st7701_type1_init_operations)`.

**Panel RGB (esp_lcd)**
- PCLK 6 MHz, 480×480, 16-bit.
- HSYNC: impuls 8, back porch 50, front porch 14. VSYNC: impuls 8, back porch 20, front porch 10. Ramka 552×518, czyli ~47,7 ms (~21 FPS).
- Piny: HSYNC 46, VSYNC 3, DE 17, PCLK 9.
- Dane D0–D15: 40, 41, 42, 2, 1, 21, 8, 18, 45, 38, 39, 10, 11, 12, 13, 14.
- `disp_active_low=1`, `fb_in_psram=1`, `double_fb=1` (2 FB w PSRAM), bounce 480×80 px, `bb_invalidate_cache=1`, `dma_burst_size=64`, `sram_trans_align=8`.
- **Tryb bez bounce (EDMA) był testowany 19.10: linie, mruganie, przesunięcia. Nie wracać do niego.**

**LVGL 9.3**
- Tryb DIRECT na obu framebufferach panelu. `flush` robi `esp_lcd_panel_draw_bitmap` przy ostatnim fragmencie klatki.
- Tick LVGL ręczny w `loop()`, bo `lv_tick_set_cb` powodował StoreProhibited.
- Render synchronizowany z VSYNC (czekanie do 40 ms).

**Dotyk GT911**
- `TouchDrvGT911` (SensorLib), `setPins(-1,-1)`, adres `GT911_SLAVE_ADDRESS_L`, 1 punkt.
- Tryb indev EVENT: odczyt ręczny co 5 ms w pętli oczekiwania na VSYNC.
- Dotknięcie tablicy tylko ją zamyka i otwiera panel. Dotyk jest tłumiony aż do puszczenia palca (maks. 2 s), żeby nie kliknąć w panel pod spodem.

**Podświetlenie**
- GPIO4 → AP3032, PWM 5 kHz, 8 bit, **aktywne niskim**: 0 = 100%, 90 = noc (~65%), 119 = przyciemnienie.
- Przyciemnienie po 60 s bez dotyku, ale nie w trybie nocnym. Dotyk przywraca jasność.

**PSRAM**
- Wymagany (OPI). Bez niego komunikat i zatrzymanie.

### 4.2 Ekran startowy (`uiBootCreate`, `ui_modern.h`)

**Wygląd**
- Nocny las: gradient, 12 gwiazd, księżyc z poświatą, wzgórza, 6 sosen.
- Jeż: kolce, mruganie co 2,6 s, co drugie mrugnięcie podskok, rumieńce pulsujące, uśmiech.
- Napisy „Leśny Dziennik” i „Aleksander”.

**Pięć kroków** (stan: w toku ⟳ / OK ✓ / pominięte –):
1. Wi-Fi (do 15 s),
2. Zegar NTP (2,5 s, potem do 12 s czekania),
3. Pamięć danych,
4. Pogoda (z cache),
5. Dane z panelu WWW (pobranie z paskiem postępu, do 25 s).

**Zakończenie**
- Status: „Gotowe!” / „Brak danych WWW – start mimo to” / „Brak czasu – start mimo to”.
- 900 ms pokazu, potem przejście do **tablicy**.
- LVGL renderuje tylko między blokującymi krokami (z jednego wątku).

W v5 krok 3 „Pamięć danych” zmienić na „Pamięć” (PSRAM i ewentualny cache) albo usunąć (decyzja D2).

### 4.3 Tablica (wygaszacz) — pierwszy ekran (`uiSaverCreate`/`uiSaverUpdate`)

Odświeżanie co 5 s. Paleta dzień/noc (21:00–7:00).

| Element | Źródło / reguła |
|---|---|
| Zegar HH:MM (dwukropek miga) | czas lokalny (strefa `CET-1CEST,M3.5.0,M10.5.0/3`) |
| Data | „wtorek, 20 października” (polskie nazwy dni i miesięcy w dopełniaczu) |
| Pigułka drzemki | patrz 4.7: „Śpi od Xh YYm” / „Drzemka ok. HH:MM” / „Czas na drzemkę” (żółty) / „Przekroczone okno czuwania” (czerwony) / pusta |
| Pogoda | animowana ikona wg kodu WMO (słońce/księżyc nocą, chmury, mgła, deszcz, śnieg, burza), temperatura „+11°”, opis PL, „odcz. X° · min° / max°”; bez danych: „Czekam na dane pogody…” / „Brak połączenia Wi-Fi” |
| Pierścień karmienia | wypełnienie = minuty od ostatniego karmienia / 240; kolor zielony < 180 min, żółty ≥ 180, czerwony ≥ 240 i pulsowanie; w środku „H:MM” |
| „Ostatnie karmienie” | „55 min temu” / „2 h 5 min temu”; „następne około HH:MM” = ostatnie + 240 min |
| 3 kafelki | karmień dziś (liczba wpisów `KARMIENIE` z dzisiejszą datą), mleka dziś (suma ml typów `MLEKO*`), średnia przerwa dziś (średnia dodatnich odstępów między dzisiejszymi karmieniami) |
| Prognoza 3 godzin | temperatura na +1, +2, +3 h z Open-Meteo `hourly` |
| Ubiór | 9 progów temperatury (≥35 upał, ≥27, ≥22, ≥18, ≥13, ≥6, ≥0, ≥−12, poniżej) |
| Wskazówka dnia | „Dzien N: …” z tablicy 0..600 wg wieku w dniach; po 600 tekst ogólny |
| Grzechotka | dekoracja statyczna |

### 4.4 Panel (po dotknięciu tablicy) — `uiDashCreate` + 6 kart

**Górny pasek**
- Liść: dotknięcie wraca do tablicy.
- „Leśny Dziennik”, „Aleksander · 10 tyg. i 3 dni | 2 mies.”.
- Ikony statusu Wi-Fi / pamięć / czas (zielona OK, czerwona błąd).
- Zegar. **Długie przytrzymanie zegara → DIAGNOSTYKA.**

**Nawigacja**
- Dolny pasek 6 zakładek z animowaną pigułką: Dziś, Mleko, Odstępy, Waga, Sen, Kąpiel.
- Przeciągnięcie w lewo lub prawo zmienia kartę (animacja 260 ms).
- Odświeżanie: dane „na żywo” co 1 s, wykresy po zmianie danych lub co 2 min.
- Powrót na tablicę po 2 min bez dotyku.

| Karta | Zawartość i reguły |
|---|---|
| **0 Dziś** | Karta „OSTATNIE KARMIENIE” z paskiem do 240 min i znacznikiem 180 min. Po 240 min karta miga biel/czerwień co 500 ms. Kafelki „Ostatnie karmienie” (`HH:MM · karmienie`, z datą `dd.mm` gdy nie dziś) i „Ostatnia butelka” (`HH:MM · 120 ml`). Kafelki 2×2: karmień dziś, ml dziś, snu dziś (dzień + noc z `SEN_*`), pieluch dziś (mokre + brudne). |
| **1 Mleko** | Słupki skumulowane z 5 dni: matki / modyfikowane / mieszane (ml); nad słupkiem suma, pod nim data i liczba karmień; oś co 100 lub 200 ml; animacja wzrostu. |
| **2 Odstępy** | Do 5 ostatnich przerw dziś (`HH:MM – HH:MM`, długość); trwająca przerwa na żółto; przerwa ≥ 240 min na czerwono. „Rytm karmień — 3 dni”: kropki karmień na osi 0–24 h dla dziś, wczoraj, przedwczoraj (czasy sortowane). |
| **3 Waga** | Tytuł, ostatnia waga „X,YY kg” i data. Dwa widoki przełączane **dotknięciem wykresu**: (A) linia wagi (do 48 punktów, punkty przy ≤16 pomiarach, poświata na ostatnim, etykieta kg) na tle przerywanej mediany WHO skalowanej do wagi urodzeniowej 3080 g, oś kg co 0,5, oś tygodni; (B) słupki przyrostu g/dzień w 10 ostatnich tygodniach (regresja z okna ±7 dni wokół środka tygodnia, min. 2 pomiary rozpięte na ≥5 dni; tydzień niepełny półprzezroczysty, ujemny czerwony). Kafelki: „Od urodzenia” (g/dzień = (ostatnia − 3080)/dzień życia, „+X g w N dni”) i „Ostatnie 2 tygodnie” (regresja z ostatnich 14 dni; kolor wg normy 25–30 g/d do 91. dnia, 15–20 do 183., 10–15 później). Przy < 2 pomiarach komunikat. |
| **4 Sen** | „Sen nocny — między karmieniami, ostatnie 14 nocy”: dla każdej nocy najdłuższa przerwa od karmienia rozpoczętego 17:00–19:59 (+90 min) do pierwszego karmienia po północy do 9:00 (−10 min). Karmienia w odstępie ≤25 min łączone w jedno. Słupki (najdłuższa wyróżniona), linia średniej, kafelki średnia / najdłuższa (z godzinami) / ostatnia. Noc zaczęta dziś liczona dopiero po jej zakończeniu. |
| **5 Kąpiel** | Kalendarz bieżącego miesiąca (Pn–Nd): dni z `KAPIEL` w zielonym kółku, dziś obwiedzione, „następna” obrys (ostatnia + 3 dni, a gdy zaległa — dziś). Kafelki „Ostatnia: dd.mm · dziś/wczoraj/N dni temu” i „Następna: dd.mm · dziś/jutro/za N dni”. |

### 4.5 Diagnostyka (długie przytrzymanie zegara w panelu)

**Treść obecnie:**
- HostSync: rewizja lokalna i zdalna, ostatnie sprawdzenie, ostatnie pobranie, nieudane pobrania, ostatni push, ostatni błąd (brak Wi-Fi / TLS / HTTP / dane).
- Wi-Fi (IP, RSSI), serwer HTTP (zbędne), NTP, pamięć danych, ostrzeżenie o dużym pliku, pogoda.
- RAM wewnętrzny wolny i minimum, PSRAM wolny, CPU %, czas pracy.
- Watchdog, liczba uruchomień, restarty watchdoga (RTC), powód ostatniego resetu.
- Przełącznik „Sen: powiadomienia Telegram”.
- Powrót: dziś do ekranu „INNE”, a w v5 do panelu.

**v5:**
- Usunąć „ostatni push” i „serwer HTTP”.
- Dodać: liczbę twardych resynchronizacji obrazu i ostatni powód; fazę bounce i wzorzec; zapas stosu zadań; wiek danych („dane sprzed X min”); wersję firmware i datę kompilacji; liczbę wpisów w danych.
- Napisać w stylu `ui_modern.h`.

### 4.6 Zachowanie czasowe (stałe)

| Parametr | Wartość |
|---|---|
| Tablica po bezczynności panelu | 2 min (`SCREENSAVER_TIMEOUT_MS`) |
| Powrót z innych ekranów do panelu | 30 s (`HOME_RETURN_TIMEOUT_MS`) — w v5 dotyczy tylko DIAGNOSTYKI |
| Przyciemnienie | 60 s, tylko w dzień |
| Tryb nocny (paleta + jasność) | 21:00–7:00, sprawdzany co 30 s, przebudowa ekranu przy zmianie |
| Polling hostingu | co 10 s rewizja, przy zmianie pobranie CSV |
| Pogoda | co 20 min, ponowienie po 60 s, „stara” po 30 min, start po 8 s |
| Watchdog | 30 s na `loopTask` |

### 4.7 Dane i obliczenia (wejście: CSV z hostingu)

**Format CSV**
- Nagłówek `data,godzina,typ,ml,piers_lewa_min,piers_prawa_min`, wiersze 4 lub 6 kolumn, czas lokalny `RRRR-MM-DD,GG:MM`.
- Typy:
  - `KARMIENIE` (L/P minuty),
  - `MLEKO`, `MLEKO_MATKI`, `MLEKO_MODYFIKOWANE`, `MLEKO_MIESZANE` (ml),
  - `PIELUCHA_MOKRA`, `PIELUCHA_BRUDNA`,
  - `ODCIAGANIE` (ml),
  - `WITAMINA_D`,
  - `WAGA` (gramy w kolumnie ml),
  - `SEN_START`, `SEN_STOP`,
  - `KAPIEL` (zapisywana z godziną 12:00).

**Reguły (v5 — zachować wyniki, poprawić znane błędy):**
- **Ostatnie karmienie / butelka:** wpis o najpóźniejszym czasie ≤ teraz+60 s. Gdy wszystkie są „w przyszłości” (zły zegar), najpóźniejszy ogółem.
- **Następne karmienie:** ostatnie + 240 min (stała `COUNTER_BLINK_MIN`). Wersja z prognozą jest opisana w `ulepszenia19-10-26.md` (pkt 12) jako rozwój.
- **Dziś:**
  - karmienia: liczba `KARMIENIE`,
  - ml: suma `MLEKO*`,
  - przerwy: z **posortowanych** czasów karmień dnia (dodatnie odstępy), średnia i najdłuższa; najdłuższa uwzględnia też trwającą przerwę,
  - pieluchy: mokre + brudne.
- **Sen (`SEN_*`):**
  - zdarzenia **sortowane po czasie**, z pominięciem zdarzeń późniejszych niż teraz+1 min (naprawa „Śpi od –”),
  - para START→STOP to sesja, odrzucana gdy < 1 min lub > 840 min (jak PHP),
  - otwarty START to sen trwający (o ile ≤ 840 min, inaczej ignorowany),
  - podział minut noc (21–7) / dzień według godziny, przypisanie do doby kalendarzowej segmentu,
  - drzemka = sesja rozpoczęta w dzień.
- **Stan czuwania i drzemki:**
  - okno czuwania z tabeli wieku (dni → min–max): 0→35–60, 28→60–90, 84→75–120, 150→120–180, 210→150–210, 330→180–240, 420→240–360, interpolacja liniowa,
  - `lastWake` = STOP ostatniej sesji,
  - stany: przed oknem „Drzemka ok. HH:MM”, w oknie „Czas na drzemkę”, po oknie „Przekroczone okno czuwania”.
- **Potrzeba snu** (do kafelka „snu dziś”): tabela 0→510/480, 30→510/420, 91→570/300, 182→600/240, 274→660/180, 365→660/120 (min noc/dzień).
- **Waga:**
  - punkty (dzień życia, gramy), sortowane, z dodanym punktem urodzeniowym 3080 g,
  - ostatnia waga = najpóźniejsza data (nie ostatni wiersz),
  - regresje jak w 4.4.
- **Kąpiele:** bitmaska dni bieżącego miesiąca, ostatnia (najpóźniejsza data), następna = ostatnia + 3.
- **Noce:** heurystyka z karmień jak w 4.4 (nazwa w UI zostaje, ewentualnie zmiana nazwy — decyzja D5).
- **Wiek:**
  - dni = różnica dat w południe,
  - tekst „N tyg. i D dni | M mies.” (pełne miesiące kalendarzowe).
- **Okno statystyk:** 15 dni (wykresy) i 8 dni (statystyki dni). W v5 jeden przebieg po danych w PSRAM liczy wszystko naraz.

### 4.8 Sieć i usługi

**Wi-Fi**
- STA, `persistent(false)` (bez zapisów NVS), `setSleep(false)`, auto-reconnect, ponowienie co 30 s.

**NTP**
- `configTzTime` (pool.ntp.org, time.google.com, time.cloudflare.com). Czas ważny, gdy rok ≥ 2025.

**Pogoda**
- Open-Meteo po HTTP (bez klucza), dla 50.3274 / 19.1285.
- Pola: `current` (temperature_2m, apparent_temperature, weather_code), `hourly` (temperatura i kod na +1..+3 h), `daily` (min i max dziś).
- Odpowiedź ≤ 12 KB.

**Hosting (tylko odczyt)**
- `GET /api/revision` → `{"rev":N,...}`.
- `GET /api/export.csv` (obsługa `Content-Length` i `chunked`).
- Pobrany plik akceptowany tylko z poprawnym nagłówkiem i zakończony `\n`.
- TLS na rdzeniu 0, z odstępem 3 s od TLS Telegrama (fragmentacja RAM).

**Telegram**
- Wiadomość startowa (czas, IP, RAM, PSRAM, pamięć, pogoda).
- Okno drzemki: raz na okno czuwania, przy wejściu w okno i przy przekroczeniu; pominięte, gdy okno minęło > 30 min temu (np. po restarcie).
- **Codzienna kopia CSV** (`sendDocument`, nazwa `karmienia_RRRR-MM-DD.csv`), wysyłana pierwszy raz po północy i tylko gdy dane są zsynchronizowane.
- Kolejka 1 wiadomości; 5 nieudanych prób i porzucenie.
- **W v5 znika** powiadomienie „po każdym wpisie z urządzenia” (brak wpisów). Opcja nowa: powiadamiać o wpisach widzianych w nowych danych z hostingu (decyzja D4).

---

## 5. Co usuwamy (i dlaczego)

- **Wszystkie zapisy danych:** `appendEntry`, `deleteEntryByIndex`, `updateFeeding`, `uploadCsvToHost`, `applyAutoSleepForFeeding`, ekrany INNE, PIELUCHA i ODCIĄGANIE. To wymóg „tylko wyświetla” i usuwa ryzyko nadpisania danych na hostingu.
- **Stare ekrany i system stylów** (`COLOR_*`, `createCard/Button/Label`, `reusable screens`, ~1300 linii).
- **Lokalna archiwizacja, kopia dzienna na flashu, rotacja pliku.** Dane są na hostingu, kopię Telegram robi się z RAM.
- **Serwer WWW, mDNS, OTA** (`web_server_disabled.h`, `web_panel.h`, `FEATURE_*`).
- **Wielokrotne skany CSV z LittleFS** (`loadLatestEntries`, `recomputeFeedingRhythm`, `refreshDayStats`, `uiFillCharts`, `entriesForDay`…), zastąpione jednym parserem do PSRAM.
- **Cache pogody we flashu** (zapis co 30 min), zastąpiony pamięcią RTC (przetrwa restart programowy) albo pobieraniem po starcie.
- **Ustawienia na flashu** (`/ustawienia.cfg`). `sleepTelegram` czytane z hostingu (decyzja D3).
- Martwe stałe: `FEEDING_ALARM_WINDOW_MINUTES`, `WEATHER_CITY`, `PANEL_API_BASE`, `PANEL_UPLOAD_*`, `OTA_PASSWORD`, `HOST_SYNC_*`, zakresy formularzy (`ML_*`, `MILK_ML_*`, `WEIGHT_*`, `DEFAULT_*`).

---

## 6. Znane błędy v4.1, których nie przenosimy

| Błąd | Poprawka w v5 |
|---|---|
| „Śpi od –” (sen w przyszłości z auto-snu WWW) | pomijanie zdarzeń > teraz+1 min, sortowanie sesji |
| Sen liczony w kolejności pliku, bez limitu 14 h | sortowanie i limit 840 min (jak PHP) |
| `dayStats()` nie zeruje `bathCount` | struktura zerowana w całości (`= {}`) |
| `lastWeightG` = ostatni wiersz, nie najpóźniejszy | najpóźniejsza data |
| Średnia przerwa przy nieposortowanych wpisach | sortowanie czasów dnia |
| Widok wagi wraca do A po ≤2 min | stan widoku poza przebudową strony; przebudowa tylko gdy dane się zmieniły |
| Przebudowa bieżącej karty co 2 min (mignięcie) | hash modelu: przebudowa tylko przy zmianie |
| Odczyty flasha w trakcie pracy (przesuwanie obrazu) | dane w PSRAM, zero LittleFS w pracy |
| Cache pogody zapisywany na flash co 30 min | RAM RTC |
| `uploadCsvToHost` bez kontroli zapisu | funkcja znika |
| TLS bez weryfikacji | certyfikat główny (`setCACert`) hostingu i Telegrama |
| Polling co 10 s z nowym handshake TLS | keep-alive / 30 s + odświeżenie przy dotknięciu (decyzja D6) |
| `syncTimeFromNTP()` blokuje pętlę do 3 s po powrocie Wi-Fi | tylko `beginNtp()` |
| Brak watchdoga zadania sieciowego | `esp_task_wdt_add` dla zadania sieci |
| Sekrety w repo | `secrets.h` poza repo, `secrets.h.example` |

---

## 7. Architektura v5

### 7.1 Struktura plików (Arduino IDE: zakładki w jednym folderze szkicu)

```
v5/
├── v5.ino              ← setup()/loop(): tylko kolejność startu i pętla (≈150 linii)
├── app_config.h        ← stałe wyświetlacza (urodziny, progi, tabele snu, czasy, URL-e hostingu)
├── secrets.h           ← (poza repo) Wi-Fi, Telegram, opcjonalny token urządzenia
├── secrets.h.example
├── hw_display.{h,cpp}  ← ekspander, ST7701, esp_lcd RGB, bounce, VSYNC, resync, podświetlenie
├── hw_touch.{h,cpp}    ← GT911 + indev LVGL (tłumienie po wygaszaczu)
├── net.{h,cpp}         ← Wi-Fi, NTP, wspólny klient HTTPS (parsowanie URL, chunked, timeouty, CA)
├── host_sync.{h,cpp}   ← polling rewizji, pobranie CSV do PSRAM, walidacja, publikacja snapshotu
├── store.{h,cpp}       ← parser CSV → tablica Entry w PSRAM (+ indeksy), bez Arduino String
├── stats.{h,cpp}       ← CZYSTE funkcje: Store + now → UiModel (wszystkie reguły z 4.7)
├── weather.{h,cpp}     ← Open-Meteo, parser, cache w RTC
├── telegram.{h,cpp}    ← start, okno drzemki, kopia dzienna z bufora w PSRAM
├── diag.{h,cpp}        ← liczniki RTC, watchdog, CPU, stos, log
├── ui_modern.h         ← jak dziś + ekran DIAGNOSTYKA w tym samym stylu
├── ui_fonts.h, ui_assets.h, development_tips.h (z PL znakami), dressing.h
└── lv_conf.h
```

**Zasada:** `stats.cpp` i `store.cpp` nie zależą od ESP32 ani LVGL, więc kompilują się w symulatorze i w testach na PC (te same pliki, zero kopiowania przez `extract_glue.py`).

### 7.2 Wątki i przepływ danych

```
rdzeń 0: netTask ──(co 10–30 s)── GET /api/revision ── zmiana? ── GET export.csv → bufor PSRAM
            │                                                     → store_parse() do NOWEGO Store (PSRAM)
            │                                                     → atomowa podmiana wskaźnika (double-buffer)
            ├── pogoda (co 20 min) → WeatherState (mutex)
            └── telegram (kolejka, okno drzemki, kopia dzienna z bufora CSV)
rdzeń 1: loop ── VSYNC → dotyk (5 ms) → co 1 s stats_live(Store, now) → UiModel → uiSaver/uiDash update
                         przy nowym Store: stats_full(...) → hash → przebuduj kartę tylko gdy hash inny
```

- **Store jest niezmienny po zbudowaniu.** Zadanie sieciowe buduje nowy, a pętla UI przełącza wskaźnik pod krótkim mutexem. Koniec z flagami „mirror changed” i dostępem do `String` z dwóch rdzeni.
- **Zero `String` w ścieżkach okresowych.** Bufory `char[]` i `snprintf`. `String` tylko przy starcie i w rzadkich komunikatach.

### 7.3 Model danych w PSRAM

```cpp
struct Entry {            // 12 B
  int32_t  t;             // epoka (czas lokalny przeliczony mktime, isdst=-1)
  uint16_t ml;            // ml / gramy
  uint8_t  type;          // enum EntryType
  uint8_t  left, right;   // minuty piersi
  uint8_t  flags;         // np. FUTURE (t > teraz+60 s w chwili parsowania)
  uint16_t dayIdx;        // dzień życia (szybkie grupowanie)
};
struct Store { Entry *e; uint32_t n; long rev; uint32_t parsedAtMs; const uint8_t *csv; size_t csvLen; };
```

- Rok danych to ~5–10 tys. wpisów, czyli ≤120 KB w PSRAM. Parsowanie ~10–20 ms raz na zmianę danych.
- Wpisy sortowane po `t` przy parsowaniu (stabilnie), więc wszystkie reguły zakładające chronologię są poprawne, nawet przy edycjach w miejscu na WWW.
- Bufor CSV zostaje w PSRAM do wysyłki kopii na Telegram (bez flasha).

### 7.4 Obraz bez przesunięć — strategia

1. **Podstawowa (Arduino IDE, wymagana):** w normalnej pracy **żadnych** operacji na flashu (LittleFS, NVS, `esp_partition_*`).

   Zostaje:
   - kod i stałe (fonty, grafiki) czytane przez cache (XIP), co nie wyłącza cache,
   - **zero zapisów**,
   - Wi-Fi z `persistent(false)`.

   Twarda resynchronizacja i detektor fazy zostają jako zabezpieczenie i diagnostyka (liczniki w DIAGNOSTYCE).
2. **Mocniejsza (zalecana do rozważenia, decyzja D1):** budowanie przez **PlatformIO / pioarduino** z własnym `sdkconfig` (Arduino jako komponent ESP-IDF):
   - `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` (dawniej `SPIRAM_FETCH_INSTRUCTIONS` + `SPIRAM_RODATA`): kod i stałe w PSRAM, więc **operacje na flashu nie wyłączają już cache PSRAM**. To zalecenie Espressif dla paneli RGB.
   - `CONFIG_LCD_RGB_ISR_IRAM_SAFE=y`: przerwania panelu działają także podczas operacji na flashu.
   - `CONFIG_LCD_RGB_RESTART_IN_VSYNC=y`: restart DMA w VSYNC robi sterownik (zbędny nasz callback).
   - `CONFIG_ESP_TASK_WDT_*`, `CONFIG_FREERTOS_HZ=1000`.

   Koszt: zmiana środowiska (VS Code + PlatformIO zamiast Arduino IDE) i dłuższa pierwsza kompilacja. Zysk: problem rozwiązany u źródła, a opcje są niedostępne w prekompilowanym rdzeniu Arduino IDE.
3. Bez względu na wariant: **nie wracać do EDMA bez bounce** (test 19.10).

### 7.5 Konfiguracja i sekrety

- `secrets.h` (poza repo): `WIFI_SSID`, `WIFI_PASSWORD`, `TELEGRAM_BOT_TOKEN` (nowy, po unieważnieniu starego), `TELEGRAM_CHAT_ID`, `HOST_DEVICE_TOKEN` (na przyszłość, gdy API dostanie autoryzację).
- `app_config.h`: urodziny i waga urodzeniowa, URL-e (`/api/revision`, `/api/export.csv`, opcjonalnie `/api/status` dla `sleepTelegram`), tabele snu, progi, czasy.
- Certyfikaty główne (PEM) w `certs.h`.

### 7.6 Pamięć i partycje

- LittleFS niepotrzebny (wariant „bez cache”) albo mały (np. 256 KB) na rzadki cache danych (decyzja D2).
- Partycja aplikacji: dziś 5 MB. Przy ~3 MB grafik i fontów warto zostawić 6–8 MB i usunąć wielką partycję danych.
- RAM wewnętrzny: bufory bounce (~150 KB) + Wi-Fi/TLS. Jeden klient TLS naraz (Telegram i hosting w jednym zadaniu, kolejno).
- PSRAM (8 MB): 2×FB (900 KB), pula LVGL 128 KB, bufory CSV i Store (<1 MB), JSON pogody.

---

## 8. Decyzje do podjęcia przed kodowaniem

| # | Pytanie | Opcje | Rekomendacja |
|---|---|---|---|
| D1 | Środowisko budowania | (a) Arduino IDE jak dziś; (b) PlatformIO z własnym `sdkconfig` | (b), jeśli przesunięcia wrócą mimo braku flasha; inaczej (a) |
| D2 | Pamięć danych po restarcie bez internetu | (a) bez cache — ekran „brak danych” do połączenia; (b) cache CSV na flashu zapisywany tylko przy zmianie, maks. raz na 30 min | (a) — prościej i zero zapisów; dane pojawiają się po ~5 s od Wi-Fi |
| D3 | Przełącznik powiadomień o drzemce | (a) czytany z hostingu (`/api/status` → `sleepTelegram`, przełącznik na WWW); (b) przełącznik na urządzeniu z zapisem na flash | (a) |
| D4 | Telegram o nowych wpisach | (a) brak (jak dziś dla wpisów z WWW); (b) urządzenie wysyła skrót nowych wpisów wykrytych w danych | (a) teraz, (b) jako opcja później — lepiej, żeby wysyłał to serwer |
| D5 | Karta „Sen” | (a) jak dziś (heurystyka z karmień); (b) z `SEN_*`, gdy są dane, inaczej heurystyka | (a) w v5.0 (1:1), (b) później |
| D6 | Częstotliwość sprawdzania danych | (a) 10 s jak dziś; (b) 30 s + natychmiast po dotknięciu ekranu; (c) keep-alive TLS co 10 s | (b) |

### Ustalenia (20.10.2026)

| # | Decyzja |
|---|---|
| D1 | **Arduino IDE.** Obraz chroniony brakiem operacji na flashu w pracy + twarda resynchronizacja i detektor fazy jako zabezpieczenie. PlatformIO tylko, jeśli przesunięcia wrócą. |
| D2 | **Cache ostatnich danych na flashu.** CSV zapisywany tylko gdy treść się zmieniła i **najwyżej raz na 30 min** (zapis zaległy wykonywany po upływie okna). Zapis jednym zrywem z PSRAM pod `FlashWriteGuard` i z twardą resynchronizacją. Przy starcie bez sieci pokazywane są dane z cache z oznaczeniem wieku danych. Mała partycja LittleFS. |
| D3 | **`sleepTelegram` czytane z hostingu** (`/api/status`). Na urządzeniu bez przełącznika i bez zapisu; w DIAGNOSTYCE tylko stan. |
| D4 | Bez powiadomień o nowych wpisach (jak dziś). |
| D5 | Karta „Sen” 1:1 jak dziś. |
| D6 | **Sprawdzanie rewizji co 10 s jak dziś.** Warto spróbować utrzymać połączenie TLS (keep-alive), żeby nie robić handshake co 10 s; przy braku wsparcia hostingu — nowe połączenie jak dziś. |

---

## 9. Zmiany po stronie hostingu (opcjonalne, niewymagane do v5.0)

- Wyłączyć auto-sen przy karmieniu (`api.php` L250–258). To usuwa przyczynę „Śpi od –” u źródła.
- Autoryzacja (urządzenie: `X-Device-Token` tylko do odczytu).
- `GET /api/export.csv` z `ETag`/`If-None-Match` (304), czyli jeden request zamiast dwóch.
- Później `GET /api/device-summary` (gotowy JSON dla UI). Wtedy `stats.cpp` na urządzeniu można by zastąpić parserem JSON. W v5.0 zostajemy przy CSV, bo hosting nie wymaga zmian.

---

## 10. Plan prac i kryteria odbioru

| Etap | Zakres | Kryterium |
|---|---|---|
| 5.0-a | Szkielet modułów, `hw_display` + `hw_touch` + LVGL + tablica z danymi demo | obraz stabilny, dotyk działa, kompilacja bez ostrzeżeń |
| 5.0-b | `net` + `host_sync` + `store` + `stats` (wszystkie reguły z 4.7) | **parytet:** na tym samym CSV `UiModel` z v5 = model z v4.1 (poza celowo poprawionymi błędami z sekcji 6) — test w symulatorze `tools/sim` |
| 5.0-c | Panel 6 kart + DIAGNOSTYKA w nowym stylu | wszystkie elementy z 4.3–4.5 widoczne; zrzuty z symulatora dzień i noc |
| 5.0-d | Pogoda (RTC cache) + Telegram (start, drzemka, kopia z RAM) | wiadomości przychodzą; brak zapisów flasha (licznik w diagnostyce = 0) |
| 5.0-e | Test na urządzeniu 24 h | 0 przesunięć obrazu (log `LCD:`), brak restartów, RAM min > 40 KB |
| 5.1 | (opcjonalnie) PlatformIO + sdkconfig (D1), poprawki hostingu (sekcja 9) | — |

**Testy:**
- `tools/sim`: scenariusze z `gen_csv.py` plus przypadki brzegowe:
  - zmiana czasu 25.10,
  - wpisy wsteczne,
  - sen w przyszłości,
  - zapomniany STOP,
  - brak danych,
  - 1 pomiar wagi,
  - pusty miesiąc kąpieli.
- Porównanie liczb z `web-php/engine/Domain.php` dla tego samego CSV. Kompilacja w GitHub Actions (`arduino-cli`).
