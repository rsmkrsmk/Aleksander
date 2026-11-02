# Aleksander — panel WWW (kopia testowa PHP)

Testowa kopia panelu WWW urządzenia ESP32, jako aplikacja PHP na Apache. Od tej
wersji jest **rozdzielona na dwie warstwy**:

```
web-php/
├── engine/      ← SILNIK DANYCH  (przetwarzanie + przechowywanie; CSV / MySQL)
├── ui/          ← WARSTWA WIZUALNA (to, co użytkownik widzi i klika)
└── data/        ← plik CSV z danymi (zawsze — jako źródło lub jako backup)
```

Podział jest celowy: **silnik** wystawia API (JSON), z którego korzysta strona WWW
**oraz — docelowo — urządzenia łączące się zdalnie**. UI nie dotyka danych; wszystko
robi przez API silnika. Dzięki temu MySQL włącza się wyłącznie w silniku, bez zmian w UI.

## Silnik danych (`engine/`)

| Plik | Rola |
|---|---|
| `config.php` | stałe (wiek, zakresy, Napper) + wybór magazynu + dane MySQL |
| `Domain.php` | czysta logika: wiek, statystyki dnia, sen, rytm, parser CSV |
| `Repository.php` | kontrakt magazynu (interfejs) |
| `CsvRepository.php` | magazyn CSV (domyślny, działa od razu) |
| `MysqlRepository.php` | magazyn MySQL **+ backup CSV przy każdym zapisie** |
| `bootstrap.php` | wybiera magazyn wg configu (fallback na CSV) |
| `api.php` | publiczne API HTTP (endpointy `/api/*`, `/export.csv`) + CORS |
| `SleepNights.php` | noce z kamery (jakość snu nocą): odbiór pliku nocy z serwera domowego, wskaźniki, magazyn `data/noce/` |
| `schema.sql` | schemat tabel MySQL |

**CSV zawsze jest backupem.** W trybie `mysql` bazą danych jest MySQL, ale każdy
zapis idzie **równolegle do CSV** w tym samym formacie co urządzenie
(`data,godzina,typ,ml,piers_lewa_min,piers_prawa_min`). W trybie `csv` (domyślnym)
CSV jest źródłem prawdy.

### Kontrakt API (bez zmian)

`GET /` · `GET /api/status` · `GET /api/entries?date=YYYY-MM-DD` ·
`GET /api/weight-series` · `POST /api/entry` · `POST /api/delete-entry` ·
`POST /api/event` · `POST /api/setting` · `POST /api/import` ·
`POST /api/upload-data` · `POST /api/send-backup` · `GET /export.csv`

Dodane 02.11.2026: `POST /api/sleep-night` (z kluczem) · `GET /api/sleep-nights?days=14` ·
`GET /api/sleep-night?night=RRRR-MM-DD`. `GET /api/weight-series` zwraca dodatkowo `weeks`
(słupki przyrostu dla każdego tygodnia życia) i `recent` (trend z ostatnich ~14 dni).

Urządzenia mogą uderzać wprost w silnik: `…/engine/api.php?route=status` (GET) itd.

**CORS jest domyślnie wyłączony** (API działa same-origin, jak dotychczas). Gdy
urządzenia będą łączyć się z innego originu, ustaw `Config::corsOrigin()` (albo zmienną
`CORS_ORIGIN`) na konkretny origin lub `*` — świadomie, bo API nie ma jeszcze
uwierzytelniania. Docelowo warto dodać token/klucz dla urządzeń.

### Przyjmowanie pliku CSV z zewnątrz — `POST /api/upload-data`

Wysyła kompletny plik CSV z danymi na serwer. Strona **najpierw robi kopię
zapasową** dotychczasowych danych pod nazwą `RRRR-MM-DD-GG-MM-SS.bakap` (aktualny
czas, w katalogu `data/` obok pliku danych), a potem **podmienia** dane na przesłane.
Plik jest walidowany: musi mieć nagłówek `data,godzina,typ,ml,piers_lewa_min,piers_prawa_min`
i co najmniej jeden poprawny wiersz; niepoprawne wiersze są pomijane.

- **Metoda / URL:** `POST https://TWOJA-DOMENA/api/upload-data`
  (albo wprost w silnik: `POST https://TWOJA-DOMENA/engine/api.php?route=upload-data`)
- **Ciało żądania — dwa warianty:**
  1. **multipart/form-data** z polem pliku o nazwie `file` — **ZALECANE** (przechodzi
     przez firewall/WAF hostingu, np. BitNinja na webd.pl), albo
  2. **surowy CSV** w ciele (`Content-Type: text/csv`) — prostsze, ale **WAF hostingu
     potrafi je blokować (403)**; używaj tylko jeśli Twój serwer nie ma takiego firewalla.
