# Aleksander — panel WWW (kopia testowa PHP / Apache)

Testowa kopia panelu WWW, który normalnie hostuje urządzenie ESP32 (firmware
`OfficialWaveshareHelloWorld.ino`). Uruchamia ten sam interfejs i **ten sam kontrakt
API** poza urządzeniem — jako zwykła aplikacja **PHP na serwerze Apache**, czytająca
dane z pliku CSV.

> **Zakres:** rozwiązanie **do testów** i baza pod dalsze prace (np. docelowy panel
> na serwerze). Frontend jest skopiowany 1:1 z urządzenia, a backend PHP odtwarza
> logikę API (wiek dziecka, statystyki dnia, sen wg Napper, rytm karmień, waga).
> Dane są przeniesione z urządzenia (eksport `karmienia.csv`).

## Struktura

```
web-php/
├── public/                 # ← katalog publiczny (trafia do public_html)
│   ├── index.html          # frontend 1:1 z urządzenia
│   ├── index.php           # front controller (routing + całe API)
│   ├── paths.php           # KONFIGURACJA ścieżek do lib/ i data/ (edytujesz przy wgraniu)
│   └── .htaccess           # rewrite na index.php
├── lib/
│   ├── config.php          # stałe odwzorowane z config.h
│   ├── store.php           # parser CSV + logika (wiek, statystyki, sen)
│   └── .htaccess           # blokada dostępu z web
├── data/
│   ├── karmienia.csv       # DANE przeniesione z urządzenia
│   └── .htaccess           # blokada dostępu z web
└── README.md
```

Dane i logika leżą **poza** `public/`, więc przy poprawnym `DocumentRoot` nie są
dostępne bezpośrednio przez przeglądarkę.

## Co odtworzone 1:1

- **Frontend** (`public/index.html`) — dokładna kopia `WEB_APP_HTML` z `web_panel.h`.
- **Endpointy API** — pełny zestaw jak w firmwarze:
  `GET /`, `GET /api/status`, `GET /api/entries?date=YYYY-MM-DD`, `GET /api/weight-series`,
  `POST /api/entry`, `POST /api/delete-entry`, `POST /api/event`, `POST /api/setting`,
  `POST /api/import`, `POST /api/send-backup`, `GET /export.csv`.
- **Format danych** — CSV z nagłówkiem
  `data,godzina,typ,ml,piers_lewa_min,piers_prawa_min` (4 lub 6 kolumn); typy:
  `KARMIENIE`, `MLEKO_MATKI`, `MLEKO_MODYFIKOWANE`, `PIELUCHA_MOKRA/BRUDNA`,
  `ODCIAGANIE`, `WITAMINA_D`, `WAGA` (gramy), `SEN_START/STOP`.
- **Logika** — wiek dziecka (od `08.08.2026`, 12:00), statystyki dnia, podział snu
  noc/dzień, okno czuwania i cele snu wg tabel Napper, „następne karmienie” =
  ostatnie + 4 h. Stałe w `lib/config.php` odpowiadają `config.h`.

## Wymagania

- **PHP 7.4+** (rozwijane i testowane na PHP 8.x).
- **Apache** z `mod_rewrite` (dla przyjaznych ścieżek `/api/*`) oraz `AllowOverride All`
  dla katalogu, żeby `.htaccess` działał.

## Wdrożenie na hosting cPanel przez FTP (webd.pl) — ZALECANE

Cała konfiguracja ścieżek sprowadza się do edycji **jednego pliku** `public/paths.php`.

**Docelowy układ na serwerze** (`~` = Twój katalog domowy na hostingu):

```
~/public_html/     ← zawartość web-php/public/  (index.php, index.html, paths.php, .htaccess)
~/panel-lib/       ← zawartość web-php/lib/      (config.php, store.php)
~/panel-data/      ← zawartość web-php/data/     (karmienia.csv)  — musi być zapisywalny
```

`panel-lib` i `panel-data` leżą **obok** `public_html` (poza katalogiem publicznym),
więc nie są dostępne z internetu.

**Kroki:**

1. **Włącz wariant cPanel w `public/paths.php`** — otwórz plik i zamień aktywne
   definicje na wersję z `panel-lib`/`panel-data`. Wystarczy zakomentować dwie
   domyślne linie `define(...)` i odkomentować dwie oznaczone „WARIANT cPanel”.
   (Ścieżki liczą się automatycznie od katalogu domowego — nie wpisujesz loginu.)

2. **Wgraj pliki przez FTP** (FileZilla / WinSCP / menedżer plików cPanel):
   - zawartość `web-php/public/` → do `public_html/`,
   - zawartość `web-php/lib/`   → do nowego katalogu `panel-lib/` (obok `public_html`),
   - zawartość `web-php/data/`  → do nowego katalogu `panel-data/` (obok `public_html`).

