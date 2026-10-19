# Ulepszenia — przegląd całego kodu (19.10.2026)

Notatka na przyszłość: co i jak poprawić w urządzeniu (ESP32-S3, `v3.ino` + nagłówki) i w panelu WWW (`web-php/`).
Stan kodu: gałąź `v4.1-claude-pazdziernik`, commit `257ee60`. **W tym przeglądzie niczego w kodzie nie zmieniano.**

Numery linii dotyczą tego commitu i będą się przesuwać — zawsze szukaj po nazwie funkcji.

**Priorytety:**
- **P0** — pilne (bezpieczeństwo, utrata danych),
- **P1** — ważne błędy i stabilność,
- **P2** — jakość, wydajność, UX,
- **P3** — porządki i rozwój.

**Wysiłek:** S (do godziny), M (kilka godzin), L (dzień lub więcej).

---

## 0. Najważniejsze w skrócie (TOP 12)

| # | Priorytet | Co | Gdzie |
|---|---|---|---|
| 1 | P0 | Repozytorium jest **publiczne**. Zawiera token bota Telegram, chat ID, hasło Wi-Fi, adres hostingu i prawdziwe dane dziecka (`web-php/data/karmienia.csv`). | `config.h`, `secrets.h`, `web-php/data/` |
| 2 | P0 | API hostingu nie ma **żadnego uwierzytelniania**. Każdy, kto zna adres (jest w repo), może czytać i nadpisywać dane, a `/api/upload-data` podmienia całą historię. `PANEL_UPLOAD_TOKEN` jest pusty. | `engine/api.php`, `config.h` |
| 3 | P1 | **Utrata wpisów przy zapisie z urządzenia.** Urządzenie po lokalnym wpisie wysyła *cały* CSV i nadpisuje hosting. Wpis dodany na WWW w ostatnich ≤10 s znika. | `appendEntry()` → `uploadCsvToHost()` |
| 4 | P1 | **Auto-sen przy każdym karmieniu z WWW.** Dopisuje `SEN_STOP` (T−30 min) i `SEN_START` (T+60 min) poza kolejnością. Efekt: „śpi od 11:00” o 10:05, sen w przyszłości, fałszywe statystyki snu. | `api.php` route `entry`, L250–258 |
| 5 | P1 | Zapis CSV w PHP ma wyścigi. Odczyt i zapis nie są w jednym zamku, a zapis nie jest atomowy. Możliwe zgubienie wpisu lub pusty plik w trakcie zapisu. | `CsvRepository::deleteByIndex/updateFeeding/bumpRevision` |
| 6 | P1 | Usuwanie i edycja po **numerze wiersza**. Gdy dane zmieniły się w międzyczasie (drugi rodzic, urządzenie), usuwany jest *inny* wpis. | `/api/delete-entry`, `/api/update-feeding` |
| 7 | P1 | **Przesuwanie obrazu.** Okresowe skany CSV z flasha (co 2–3 min) i porównanie pliku po pobraniu czytają flash bez ochrony. Odczyt flasha też wyłącza cache. | `uiFillCharts`, `recomputeFeedingRhythm`, `fileEqualsBuffer` |
| 8 | P1 | **Zmiana czasu 25.10.2026** (najbliższa niedziela). CSV trzyma czas lokalny bez strefy. Wpisy 02:00–02:59 są niejednoznaczne, a przerwy i sen mogą wyjść ujemne lub o godzinę za długie. | format CSV, `csvDateTimeToEpoch`, `Domain::csvDateTimeToDate` |
| 9 | P2 | Ta sama logika jest napisana **3 razy** (C++, PHP, JS) i już się rozjechała: sen (sortowanie, limit 14 h), ostatnia waga, ostatnia kąpiel, WHO. | `v3.ino`, `Domain.php`, `app.js` |
| 10 | P2 | Urządzenie co 10 s robi **nowy handshake TLS** do hostingu. Obciąża RAM wewnętrzny (fragmentacja), CPU i Wi-Fi. | `telegramTask`, `fetchRevision` |
| 11 | P2 | ~1300 linii **martwego lub półmartwego UI** (stare ekrany LVGL). Część jest nadal osiągalna przez Diagnostyka → POWRÓT → „INNE” i zapisuje dane z pominięciem nowego modelu. | `v3.ino` L2580–3390, L3981–4283, L5358–5524 |
| 12 | P2 | Dokumentacja jest częściowo nieaktualna i myląca (MINDTRANSFER, VALIDATION, tabela plików w README, README `web-php`, komentarze w kodzie RGB). | `*.md`, `v3.ino` L786, L1057, L1151 |

---

## 1. Bezpieczeństwo i prywatność (P0)

### 1.1 Sekrety w publicznym repozytorium — S (+ czynności poza kodem)
- `config.h`: `TELEGRAM_BOT_TOKEN`, `TELEGRAM_CHAT_ID`, adresy `PANEL_*_URL`.
- `secrets.h`: `WIFI_SSID`, `WIFI_PASSWORD` (prawdziwe wartości, plik w historii od commitu `7f76b74`).
- `secrets.h.example`, o którym mówią README i nagłówek `v3.ino`, **nie istnieje** w repo.

**Co zrobić:**
1. Natychmiast unieważnić token bota (@BotFather → `/revoke`) i wygenerować nowy. Rozważyć zmianę hasła Wi-Fi.
2. Repo ustawić jako prywatne albo usunąć sekrety także z historii:
   ```bash
   # w świeżym klonie (mirror), potem push --force wszystkich gałęzi
   git filter-repo --path secrets.h --invert-paths
   git filter-repo --replace-text <(echo 'STARY_TOKEN==>REDACTED')
   ```
   Same commity „usuwające” plik nie wystarczą, bo GitHub i forki trzymają historię. Rotacja tokenów jest obowiązkowa niezależnie od tego.
