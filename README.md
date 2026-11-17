# wiewiorka.mom

Strona do wspomagania opieki nad niemowlęciem: karmienia, butelki, pieluchy, sen, waga, kąpiele, witamina D, kalendarz, statystyki i pocztówki-wspomnienia. Przewodnim motywem jest **opiekuńcza wiewiórka**, która „czuwa” nad maleństwem. Każdy użytkownik ma własne konto i widzi wyłącznie swoje dane.

Gałąź `wiewiorka.mom` to kopia strony **Leśny Dziennik 5.5.0** (gałąź `v5`) z kontami, profilem dziecka i nowym wyglądem. Gałęzie `v5` i `v4.1-claude-pazdziernik` (strona Aleksandra i urządzenie ESP32) zostają bez zmian i są źródłem — zmiany wprowadzone tam można przenosić tutaj ręcznie.

## Spis treści

1. [Układ katalogów (repo = hosting)](#1-układ-katalogów-repo--hosting)
2. [Wdrożenie krok po kroku (cPanel, webd.pl)](#2-wdrożenie-krok-po-kroku-cpanel-webdpl)
3. [Konta i logowanie](#3-konta-i-logowanie)
4. [Dane maleństwa (profil)](#4-dane-maleństwa-profil)
5. [Wygląd: motyw wiewiórki](#5-wygląd-motyw-wiewiórki)
6. [Czym ta wersja różni się od Leśnego Dziennika](#6-czym-ta-wersja-różni-się-od-leśnego-dziennika)
7. [API](#7-api)
8. [Pliki danych](#8-pliki-danych)
9. [Aktualizacja strony](#9-aktualizacja-strony)
10. [Typowe problemy](#10-typowe-problemy)
11. [Uruchomienie lokalne i testy](#11-uruchomienie-lokalne-i-testy)

---

## 1. Układ katalogów (repo = hosting)

Katalogi w repozytorium mają **te same nazwy i to samo położenie** co na hostingu (`/home/phpmapy1/`), więc nic nie trzeba przemianowywać:

| Repo | Hosting | Co to jest |
|---|---|---|
| `public_html/wiewiorka/` | `/home/phpmapy1/public_html/wiewiorka/` | strona (domena `wiewiorka.mom` wskazuje na ten katalog) |
| `panel-engine-wiewiorka/` | `/home/phpmapy1/panel-engine-wiewiorka/` | silnik PHP (poza katalogiem publicznym) |
| `panel-data-wiewiorka/` | `/home/phpmapy1/panel-data-wiewiorka/` | dane kont (poza katalogiem publicznym, **zapisywalny**) |

Strona Aleksandra (`public_html/` bez podkatalogu, `panel-engine/`, `panel-data/`) jest od tej wersji całkowicie niezależna: osobny silnik, osobne dane. Wgranie wiewiorka.mom niczego w niej nie zmienia, a urządzenie ESP32 dalej łączy się tylko ze stroną Aleksandra.

Zawartość:

```
public_html/wiewiorka/
  index.php      router: logowanie, API, strona (wszystko przechodzi tędy)
  paths.php      ścieżki do silnika i danych (dwa poziomy wyżej)
  index.html     wygląd (style, ekrany logowania, konto, maskotka)
  app.js         logika strony
  .htaccess      przekierowanie do index.php, opcjonalne wymuszenie HTTPS
  fonts/         czcionki Sora i Manrope (licencja OFL)
  img/           grafiki SVG: wiewiorka.svg, wiewiorka-spi.svg, las-gora.svg, las-dol.svg, favicon.svg
panel-engine-wiewiorka/
  Auth.php       konta, hasła, logowanie, blokady, profil dziecka
  api.php        API danych (wywoływane tylko przez index.php po zalogowaniu)
  config.php     normy (sen, waga, formularze) + profil dziecka z konta
  Domain.php, CsvRepository.php, Repository.php, bootstrap.php, development_tips.php,
  SleepAnalysis.php, SleepNights.php, SleepStats.php (część „noc z kamery” nieaktywna)
  .htaccess      zakaz dostępu z WWW
panel-data-wiewiorka/
  .htaccess      zakaz dostępu z WWW (reszta plików powstaje sama na serwerze)
```

## 2. Wdrożenie krok po kroku (cPanel, webd.pl)

Bez gita: pliki pobierasz z GitHub (gałąź `wiewiorka.mom` → plik → **Download raw file**) albo całą gałąź jako ZIP (**Code → Download ZIP**) i wgrywasz przez **Menedżer plików** cPanel.

### 2.1 Katalogi i pliki

1. W Menedżerze plików wejdź do `/home/phpmapy1/` (katalog domowy, ten sam poziom co `public_html`).
2. Utwórz trzy katalogi: `public_html/wiewiorka`, `panel-engine-wiewiorka`, `panel-data-wiewiorka`.
3. Wgraj:
   - całą zawartość `public_html/wiewiorka/` z repo (z podkatalogami `fonts/` i `img/` oraz plikiem `.htaccess`) → `/home/phpmapy1/public_html/wiewiorka/`;
   - wszystkie pliki z `panel-engine-wiewiorka/` (razem z `.htaccess`) → `/home/phpmapy1/panel-engine-wiewiorka/`;
   - `panel-data-wiewiorka/.htaccess` → `/home/phpmapy1/panel-data-wiewiorka/`.
   Pliki zaczynające się od kropki (`.htaccess`) w Menedżerze plików są widoczne po włączeniu **Ustawienia → Pokaż ukryte pliki**.
4. Prawa zapisu: katalog `panel-data-wiewiorka` musi być zapisywalny dla PHP. Na webd.pl domyślne `755` (właściciel = Twoje konto, PHP działa jako Ty) wystarcza. Jeśli przy pierwszym wejściu pojawi się „Katalog danych portalu jest niezapisywalny”, ustaw `775` (prawy klik → Zmień uprawnienia).

**Nie ruszaj** niczego w `public_html/` poza nowym podkatalogiem `wiewiorka` — w szczególności `public_html/.htaccess` (wybór PHP 8; po nadpisaniu wraca PHP 5.6 i obie strony dają błąd 500). Nowa strona dziedziczy z niego wersję PHP.

### 2.2 Domena wiewiorka.mom

1. Kup / przenieś domenę `wiewiorka.mom` i ustaw jej serwery DNS na serwery webd.pl (albo rekord A na adres serwera hostingu — w panelu rejestratora).
2. cPanel → **Domeny** → **Utwórz nową domenę**: nazwa `wiewiorka.mom`, odznacz „dzieli katalog główny z…”, **Katalog główny dokumentów**: `public_html/wiewiorka`. Zapisz.
3. cPanel → **SSL/TLS Status** → zaznacz `wiewiorka.mom` i `www.wiewiorka.mom` → **Run AutoSSL**. Certyfikat pojawia się zwykle w kilka–kilkanaście minut po tym, jak DNS zacznie wskazywać na hosting.
4. Gdy `https://wiewiorka.mom` się otwiera: w `public_html/wiewiorka/.htaccess` usuń `#` z dwóch linii pod „Wymuszenie HTTPS” — od tej chwili `http://` przekierowuje na `https://`.

Dopóki domena nie działa, strona jest też pod `https://phpmapy1.webd.pro/wiewiorka/` (konta i dane są te same; logowanie z tego adresu jest osobne od logowania na domenie, bo przeglądarka trzyma ciasteczka osobno dla każdego adresu).

### 2.3 Pierwsze uruchomienie

1. Otwórz `https://wiewiorka.mom/`. Przy pierwszym wejściu silnik sam tworzy w `panel-data-wiewiorka/` plik **`portal.cfg`** z losowym kodem zaproszenia i sekretem.
2. W Menedżerze plików otwórz `panel-data-wiewiorka/portal.cfg` (Edytuj). Linia `zaproszenie=wiewiorka-xxxxxx` to kod, który trzeba podać przy zakładaniu konta. Możesz go zmienić na własny (np. `zaproszenie=lesne-maluchy-2026`) i zapisać. Pusta wartość (`zaproszenie=`) zamyka rejestrację — istniejące konta logują się dalej.
3. **Nie zmieniaj** linii `sekret=` (zmiana wylogowuje wszystkich; nie udostępniaj jej nikomu).
4. Na stronie: „Załóż konto” → kod zaproszenia, login, hasło (2×) → „Twoje maleństwo” (data urodzenia, waga urodzeniowa, imię nieobowiązkowe) → strona startowa.

### 2.4 Szybki test po wdrożeniu

- `https://wiewiorka.mom/api/auth` → `{"loggedIn":false,...,"registrationOpen":true}`.
- `https://wiewiorka.mom/api/status` bez logowania → `{"message":"Zaloguj się…","auth":true}` (kod 401).
- `https://wiewiorka.mom/paths.php` i `https://wiewiorka.mom/index.html` → brak dostępu (403).
- Strona Aleksandra `https://phpmapy1.webd.pro/` działa jak wcześniej.

## 3. Konta i logowanie

- **Pierwsza wizyta** na danym urządzeniu pokazuje „Załóż konto”; kolejne — „Zaloguj się” (przełączniki „Załóż konto” / „Zaloguj się” pod formularzem).
- **Zakładanie konta** wymaga kodu zaproszenia z `portal.cfg`. Login: 3–32 znaki (litery bez polskich znaków, cyfry, `.`, `-`, `_`), wielkość liter bez znaczenia (`Tata` = `tata`). Hasło: co najmniej 8 znaków.
- **Hasła** są zapisywane wyłącznie jako skrót bcrypt (`password_hash`), nigdy jawnie. Nikt (także administrator) nie odczyta hasła z pliku.
- **Zalogowanie** to podpisane ciasteczko `wm_auth` (HMAC-SHA256 z sekretem z `portal.cfg`): numer konta, ważność, wersja konta. Ciasteczko jest `HttpOnly` (niedostępne dla skryptów), `SameSite=Lax`, przez HTTPS także `Secure`, i ważne tylko dla adresu strony. Bez plików sesji — działa po restartach serwera.
  - „Zapamiętaj mnie” (domyślnie zaznaczone): logowanie ważne 30 dni, odnawiane przy korzystaniu.
  - Bez „Zapamiętaj mnie”: do zamknięcia przeglądarki (najdłużej 1 dzień).
- **Blokady**: 5 nieudanych logowań na jeden login albo 20 z jednego adresu IP w ciągu 15 minut → logowanie zablokowane do końca okna („Zbyt wiele nieudanych prób. Spróbuj ponownie za N min.”). Tak samo 5 błędnych kodów zaproszenia z jednego IP.
- **Ochrona przed podszywaniem (CSRF)**: każdy zapis (POST) musi przyjść z tej samej strony (nagłówek `Origin`/`Referer` = adres strony); inaczej 403 „Odrzucono zapis spoza strony”.
- **Izolacja danych**: po sprawdzeniu logowania router podaje silnikowi wyłącznie katalog `panel-data-wiewiorka/u_<nr konta>/`. API nie przyjmuje numeru konta ani ścieżki od przeglądarki, więc nie da się odczytać ani zmienić danych innego konta (sprawdzone testem: drugie konto nie widzi ani nie usunie wpisów pierwszego, podrobione ciasteczko z innym numerem konta jest odrzucane).
- **Konto** (przycisk „konto” w prawym górnym rogu): dane maleństwa, zmiana hasła (po zmianie inne urządzenia są wylogowane, to zostaje zalogowane), „Wyloguj”.

### Reset zapomnianego hasła (bez e-maila)

Strona nie zbiera adresów e-mail, więc reset robi administrator w Menedżerze plików:

1. Najprościej: usuń konto i załóż je od nowa z tym samym loginem, zachowując dane:
   1. w `panel-data-wiewiorka/konta.json` znajdź wpis z loginem i zanotuj jego `"id"` (np. `4`);
   2. zmień nazwę katalogu `u_4` na np. `u_4_stare`;
   3. usuń z `konta.json` cały blok tego loginu (od `"login-małymi-literami": {` do zamykającego `},` — zachowaj poprawny JSON);
   4. użytkownik zakłada konto ponownie (dostaje nowy numer, np. `u_7`);
   5. przenieś pliki z `u_4_stare/` do nowego `u_7/` (nadpisz) — wrócą wpisy i profil.
2. Albo: wygeneruj nowy skrót hasła (np. na swoim komputerze `php -r 'echo password_hash("NoweHaslo123", PASSWORD_DEFAULT);'`) i wklej go w `konta.json` w pole `"hash"` tego konta, a `"ver"` zwiększ o 1.

## 4. Dane maleństwa (profil)

Po założeniu konta strona prosi o:

| Pole | Do czego służy | Zakres |
|---|---|---|
| Imię (nieobowiązkowe) | powitanie („Zosia pod opieką wiewiórki”), nagłówek, tytuł karty, wiek („Zosia ma 58 dni”), pocztówki | do 30 znaków: litery, spacja, `-`, `'`; puste → „Maluszek” / „Maleństwo” |
| Data urodzenia | wiek, porada dnia, okna czuwania i normy snu (wg wieku), dni życia na wykresach | nie w przyszłości, nie starsza niż 6 lat |
| Waga urodzeniowa (g) | punkt startowy wykresów wagi (WHO) i przyrostu, „od urodzenia” | 500–6500 g |

Zapisane w `panel-data-wiewiorka/u_<nr>/profil.json`. Zmiana w „konto” → „Dane maleństwa” od razu przelicza stronę. Bez uzupełnionego profilu API danych odpowiada 409 i strona pokazuje ekran „Twoje maleństwo”.

## 5. Wygląd: motyw wiewiórki

Układ, kolory, czcionki i wszystkie podstrony są takie jak w Leśnym Dzienniku (styl „bajkowy” z przerywanymi ramkami, tryb dzień/noc). Doszły elementy wiewiórki — wszystkie narysowane od podstaw jako SVG (bez cudzych grafik i bibliotek):

- **Logo**: wiewiórka z żołędziem w kremowym kółku z rudą obwódką (to samo w ikonce karty przeglądarki — `img/favicon.svg`). Nazwa strony „wiewiorka.mom”, pod nią imię i wiek.
- **Powitanie z maskotką** na starcie: „Dzień dobry / Miłego dnia / Dobry wieczór / Dobranoc” wg godziny, „{Imię} pod opieką wiewiórki” i zdanie od wiewiórki. Maskotka zmienia się wg stanu dnia:
  | Stan | Wiewiórka | Zdanie |
  |---|---|---|
  | dziecko śpi (sen w toku) | zwinięta w kłębek pod ogonem, „z z Z” | „Zosia śpi — wiewiórka pilnuje ciszy.” |
  | okno drzemki / przekroczone okno czuwania | przymknięte oczy | „Pora na drzemkę. Ziewamy?” / „Okno czuwania minęło — czas wyciszyć maleństwo.” |
  | ≥3 h / ≥4 h od karmienia | podskakuje, pomarańczowy wykrzyknik | „Za chwilę może być głodny.” / „Minęły 4 godziny od karmienia.” |
  | noc | księżyc i gwiazdki | „Nocna zmiana. Wiewiórka czuwa przy Was.” |
  | pozostałe | mruga, trzyma żołądź | „Czuwam razem z Wami.” |
  Animacje (mrugnięcie, podskok) wyłączają się, gdy telefon ma włączone „ogranicz ruch”.
- **Tło**: gałąź dębu z liśćmi i żołędziami w prawym górnym rogu oraz gałąź z małą wiewiórką w lewym dolnym; delikatne, za kartami, w trybie nocnym przygaszone.
- **Puste stany** („Brak wpisów”, „Brak pomiarów”…) z małą wiewiórką.
- **Ekran logowania**: zielone leśne tło z gałęziami, duża wiewiórka, „wiewiorka.mom”, krótki opis i karta z formularzem.

Zmiany wyglądu są tylko w `index.html` (sekcja CSS na końcu stylów „wiewiorka.mom — motyw opiekuńczej wiewiórki”) i `img/`.

## 6. Czym ta wersja różni się od Leśnego Dziennika

| Leśny Dziennik (v5) | wiewiorka.mom |
|---|---|
| jedno dziecko, dane wpisane w `config.php` | dowolnie wiele kont, każde z własnym profilem i danymi |
| bez logowania | logowanie, zakładanie kont z kodem zaproszenia |
| silnik `panel-engine`, dane `panel-data` | `panel-engine-wiewiorka`, `panel-data-wiewiorka/u_<nr>` |
| noce z kamery (t620), urządzenie ESP32, Telegram, wgrywanie CSV z zewnątrz (`upload-data`), wersja prosta `indexesp.html` | brak (ukryte w wyglądzie, wyłączone w API) |
| MySQL jako opcja | tylko CSV (osobny plik na konto) |
| adresy `/api/...` od korzenia | adresy względne — strona działa na domenie i w podkatalogu |

Wszystko inne (karmienia z butelką, pieluchy, odciąganie, witamina D, waga z wykresami WHO i przyrostu, kąpiele, sen z oknami czuwania, kalendarz, statystyki, historia, rytm, pocztówki-wspomnienia, import i eksport CSV — własnych danych) działa tak samo, łącznie z poprawkami 5.5.0 (sen po czasie, bezpieczny zapis pliku, usuwanie z weryfikacją i z mlekiem, limit czasu zapytań).

## 7. API

Wszystkie adresy względem strony (`https://wiewiorka.mom/…`). Odpowiedzi JSON.

**Konta (bez logowania):**

| Metoda i adres | Pola | Co robi |
|---|---|---|
| `GET api/auth` | — | `loggedIn`, `login`, `profileComplete`, `profile` (`name`, `birthDate`, `birthWeightG`), `registrationOpen` |
| `POST api/register` | `invite`, `login`, `password`, `password2`, `remember` (1/0) | zakłada konto i loguje (201) |
| `POST api/login` | `login`, `password`, `remember` | loguje (200) albo 400 z komunikatem |
| `POST api/logout` | — | wylogowuje |

**Po zalogowaniu** (bez logowania → 401 `{"auth":true}`):

| Metoda i adres | Co robi |
|---|---|
| `POST api/profile` (`name`, `birthDate` RRRR-MM-DD, `birthWeightG`) | zapisuje dane maleństwa |
| `POST api/password` (`old`, `password`, `password2`) | zmienia hasło |

**Dane** (po zalogowaniu i z uzupełnionym profilem; bez profilu → 409 `{"profile":true}`): `GET api/status` (dodatkowo pole `childName`), `GET api/entries?date=`, `GET api/weight-series`, `GET api/bath-calendar`, `GET api/revision`, `GET export.csv`, `POST api/entry`, `POST api/event`, `POST api/delete-entry`, `POST api/update-feeding`, `POST api/import` — format jak w Leśnym Dzienniku 5.5.0.

Wyłączone (404): `upload-data`, `sleep-night`, `sleep-nights`, `device`, `setting`, `send-backup`.

## 8. Pliki danych

W `panel-data-wiewiorka/` (wszystko powstaje samo):

| Plik | Zawartość |
|---|---|
| `portal.cfg` | `zaproszenie=` kod rejestracji, `sekret=` klucz podpisu logowań |
| `konta.json` | konta: login, skrót hasła (bcrypt), numer, wersja, data założenia |
| `proby.json` | nieudane próby logowania i kodu (sprzątane po 15 min) |
| `konta.lock` | blokada zapisu kont (pusty) |
| `u_<nr>/karmienia.csv` | wpisy konta (format CSV jak w Leśnym Dzienniku) |
| `u_<nr>/profil.json` | dane maleństwa |
| `u_<nr>/karmienia.rev`, `karmienia.csv.lock`, `karmienia_backup.csv`, `cache/` | rewizja, blokada, kopia sprzed importu, pamięć odpowiedzi |

**Kopia zapasowa**: wystarczy skopiować cały katalog `panel-data-wiewiorka` (cPanel → Menedżer plików → Kompresuj → pobierz). Każdy użytkownik może też pobrać swoje dane: Statystyki → „Pobierz CSV”.

**Usunięcie konta**: usuń jego wpis z `konta.json` i katalog `u_<nr>`.

## 9. Aktualizacja strony

Przy każdej zmianie wgrywasz tylko zmienione pliki z gałęzi `wiewiorka.mom`:

- `public_html/wiewiorka/*` → `/home/phpmapy1/public_html/wiewiorka/` — przy zmianie `app.js` zawsze razem z `index.html` (ładuje `app.js?v=<wersja>`, numer podbijany przy każdej zmianie, więc telefony pobiorą nowy plik same);
- `panel-engine-wiewiorka/*` → `/home/phpmapy1/panel-engine-wiewiorka/`.

Nigdy nie nadpisuj `panel-data-wiewiorka/` (konta i dane) ani `public_html/.htaccess`.

## 10. Typowe problemy

| Objaw | Przyczyna / co zrobić |
|---|---|
| „Katalog danych portalu jest niezapisywalny…” | brak katalogu `panel-data-wiewiorka` obok `public_html` albo brak prawa zapisu — utwórz go / ustaw 775 |
| Błąd 500 na całej stronie | najczęściej nadpisany `public_html/.htaccess` (PHP 5.6) — przywróć wybór PHP 8 w cPanel (MultiPHP Manager) |
| Po zalogowaniu od razu znowu ekran logowania | przeglądarka blokuje ciasteczka albo strona otwarta raz przez `http`, raz przez `https` — włącz wymuszenie HTTPS (rozdział 2.2) |
| „Nieprawidłowy kod zaproszenia” | sprawdź `zaproszenie=` w `portal.cfg` (bez spacji); po 5 błędach z jednego adresu 15 min blokady |
| „Zbyt wiele nieudanych prób” | odczekaj podany czas albo usuń `panel-data-wiewiorka/proby.json` |
| Zapomniane hasło | rozdział 3, „Reset zapomnianego hasła” |
| Stary wygląd po aktualizacji | wgraj `index.html` razem z `app.js`; w razie potrzeby Ctrl+F5 |

## 11. Uruchomienie lokalne i testy

```bash
# z katalogu repozytorium (wymaga PHP 8)
cd public_html/wiewiorka && php -S 127.0.0.1:8120 index.php
# strona: http://127.0.0.1:8120/  — dane trafią do panel-data-wiewiorka/ (poza gitem)
```

Sprawdzone przed wydaniem (PHP 8.3 + Chromium/Playwright, ekran telefonu 400×860):

- pełny przebieg w przeglądarce: pierwsza wizyta → załóż konto → dane maleństwa → strona → dodanie karmienia → wylogowanie → zalogowanie (dane zostają) — na domenie (`/`) i w podkatalogu (`/wiewiorka/`), bez błędów JS;
- 34 testy API: izolacja dwóch kont (status, wpisy, eksport, próba usunięcia cudzego wpisu), 401 bez logowania, 403 dla zapisu z obcej strony i bez nagłówka `Origin`, podrobione ciasteczko, wyłączone trasy, blokada po 5 błędnych hasłach i 5 błędnych kodach, zmiana hasła wylogowuje inne urządzenia, hasła tylko jako bcrypt;
- wygląd: start (dzień i noc, maskotka czuwa / śpi), dziennik, statystyki, waga, sen, okno „Konto”, ekrany logowania, zakładania konta i danych maleństwa.