- **Limit rozmiaru:** 512 KB.
- **Token (opcjonalny, zalecany dla urządzeń):** ustaw zmienną środowiskową
  `UPLOAD_TOKEN=twoj_sekret` na serwerze. Gdy ustawiona, żądanie MUSI podać ten sam
  token — w nagłówku `X-Upload-Token: twoj_sekret` **lub** w query `?token=twoj_sekret`.
  Gdy `UPLOAD_TOKEN` jest pusty, endpoint jest otwarty (jak dotychczasowy import).
- **Odpowiedź:** `200` `{"message":"...","backup":"RRRR-MM-DD-GG-MM-SS.bakap","lines":N}`;
  błędy: `400` (pusty/za duży/niepoprawny plik), `403` (brak/zły token).

**Przykłady wywołania (urządzenie / inna strona):**

```bash
# 1) ZALECANE — multipart (pole "file"), token w nagłówku. Przechodzi przez WAF.
curl -X POST "https://TWOJA-DOMENA/api/upload-data" \
     -H "X-Upload-Token: twoj_sekret" \
     -F "file=@karmienia.csv"

# 2) multipart bez tokenu (gdy UPLOAD_TOKEN nie jest ustawiony)
curl -X POST "https://TWOJA-DOMENA/api/upload-data" \
     -F "file=@karmienia.csv"

# 3) surowy CSV w ciele (może zostać zablokowane przez WAF hostingu -> 403)
curl -X POST "https://TWOJA-DOMENA/api/upload-data" \
     -H "Content-Type: text/csv" \
     -H "X-Upload-Token: twoj_sekret" \
     --data-binary @karmienia.csv
```

> **Firewall hostingu (WAF):** webd.pl używa BitNinja, który blokuje surowe body
> POST (`text/csv`, `--data-binary`) do `/api/*` — zwraca stronę „403 Forbidden".
> Dlatego zarówno przycisk „wgraj CSV" w panelu, jak i urządzenie powinny wysyłać
> plik jako **multipart/form-data** (pole `file`). Ten sam mechanizm co zwykłe
> zapisy panelu (formularze), więc WAF go przepuszcza.

> Ręczne wgranie z przeglądarki: w prawym górnym rogu (przy dacie) jest tymczasowy
> link **„wgraj CSV"** — wskazuje plik i wysyła go tym samym endpointem. To wersja
> testowa; docelowo przeniesiemy ten przycisk w inne miejsce.

### Przyrost wagi — słupki tygodniowe (`GET /api/weight-series` → `weeks`)

Serwer liczy jeden słupek na **każdy** tydzień życia, od 1. do tygodnia ostatniego ważenia.
Obie strony (nowoczesna i prosta) tylko go rysują. Wartość (g/dzień) to trend z ważeń
(regresja liniowa), bo pojedyncze ważenia różnią się nawet o 100 g:

1. okno ±7 dni wokół środka tygodnia (co najmniej 2 ważenia odległe o ≥ 5 dni),
2. przy mniejszej liczbie ważeń okno rośnie do ±10, ±14, ±21 dni,
3. w ostateczności wartość wynika z dwóch najbliższych ważeń przed i po tygodniu.

Kroki 2–3 oznaczają słupek jako `estimated` (jaśniejszy, z przerywaną ramką, w legendzie
„szacunek (mało ważeń)”). Waga urodzeniowa (`BIRTH_WEIGHT_G`) to punkt dnia 0. Wcześniej
tydzień bez 2 ważeń w oknie ±7 dni dostawał „–” (dziury).

Odświeżanie: strona wagi (nowoczesna) i okno wagi (prosta) w pętli co 10 s sprawdzają
`/api/weight-series` i rysują oba wykresy od nowa, gdy zmienią się pomiary albo dzień życia.
Po zapisie pomiaru odświeżają się oba wykresy (wcześniej tylko linia).

### Noce z kamery — jakość snu nocą (`POST /api/sleep-night`)

Serwer domowy (t620, kontener „kamery”, `zapisywacz.py` z repozytorium **wiewiornik**) co rano,
ok. 3 min po końcu nocy (06:00), wysyła plik danych zakończonej nocy. Strona zapisuje go bez zmian
w `data/noce/noc_RRRR-MM-DD.json`, liczy wskaźniki i trzyma je w `data/noce/index.json`.

**Włączenie odbioru (jednorazowo):** w katalogu danych (`panel-data/`, obok `karmienia.csv`)
utwórz plik `noce_klucz.txt` z długim losowym kluczem w pierwszej linii, np. z
`openssl rand -hex 24`. Alternatywnie ustaw zmienną środowiskową `NOCE_KLUCZ`. Bez klucza
endpoint odpowiada `503`, a ze złym kluczem `403`. Plik klucza nie trafia do repozytorium.

- **Żądanie:** `POST https://TWOJA-DOMENA/api/sleep-night`, multipart (pole `file` = JSON nocy).
  Multipart jest zalecany ze względu na WAF hostingu. Działa też surowy JSON w ciele.
- **Klucz:** `Authorization: Bearer KLUCZ`, `X-Klucz: KLUCZ` albo pole formularza `klucz`.
  Część hostingów obcina nagłówek `Authorization`; `ui/.htaccess` go przekazuje, a pozostałe
  dwie drogi działają zawsze.