3. Wszystkie sekrety (Telegram, token urządzenia do API, Wi-Fi, ewentualnie URL) przenieść do `secrets.h`. Dodać `secrets.h` do `.gitignore` i dodać do repo `secrets.h.example` z pustymi wartościami.
4. `web-php/data/karmienia.csv` (dane zdrowotne dziecka) usunąć z repo i dodać `web-php/data/*.csv` oraz `*.rev` do `.gitignore`. Do testów zrobić syntetyczny `data/przyklad.csv` (jest generator `tools/sim/gen_csv.py`).

### 1.2 Brak uwierzytelniania API hostingu — M
Teraz każdy endpoint jest otwarty: `GET /export.csv` (cała historia), `POST /api/entry`, `/api/delete-entry`, `/api/import`, `/api/upload-data` (podmiana całości).

**Propozycja (prosta, bez bibliotek):**
- **Rodzice (przeglądarka):** logowanie hasłem. Hash w zmiennej środowiskowej `PANEL_PASSWORD_HASH` (`password_hash()`). Sesja PHP z ciasteczkiem `HttpOnly; Secure; SameSite=Strict`. Długa sesja (np. 180 dni), żeby nie logować się co chwilę na telefonie.
- **Urządzenie:** nagłówek `X-Device-Token`, porównanie przez `hash_equals` z `DEVICE_TOKEN` w env. Token urządzenia może być tylko do odczytu (rev, export, device-summary) plus ewentualnie `event`.
- Jedna funkcja `requireAuth(string $route)` na początku `handle_api()` z białą listą tras publicznych (żadnych, poza ewentualnym `/api/health`).
- CSRF: przy `SameSite=Strict` wystarczy dodatkowo sprawdzić nagłówek `Origin` dla POST (odrzucić obcy origin).
- `upload-data` i `import`: tylko po zalogowaniu. Token w query (`?token=`) usunąć, bo trafia do logów serwera.
- Ograniczenie prób logowania: plik z licznikiem na IP, np. 10 prób na 15 min.

### 1.3 TLS bez weryfikacji (`setInsecure()`) — M
Urządzenie łączy się z Telegramem i hostingiem bez sprawdzania certyfikatu (`sendBackupViaTelegram`, `uploadCsvToHost`, `fetchRevision`, `downloadCsvFromHost`, `pumpTelegramQueue`). Ktoś w tej samej sieci może podszyć się pod serwer i przechwycić token bota lub podmienić dane.
- Dodać `client.setCACert(ROOT_CA)` z certyfikatem głównym hostingu. Sprawdzić łańcuch `openssl s_client -connect phpmapy1.webd.pro:443 -showcerts`. `test_api.sh` wspomina, że Windows mu nie ufa, więc być może brakuje certyfikatu pośredniego.
- Dla Telegrama: certyfikat główny `api.telegram.org`.
- Alternatywa: pinning odcisku SHA-256 certyfikatu. Prostsze, ale wymaga aktualizacji przy odnowieniu certyfikatu.

### 1.4 Drobne
- `.htaccess` w `data/` blokuje pliki, ale `.bakap` mnożą się bez limitu (patrz 7.6). Upewnić się na serwerze, że `data/` i `engine/` są **poza** `public_html` (wariant cPanel w `ui/paths.php`).
- `index.html` ma `maximum-scale=1`, co blokuje powiększanie. To problem dostępności (zmęczone oczy w nocy), zdjąć.
- Fonty z Google Fonts (`index.html` L9–11) wysyłają IP do Google. Lepiej hostować lokalnie (woff2 w `ui/`).

---

## 2. Architektura i spójność danych (urządzenie ↔ hosting)

### 2.1 Obecny model i jego słabości
- Hosting jest źródłem prawdy. Urządzenie co 10 s pyta o `/api/revision`, a przy zmianie pobiera **cały CSV**, zapisuje go do LittleFS i potem **parsuje CSV kilkoma niezależnymi skanami** (`loadLatestEntries`, `refreshDayStats`, `recomputeFeedingRhythm`, `uiFillCharts`, …).
- Urządzenie nadal może zapisywać lokalnie (stare ekrany „INNE”: pielucha, odciąganie), a potem robi push **całego** pliku (`uploadCsvToHost`). Nadpisuje to zmiany z WWW, które zaszły od ostatniego pollingu. Zostaje tylko `.bakap` na serwerze.
- Logika statystyk istnieje w trzech językach (C++ firmware, PHP `Domain.php`, JS `app.js`) i już się różni:

| Reguła | Firmware | PHP | JS (WWW) |
|---|---|---|---|
| Parowanie snu | kolejność w pliku | sortowanie po czasie | sortowanie po czasie (`sleepSessions()`) |
| Limit sesji snu | brak | 840 min (`SLEEP_MAX_SESSION_MIN`) | `SLEEP_MAX_MIN` (osobna stała w JS) |
| Ostatnia waga | ostatnia w pliku (`loadLatestEntries`) / najpóźniejsza data (`uiFillCharts`) | ostatnia w pliku | z serii |
| Ostatnia kąpiel | najpóźniejsza | ostatnia w pliku | z `bath-calendar` |
| WHO | tablice uśrednione ch/dz | — | to samo, + pasmo „od wypisu” 2850 g na sztywno |

### 2.2 Kierunek docelowy (zalecany): urządzenie jako „cienki wyświetlacz” — L
1. **Nowy endpoint `GET /api/device-summary`** (z tokenem urządzenia). Zwraca gotowy JSON (~3–6 KB) z dokładnie tymi polami, których potrzebuje `UiModel`:
   - ostatnie karmienie i butelka,
   - statystyki dziś i 5 dni,
   - przerwy dziś i rytm 3 dni,
   - punkty wagi i kafelki przyrostu,
   - noce,
   - kąpiele w miesiącu,
   - stan snu i okno drzemki,
   - wskazówka dnia.

   Liczy to PHP, czyli **jedna** implementacja logiki.
