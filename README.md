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
| `host_sync.cpp` | **zadanie sieciowe (rdzeń 0)**: rewizja co 10 s → pobranie CSV → parsowanie → przekazanie do UI; co 10 min `/api/status` (ustawienie Telegrama, noc z kamery: `camNight`, `camDev`); pogoda; Telegram; kopia dzienna; cache na flashu |
| `store.cpp` | parser CSV → tablica wpisów w PSRAM, posortowana po czasie |
| `stats.cpp` | **wszystkie obliczenia** do ekranu (karmienia, przerwy, rytm, mleko, sen, okno drzemki, waga, noce, kąpiele, wiek, wskazówka) — czysty C++, testowany na PC |
| `weather.cpp`, `weather_parse.cpp` | Open-Meteo (HTTP), parser, pamięć RTC |
| `telegram.cpp` | wiadomość startowa, okno drzemki, kopia CSV (`sendDocument`) |
| `diag.cpp` | watchdog zadań, liczniki uruchomień (RTC), CPU, minimum RAM |
| `ui.cpp` + `ui_modern.h` | ekrany: start (jeż), tablica, panel (Dziś, Mleko, Odstępy, Waga, Sen, Kąpiel), diagnostyka |
| `ui_model.h` | dane do wyświetlenia (`UiModel`, `UiCam` = noc z kamery) |
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
6. **Jednorazowo** ustaw **Erase All Flash Before Sketch Upload: Enabled** (czyści stare ustawienia Wi-Fi/NVS z v4), potem wróć do Disabled.
7. **Wgraj.** Zmiana tablicy partycji czyści stare dane LittleFS z v4. To nic nie szkodzi: dane są na hostingu, a cache odtworzy się po pierwszym pobraniu.

Rozmiar (kompilacja 3.11.2026, v5.4.0, rdzeń 3.3.12): program ~2,28 MB z 8 MB, zmienne globalne 64 KB RAM.

---

## Jak działa

```
Panel WWW (PHP) ──GET /api/revision co 10 s──► zadanie sieciowe (rdzeń 0)
               ──GET /api/export.csv przy zmianie──► parser → tablica wpisów w PSRAM ──► pętla UI (rdzeń 1)
               ──GET /api/status co 10 min (powiadomienia o drzemce, noc z kamery)       │
Open-Meteo ────HTTP co 20 min──► pogoda (RAM + RTC)                                     ├─ stats.cpp → UiModel
Telegram ◄─── start, okno drzemki, kopia CSV raz dziennie                                └─ LVGL: tablica / panel / diagnostyka
```

**Ekrany i nawigacja**
- **Start:** jeż, 5 kroków — Wi-Fi, zegar, pamięć (cache), pogoda, dane z WWW.
- **Tablica:** zegar, pogoda, drzemka, pierścień od ostatniego karmienia, 3 kafelki dnia, prognoza godzinowa, ubiór, wskazówka dnia.
- **Panel:** dotknięcie tablicy. 6 kart przełączanych paskiem lub gestem; na kartach **Waga** (waga / przyrost) i **Sen** (sen z dziennika / noc z kamery) dotknięcie wykresu zmienia widok.
- **Karta Sen (od 5.2.0):** pod tytułem linia ostatniej nocy z kamery w łóżeczku, np. „Noc 1.11: zasnął po 9 min · spokój 87% · blok 1 h 03”, z kropką w kolorze oceny nocy. Dane z `/api/status` (obiekt `camNight`, odświeżane co 10 min); bez danych z kamery linii nie ma.
- **Karta Sen, widok „Noc z kamery” (od 5.3.0):** gdy hosting ma noce z kamery, dotknięcie karty przełącza widoki (jak na Wadze; dwie kropki w prawym górnym rogu, wybór zostaje po odświeżeniu danych). Drugi widok pokazuje ostatnią zakończoną noc:
  - wynik zasypiania („Zasnął po 18 min”, „Sen aktywny” albo „brak danych”), godziny położenia, zaśnięcia i końca snu oraz ocenę nocy (kropka: zielona spokojna, żółta umiarkowana, pomarańczowa niespokojna, szara mało danych);
  - pasek nocy 18:30–06:00 po pół godziny: fioletowy = spokój, żółty = ruch (im mocniejszy, tym więcej ruchu), pusta ramka = dziecko poza łóżeczkiem (np. karmienie), szary = brak danych; czerwona kropka nad paskiem = płacz; zielona kreska pod paskiem = okres snu;
  - kafelki: spokój (% czasu w łóżeczku), najdłuższy blok bez ruchu, przebudzenia (z liczbą płaczów);
  - ostatnie 7 nocy: słupki spokojnego snu w godzinach (kolor = ocena nocy) i linia porównania z 7 wcześniejszymi nocami (mediana zasypiania, średni spokojny sen; pojawia się, gdy obie grupy mają co najmniej 4 noce).
  Dane liczy hosting (obiekt `camDev` w `/api/status`, odświeżany co 10 min). „Co wpływa na sen” jest tylko na stronie WWW. Bez nocy z kamery karta wygląda jak dawniej (jeden widok).
