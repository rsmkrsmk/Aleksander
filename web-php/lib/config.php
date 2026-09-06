<?php
// Stale odwzorowane 1:1 z firmware config.h (repozytorium Aleksander).
// Zmiana tych wartosci zmienia obliczenia wieku, snu i zakresow — trzymaj zgodnie z urzadzeniem.
declare(strict_types=1);

// Strefa czasowa: firmware liczy wszystko w czasie LOKALNYM Polski.
date_default_timezone_set('Europe/Warsaw');

final class Config
{
    // ------------------------------- Dane Aleksandra -------------------------------
    const BIRTH_DAY = 8;
    const BIRTH_MONTH = 8;
    const BIRTH_YEAR = 2026;

    // -------------------------------- Formularze -----------------------------------
    const ML_MIN = 10;
    const ML_MAX = 120;
    const DEFAULT_ML = 30;
    const WEIGHT_MIN_G = 2000;
    const WEIGHT_MAX_G = 15000;
    const DEFAULT_WEIGHT_G = 3700;
    const BIRTH_WEIGHT_G = 3080;

    // Progi belki licznika (minuty od ostatniego karmienia).
    const COUNTER_WARN_MIN = 180;   // zolty
    const COUNTER_BLINK_MIN = 240;  // czerwony; = "nastepne karmienie" +4h

    // -------------------------------- Sen (Napper) ---------------------------------
    const SLEEP_NIGHT_START_HOUR = 21;
    const SLEEP_NIGHT_END_HOUR = 7;

    const WAKE_WIN_AGE_DAYS     = [0, 28, 84, 150, 210, 330, 420];
    const WAKE_WIN_MIN_MINUTES  = [35, 60, 75, 120, 150, 180, 240];
    const WAKE_WIN_MAX_MINUTES  = [60, 90, 120, 180, 210, 240, 360];

    const SLEEP_NEED_AGE_DAYS   = [0, 30, 91, 182, 274, 365];
    const SLEEP_NEED_NIGHT_MIN  = [510, 510, 570, 600, 660, 660];
    const SLEEP_NEED_DAY_MIN    = [480, 420, 300, 240, 180, 120];

    const NAP_TARGET_AGE_DAYS   = [0, 120, 210, 365, 550];
    const NAP_TARGET_NAPS       = [5, 4, 3, 2, 1];

    // ------------------------------ Motyw nocny ------------------------------------
    const NIGHT_START_HOUR = 21;
    const NIGHT_END_HOUR = 7;

    // ------------------------------- Pliki danych ----------------------------------
    const CSV_HEADER = 'data,godzina,typ,ml,piers_lewa_min,piers_prawa_min';

    // Katalog z danymi: 1) zmienna srodowiskowa, 2) stala PANEL_DATA_DIR z public/paths.php,
    // 3) domyslnie data/ obok lib/ (dziala lokalnie i przy jednolitym ukladzie katalogow).
    private static function dataDir(): string
    {
        if (defined('PANEL_DATA_DIR')) {
            return rtrim(PANEL_DATA_DIR, '/');
        }
        return __DIR__ . '/../data';
    }
    public static function dataFile(): string
    {
        return getenv('DATA_FILE') ?: (self::dataDir() . '/karmienia.csv');
    }
    public static function backupFile(): string
    {
        return getenv('BACKUP_FILE') ?: (self::dataDir() . '/karmienia_backup.csv');
    }
    public static function settingsFile(): string
    {
        return getenv('SETTINGS_FILE') ?: (self::dataDir() . '/ustawienia.cfg');
    }
}