2. Urządzenie: `If-None-Match`/rewizja → przy zmianie pobiera JSON do PSRAM → parsuje (ArduinoJson, alokator PSRAM) → wypełnia `UiModel`. **Zero skanów CSV i zero zapisów flasha w normalnej pracy.** Opcjonalnie zapisuje JSON jako cache startowy co najwyżej raz na godzinę pod `FlashWriteGuard`.
3. Zapisy z urządzenia (jeśli zostaną) przez `POST /api/event` (pojedynczy wpis), **nigdy** przez podmianę całego pliku. `uploadCsvToHost()` usunąć.
4. **Telegram przenieść na serwer.** PHP wysyła powiadomienie przy zapisie wpisu, a cron w cPanel robi codzienny backup CSV i przypomnienia o oknie drzemki. Urządzenie nie potrzebuje wtedy drugiego klienta TLS (koniec `TLS_COOLDOWN_MS` i błędów „esp-aes: Failed to allocate memory”). Powiadomienia działają też, gdy urządzenie jest wyłączone.
5. Efekt uboczny: z `v3.ino` znika ~1500–2000 linii (CSV, archiwizacja, backup, Telegram, push) i główna przyczyna przesuwania obrazu (zapisy flasha).

Migracja etapami, żeby nic nie zepsuć:
- (a) dodać endpoint i porównać jego wynik z `UiModel` z firmware w symulatorze (`tools/sim`),
- (b) firmware z flagą `FEATURE_DEVICE_SUMMARY` i fallbackiem na CSV,
- (c) po tygodniu testów usunąć ścieżkę CSV.

### 2.3 Jeśli zostajemy przy CSV na urządzeniu (wersja minimum) — M
- **CSV w PSRAM.** Po pobraniu z hostingu trzymać bufor (już jest w `downloadCsvFromHost`) i parsować go raz do tablicy `UiRow[]` w PSRAM. Wszystkie statystyki liczyć z tej tablicy, nie z pliku. Plik na flashu tylko jako cache startowy.
- Zablokować lokalne zapisy z urządzenia albo kierować je do `POST /api/event`.
- Jeśli push musi zostać: przed wysłaniem pobrać rewizję. Jeśli `hostRevRemote != hostRevisionLocal`, najpierw pobrać i scalić (dopisać tylko nowe lokalne wiersze), dopiero potem wysłać.

### 2.4 Format danych: strefa czasowa i identyfikator wpisu — M
- **Zmiana czasu (najbliższa: 25.10.2026, 03:00 → 02:00).** Wiersz `2026-10-25,02:30,...` jest niejednoznaczny. `mktime(isdst=-1)` i PHP `setTime` wybiorą jedną z dwóch godzin. Przerwa między 02:50 (CEST) a 02:10 (CET) wyjdzie ujemna i zostanie pominięta, a noc wydłuży się lub skróci o godzinę.
  - Szybko: zaakceptować i opisać, bo dotyczy jednej godziny dwa razy w roku.
  - Porządnie: dodać kolumnę `tz` (`+01`/`+02`) albo zapisywać epokę UTC w dodatkowej kolumnie. Parsery firmware (`parseCsvLine`, `uiParseRow`) i PHP ignorują dodatkowe kolumny po 6., więc to zmiana kompatybilna wstecz.
- **Stabilny identyfikator wpisu.** Teraz `lineIndex` to fizyczna pozycja wiersza (patrz 7.3). Opcje:
  - (a) 7. kolumna `id` (np. `base36(time)+rand`),
  - (b) w API usuwania i edycji wymagać dodatkowo `expect=<treść wiersza>` i odrzucać z `409 Conflict`, gdy wiersz się różni. To minimalna zmiana i wystarczy.
  - Tryb MySQL używa `id`, więc tam problem nie występuje.

---

## 3. Urządzenie — obraz RGB (przesuwanie, mruganie)

Stan: tryb bounce (2×80 linii) + 2 framebuffery w PSRAM, twarda resynchronizacja (`rgbHardResync`) po zapisie flasha, detektor fazy bounce↔VSYNC, resynchronizacja po nowych danych WWW. **Nieprzetestowane na sprzęcie** (log `LCD: detektor przesuniecia obrazu aktywny/NIEDOSTEPNY`).

### 3.1 Odczyty flasha też wyłączają cache — P1, M
Komentarz w `LineReader` (L558) słusznie mówi, że odczyt flasha wyłącza cache pamięci zewnętrznej. Mimo to bez `FlashWriteGuard` i bez resynchronizacji wykonywane są:
- `uiFillCharts()` (pełny skan CSV) co 2 min albo po zmianie danych (`updateHomeInformation`, L3938),
- `recomputeFeedingRhythm()` co 3 min (`uiFillLive`, L3580) — **dubluje** obliczenia `loadLatestEntries()`,
- `refreshDayStats()` przy unieważnieniu cache i o północy,
- `fileEqualsBuffer()` i `csvFileLooksValid()` w `telegramTask` po każdym pobraniu (L5116, L5132). Komentarz L4859 „odczyt nie zakłóca panelu” jest niezgodny z L558.
- Każde wejście w ekran tworzy kolejne skany (`populateDayEntries`, `entriesForDay`, `createFeedingChartScreen`).

`delay(1)` co 512 B łagodzi problem, ale go nie usuwa. **Rozwiązanie:** pkt 2.2 albo 2.3 (dane w PSRAM). W stanie spoczynku nie powinno być żadnego dostępu do LittleFS. Do czasu zmiany: detektor fazy (powód 4) powinien to łapać. Sprawdzić w logu, czy po skanach pojawia się „powód 4”.

### 3.2 Pozostałe uwagi RGB — P2/P3
- `RGB_PCLK_NORMAL_HZ` (L786–790): komentarz mówi o 12 MHz i ~42 FPS, a stała to 6 MHz (~21 FPS). Poprawić komentarz. Nie podnosić bez testów, bo pasmo PSRAM jest na granicy.
- Komentarze L1057–1066 i L1151–1160 opisują stan „bez bounce” (EDMA), który został wycofany. Przepisać, żeby nie zmyliły przy kolejnych zmianach.
- `rgbVsyncCallback` woła `esp_lcd_rgb_panel_restart` przy **każdym** VSYNC. Gdy detektor fazy potwierdzi skuteczność, rozważyć test bez restartu co klatkę (mniej pracy w ISR). Tylko z flagą i obserwacją logu.
- Do ekranu DIAGNOSTYKA dodać: liczbę twardych resynchronizacji (`rgbHardResyncCount`), ostatni powód, aktualną fazę i wzorzec. Pozwoli diagnozować bez kabla USB.
- `FlashWriteGuard` robi `delay(70)` w wątku, który go tworzy. W `loop()` to 70 ms zamrożenia UI na zapis. Akceptowalne, ale przy 2.2 zniknie samo.