- **Karta Waga, wykres przyrostu (od 5.3.0):** liczony jak na hostingu — słupek dla **każdego** tygodnia życia od 1 do tygodnia ostatniego ważenia (bez dziur i bez pustych tygodni na przyszłość). Wartość to trend z ważeń z okna ±7 dni; gdy ważeń jest mało, okno rośnie (±10, ±14, ±21 dni), a w ostateczności wartość wynika z dwóch najbliższych ważeń — taki słupek jest jaśniejszy, z obwódką („szacunek”). Kilka ważeń jednego dnia jest uśrednianych. Tło słupka = zakres normy dla wieku (25–30, 15–20, 10–15 g/dzień), kolor: zielony w normie, pomarańczowy poniżej, niebieski powyżej, czerwony spadek. Kafelek „Ostatnie 2 tygodnie” liczony tak samo jak na stronie.
- **Powroty i diagnostyka:** liść w panelu wraca do tablicy; po 2 min bezczynności panel też wraca do tablicy. Długie przytrzymanie zegara w panelu otwiera diagnostykę.

**Obsługa dotyku (od 5.4.1).** Kontroler GT911 potrafi w środku jednego dotknięcia na kilka milisekund zgłosić „brak dotyku”. Biblioteka LVGL widziała wtedy puszczenie i ponowne dotknięcie, więc jedno dotknięcie przełączało widok (Waga, Sen), kartę lub ekran kilka razy. Są dwa zabezpieczenia:
- `hw_touch.cpp`: po ostatnim odczycie z dotykiem stan „wciśnięty” trzymany jest jeszcze 60 ms (`TOUCH_RELEASE_HOLD_MS`), więc krótki zanik odczytu nie kończy dotknięcia. Puszczenie palca jest przez to opóźnione o 60 ms, na co nie zwraca się uwagi.
- `ui_modern.h`, funkcja `ui_tap_ok()`: wspólny odstęp między przełączeniami — 350 ms dla widoków Waga/Sen, 250 ms dla paska kart, 300 ms dla przesunięcia gestem, 500 ms dla liścia (powrót do tablicy) i powrotu z diagnostyki. Kolejne kliknięcie w tym czasie jest ignorowane.
Jeśli nadal zdarzają się podwójne przełączenia, zwiększ `TOUCH_RELEASE_HOLD_MS` (np. do 100) w `hw_touch.cpp`. Jeśli dotyk wydaje się ociężały (zdarza się, gdy ktoś robi bardzo szybkie dwa dotknięcia), zmniejsz odstępy w `ui_tap_ok()`.

**Tryb nocny** (21:00–7:00): ciemna paleta i jasność ~65%. W dzień ekran przyciemnia się po 60 s.

**Brak internetu przy starcie:** urządzenie pokazuje ostatnie dane z cache. Cache jest zapisywany tylko przy zmianie danych, najwyżej raz na 30 min — to **jedyny zapis do flasha** w pracy urządzenia.

**Powiadomienia o oknie drzemki** włącza się na **stronie WWW** (przełącznik „Sen: powiadomienia Telegram”). Urządzenie tylko odczytuje to ustawienie.

**Stabilność obrazu**
- Przyczyną przesuwania obrazu o 80 linii i poziomych artefaktów był licznik przerwań w sterowniku ESP-IDF. W rdzeniu Arduino 3.3.x z `RESTART_IN_VSYNC` nigdy nie był zerowany. Od v5.1.0 urządzenie zeruje go w każdej klatce. Szczegóły: `ANALIZA_V5.md` 7.4, model `tools/sim5/rgb_bounce_model.py`.
- Przy zapisie flasha i starcie Wi-Fi zegar pikseli spada na chwilę do 1 MHz.
- Diagnostyka (sekcja OBRAZ) i polecenie `l` w monitorze portu pokazują, ile klatek było zakłóconych i naprawionych.

