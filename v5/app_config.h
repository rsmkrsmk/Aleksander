// =============================================================================
//  app_config.h — stale Lesnego Dziennika v5 (urzadzenie = tylko wyswietlacz)
//  Sekrety (Wi-Fi, Telegram) sa w secrets.h (poza repozytorium, wzor: secrets.h.example).
// =============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

#define FW_VERSION "5.0.0"

// ------------------------------- Dziecko ------------------------------------
constexpr int BIRTH_DAY = 8;
constexpr int BIRTH_MONTH = 8;
constexpr int BIRTH_YEAR = 2026;
constexpr int BIRTH_WEIGHT_G = 3080;          // punkt startowy wykresu wagi
#define CHILD_NAME "Aleksander"

// ------------------------------- Czas ---------------------------------------
// Polska: automatyczna zmiana CET/CEST.
#define TIMEZONE_RULE "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define NTP_SERVER_3 "time.cloudflare.com"

// ------------------------------- Wi-Fi --------------------------------------
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_RETRY_MS = 30000;

// ------------------------------- Hosting (tylko odczyt) ---------------------
#define HOST_NAME "phpmapy1.webd.pro"
constexpr uint16_t HOST_PORT = 443;
#define HOST_PATH_REVISION "/api/revision"
#define HOST_PATH_CSV "/api/export.csv"
#define HOST_PATH_STATUS "/api/status"         // stad czytamy ustawienie sleepTelegram
constexpr uint32_t HOST_POLL_MS = 10000;          // sprawdzanie rewizji danych (decyzja D6)
constexpr uint32_t HOST_STATUS_REFRESH_MS = 10UL * 60UL * 1000UL;  // ustawienia z hostingu
constexpr uint32_t HOST_BOOT_SYNC_MS = 25000;     // limit czekania na dane przy starcie
constexpr size_t HOST_CSV_MAX_BYTES = 1024UL * 1024UL;  // gorny limit pobieranego CSV (PSRAM)
constexpr uint32_t TLS_GAP_MS = 1500;             // odstep miedzy kolejnymi sesjami TLS

// ------------------------------- Cache danych (decyzja D2) ------------------
// Ostatnie dane zapisywane na flash TYLKO gdy sie zmienily i najwyzej raz na 30 min.
#define CACHE_FILE "/dane.csv"
constexpr uint32_t CACHE_MIN_INTERVAL_MS = 30UL * 60UL * 1000UL;

// ------------------------------- Pogoda (Open-Meteo, HTTP) ------------------
constexpr float WEATHER_LAT = 50.3274f;
constexpr float WEATHER_LON = 19.1285f;
constexpr uint32_t WEATHER_REFRESH_MS = 20UL * 60UL * 1000UL;
constexpr uint32_t WEATHER_RETRY_MS = 60000UL;
constexpr uint32_t WEATHER_START_DELAY_MS = 8000UL;

// ------------------------------- Ekran / zachowanie -------------------------
constexpr uint32_t SAVER_TIMEOUT_MS = 2UL * 60UL * 1000UL;   // panel -> tablica
constexpr uint32_t DIAG_TIMEOUT_MS = 30UL * 1000UL;          // diagnostyka -> panel
constexpr uint32_t DIM_TIMEOUT_MS = 60UL * 1000UL;           // przyciemnienie (tylko w dzien)
constexpr int NIGHT_START_HOUR = 21;
constexpr int NIGHT_END_HOUR = 7;
// Podswietlenie AP3032: PWM aktywne NISKIM poziomem (0 = 100%).
constexpr uint8_t BACKLIGHT_FULL_DUTY = 0;
constexpr uint8_t BACKLIGHT_NIGHT_DUTY = 90;
constexpr uint8_t BACKLIGHT_DIM_DUTY = 119;

// Okresowa twarda resynchronizacja obrazu (siatka bezpieczenstwa: przesuniecie, ktorego nie
// wykryje detektor, znika najpozniej po tym czasie). 0 = wylaczone.
constexpr uint32_t RGB_PERIODIC_RESYNC_MS = 60UL * 1000UL;

// ------------------------------- Karmienie ----------------------------------
constexpr int COUNTER_WARN_MIN = 180;   // zolty
constexpr int COUNTER_ALARM_MIN = 240;  // czerwony + "nastepne karmienie" = ostatnie + tyle minut
constexpr int BATH_INTERVAL_DAYS = 3;

// ------------------------------- Sen (Napper) -------------------------------
constexpr int SLEEP_NIGHT_START_HOUR = 21;   // sen 21..7 = nocny
constexpr int SLEEP_NIGHT_END_HOUR = 7;
constexpr int SLEEP_MAX_SESSION_MIN = 840;   // dluzsza sesja = zapomniany STOP -> odrzucona
constexpr int WAKE_WIN_COUNT = 7;
constexpr int WAKE_WIN_AGE_DAYS[WAKE_WIN_COUNT] = {0, 28, 84, 150, 210, 330, 420};
constexpr int WAKE_WIN_MIN_MINUTES[WAKE_WIN_COUNT] = {35, 60, 75, 120, 150, 180, 240};
constexpr int WAKE_WIN_MAX_MINUTES[WAKE_WIN_COUNT] = {60, 90, 120, 180, 210, 240, 360};
constexpr int SLEEP_NEED_COUNT = 6;
constexpr int SLEEP_NEED_AGE_DAYS[SLEEP_NEED_COUNT] = {0, 30, 91, 182, 274, 365};
constexpr int SLEEP_NEED_NIGHT_MIN[SLEEP_NEED_COUNT] = {510, 510, 570, 600, 660, 660};
constexpr int SLEEP_NEED_DAY_MIN[SLEEP_NEED_COUNT] = {480, 420, 300, 240, 180, 120};

// ------------------------------- Watchdog -----------------------------------
constexpr uint32_t WATCHDOG_TIMEOUT_S = 45;