---

## 4. Urządzenie — pamięć, wątki, sieć

### 4.1 Polling hostingu co 10 s z nowym TLS — P2, M
`HOST_POLL_MS = 10000` (`config.h`). Każdy `fetchRevision()` robi pełny handshake TLS: ~40 KB RAM wewnętrznego chwilowo, ~0,5–1 s CPU na rdzeniu 0, ruch Wi-Fi. Pobranie CSV to drugi handshake.
- Utrzymywać jedno połączenie keep-alive (jeden `WiFiClientSecure` w zadaniu, `Connection: keep-alive`, ponowne użycie). Hosting może zamykać połączenie, więc obsłużyć reconnect.
- Albo wydłużyć polling do 30–60 s i odświeżać natychmiast przy dotknięciu ekranu (rodzic patrzy, więc wtedy warto mieć świeże dane).
- Albo `GET /export.csv` z `If-None-Match: <rev>` → `304` bez treści. Jeden request zamiast dwóch.

### 4.2 Wysyłka na hosting bez kontroli zapisu i z czytaniem flasha w trakcie TLS — P1, S
`uploadCsvToHost()` (L4596):
- `client.print(head)`, `client.write(chunk,n)` i `client.print(tail)` **nie sprawdzają wyniku**. To ten sam błąd, który już poprawiono w backupie Telegrama (`writeAll`). Częściowy zapis daje zły `Content-Length` i odrzucenie.
- Plik czytany jest z flasha kawałkami w trakcie TLS, bez `FlashWriteGuard` (patrz 3.1).
- Naprawa: jak w `sendBackupViaTelegram` (najpierw do PSRAM, potem `writeAll`). Docelowo usunąć funkcję (2.2).

### 4.3 Powielony kod — P3, S
Parsowanie URL (`https://host[:port]/path`) jest skopiowane 3× (`uploadCsvToHost`, `fetchRevision`, `downloadCsvFromHost`), a obsługa odpowiedzi HTTP 4×. Wydzielić:
- `struct HostUrl { char host[160]; char path[256]; int port; }; bool parseHttpsUrl(const char*, HostUrl&);`
- `int httpsGet(const HostUrl&, Sink&)` z obsługą chunked / Content-Length (już jest w `downloadCsvFromHost`).

### 4.4 Zadania FreeRTOS i watchdog — P2, S
- Watchdog pilnuje tylko `loopTask`. Gdy `telegramTask` zawiśnie w TLS (np. `client.connected()` bez timeoutu w rzadkiej ścieżce), synchronizacja z hostingiem stanie na zawsze bez restartu. Dodać `esp_task_wdt_add()` dla `telegramTask` (timeout np. 60 s) i `esp_task_wdt_reset()` w pętli zadania.
- Stosy: `telegramTask` 8192, `weatherTask` 4096. W DIAGNOSTYCE dodać `uxTaskGetStackHighWaterMark()` obu zadań, żeby wiedzieć, ile zapasu zostało, zanim dojdą nowe funkcje.
- W `loop()` po odzyskaniu Wi-Fi `syncTimeFromNTP()` blokuje UI do 3 s (L6408 → `waitForNtp(3000)`). Wystarczy `beginNtp()`, bo SNTP zsynchronizuje się w tle i `uiFillLive` samo wykryje ważny czas.
- `resyncNtpIfDue()` w praktyce nic nie robi (wraca, gdy zegar działa), a SNTP i tak resynchronizuje się samo co godzinę. Usunąć.

### 4.5 RAM i fragmentacja — P2
- Dużo `String` w ścieżkach okresowych (`uiEntryLine`, `formatEntryForUi` w `loadLatestEntries` dla **każdego** karmienia w pliku, `calculateAgeText` co sekundę). Przy 2.2 zniknie. Bez 2.2: w `loadLatestEntries` zapamiętać tylko indeks lub znacznik czasu najpóźniejszego wpisu i sformatować tekst raz, po pętli.
- `uiFillLive` co sekundę woła `calculateAgeText()`, `calculateAgeDays()` 2–3× (`mktime`), `developmentTipForToday()` i `dayStats()`. Wiek i wskazówkę liczyć raz na dobę (cache po dacie).
- `WEB_APP_HTML` (84 KB, `web_panel.h`) jest dołączany do szkicu, choć serwer WWW jest wyłączony. Najpewniej linker go wyrzuca, ale nagłówek warto usunąć z `#include` (patrz 10.1).

### 4.6 Start urządzenia — P3
- `syncOnBoot()` czeka do 25 s na hosting. Przy braku internetu start trwa długo. Można skrócić do ~8 s, skoro mirror i tak dociągnie się w tle.
- `initialiseStorage()` przy błędzie montowania **formatuje** LittleFS. Przy modelu „hosting = prawda” to akceptowalne, ale warto dodać licznik formatowań w diagnostyce (sygnał zużycia lub uszkodzenia flasha).

---

## 5. Urządzenie — logika danych i znalezione błędy