**Polecenia w monitorze portu (115200):**
- `i` — diagnostyka,
- `l` — statystyki obrazu,
- `h` — ręczna twarda resynchronizacja.

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

**Wynik obliczeń** porównany z v4.1 na tym samym pliku 40 dni danych (`tools/sim5`): identyczne wykresy tygodni, nocy, kąpieli i wagi. Od 5.3.0 wykres przyrostu wagi celowo różni się od v4.1 — liczony jest jak na hostingu (każdy tydzień, szacunki).

---

## Diagnostyka i rozwiązywanie problemów

### Po wgraniu nowego firmware (v5.4.1): co sprawdzić

Otwórz monitor portu (115200). W logu startowym ma być, kolejno:

| Linia w logu | Znaczenie |
|---|---|
| `Lesny Dziennik v5.4.1 — tylko wyswietlacz.` | wgrała się właściwa wersja |
| `LCD: poprawka licznika EOF aktywna (pole +296 B, blokada tak).` | **poprawka przesuwania obrazu działa** (sterownik ekranu znaleziony i zabezpieczony) |
| `LCD: 2 FB w PSRAM, bounce 80 linii, wolny RAM wewn. … KB` | ekran wystartował |
| `Wi-Fi (...): ... IP ..., ... dBm` | Wi-Fi połączone; sygnał słabszy niż ok. −75 dBm powoduje błędy sieci |
| `Host: CSV pobrany (... wpisow ...)` | dane z hostingu pobrane |

Warianty linii o ekranie:
- `LCD: struktura sterownika nie pasuje ...` albo `LCD: nie znaleziono pol sterownika ...` — poprawka jest wyłączona (inna wersja rdzenia ESP32 niż 3.3.12). Wyślij tę linię razem z wersją rdzenia.
- `LCD: rdzen bez RESTART_IN_VSYNC ...` — rdzeń sam zeruje licznik, poprawka niepotrzebna.

Przez kilka minut obserwuj obraz: ma być prosty, bez poziomych artefaktów, także zaraz po restarcie i po zmianie wpisów na stronie. Od 5.4.0 animowane elementy (słupki, przesuwanie stron panelu, chmury, krople, pierścień) nie mogą mrugać ani być niedorysowane: po kilku minutach wyślij `l` — „rysowanie w wyswietlanym buforze” ma być **0**.

### Polecenia w monitorze portu (115200, wyślij jedną literę)

| Znak | Co robi |
|---|---|
| `i` | otwiera ekran diagnostyki |
| `l` | wypisuje statystyki obrazu: czy poprawka działa, liczba klatek, **zakłócone klatki** i ile z nich naprawiono, spóźnione VSYNC, zapisy flasha; od 5.4.0 także: rysowanie w wyświetlanym buforze (ma być 0), klatki pokazane o klatkę później, czas rysowania (średni i maks. w ostatniej minucie), czas od VSYNC do przełączenia bufora (ok. 30–32 ms), najdłuższą pracę pętli; oraz co ile sekund i jak (HTTP/HTTPS) sprawdzana jest rewizja danych |
| `h` | ręczna twarda resynchronizacja obrazu |

Ekran diagnostyki na urządzeniu: dotknij panel (z tablicy), potem **przytrzymaj zegar** albo tytuł (długie dotknięcie). Są tam sekcje m.in. sieć/host (rewizja, błędy pobierania, cache), pamięć, obraz.

### Typowe problemy