- **Odpowiedź:** `200` `{"message":"Zapisano noc …","night":"…","metrics":{…}}`. Błędy:
  `400` (zły/pusty JSON, brak `night`/`timeline`/`summary`, > 1 MB).
- Ta sama noc wysłana ponownie nadpisuje poprzednią wersję.

Test z serwera domowego:

```bash
curl -s -X POST "https://TWOJA-DOMENA/api/sleep-night" -H "X-Klucz: KLUCZ" \
     -F "file=@/data/dane/noc_2026-11-01.json"
```

**Wskaźniki** (`SleepNights::metrics`). Kamera widzi tylko ruch w łóżeczku, więc to nie jest
pomiar faz snu.

| Wskaźnik | Definicja |
|---|---|
| spokój (`quietPct`) | % minut nocy bez ruchu (`timeline[i] == 0`) |
| najdłuższy spokój | najdłuższy odcinek bez ruchu, z detektora (`longest_quiet_s`) |
| epizody ruchu | minuty z ruchem połączone, gdy przerwa < 5 min; „dłuższe” = co najmniej 3 minuty z ruchem |
| ocena | `spokojna`: ≥ 90% i ≥ 2,5 h spokoju ciągiem · `niespokojna`: < 75% albo > 10 dłuższych epizodów · `umiarkowana`: pozostałe · `brak-danych`: kamera obserwowała < 60% nocy |

**Widok:** nowoczesna strona → Sen → „Jakość snu nocą”:
- wybór nocy strzałkami albo dotknięciem słupka,
- duży wskaźnik spokoju z oceną,
- pasek nocy z ruchem minuta po minucie i karmieniami z dziennika (pomarańczowe znaczniki),
- 6 kafelków,
- wykres ostatnich 14 nocy (najdłuższy spokój, kolor wg oceny, nad słupkiem % minut bez ruchu).

Dane są odświeżane co 5 min. Prosta strona bez zmian graficznych.

## Warstwa wizualna (`ui/`)

| Plik | Rola |
|---|---|
| `index.html` | struktura + design (glassmorphism, dzień/noc) |
| `app.js` | interakcje, woła API silnika |
| `index.php` | punkt wejścia: serwuje UI, deleguje `/api/*` do silnika |
| `paths.php` | **jedyny plik do edycji przy wdrożeniu** — ścieżki do engine/ i data/ |

## Włączenie MySQL (gdy zechcesz)

Domyślnie działa CSV — nic nie trzeba robić. Aby przełączyć na MySQL:

1. W cPanel → **Bazy danych MySQL**: utwórz bazę i użytkownika, nadaj mu uprawnienia.
2. W **phpMyAdmin** uruchom `engine/schema.sql` (tworzy tabele `entries`, `settings`).
3. W `engine/config.php` uzupełnij `Config::mysql()` (host/nazwa/użytkownik/hasło)
   albo ustaw zmienne środowiskowe `DB_*`.
4. Ustaw magazyn na MySQL: w `config.php` `storageDriver()` zwróć `'mysql'`
   (lub zmienna `STORAGE_DRIVER=mysql`).
5. Pierwsze dane wgraj przyciskiem **IMPORTUJ DANE** w panelu (wczyta CSV do bazy)
   albo załaduj CSV w phpMyAdmin.

Jeśli baza będzie niedostępna, silnik automatycznie wróci do CSV (panel nie padnie).

## Wdrożenie na cPanel/webd.pl (FTP)

Zalecany układ (dane i silnik **poza** katalogiem publicznym):

```
~/public_html/     ← zawartość web-php/ui/     (index.php, index.html, app.js, paths.php, .htaccess)
~/panel-engine/    ← zawartość web-php/engine/
~/panel-data/      ← zawartość web-php/data/   (zapisywalny; tu też noce_klucz.txt i katalog noce/)
```

1. W `ui/paths.php` włącz wariant cPanel (zakomentuj domyślne `define`, odkomentuj
   wersję z `panel-engine` / `panel-data`).
2. Wgraj katalogi jak wyżej. `panel-data/` musi mieć prawo zapisu (w cPanel `755`/`775`).
3. **DocumentRoot** ma wskazywać na `ui/` (u Ciebie: zawartość `ui/` w `public_html`).

Prostszy wariant (mniej bezpieczny): wrzuć `ui/`, `engine/`, `data/` obok siebie
w `public_html` i zostaw `paths.php` domyślny — chronią wtedy obronne `.htaccess`.

## Uruchomienie lokalne (test)

```bash
cd web-php
php -S 127.0.0.1:8080 ui/index.php
```

Panel: http://127.0.0.1:8080

## Zakres / czego nie ma

To środowisko **testowe** panelu, oddzielone od urządzenia (dane się nie
synchronizują z ESP32 — to osobny zbiór). Pominięte funkcje sprzętowe (Telegram,
pogoda, watchdog, mDNS); pola diagnostyczne w `/api/status` mają wartości zastępcze.