| # | Prio | Błąd / uwaga | Miejsce | Naprawa |
|---|---|---|---|---|
| 5.1 | P1 | Sen „w przyszłości”: po wpisie karmienia z WWW ostatni wiersz snu to `SEN_START` o T+60 min, więc `sleepInProgress=true` z czasem startu w przyszłości („Śpi od 0m”, okno drzemki nieliczone). | `loadLatestEntries` L1543 (+ przyczyna w `api.php`, 7.2) | ignorować zdarzenia snu z czasem > teraz+1 min; usunąć auto-sen (7.2) |
| 5.2 | P1 | Parowanie snu zakłada chronologię pliku („plik jest chronologiczny”), a nie jest (edycja w miejscu, wpisy wsteczne, auto-sen). Brak limitu długości sesji, więc zapomniany STOP daje 20 h snu. | `refreshDayStats` L2195–2217 | sortować zdarzenia snu po czasie i odrzucać > `SLEEP_MAX_SESSION_MIN` (jak PHP) — albo 2.2 |
| 5.3 | P2 | `dayStats()` przy dniu spoza okna nie zeruje `bathCount` (niezainicjowana pamięć → „KAPIELI: 1234”). Dziś praktycznie nieosiągalne. | L2265–2285 | `out = DaySummary{};` na początku |
| 5.4 | P2 | `lastWeightG` = ostatni wiersz WAGA w pliku, a nie najpóźniejszy w czasie (wpis wsteczny psuje wartość startową suwaka). `uiFillCharts` liczy to poprawnie. | `loadLatestEntries` L1536 | porównywać datę jak przy karmieniu |
| 5.5 | P2 | Przerwy „dziś” zakładają kolejność wierszy w dniu. `uiFillCharts` sortuje, ale `loadLatestEntries`/`recomputeFeedingRhythm` nie. Średnia przerwa na kafelku (`avgFeedingGapMin`) bywa więc zaniżona. | L1516–1529, L1597–1611 | sortować czasy dnia przed liczeniem (jak `uiFillCharts`) |
| 5.6 | P2 | „Następne karmienie” = ostatnie + 4 h na sztywno (`COUNTER_BLINK_MIN`), także na WWW. | `nextFeedingEta` | prognoza z mediany przerw z 3–7 dni, osobno dzień/noc (patrz 12) |
| 5.7 | P2 | Wykres „Sen nocny” na urządzeniu i WWW to heurystyka z karmień (karmienie 17–20 + 90 min → pierwsze karmienie po północy − 10 min), a nie dane `SEN_*`. Nazwa wprowadza w błąd. | `uiFillCharts` L3850+, `ui_build_sleep` | nazwać „Najdłuższa nocna przerwa w karmieniu” albo liczyć z `SEN_*`, gdy są dane |
| 5.8 | P3 | Kąpiel „co 3 dni” na sztywno w firmware (`m.bathInterval = 3`), a w PHP stała `BATH_INTERVAL_DAYS`. | L3916 | stała w `config.h` |
| 5.9 | P3 | `developmentTipForToday`: tekst „po dniu 600” na sztywno, niezależny od `DEVELOPMENT_TIP_COUNT`. Wskazówki są bez polskich znaków („aby byly czytelne na domyslnym foncie”), a nowy UI ma fonty z polskimi znakami. | L2541, `development_tips.h` | przywrócić diakrytyki (jest wersja w `engine/development_tips.php`) — jedno źródło (generator) dla obu |
| 5.10 | P3 | Porady ubioru zdublowane: `dressing_guide.h` (bez polskich znaków, ze źródłami) i `UI_DRESS_PL` w `v3.ino` z warunkiem `DRESSING_BAND_COUNT == 9`. Opis pasma „Upał >30C” przy progu 35. | L3408–3427 | jedna tablica z polskimi znakami w `dressing_guide.h` |
| 5.11 | P3 | `applyAutoSleepForFeeding()` istnieje też w firmware (nieużywane przez UI). | L2959 | usunąć razem z decyzją 7.2 |

---

## 6. Urządzenie — interfejs (LVGL)

### 6.1 Martwy i półmartwy kod starego UI — P2, M
Nowy interfejs (`ui_modern.h`) zastąpił ekran główny, ale w `v3.ino` zostały stare ekrany:
- **Osiągalne:** długie przytrzymanie zegara → DIAGNOSTYKA → POWRÓT → **INNE** (Pielucha, Odciąg pokarmu). Zapisują lokalnie i robią push całego CSV (problem 2.1). Wyglądają też zupełnie inaczej niż nowy UI (stara paleta `COLOR_*`, czcionki Montserrat).
- **Nieosiągalne** (brak wywołań, tylko definicje i deklaracje):
  - formularz karmienia (`createFormScreen`, `openEntryForm*`, `performSaveForm`…),
  - kalendarz (`createCalendarScreen`, `createDayDetailScreen`, `populateDayEntries`),
  - podsumowanie z usuwaniem (`createFeedingChartScreen`, `delete*Event`),
  - ekrany SEN i WAGA (`createSleepScreen`, `createWeightScreen`, `sleepOpenEvent`, `weightOpenEvent`),
  - kafelki `createHomeInfoTile` i `createHomeNavTile`, `drawWeatherIcon`, `weatherDescPL`, `compactHomeEntry`, `feedingAgeColor`,
  - zmienne `ss*`, `homeLed*`, `form*`, stałe `FEEDING_ALARM_WINDOW_MINUTES`, `WEATHER_CITY` (opisany jako wttr.in, a używane jest Open-Meteo), `PANEL_API_BASE`, `OTA_PASSWORD`.
- **Decyzja do podjęcia:**
  - (a) urządzenie wyłącznie wyświetla: usunąć wszystko powyżej, a DIAGNOSTYKĘ przepisać w stylu `ui_modern.h` z POWROTEM do panelu;
  - (b) zostawić szybkie akcje: przenieść je do nowego UI jako duże kafelki („Pielucha mokra”, „brudna”, „Zasnął/Obudził się”) wysyłające `POST /api/event`.

  Szacunek: −1300 linii, krótsza kompilacja, mniej miejsc na błędy.