| Objaw | Przyczyna | Co zrobić |
|---|---|---|
| Obraz przesunięty o ok. 1/6 ekranu, poziome pasy | licznik przerwań EOF sterownika ekranu (naprawione w 5.1.0) | wgraj 5.1.0; sprawdź w logu linię „poprawka licznika EOF aktywna”; jeśli jest, a obraz nadal skacze, wyślij wynik polecenia `l` |
| Jedno dotknięcie przełącza widok / kartę kilka razy | zaniki odczytu dotyku GT911 (naprawione w 5.4.1) | wgraj 5.4.1; opis w „Obsługa dotyku”; gdy nadal się zdarza, zwiększ `TOUCH_RELEASE_HOLD_MS` w `hw_touch.cpp` |
| `Host: rewizja — blad (HTTP 0)` | brak sieci albo nieudany handshake TLS (słaby sygnał) | zbliż urządzenie do routera; zobacz dBm w logu; 5.0.2+ ponawia połączenie i opisuje powód |
| `Wi-Fi: rozlaczono, powod N` | kod powodu rozłączenia z routera (np. 201 = nie widać sieci, 15 = przerwane uzgadnianie hasła, 202 = błąd uwierzytelnienia) | sprawdź SSID i hasło w `secrets.h`, sygnał, router |
| `Cache: plik uszkodzony — pomijam` | pierwszy start po zmianie partycji | normalne; cache odtworzy się po pobraniu danych |
| Ekran startowy utknął na kroku „dane z WWW” | hosting niedostępny albo błąd 500 strony | otwórz stronę w przeglądarce; opis błędu 500: `web-php/README.md` rozdział 5; urządzenie pokaże dane z cache |
| `BLAD: brak PSRAM` | zły wybór w Narzędziach | PSRAM = **OPI PSRAM** |
| Animowane elementy czasem mrugają albo są niedorysowane | LVGL rysował w buforze, który ekran jeszcze wyświetlał (naprawione w 5.4.0; opis: repo `wiewiornik`, `2112026.md` rozdział G) | wgraj 5.4.0; `l`: „rysowanie w wyswietlanym buforze” = 0, „czekanie >400 ms” = 0 |
| Wpis z telefonu pojawia się na urządzeniu po ok. minucie | od 5.4.0 rewizja sprawdzana co 10 s tylko, gdy ktoś dotykał ekranu w ostatnich 5 min albo dane zmieniły się w ostatnich 10 min; inaczej co 30 s (dzień) / 60 s (noc) | dotknij ekranu — od razu wraca 10 s |
| Dane na urządzeniu nie zmieniają się po wpisie na stronie | rewizja nie rośnie albo host nie odpowiada | `https://phpmapy1.webd.pro/api/revision` w przeglądarce; liczba `rev` powinna rosnąć po zapisie |

## Testy na PC

```bash
cd tools/sim5
g++ -std=c++17 -O1 -I../../v5 test_stats.cpp ../../v5/store.cpp ../../v5/stats.cpp -o test_stats
python3 ../sim/gen_csv.py                      # syntetyczne dane do /tmp/fw_data.csv
TZ="CET-1CEST,M3.5.0,M10.5.0/3" ./test_stats /tmp/fw_data.csv
```

Zrzuty ekranów robi symulator LVGL (`tools/sim`, scenariusz `tools/sim5/scen_v5.h`, opis w pliku).

---

## Panel WWW i kamera

- **Strona na hostingu** (`web-php/`): jedyne miejsce zapisu danych. Pełny opis — układ na hostingu, aktualizacja plik po pliku, API, wykres wagi, błąd 500: **`web-php/README.md`**.
- **Jakość snu nocą z kamery:** serwer domowy t620 wysyła co rano dane nocy na stronę (`POST /api/sleep-night`); wykres jest w Sen → „Jakość snu nocą”. Opis strony: `web-php/README.md` rozdział 10; konfiguracja serwera domowego: repozytorium `wiewiornik` (prywatne), `INSTRUKCJA.md` rozdział 3c2.
- Urządzenie (od 5.3.0) nie rozmawia z kamerą ani z t620: bierze gotowy skrót ostatniej nocy ze strony (obiekty `camNight` i `camDev`; od 5.4.0 z lekkiego `/api/device`, a gdy hosting go jeszcze nie ma — z `/api/status`) i pokazuje go na karcie **Sen** (drugi widok „Noc z kamery”). Analiza i „Co wpływa na sen” są tylko na stronie.
- Zmiany samej strony (wykresy, analiza) wymagają tylko aktualizacji plików na hostingu, bez wgrywania firmware. Firmware trzeba wgrać dopiero przy zmianach z folderu `v5/`.

### Aktualizacja firmware do 5.4.1 (z 5.3.0 lub 5.4.0)

