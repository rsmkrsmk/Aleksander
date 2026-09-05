# Aleksander — panel WWW (kopia testowa w Dockerze)

Testowa kopia panelu WWW, który normalnie hostuje urządzenie ESP32 (firmware
`OfficialWaveshareHelloWorld.ino`). Uruchamia ten sam interfejs i **ten sam kontrakt
API** poza urządzeniem — jako kontener Docker z Node.js + Express, czytający dane
z pliku CSV.

> **Zakres:** to rozwiązanie **do testów** i baza pod dalsze prace (np. migracja panelu
> na hosting/VPS). Frontend jest skopiowany 1:1 z urządzenia, a backend odtwarza logikę
> API (wiek dziecka, statystyki dnia, sen wg Napper, rytm karmień, waga). Dane są
> przeniesione z urządzenia (eksport `karmienia.csv`).

## Co jest odwzorowane 1:1

- **Frontend** (`public/index.html`) — dokładna kopia `WEB_APP_HTML` z `web_panel.h`.
- **Endpointy API** — pełny zestaw jak w firmwarze:
  `GET /`, `GET /api/status`, `GET /api/entries?date=YYYY-MM-DD`, `GET /api/weight-series`,
  `POST /api/entry`, `POST /api/delete-entry`, `POST /api/event`, `POST /api/setting`,
  `POST /api/import`, `POST /api/send-backup`, `GET /export.csv`.
- **Format danych** — CSV z nagłówkiem
  `data,godzina,typ,ml,piers_lewa_min,piers_prawa_min` (4 lub 6 kolumn), typy:
  `KARMIENIE`, `MLEKO_MATKI`, `MLEKO_MODYFIKOWANE`, `PIELUCHA_MOKRA/BRUDNA`,
  `ODCIAGANIE`, `WITAMINA_D`, `WAGA` (gramy), `SEN_START/STOP`.
- **Logika obliczeń** — wiek dziecka (od `08.08.2026`, 12:00), statystyki dnia,
  podział snu noc/dzień, okno czuwania i cele snu wg tabel Napper, „następne
  karmienie” = ostatnie + 4 h. Stałe w `config.js` odpowiadają `config.h` urządzenia.

## Czego (świadomie) nie ma

Funkcje ściśle sprzętowe/zewnętrzne urządzenia zostały pominięte — nie są potrzebne
do testów panelu:

- powiadomienia **Telegram** i wysyłka backupu (`/api/send-backup` zwraca komunikat
  „nie skonfigurowany”),
- pobieranie **pogody**, ekran LVGL, dotyk, watchdog, mDNS `karmienie.local`,
- pola diagnostyczne w `/api/status` (RAM/PSRAM/CPU/RSSI/uptime/bootCount itd.) mają
  wartości zastępcze (`0`/`false`/`docker`); `ip` = `"docker-panel"`,
- `developmentTip` to neutralne zdanie zależne od dnia życia, a nie pełna treść z
  tablicy `DEVELOPMENT_TIPS[]` urządzenia (tej tablicy nie replikujemy w kopii testowej),
- brak bramek zależnych od sprzętu: odpowiedzi **409** (niezsynchronizowany zegar NTP)
  i **507** (brak miejsca/RAM przy imporcie) nie występują — w kontenerze zegar jest
  zawsze poprawny, a miejsce nie jest ograniczone jak Flash ESP32.

## Struktura

```
docker-panel/
├── public/index.html      # frontend 1:1 z urządzenia
├── server.js              # Express: routing + kontrakt API
├── store.js               # parser CSV + logika (wiek, statystyki, sen)
├── config.js              # stałe odwzorowane z config.h
├── data/karmienia.csv     # DANE przeniesione z urządzenia (wolumen)
├── Dockerfile
├── docker-compose.yml
└── package.json
```

## Uruchomienie — Docker (zalecane)

```bash
cd docker-panel
docker compose up --build
```

Panel: **http://localhost:8080**

Dane leżą w `./data/karmienia.csv` i są montowane jako wolumen — możesz je
podmienić/edytować bez przebudowy obrazu (po zmianie odśwież panel, poll co 10 s).

Zatrzymanie:

```bash
docker compose down
```

## Uruchomienie — bez Dockera (Node.js ≥ 18)

```bash
cd docker-panel
npm install
TZ=Europe/Warsaw node server.js       # domyślnie port 8080
```

## Konfiguracja (zmienne środowiskowe)

| Zmienna         | Domyślnie                   | Opis                                   |
|-----------------|-----------------------------|----------------------------------------|
| `PORT`          | `8080`                      | Port HTTP                              |
| `TZ`            | `Europe/Warsaw`             | Strefa czasowa (ważne dla obliczeń)    |
| `DATA_FILE`     | `data/karmienia.csv`        | Plik danych CSV                        |
| `SETTINGS_FILE` | `data/ustawienia.cfg`       | Ustawienia (`sleepTelegram`)           |
| `BACKUP_FILE`   | `data/karmienia_backup.csv` | Kopia robiona przed importem           |

## Uwaga o strefie czasowej

Firmware liczy wszystkie czasy w czasie **lokalnym** (Polska). Kontener ustawia
`TZ=Europe/Warsaw`, więc wiek, granice doby i podział snu wychodzą tak samo jak na
urządzeniu. Uruchamiając bez Dockera, ustaw `TZ` ręcznie.

## Dane

`data/karmienia.csv` to rzeczywisty eksport z urządzenia (wpisy z okresu
23.08–05.09.2026). Aby wgrać inny zestaw: podmień plik albo użyj przycisku
**IMPORTUJ DANE** w panelu (robi backup poprzednich danych, potem zastępuje).