### 6.2 Drobne UX — P2/P3
- **Strona „Waga”:** po dotknięciu wykresu (widok B) panel po ≤2 min wraca do widoku A, bo `updateHomeInformation` co 120 s przebudowuje stronę (`ui_build_weight` zeruje `uiWtShown`). Zapamiętać wybrany widok poza `ui_page_reset`. To samo przy każdej nowej porcji danych z WWW.
- `uiDashUpdate(..., rebuild=true)` przebudowuje całą bieżącą stronę co 2 min (krótkie mignięcie, animacje słupków od zera przy wejściu). Przebudowywać tylko, gdy dane faktycznie się zmieniły (porównać hash istotnych pól `UiModel`).
- WHO: tablice są „chłopcy/dziewczęta uśrednione”, a Aleksander jest chłopcem. Użyć tablic WHO dla chłopców (weight-for-age, mediana i percentyle; najlepiej LMS, żeby policzyć percentyl). Skalowanie pasm WHO przez wagę urodzeniową (`ui_who_band`, `expectedWeightBand` w JS) nie jest metodą WHO. Lepiej pokazywać prawdziwą linię mediany i percentyl dziecka („~40. percentyl”). To samo w `app.js`.
- Normy przyrostu `ui_gain_per_day` (25–30 / 15–20 / 10–15 g/d) to przybliżenie. Opisać na ekranie jako orientacyjne, a źródło trzymać w jednym miejscu (2.2).
- Wygaszacz (tablica) nie ma informacji o tym, że dane z WWW są nieświeże (np. brak kontaktu > 10 min). Dodać małą kropkę lub tekst „dane sprzed 25 min”, bo to ważne, gdy rodzic ufa „ostatniemu karmieniu”.
- W nocy (`nightModeActive`) jasność 65%. Dla sypialni można dodać tryb „bardzo ciemny” (np. 21:00–6:00 tylko zegar i licznik, czerwonawe barwy).

---

## 7. Strona — backend PHP (`web-php/engine`)

### 7.1 Uwierzytelnianie — P0 (patrz 1.2)

### 7.2 Auto-sen przy karmieniu — P1, S
`api.php` L250–258: przy każdym `POST /api/entry` typu KARMIENIE:
- jeśli dziecko „śpi” → `SEN_STOP` teraz,
- `SEN_STOP` = czas karmienia − 30 min,
- `SEN_START` = czas karmienia + 60 min (**w przyszłości**).

Skutki:
- dane snu powstają z karmień, a nie z obserwacji, więc statystyki snu są fikcją,
- wiersze są poza kolejnością chronologiczną (problem dla firmware 5.2),
- do T+60 min stan pokazuje „śpi”, choć dziecko czuwa (5.1),
- przy wpisie wstecznym `SEN_STOP` ląduje w środku dawnego snu.

**Propozycja:** usunąć auto-sen albo zrobić go opcją w ustawieniach (domyślnie wyłączoną). Nigdy nie zapisywać zdarzeń w przyszłości. Jeśli rodzicom zależy na wygodzie, dodać na WWW przycisk „Zasnął po karmieniu”, który zapisuje `SEN_START` teraz.

### 7.3 Spójność zapisów CSV — P1, M
- `deleteByIndex`, `updateFeeding`, `importCsv`, `replaceRawCsv` robią odczyt `file_get_contents`, modyfikację i `file_put_contents(LOCK_EX)`. Zamek obejmuje tylko zapis, więc równoległy `append` między odczytem a zapisem **zostaje utracony**.
- `file_put_contents(…, LOCK_EX)` najpierw obcina plik, potem pisze. Czytelnik bez zamka (`allEntries`, `rawCsv`, `/export.csv` pobierany przez urządzenie) może zobaczyć pusty lub ucięty plik. Urządzenie odrzuci go walidacją, ale WWW pokaże „brak danych”.
- `bumpRevision()` to odczyt i zapis licznika bez wspólnego zamka. Dwa równoległe zapisy dają ten sam numer.

**Naprawa (jedna funkcja pomocnicza):**
```php
private function mutate(callable $fn): mixed {
    $lock = fopen($this->file . '.lock', 'c');
    flock($lock, LOCK_EX);
    try {
        $text = is_file($this->file) ? file_get_contents($this->file) : Config::CSV_HEADER . "\n";
        [$newText, $result] = $fn($text);          // czysta modyfikacja w pamieci
        if ($newText !== null && $newText !== $text) {
            $tmp = $this->file . '.tmp' . getmypid();
            file_put_contents($tmp, $newText);
            rename($tmp, $this->file);               // atomowa podmiana
            $this->bumpRevisionLocked();             // pod tym samym zamkiem
        }
        return $result;
    } finally { flock($lock, LOCK_UN); fclose($lock); }
}
```
Rewizja może być też pochodną treści (`crc32`/`sha1` pliku albo `filemtime+filesize`). Wtedy nie trzeba osobnego licznika, a tryb MySQL działa „za darmo” (7.5).

### 7.4 Usuwanie i edycja po indeksie — P1, S
`/api/delete-entry?line=N` i `/api/update-feeding?feedLine=N`: jeśli między wyświetleniem listy a kliknięciem ktoś dopisał lub usunął wiersz wcześniej, indeks wskazuje inny wpis. Przy `deleteByIndex` jest to wpis usunięty bez ostrzeżenia.
- Minimalnie: klient wysyła też `expect` (dokładna treść wiersza z `/api/entries`). Serwer porównuje pod zamkiem i przy różnicy zwraca `409` z komunikatem „Dane zmieniły się — odśwież listę”.
- Docelowo: stabilne `id` (2.4).

### 7.5 Tryb MySQL — P2
- `MysqlRepository::append/delete/update/import` przepisuje CSV (`rewriteCsvBackup`), ale **nie podbija rewizji**. W trybie MySQL urządzenie nigdy nie zobaczy zmian. Wywołać bump albo przejść na rewizję liczoną z treści (7.3).
- `lineIndex` = `id` w MySQL, a fizyczny numer wiersza w CSV. Firmware (`deleteEntryByIndex`) liczy po pozycji, więc tryby nie są zamienne. Przy 2.2 problem znika.

### 7.6 Kopie `.bakap` — P2, S
Każdy `upload-data` tworzy `RRRR-MM-DD-GG-MM-SS.bakap` bez limitu. Przy pushu z urządzenia po każdej zmianie powstają setki plików. Rotacja: trzymać np. 50 ostatnich plus jedną na dzień z ostatnich 30 dni. Plus cron z kopią poza serwer (Telegram/e-mail).

