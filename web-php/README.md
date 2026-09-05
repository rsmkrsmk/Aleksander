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
├── public/                 # ← DocumentRoot Apache wskazuje TUTAJ
│   ├── index.html          # frontend 1:1 z urządzenia
│   ├── index.php           # front controller (routing + całe API)
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

## Wdrożenie na Apache + PHP

1. Skopiuj katalog `web-php/` na serwer, np. do `/var/www/aleksander`.

2. Ustaw **DocumentRoot na podkatalog `public/`** (zalecane — dane i logika zostają
   poza web). Przykładowy VirtualHost:

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

   Włącz rewrite i przeładuj Apache:
   ```bash
   sudo a2enmod rewrite
   sudo systemctl reload apache2
   ```

3. Nadaj serwerowi WWW prawo **zapisu do katalogu `data/`** (panel zapisuje wpisy,
   import robi backup):
   ```bash
   sudo chown -R www-data:www-data /var/www/aleksander/web-php/data
   sudo chmod 775 /var/www/aleksander/web-php/data
   ```

4. Otwórz `http://aleksander.twojadomena.pl/` — panel działa.

### Alternatywa: wrzucenie do podkatalogu istniejącego hostingu

Jeśli nie możesz zmienić DocumentRoot (współdzielony hosting), umieść zawartość
`public/` w katalogu dostępnym z web, a `lib/` i `data/` **o poziom wyżej** (poza
`public_html`), i popraw ścieżki w `lib/config.php` (`dataFile()`/`settingsFile()`)
albo ustaw zmienne środowiskowe `DATA_FILE` / `SETTINGS_FILE`. Obronne `.htaccess`
w `data/` i `lib/` blokują dostęp, gdyby katalogi trafiły pod web.

## Uruchomienie lokalne (test, bez Apache)

```bash
cd web-php
php -S 127.0.0.1:8080 public/index.php
```

Panel: **http://127.0.0.1:8080** (`index.php` pełni rolę routera, jak front controller).

## Konfiguracja (opcjonalne zmienne środowiskowe)

| Zmienna         | Domyślnie                | Opis                              |
|-----------------|--------------------------|-----------------------------------|
| `DATA_FILE`     | `data/karmienia.csv`     | Plik danych CSV                   |
| `SETTINGS_FILE` | `data/ustawienia.cfg`    | Ustawienia (`sleepTelegram`)      |
| `BACKUP_FILE`   | `data/karmienia_backup.csv` | Kopia robiona przed importem   |

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
