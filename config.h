#pragma once

// ---------------------------- Sieć i aktualny czas ----------------------------
// Polska: automatyczna zmiana CET/CEST. Nie zmieniaj bez potrzeby.
constexpr char TIMEZONE_RULE[] = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 30000;

// ------------------------------- Dane Aleksandra -------------------------------
// Zmień te trzy wartości, jeśli data urodzenia będzie inna.
constexpr int BIRTH_DAY = 8;
constexpr int BIRTH_MONTH = 8;
constexpr int BIRTH_YEAR = 2026;

// -------------------------------- Formularze -----------------------------------
constexpr int ML_MIN = 10;
constexpr int ML_MAX = 120;
constexpr int DEFAULT_ML = 30;
// Zakres wagi dziecka w gramach (ekran WAGA i walidacja API). Domyslnie 3700 g.
constexpr int WEIGHT_MIN_G = 2000;
constexpr int WEIGHT_MAX_G = 15000;
constexpr int DEFAULT_WEIGHT_G = 3700;
// Waga urodzeniowa (g) — punkt startowy krzywej oczekiwanej wagi w panelu WWW.
constexpr int BIRTH_WEIGHT_G = 3080;
// Progi belki licznika na ekranie glownym (minuty od ostatniego karmienia).
constexpr uint16_t COUNTER_WARN_MIN = 180;  // zolty pas — zbliza sie pora
constexpr uint16_t COUNTER_BLINK_MIN = 240; // miganie biel/czerien — czas na karmienie
constexpr uint16_t FEEDING_ALARM_WINDOW_MINUTES = 30;

// -------------------------- Wewnętrzna pamięć urządzenia -----------------------
// LittleFS przechowuje dane w pamięci Flash ESP32. Nie trzeba podłączać karty
// microSD ani żadnego przewodu. Plik tworzy się automatycznie przy pierwszym starcie.
constexpr char DATA_FILE_PATH[] = "/karmienia.csv";
constexpr char BACKUP_FILE_PATH[] = "/karmienia_backup.csv";
// Prog, po ktorym plik danych jest jednorazowo archiwizowany (kopia ze znacznikiem
// daty) z ostrzezeniem w logu/diagnostyce. Dane NIE sa usuwane — to sygnal, ze
// historia urosla i skany CSV staja sie kosztowne. 256 KB = wiele tysiecy wpisow.
constexpr size_t DATA_FILE_ROTATE_BYTES = 256UL * 1024UL;

// ------------------------------ Motyw nocny ------------------------------------
// W tych godzinach urządzenie i panel WWW przechodzą na ciemną paletę
// oraz łagodniejsze podświetlenie.
constexpr int NIGHT_START_HOUR = 21;
constexpr int NIGHT_END_HOUR = 7;
// Noc: ok. 65% jasnosci — wyraznie przyciemnione, ale pewnie widoczne
// (160 = ~37% gasilo podswietlenie calkowicie, ponizej progu AP3032).
constexpr uint8_t BACKLIGHT_NIGHT_DUTY = 90;

// --------------------------- Utrzymanie zegara NTP -----------------------------
constexpr uint32_t NTP_RESYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;

// ------------------------- Wygaszacz ekranu i pogoda ---------------------------
// Po tym czasie bez dotkniecia ekran glowny przechodzi w tryb zegara i pogody.
constexpr uint32_t SCREENSAVER_TIMEOUT_MS = 2UL * 60UL * 1000UL;
// Po tym czasie bezczynnosci na DOWOLNYM ekranie nastepuje powrot na ekran glowny.
constexpr uint32_t HOME_RETURN_TIMEOUT_MS = 30UL * 1000UL;
// Zrodlo: wttr.in (darmowe, bez klucza API, HTTP). Miasto zmieniasz tutaj.
constexpr char WEATHER_CITY[] = "BEDZIN";
constexpr float WEATHER_LAT = 50.3274f;
constexpr float WEATHER_LON = 19.1285f;
constexpr uint32_t WEATHER_REFRESH_MS = 20UL * 60UL * 1000UL; // odswiezanie w tle
constexpr uint32_t WEATHER_STALE_MS = 30UL * 60UL * 1000UL;   // pobierz od razu gdy starsze
constexpr uint32_t WEATHER_START_DELAY_MS = 8000UL;           // daj Wi-Fi/DNS czas po starcie
constexpr uint32_t WEATHER_RETRY_DELAY_MS = 60000UL;          // kolejna proba po bledzie
constexpr char WEATHER_CACHE_FILE[] = "/pogoda.cache";

// ----------------------- Usługi opcjonalne (uzupełnij) -------------------------
// Telegram: powiadomienia o wpisach do drugiego rodzica. Token od @BotFather,
// chat ID odbiorcy. Puste pola = powiadomienia wyłączone.
constexpr char TELEGRAM_BOT_TOKEN[] = "8468018843:AAHjt-X20pzkJWG07HkTeRya6b5iR-6oVc4";
constexpr char TELEGRAM_CHAT_ID[] = "6567938576";

// ArduinoOTA: hasło do wgrywania szkicu przez Wi-Fi. Puste = OTA wyłączone.
constexpr char OTA_PASSWORD[] = "";