### 7.7 Drobne — P2/P3
- Wyniki `$repo->append()` są ignorowane w `api.php`: API zwraca 201 „Zapisano”, nawet gdy zapis się nie udał (np. brak miejsca lub uprawnień). Sprawdzać i zwracać 500.
- Karmienie zapisywane z WWW to kilka osobnych `append` (KARMIENIE + mleko + sen), więc kilka rewizji i niespójne stany pośrednie widoczne dla urządzenia. Pod 7.3 zrobić jeden zapis wielu wierszy.
- `/api/status` liczy wszystko od zera przy każdym wywołaniu (co 10 s na otwartą kartę). Dziś danych jest mało (~150 wierszy), ale przy roku historii warto cache'ować wynik po rewizji (plik `status.<rev>.json`).
- `param()` czyta też `$_GET` dla endpointów POST. Lepiej rozdzielić.
- `status` zwraca pola zastępcze z czasów serwera na urządzeniu (`ip`, `mdns`, `freeHeap`, `undoWindowSec`…). Usunąć, gdy `indexesp.html` zniknie (8.4).
- `Domain::loadLatestEntries`: `lastWeightG` i `lastBath` = ostatni w pliku, a nie najpóźniejszy (jak 5.4).
- Spójny styl odpowiedzi błędów (`{error:{code,message}}`) i kody HTTP: 409 przy konflikcie, 422 przy walidacji.

---

## 8. Strona — frontend (`web-php/ui`)

### 8.1 Odświeżanie i stan sieci — P2, S
- `setInterval(refresh,10000)` (app.js L1591) działa także w tle (karta niewidoczna, telefon zablokowany). Wstrzymać przy `document.hidden` (`visibilitychange`), a po powrocie od razu odświeżyć.
- Przy braku sieci `refresh()` wyświetla toast błędu **co 10 s**. Zamiast tego jeden pasek „Brak połączenia — dane z HH:MM” i wykładniczy backoff.
- Zamiast pełnego `/api/status` co 10 s najpierw tani `/api/revision`, a pełny status dopiero przy zmianie.

### 8.2 Bezpieczeństwo frontu — P2, S
- Dużo `innerHTML` z danymi z serwera (`buildEntry`, `buildTimeline`, `toast`, `renderAnalysis`…). Dziś dane to typy z białej listy i liczby, ale CSV z `upload-data`/`import` przyjmuje dowolny tekst w kolumnie `typ` (`parseCsvLine` nie waliduje typu). To otwiera drogę do XSS po wgraniu spreparowanego pliku. Dodać `esc()` dla wszystkich wstawianych napisów. Po stronie serwera walidować `typ` z białej listy przy imporcie i uploadzie.
- `fetch('/export.csv')` i `/api/...` to ścieżki absolutne. Instalacja w podkatalogu się zepsuje. Używać względnych (`api/status`).

### 8.3 PWA i wygoda rodziców — P2, M
- Dodać `manifest.webmanifest` i ikony (instalacja „na ekran główny”, pełny ekran, własna ikona).
- Service worker: cache statyków i **kolejka offline** wpisów (IndexedDB). Rodzic zapisuje karmienie w piwnicy lub na spacerze, a wpis wysyła się po odzyskaniu sieci (z oryginalnym `when`).
- Duże przyciski szybkich akcji na górze strony głównej (Karmienie / Butelka / Pielucha / Sen) z ostatnio używanymi wartościami.
- Cofnięcie ostatniego wpisu (toast „Cofnij” przez 10 s) zamiast `confirm()` przy dodawaniu.

### 8.4 Dwa interfejsy WWW — P3, S
`index.html` + `app.js` (nowy) oraz `indexesp.html` (stary, generowany skryptem PowerShell `extract_indexesp.ps1` z `web_panel.h` urządzenia). Ta sama funkcjonalność jest utrzymywana dwa razy, a `index.html` przekierowuje do starego przy `localStorage.viewMode=esp`. **Zalecenie:** zamrozić lub usunąć `indexesp.html`, `extract_indexesp.ps1` i `web_panel.h`, a z API usunąć pola używane tylko przez stary widok.

### 8.5 Struktura kodu — P3, M
- `app.js` ma 1591 linii w jednym pliku, z bardzo długimi liniami (trudne do przeglądu i diffów). Podzielić na moduły ES (`api.js`, `charts.js`, `diary.js`, `weight.js`, `sleep.js`, `memories.js`) — bez narzędzi budowania, `<script type="module">`.
- Stałe domenowe w JS na sztywno: `DISCHARGE_DAY=2`, `DISCHARGE_G=2850` (waga wypisowa), tablice WHO. Przenieść do `/api/status` (z `Config`), żeby była jedna konfiguracja.
- `index.html` ma ~95 KB z CSS w środku. Wydzielić `style.css` (cache przeglądarki).

---

## 9. Logika domenowa — wspólne uwagi

- **Jedno źródło prawdy dla reguł:** wiek, okna czuwania, normy snu, WHO, przyrosty, kąpiele. Dziś są w `config.h`, `config.php` i `app.js`, a zmiana daty urodzenia wymaga edycji 2–3 plików. Docelowo serwer (2.2) albo jeden plik JSON z konfiguracją, generujący `config.h`.
- **Okna czuwania i normy snu** (tabele Napper/Stanford) dla noworodka bywają za sztywne. Komunikaty „Przekroczone okno — ryzyko przemęczenia” i Telegram o przemęczeniu mogą niepotrzebnie stresować. Rozważyć łagodniejsze sformułowania i wyłączenie powiadomień w nocy.
- **Waga:** dla chłopca tablice chłopców (6.2), percentyl zamiast pasma skalowanego, przyrost liczony regresją z 14 dni (już jest), z minimalną liczbą pomiarów i opisem niepewności.
- **Sen:** jedna definicja sesji (sortowanie, limit długości, sen trwający, podział noc/dzień 21–07) w jednym miejscu.

---

## 10. Porządki w repozytorium i dokumentacja

