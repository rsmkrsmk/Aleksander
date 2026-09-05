// Stale odwzorowane 1:1 z firmware config.h (repozytorium Aleksander).
// Zmiana tych wartosci zmienia obliczenia wieku, snu i zakresow — trzymaj zgodnie z urzadzeniem.

// ------------------------------- Dane Aleksandra -------------------------------
export const BIRTH_DAY = 8;
export const BIRTH_MONTH = 8;
export const BIRTH_YEAR = 2026;

// -------------------------------- Formularze -----------------------------------
export const ML_MIN = 10;
export const ML_MAX = 120;
export const DEFAULT_ML = 30;
export const WEIGHT_MIN_G = 2000;
export const WEIGHT_MAX_G = 15000;
export const DEFAULT_WEIGHT_G = 3700;
export const BIRTH_WEIGHT_G = 3080;

// Progi belki licznika (minuty od ostatniego karmienia).
export const COUNTER_WARN_MIN = 180; // zolty
export const COUNTER_BLINK_MIN = 240; // czerwony; = "nastepne karmienie" +4h

// -------------------------- Pamiec / pliki danych -----------------------------
// W kopii Docker odpowiednikiem LittleFS jest katalog data/ montowany jako wolumen.
export const DATA_FILE = process.env.DATA_FILE || 'data/karmienia.csv';
export const BACKUP_FILE = process.env.BACKUP_FILE || 'data/karmienia_backup.csv';
export const SETTINGS_FILE = process.env.SETTINGS_FILE || 'data/ustawienia.cfg';
export const CSV_HEADER = 'data,godzina,typ,ml,piers_lewa_min,piers_prawa_min';

// -------------------------------- Sen (Napper) ---------------------------------
export const SLEEP_NIGHT_START_HOUR = 21;
export const SLEEP_NIGHT_END_HOUR = 7;

export const WAKE_WIN_AGE_DAYS = [0, 28, 84, 150, 210, 330, 420];
export const WAKE_WIN_MIN_MINUTES = [35, 60, 75, 120, 150, 180, 240];
export const WAKE_WIN_MAX_MINUTES = [60, 90, 120, 180, 210, 240, 360];

export const SLEEP_NEED_AGE_DAYS = [0, 30, 91, 182, 274, 365];
export const SLEEP_NEED_NIGHT_MIN = [510, 510, 570, 600, 660, 660];
export const SLEEP_NEED_DAY_MIN = [480, 420, 300, 240, 180, 120];

export const NAP_TARGET_AGE_DAYS = [0, 120, 210, 365, 550];
export const NAP_TARGET_NAPS = [5, 4, 3, 2, 1];

// ------------------------------ Motyw nocny ------------------------------------
export const NIGHT_START_HOUR = 21;
export const NIGHT_END_HOUR = 7;

// ------------------------------- Serwer ----------------------------------------
export const PORT = Number(process.env.PORT || 8080);
// Strefa czasowa: firmware liczy wszystko w czasie lokalnym Polski.
// Kontener ustawia TZ=Europe/Warsaw (patrz Dockerfile/compose), wiec Date dziala jak na urzadzeniu.

// -------- Wskazowki rozwojowe (development tips) --------
// Firmware ma tablice DEVELOPMENT_TIPS[] w development_tips.h. W kopii testowej
// nie replikujemy pelnej tresci — zwracamy neutralny komunikat na podstawie dnia zycia.
// (Panel pokazuje pole "developmentTip" tylko informacyjnie.)
export const DEVELOPMENT_TIP_COUNT = 600;