3. **Prawa zapisu do `panel-data/`.** Na współdzielonym cPanel PHP działa zwykle jako
   Twój użytkownik, więc katalog jest zapisywalny domyślnie. Jeśli zapis/import nie
   działa, w menedżerze plików cPanel ustaw uprawnienia katalogu `panel-data/` na
   `755` (a gdyby nadal nie działało — `775`).

4. Otwórz swoją domenę w przeglądarce — panel działa. `mod_rewrite` i `.htaccess`
   na webd.pl są włączone, więc ścieżki `/api/*` zadziałają bez dodatkowej konfiguracji.

> **Prostszy (mniej bezpieczny) wariant:** jeśli nie chcesz tworzyć katalogów poza
> `public_html`, możesz wrzucić `public/`, `lib/` i `data/` razem do `public_html/`
> (np. `public_html/`, `public_html/lib/`, `public_html/data/`) i **zostawić
> `paths.php` bez zmian** (wariant domyślny wskazuje `../lib` i `../data`). Wtedy
> dane chronią tylko obronne pliki `.htaccess` w `lib/` i `data/` — działa, ale
> trzymanie danych poza `public_html` (kroki 1–3) jest bezpieczniejsze.

## Wdrożenie na własny serwer Apache (VirtualHost)

Ustaw **DocumentRoot na `web-php/public/`**, `AllowOverride All`, `mod_rewrite` wł.:

```apache
<VirtualHost *:80>
    ServerName aleksander.twojadomena.pl
    DocumentRoot /var/www/aleksander/web-php/public
    <Directory /var/www/aleksander/web-php/public>
        AllowOverride All
        Require all granted
    </Directory>
</VirtualHost>
```

Przy tym układzie `paths.php` zostaje domyślny (lib/ i data/ obok public/). Nadaj
użytkownikowi Apache prawo zapisu do `web-php/data/`.

## Uruchomienie lokalne (test, bez Apache)

```bash
cd web-php
php -S 127.0.0.1:8080 public/index.php
```

Panel: **http://127.0.0.1:8080** (`index.php` pełni rolę routera, jak front controller).

## Konfiguracja ścieżek

Najprościej — edytujesz **`public/paths.php`** (dwie stałe: `PANEL_LIB_DIR`,
`PANEL_DATA_DIR`). Plik ma gotowe, opisane warianty: domyślny (wszystko w jednym
katalogu) i cPanel (lib/data poza `public_html`).

Alternatywnie możesz nadpisać ścieżki zmiennymi środowiskowymi (mają priorytet):

| Zmienna         | Domyślnie                   | Opis                            |
|-----------------|-----------------------------|---------------------------------|
| `DATA_FILE`     | `<PANEL_DATA_DIR>/karmienia.csv` | Plik danych CSV            |
| `SETTINGS_FILE` | `<PANEL_DATA_DIR>/ustawienia.cfg` | Ustawienia (`sleepTelegram`) |
| `BACKUP_FILE`   | `<PANEL_DATA_DIR>/karmienia_backup.csv` | Kopia przed importem  |

Strefa czasowa jest ustawiana w kodzie na `Europe/Warsaw` (`lib/config.php`), tak jak
liczy urządzenie — wiek, granice doby i podział snu wychodzą identycznie.

## Czego (świadomie) nie ma

Funkcje ściśle sprzętowe/zewnętrzne urządzenia — niepotrzebne do testu panelu:

- powiadomienia **Telegram** i wysyłka backupu (`/api/send-backup` zwraca komunikat
  „nie skonfigurowany”),
- **pogoda**, ekran LVGL, dotyk, watchdog, mDNS `karmienie.local`,
- pola diagnostyczne w `/api/status` (RAM/PSRAM/CPU/RSSI/uptime/bootCount…) mają
  wartości zastępcze (`0`/`false`/`php`); `ip` = `"panel-php"`,
- `developmentTip` to neutralne zdanie zależne od dnia życia, a nie pełna treść z
  tablicy `DEVELOPMENT_TIPS[]` urządzenia,
- brak odpowiedzi **409** (niezsynchronizowany zegar) i **507** (brak miejsca/RAM) —
  nie dotyczą serwera PHP.

## Dane

`data/karmienia.csv` to rzeczywisty eksport z urządzenia (wpisy 23.08–05.09.2026).
Aby wgrać inny zestaw: podmień plik albo użyj przycisku **IMPORTUJ DANE** w panelu
(robi backup poprzednich danych, potem zastępuje).
