# Leśny Dziennik Aleksandra — panel WWW (PHP)

Strona opieki nad niemowlęciem: wpisy (karmienia, sen, pieluchy, waga, kąpiele), statystyki, wykresy i jakość snu nocą z kamery. Aplikacja PHP na hostingu cPanel (webd.pl, `phpmapy1.webd.pro`). Jest **jedynym miejscem zapisu danych**; urządzenie ESP32 (`v5/`) tylko je pobiera i wyświetla.

## Spis treści

1. [Budowa: silnik i warstwa wizualna](#1-budowa-silnik-i-warstwa-wizualna)
2. [Dane i pliki, które tworzy strona](#2-dane-i-pliki-które-tworzy-strona)
3. [Wdrożenie na hostingu](#3-wdrożenie-na-hostingu)
4. [Aktualizacja strony na hostingu (plik po pliku)](#4-aktualizacja-strony-na-hostingu-plik-po-pliku)
5. [Błąd 500 i inne awarie](#5-błąd-500-i-inne-awarie)
6. [Dwie wersje strony](#6-dwie-wersje-strony)
7. [API — wszystkie endpointy](#7-api--wszystkie-endpointy)
8. [Wgrywanie całego CSV: `POST /api/upload-data`](#8-wgrywanie-całego-csv-post-apiupload-data)
9. [Wykres przyrostu wagi — słupki tygodniowe](#9-wykres-przyrostu-wagi--słupki-tygodniowe)
10. [Jakość snu nocą (kamera)](#10-jakość-snu-nocą-kamera)
11. [Włączenie MySQL](#11-włączenie-mysql)
12. [Uruchomienie lokalne i testy](#12-uruchomienie-lokalne-i-testy)
13. [Bezpieczeństwo i czego jeszcze nie ma](#13-bezpieczeństwo-i-czego-jeszcze-nie-ma)

---

## 1. Budowa: silnik i warstwa wizualna

```
web-php/
├── engine/      ← SILNIK DANYCH  (logika, przechowywanie, API; CSV / MySQL)
├── ui/          ← WARSTWA WIZUALNA (to, co użytkownik widzi i klika)
└── data/        ← plik CSV z danymi (źródło prawdy) i katalog noc/ z nocami z kamery
```

Podział jest celowy: **silnik** wystawia API (JSON), z którego korzysta strona WWW oraz urządzenia łączące się zdalnie. UI nie dotyka danych, wszystko robi przez API. Dzięki temu MySQL włącza się wyłącznie w silniku, bez zmian w UI.

**Droga żądania:** przeglądarka → `ui/index.php` (router) → dla `/api/*` i `/export.csv` ładuje `engine/api.php` i wywołuje `handle_api()`; dla reszty serwuje `index.html`, `app.js` itd.

### Silnik (`engine/`)

| Plik | Rola |
|---|---|
| `config.php` | stałe (data urodzenia, zakresy ml i wagi, progi przyrostu, okna czuwania, Napper) + wybór magazynu + dane MySQL + ścieżki plików danych |
| `Domain.php` | czysta logika, bez zapisu: wiek, statystyki dnia, sen i okna drzemek, rytm, kąpiele, serie wagi i słupki przyrostu, parser CSV |
| `Repository.php` | kontrakt magazynu (interfejs) |
| `CsvRepository.php` | magazyn CSV (domyślny, działa od razu) |
| `MysqlRepository.php` | magazyn MySQL **+ kopia CSV przy każdym zapisie** |
| `bootstrap.php` | wybiera magazyn wg configu (przy awarii bazy wraca do CSV) |
| `api.php` | publiczne API HTTP (`/api/*`, `/export.csv`) + CORS |
| `SleepNights.php` | noce z kamery: odbiór pliku nocy, wskaźniki, magazyn `data/noce/` |
| `development_tips.php` | wskazówki dnia 0–600 |
| `schema.sql` | schemat tabel MySQL |
| `.htaccess` | blokuje bezpośredni dostęp do `*.php` i `*.sql` poza `api.php` |

**CSV zawsze jest backupem.** W trybie `mysql` każdy zapis idzie równolegle do CSV w tym samym formacie co urządzenie: `data,godzina,typ,ml,piers_lewa_min,piers_prawa_min`. W trybie `csv` (domyślnym) CSV jest źródłem prawdy.

### Warstwa wizualna (`ui/`)

| Plik | Rola |
|---|---|
| `index.php` | punkt wejścia i router: serwuje UI i statyki, deleguje `/api/*` do silnika |
| `index.html` | **nowoczesna** strona: struktura + design (szkło, dzień/noc), wykresy, jakość snu nocą |
| `indexesp.html` | **prosta** strona (zapasowa, lekka); ta sama funkcjonalność danych, bez zmian graficznych |
| `app.js` | interakcje i wykresy nowoczesnej strony; woła API |
| `paths.php` | **jedyny plik ze ścieżkami** do `engine/` i `data/`; na hostingu wykrywa układ automatycznie (rozdział 3) |
| `.htaccess` | przekierowanie wszystkiego do `index.php`, blokada bezpośredniego `paths.php`; **na hostingu zawiera też wybór wersji PHP** (rozdział 4) |

## 2. Dane i pliki, które tworzy strona

Wszystko w katalogu danych (`panel-data/` na hostingu, `data/` w repo). Katalog musi być zapisywalny dla PHP.

| Plik | Zawartość |
|---|---|
| `karmienia.csv` | **dane** (źródło prawdy): wszystkie wpisy |
| `karmienia.rev` | numer rewizji; rośnie przy każdej zmianie. Urządzenie sprawdza go co 10 s (`GET /api/revision`) i pobiera CSV tylko gdy się zmienił |
| `karmienia_backup.csv` | kopia bieżąca (tworzona przy zapisie; nie w repo) |
| `ustawienia.cfg` | ustawienia z panelu (np. powiadomienia Telegram o drzemce) |
| `RRRR-MM-DD-GG-MM-SS.bakap` | kopie sprzed wgrania całego CSV (rozdział 8) |
| `noce/noc_RRRR-MM-DD.json`, `noce/index.json` | noce z kamery i ich wskaźniki (rozdział 10) |

Format CSV: nagłówek `data,godzina,typ,ml,piers_lewa_min,piers_prawa_min`, jeden wpis na wiersz. Typy: `KARMIENIE` (pierś, minuty w kolumnach `piers_*`), `MLEKO_MATKI`, `MLEKO_MODYFIKOWANE`, `PIELUCHA_MOKRA`, `PIELUCHA_BRUDNA`, `WITAMINA_D`, `ODCIAGANIE`, `WAGA` (w polu `ml` są gramy), `SEN_START`, `SEN_STOP`, `KAPIEL`.

## 3. Wdrożenie na hostingu

### 3.1 Układ katalogów (taki jest na `phpmapy1.webd.pro`)

```
/home/phpmapy1/public_html/   ← zawartość web-php/ui/      (index.php, index.html, indexesp.html, app.js, paths.php, .htaccess)
/home/phpmapy1/panel-engine/  ← zawartość web-php/engine/
/home/phpmapy1/panel-data/    ← zawartość web-php/data/    (zapisywalny; tu także noce/)
```

Dane i silnik leżą **poza** katalogiem publicznym. `ui/paths.php` sam rozpoznaje ten układ: jeśli obok `public_html` istnieje katalog `panel-engine`, używa `panel-engine` i `panel-data`; w przeciwnym razie przyjmuje układ z repo (`ui/`, `engine/`, `data/` obok siebie). **Nie trzeba go edytować.**

### 3.2 Wersja PHP

Strona wymaga **PHP 8.x** (używa `declare(strict_types=1)` i typów zwracanych; na PHP 5.6 daje błąd 500). W cPanel: **MultiPHP Manager** → domena → wybierz **8.x**. Zapisanie tego wyboru dopisuje do `public_html/.htaccess` linie `AddHandler`/`SetHandler` z wersją PHP; **dlatego pliku `.htaccess` na hostingu nigdy się nie nadpisuje** (rozdział 4). Kopia poprzedniego `.htaccess` bywa w `.htaccess.backup.*`.

### 3.3 Prawa zapisu

`panel-data/` (i podkatalog `noce/`, który tworzy się sam) musi być zapisywalny dla PHP: w cPanel prawa `755` lub `775`.

### 3.4 Prostszy wariant

Jeśli `ui/`, `engine/`, `data/` leżą obok siebie w jednym katalogu (np. w `public_html`), strona też działa; wtedy chronią je `.htaccess` w `engine/` i `data/`. Mniej bezpieczny niż rozdzielony układ.

## 4. Aktualizacja strony na hostingu (plik po pliku)

Bez gita, przez **Menedżer plików** cPanel albo FTP. Pliki bierzesz z repo: GitHub → gałąź → plik → **Download raw file**.

**Wgraj (nadpisz):**

| Z repo | Na hosting |
|---|---|
| `web-php/ui/index.php`, `index.html`, `indexesp.html`, `app.js` | `/home/phpmapy1/public_html/` |
| `web-php/engine/*.php` (`api.php`, `Domain.php`, `config.php`, `SleepNights.php`, `bootstrap.php`, `CsvRepository.php`, `MysqlRepository.php`, `Repository.php`, `development_tips.php`) | `/home/phpmapy1/panel-engine/` |

**NIE nadpisuj:**

| Plik na hostingu | Dlaczego |
|---|---|
| `public_html/.htaccess` | ma wybór PHP 8 z cPanel; po nadpisaniu wraca PHP 5.6 i strona daje **błąd 500** |
| `public_html/paths.php` | wskazuje `panel-engine`/`panel-data` (wersja z repo rozpoznaje to sama, ale nie ma powodu jej podmieniać) |
| `panel-data/*` | to są **dane** (CSV, ustawienia, noce) |

Po wgraniu: odśwież stronę **Ctrl+F5** (przeglądarka trzyma starą wersję `app.js`).

**Szybki test po aktualizacji** (w przeglądarce): `https://phpmapy1.webd.pro/api/revision` → `{"rev":...}`; `https://phpmapy1.webd.pro/api/sleep-nights?days=14` → `{"nights":[...]}` (lista może być pusta).

## 5. Błąd 500 i inne awarie

Strona biała lub „500 Internal Server Error”. Zwykle `error_log` nie istnieje (hosting go nie tworzy), więc diagnozuje się po objawach:

| Objaw | Przyczyna | Naprawa |
|---|---|---|
| 500 na każdej podstronie, zaraz po wgraniu plików | `public_html/.htaccess` został nadpisany plikiem z repo i hosting wrócił do PHP 5.6 | cPanel → MultiPHP Manager → ustaw PHP 8.x; ewentualnie przywróć `.htaccess` z `.htaccess.backup.*` |
| 500, a PHP 8 ustawiony | `paths.php` wskazuje nieistniejący katalog (`engine BRAK`) | wgraj `ui/paths.php` z repo (wykrywa układ automatycznie); sprawdź, że obok `public_html` są `panel-engine` i `panel-data` |
| Strona działa, ale nie zapisuje | `panel-data` bez prawa zapisu | prawa `755`/`775` dla `panel-data` |
| `Pusty ekran` po zmianie `app.js` | stara pamięć przeglądarki | Ctrl+F5 |
| `403 Forbidden` przy wysyłce z urządzenia/serwera | firewall hostingu (BitNinja) blokuje surowe body POST | wysyłaj **multipart** (pole `file`) |
| `HTTP 404` na `/api/sleep-night` | na hostingu stary `api.php` lub `SleepNights.php` | wgraj pliki z rozdziału 4 |
| Certyfikat samopodpisany (błąd SSL u klienta) | hosting nie ma zaufanego certyfikatu | urządzenie ESP32 nie weryfikuje certyfikatu; zapisywacz kamer ma `WYSYLKA_SSL_SPRAWDZ=0` |

Gdy nic nie pomaga: tymczasowy plik diagnostyczny w `public_html/` (np. `diag.php` z `phpversion()` i `is_dir()` do `panel-engine`/`panel-data`), a po użyciu **usuń go**.

## 6. Dwie wersje strony

- **Nowoczesna** (`index.html` + `app.js`): domyślna. Pełny wygląd, wykresy, sekcja „Jakość snu nocą”.
- **Prosta** (`indexesp.html`): lekka wersja zapasowa. Zmiany **funkcjonalne** (dane, wagi, odświeżanie) trafiają do obu; zmiany **graficzne** i wykres snu tylko do nowoczesnej.

## 7. API — wszystkie endpointy

Adresy względem strony: `https://phpmapy1.webd.pro/api/<nazwa>` (albo wprost `…/engine/api.php?route=<nazwa>`). Wszystkie bez uwierzytelniania (rozdział 13).

| Metoda i adres | Co robi |
|---|---|
| `GET /export.csv` i `GET /api/export.csv` | cały CSV; pobiera go urządzenie |
| `GET /api/revision` | `{"rev":N,"count":liczba wpisów,"updatedAt":…}`; `rev` rośnie przy każdej zmianie danych |
| `GET /api/status` | stan do panelu i urządzenia: wiek, statystyki dnia, sen (`czuwa`/`okno`/`przekroczone`/`spi`), ustawienia (`sleepTelegram`) |
| `GET /api/entries?date=RRRR-MM-DD` | wpisy z jednego dnia |
| `GET /api/weight-series` | `points` (ważenia), `weeks` (słupki przyrostu), `recent` (trend ~14 dni), `today` (dzień życia), `birthWeightG` |
| `GET /api/bath-calendar` | kąpiele: kalendarz i podsumowanie |
| `POST /api/entry` | nowe karmienie (`type`, `when`, `ml`, piersi) |
| `POST /api/update-feeding` | edycja karmienia (`feedLine`, `when`, mleko) |
| `POST /api/delete-entry` | usunięcie wpisu (`line`) |
| `POST /api/event` | zdarzenia: `PIELUCHA_MOKRA`, `PIELUCHA_BRUDNA`, `WITAMINA_D`, `ODCIAGANIE`, `WAGA` (gramy w `ml`), `SEN_START`, `SEN_STOP`, `KAPIEL`; opcjonalnie `when` (nie w przyszłości) |
| `POST /api/setting` | ustawienie `sleepTelegram` (`key`, `value`) |
| `POST /api/import` | import CSV z panelu (surowe body, limit 512 KB) |
| `POST /api/upload-data` | podmiana całego CSV z kopią zapasową (rozdział 8) |
| `POST /api/send-backup` | w tej kopii: zawsze błąd (Telegram obsługuje urządzenie) |
| `POST /api/sleep-night` | odbiór nocy z kamery (rozdział 10) |
| `GET /api/sleep-nights?days=14` | lista nocy ze wskaźnikami, od najstarszej (max 400) |
| `GET /api/sleep-night?night=RRRR-MM-DD` | szczegóły jednej nocy (oś minut, godziny, ruchy) lub `404` |

CORS jest domyślnie wyłączony (API działa same-origin). Gdy urządzenia będą łączyć się z innego originu, ustaw `Config::corsOrigin()` (albo zmienną `CORS_ORIGIN`) na konkretny origin lub `*`.

## 8. Wgrywanie całego CSV: `POST /api/upload-data`

Strona **najpierw robi kopię** dotychczasowych danych pod nazwą `RRRR-MM-DD-GG-MM-SS.bakap` (w katalogu danych), a potem **podmienia** dane. Plik jest walidowany: musi mieć nagłówek `data,godzina,typ,ml,piers_lewa_min,piers_prawa_min` i co najmniej jeden poprawny wiersz; niepoprawne wiersze są pomijane.

- **Ciało żądania:** **multipart/form-data** z polem `file` (**zalecane**, przechodzi przez firewall) albo surowy CSV (`Content-Type: text/csv`; hosting potrafi to blokować kodem 403).
- **Limit:** 512 KB.
- **Token (opcjonalny):** zmienna środowiskowa `UPLOAD_TOKEN=sekret` na serwerze; wtedy żądanie musi podać go w nagłówku `X-Upload-Token` albo w `?token=`. Gdy pusty, endpoint jest otwarty.
- **Odpowiedź:** `200` `{"message":"…","backup":"RRRR-MM-DD-GG-MM-SS.bakap","lines":N}`; błędy `400` (pusty, za duży, niepoprawny), `403` (zły token).

```bash
curl -X POST "https://phpmapy1.webd.pro/api/upload-data" -F "file=@karmienia.csv"
```

W prawym górnym rogu panelu jest tymczasowy link **„wgraj CSV”** (ten sam endpoint, z przeglądarki).

> **Firewall hostingu (WAF):** webd.pl używa BitNinja, który blokuje surowe body POST do `/api/*` stroną „403 Forbidden”. Dlatego panel, urządzenie i zapisywacz kamer wysyłają dane jako **multipart**.

## 9. Wykres przyrostu wagi — słupki tygodniowe

Serwer (`Domain::weightWeeks`) liczy jeden słupek na **każdy** tydzień życia, od 1. do tygodnia ostatniego ważenia; obie strony tylko je rysują. Wartość (g/dzień) to trend z ważeń (regresja liniowa), bo pojedyncze ważenia różnią się nawet o 100 g:

1. okno ±7 dni wokół środka tygodnia (co najmniej 2 ważenia odległe o ≥ 5 dni),
2. przy mniejszej liczbie ważeń okno rośnie do ±10, ±14, ±21 dni,
3. w ostateczności wartość wynika z dwóch najbliższych ważeń przed i po tygodniu.

Kroki 2–3 oznaczają słupek jako `estimated` (jaśniejszy, z przerywaną ramką, w legendzie „szacunek (mało ważeń)”). Waga urodzeniowa (`BIRTH_WEIGHT_G`) to punkt dnia 0. W pierwszych 2 dniach życia (`GAIN_DISCHARGE_DAY`) nie ma pasma odniesienia (spadek wagi po porodzie).

**Pasma odniesienia** (`config.php`): do 91. dnia 25–30 g/dzień, do 183. dnia 15–20, potem 10–15.

**Odświeżanie:** strona wagi (nowoczesna) i okno wagi (prosta) co 10 s sprawdzają `/api/weight-series` i rysują oba wykresy od nowa, gdy zmienią się pomiary albo dzień życia. Po zapisie pomiaru odświeżają się oba wykresy.

## 10. Jakość snu nocą (kamera)

### 10.1 Skąd dane

Serwer domowy (t620, kontener „kamery”, `zapisywacz.py` z repo **wiewiornik**) wykrywa ruch w łóżeczku i co rano, ok. 3 min po 6:00, wysyła plik danych zakończonej nocy. Jak to się konfiguruje po stronie serwera domowego: repo `wiewiornik`, `INSTRUKCJA.md` rozdział 3c2.

### 10.2 Odbiór: `POST /api/sleep-night`

- **Żądanie:** multipart, pole `file` = JSON nocy (działa też surowy JSON w ciele). Bez klucza i haseł.
- **Walidacja:** niepusty, ≤ 1 MB, poprawny JSON, pola `night` (data wieczoru `RRRR-MM-DD`), `timeline` i `summary`.
- **Zapis:** bez zmian w `data/noce/noc_RRRR-MM-DD.json`, wskaźniki w `data/noce/index.json` (zapis atomowy z blokadą). Katalog `noce/` tworzy się sam. Ta sama noc wysłana ponownie nadpisuje poprzednią.
- **Odpowiedź:** `200` `{"message":"Zapisano noc …","night":"…","metrics":{…}}`; `400` przy złych danych, `500` gdy nie da się zapisać.

```bash
curl -s -X POST "https://phpmapy1.webd.pro/api/sleep-night" -F "file=@/data/dane/noc_2026-11-01.json"
```

### 10.3 Wskaźniki (`SleepNights::metrics`)

Kamera widzi tylko ruch, więc to **nie jest pomiar faz snu**, tylko orientacyjna ocena spokoju.

| Wskaźnik | Definicja |
|---|---|
| spokój (`quietPct`) | % minut nocy bez ruchu (`timeline[i] == 0`) |
| najdłuższy spokój (`longestQuietMin`) | najdłuższy odcinek bez ruchu, z detektora (`longest_quiet_s`), awaryjnie z osi minut |
| epizody ruchu (`episodes`) | minuty z ruchem łączone w jeden epizod, gdy przerwa ≤ 5 min |
| dłuższe epizody (`longEpisodes`) | epizody z co najmniej 3 minutami ruchu |
| pokrycie (`coveragePct`) | jaką część nocy detektor faktycznie obserwował |
| ocena (`rating`) | `spokojna`: spokój ≥ 90% i najdłuższy spokój ≥ 150 min · `niespokojna`: spokój < 75% albo > 10 dłuższych epizodów · `umiarkowana`: pozostałe · `brak-danych`: pokrycie < 60% |

Progi są stałymi w `SleepNights.php` (`EPISODE_GAP_MIN`, `LONG_EPISODE_MIN`, `LOW_COVERAGE_PCT`) i w `metrics()`.

### 10.4 Widok

Nowoczesna strona → **Sen** → **„Jakość snu nocą”**:

- wybór nocy strzałkami albo dotknięciem słupka,
- duży wskaźnik spokoju z oceną,
- pasek nocy z ruchem minuta po minucie i karmieniami z dziennika (pomarańczowe znaczniki),
- 6 kafelków ze wskaźnikami,
- wykres ostatnich 14 nocy (najdłuższy spokój; kolor wg oceny; nad słupkiem % minut bez ruchu),
- u góry średnia „średnio X h spokoju ciągiem”.

Dane odświeżają się co 5 min. Dopóki nie przyszła żadna noc, sekcja pokazuje stan pusty. Prosta strona nie ma tej sekcji.

## 11. Włączenie MySQL

Domyślnie działa CSV; nic nie trzeba robić. Aby przełączyć:

1. W cPanel → **Bazy danych MySQL**: utwórz bazę i użytkownika, nadaj uprawnienia.
2. W **phpMyAdmin** uruchom `engine/schema.sql` (tabele `entries`, `settings`).
3. W `engine/config.php` uzupełnij `Config::mysql()` (host, nazwa, użytkownik, hasło) albo ustaw zmienne `DB_*`.
4. W `config.php` `storageDriver()` zwróć `'mysql'` (albo zmienna `STORAGE_DRIVER=mysql`).
5. Pierwsze dane wgraj przyciskiem **IMPORTUJ DANE** w panelu albo załaduj CSV w phpMyAdmin.

Gdy baza będzie niedostępna, silnik wraca do CSV (panel nie padnie). Noce z kamery zawsze leżą w plikach, niezależnie od magazynu.

## 12. Uruchomienie lokalne i testy

```bash
cd web-php
php -S 127.0.0.1:8080 ui/index.php          # panel: http://127.0.0.1:8080
```

Test kontraktu API (odczyt; `TEST_WRITE=1` dodaje i usuwa wpis testowy; `BASE=` zmienia adres):

```bash
./test_api.sh
BASE="http://127.0.0.1:8080" TEST_WRITE=1 ./test_api.sh
```

Lokalnie dane (`data/karmienia.csv`) są osobnym zbiorem, nie synchronizują się z hostingiem.

## 13. Bezpieczeństwo i czego jeszcze nie ma

- **API nie ma uwierzytelniania** (świadoma decyzja na ten etap). Hasła i szyfrowanie zostaną dodane **od razu dla całego portalu** (strona, API, odbiór nocy, kamery); nie dodaje się ich pojedynczo do wybranych endpointów.
- Wyjątek opcjonalny: `UPLOAD_TOKEN` dla `upload-data` (rozdział 8).
- Dane i silnik leżą poza katalogiem publicznym; `data/` ma `.htaccess` z całkowitym zakazem dostępu.
- Pominięte funkcje sprzętowe (Telegram, pogoda, watchdog, mDNS) obsługuje urządzenie, nie strona.
- Znane uwagi z przeglądu kodu: `ulepszenia19-10-26.md` (m.in. wyścigi zapisu CSV, auto-sen dopisujący zdarzenia w przyszłości).