1. Hosting (najpierw, ale firmware działa też ze starą stroną): wgraj pliki strony z rozdziału 4 `web-php/README.md` — m.in. `api.php` (nowe `/api/device`) i nowy folder `fonts/`.
2. Z folderu `v5` gałęzi `v5` podmień: `app.cpp`, `app_config.h`, `host_sync.cpp`, `host_sync.h`, `hw_display.cpp`, `hw_display.h`, `hw_touch.cpp`, `net.cpp`, `net.h`, `stats.cpp`, `ui_modern.h` (przy aktualizacji z 5.4.0 wystarczą `app_config.h`, `hw_touch.cpp`, `ui_modern.h`). Swój `secrets.h` zostaw. Biblioteki bez zmian (sprawdzone kompilacją: rdzeń 3.3.12, lvgl 9.3.0, GFX 1.6.9, SensorLib 0.5.1).
3. Wgraj jak zwykle. Nie trzeba czyścić flasha.
4. Sprawdź: w logu `v5.4.1`; jedno dotknięcie przełącza widok / kartę tylko raz; po kilku minutach polecenie `l` — „rysowanie w wyswietlanym buforze 0”, druga linia `Host: rewizja co 30 s przez HTTP` (albo `HTTPS`, jeśli hosting nie obsługuje zwykłego HTTP — też dobrze) i `ustawienia z /api/device`.

### Aktualizacja firmware do 5.3.0 (z 5.2.0 lub wcześniejszych; dziś lepiej od razu do 5.4.1 jak wyżej)

1. Hosting: wgraj do `/home/phpmapy1/panel-engine/` pliki `api.php` i `SleepStats.php` z `web-php/engine/` (bez nich karta Sen zostaje z jednym widokiem; nic się nie psuje).
2. Pobierz gałąź `v5` (Code → Download ZIP) i z folderu `v5` podmień u siebie: `app.cpp`, `app_config.h`, `host_sync.cpp`, `host_sync.h`, `hw_touch.cpp`, `stats.cpp`, `ui_model.h`, `ui_modern.h`. Swój `secrets.h` zostaw (przy aktualizacji z 5.3.0 wystarczą `app_config.h`, `hw_touch.cpp`, `ui_modern.h`).
3. Wgraj jak zwykle (ustawienia jak w rozdziale „Kompilacja”). Nie trzeba czyścić flasha.
4. Sprawdź: w logu `v5.3.0` (lub nowszy); na karcie Sen kropki w prawym górnym rogu i po dotknięciu widok „Noc z kamery” (pojawia się do 10 min po starcie, gdy hosting ma przynajmniej jedną zakończoną noc); na karcie Waga wykres przyrostu ze słupkami w każdym tygodniu.

### Historia wersji firmware v5

| Wersja | Zmiany |
|---|---|
| 5.0.x | urządzenie tylko wyświetla; dane z hostingu; cache na flashu |
| 5.1.0 | poprawka licznika EOF sterownika ekranu (przesuwanie obrazu), detektor przesunięcia, polecenia `l`, `h` |
| 5.2.0 | linia ostatniej nocy z kamery na karcie Sen (`camNight`) |
| 5.3.0 | karta Sen: drugi widok „Noc z kamery” (`camDev`); wykres przyrostu wagi jak na hostingu (każdy tydzień, szacunki, norma) |
| 5.4.0 | koniec mrugania animacji (LVGL czeka, aż sterownik przełączy bufor: `on_frame_buf_complete` + `lv_display_set_flush_wait_cb`), liczniki obrazu w `l` i diagnostyce; krótsze przesuwanie stron i wzrost słupków; rewizja co 10/30/60 s zależnie od użycia i zwykłym HTTP (zapas HTTPS); `/api/device` zamiast `/api/status`; sumy dnia liczone tylko po zmianie danych albo minuty |
| 5.4.1 | obsługa dotyku: jedno dotknięcie = jedno przełączenie (odporność na zaniki odczytu GT911 + odstęp między kliknięciami), patrz „Obsługa dotyku” |

Ważne uwagi z przeglądu (`ulepszenia19-10-26.md`):
- API nie ma uwierzytelniania — hasła dla całego portalu zostaną dodane później jednym ruchem (P0),
- auto-sen przy każdym karmieniu dopisuje zdarzenia w przyszłości (P1),
- zapisy CSV mają wyścigi (P1).