### 10.1 Pliki do usunięcia lub przeniesienia (P3)
- `web_server_disabled.h` (872 linie, wyłączony serwer WWW urządzenia, mDNS, OTA) — przenieść do `archive/` albo usunąć (jest w historii git).
- `web_panel.h` (84 KB) — tylko dla wyłączonego serwera i generatora `indexesp.html`.
- Stare ekrany LVGL (6.1).
- `tools/sim/glue_extract.h` nie może trafiać do repo (generowany). Dodać do `.gitignore`.

### 10.2 Dokumentacja (P2, S)
- `README.md`: tabela „Zawartość projektu” wskazuje nieistniejące ścieżki (`OfficialWaveshareHelloWorld/...`, `LVGL_CONFIGURATION/...`, `LCD_Official_Config_Test/...`, `secrets.h.example`). Opis zapisu na urządzeniu („każde ZAPISZ dopisuje wiersz”) dotyczy starego modelu.
- `MINDTRANSFER.md`: opisuje urządzenie jako źródło prawdy, plik `OfficialWaveshareHelloWorld.ino` i gałąź `v3`. Oznaczyć jako historyczny albo zaktualizować do v4.1.
- `VALIDATION.md`: „bounce 30 linii, single framebuffer” jest nieaktualne (80 linii, 2 FB).
- `PROJECT_DESIGN.md`: tabela ekranów opisuje stary UI (KALENDARZ, DZIENNIK DNIA, PODSUMOWANIE na urządzeniu).
- `web-php/README.md`: „dane się nie synchronizują z ESP32 — to osobny zbiór” jest nieaktualne. Brakuje `GET /api/revision`, `/api/update-feeding`, `/api/bath-calendar` w kontrakcie API.
- Nagłówek `v3.ino` (L6–9): odwołanie do `secrets.h.example`.

---

## 11. Testy i automatyzacja (P2, M)

1. **GitHub Actions — kompilacja firmware** (`arduino-cli compile --fqbn esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=custom ...` z `lv_conf.h`). Błąd typu `'fileBuf' was not declared` (z 19.10) wyszedłby przed wgraniem na urządzenie. Sekrety z `secrets.h.example`.
2. **CI dla PHP:** `php -l` wszystkich plików plus mały zestaw testów `Domain` (bez frameworka: `tests/DomainTest.php` z `assert`), w tym: parowanie snu, zmiana czasu, wpisy wsteczne, mleko mieszane, `updateFeeding`, konflikt indeksów.
3. **Symulator** (`tools/sim`) już istnieje. Dodać scenariusze regresji dla `uiFillCharts` (dane z `gen_csv.py`) i porównanie wyników z PHP (ten sam CSV → te same liczby). Wykryje rozjazdy z tabeli 2.1.
4. `test_api.sh`: dodać testy autoryzacji (401/403 bez tokena), konfliktu 409 i równoległych zapisów (2× `curl &`).

---

## 12. Rozwój funkcji (z rozmowy 19.10 — do wyboru później)

Najwięcej wartości przy małym koszcie:
1. **Prognoza następnego karmienia** z mediany przerw (dzień/noc osobno, 3–7 dni) zamiast „+4 h” (5.6). Pokazać jako przedział „ok. 14:20 ± 20 min”.
2. **Mapa cieplna doby** (godzina × dzień, ostatnie 14–28 dni) na WWW: karmienia, a drugą warstwą sen.
3. **Karta „ten tydzień vs poprzedni”** (karmienia, ml, sen, pieluchy) ze strzałkami i krótkim komentarzem.
4. **Raport dla pediatry** (druk/PDF, 2–4 tygodnie): waga na siatce WHO chłopców z percentylem, średnie karmień, snu i pieluch, witamina D.
5. **Pieluchy** to dziś 1 wpis w danych, a to podstawowy wskaźnik najedzenia noworodka. Ułatwić zapis (szybki przycisk na WWW i ewentualnie na urządzeniu przez `POST /api/event`), dodać alert „mniej niż 6 mokrych na dobę” w pierwszych tygodniach.
6. Nowe typy wpisów (kompatybilne z CSV, kolumna `typ`): `TEMPERATURA` (°C×10 w `ml`), `LEK` (z nazwą — wymaga kolumny tekstowej, patrz 2.4), `SZCZEPIENIE`, `BRZUSZEK` (tummy time, minuty), `ULEWANIE`, `OBWOD_GLOWY`/`DLUGOSC` (mm).
7. Rekordy i kamienie milowe do „Wspomnień” (najdłuższa noc, 100. karmienie, 100 dni).

Każdy nowy typ trzeba dopisać w: białej liście `api.php`, `Domain::buildDayStats`, `app.js` (ikony, etykiety), a przy obecnej architekturze także w firmware. To kolejny argument za 2.2.

---

## 13. Proponowana kolejność prac

| Etap | Zakres | Wysiłek |
|---|---|---|
| **A (teraz)** | 1.1 rotacja tokenów, repo prywatne lub czyszczenie historii, usunięcie danych dziecka z repo; 1.2 logowanie i token urządzenia | S + M |
| **B** | 7.2 usunąć auto-sen; 5.1 ignorować sen w przyszłości; 6.1 zablokować zapisy ze starego ekranu „INNE” (albo przez `POST /api/event`) | S |
| **C** | 7.3 atomowe zapisy z zamkiem; 7.4 `expect`/409; 7.6 rotacja `.bakap`; 4.2 `writeAll` w uploadzie | M |
| **D** | 2.3 CSV w PSRAM i zero dostępu do flasha w spoczynku (3.1) — potwierdzić logiem, że przesunięcia znikają | M |
| **E** | 11.1–11.2 CI (kompilacja firmware, `php -l`, testy Domain) | M |
| **F** | 2.2 `/api/device-summary` + Telegram na serwerze + usunięcie CSV i Telegrama z urządzenia; 6.1 usunięcie starego UI; 10.x porządki | L |
| **G** | 8.x PWA, offline, moduły JS; 6.2/9 WHO chłopców i percentyle; 12 nowe funkcje | M–L |

Przed każdym etapem urządzenia: kompilacja w Arduino IDE, sprawdzenie w symulatorze (`make sim_ui`) i obserwacja logu `LCD:` przez kilka godzin.
