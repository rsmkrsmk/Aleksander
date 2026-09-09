<?php
/* ============================================================================
   SILNIK DANYCH — konfiguracja
   Stale odwzorowane 1:1 z firmware config.h + ustawienia magazynu danych.
   Ta warstwa NIE zajmuje sie wygladem — tylko dane i reguly domenowe.
   ============================================================================ */
declare(strict_types=1);

// Firmware liczy wszystko w czasie LOKALNYM Polski — trzymamy tak samo.
date_default_timezone_set('Europe/Warsaw');

final class Config
{
    // ------------------------------- Dane Aleksandra -------------------------------
    const BIRTH_DAY = 8;
    const BIRTH_MONTH = 8;
    const BIRTH_YEAR = 2026;

    // -------------------------------- Formularze -----------------------------------
    // ML_MIN/ML_MAX/DEFAULT_ML dotyczą ODCIAGANIA (bez zmian).
    const ML_MIN = 10;
    const ML_MAX = 120;
    const DEFAULT_ML = 30;
    // Osobny zakres dla ILOSCI MLEKA w karmieniu (butelka): 20..200 ml, skok 10, domyslnie 60.
    const MILK_ML_MIN = 20;
    const MILK_ML_MAX = 200;
    const MILK_ML_STEP = 10;
    const MILK_ML_DEFAULT = 60;
    const WEIGHT_MIN_G = 2000;
    const WEIGHT_MAX_G = 15000;
    const DEFAULT_WEIGHT_G = 3700;
    const BIRTH_WEIGHT_G = 3080;

    // Progi belki licznika (minuty od ostatniego karmienia).
    const COUNTER_WARN_MIN = 180;
    const COUNTER_BLINK_MIN = 240;

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

    // ------------------------------- Format CSV ------------------------------------
    const CSV_HEADER = 'data,godzina,typ,ml,piers_lewa_min,piers_prawa_min';

    // ============================================================================
    //  MAGAZYN DANYCH
    //  'csv'   — plik CSV jest zrodlem prawdy (domyslnie, dziala od razu).
    //  'mysql' — baza MySQL jest zrodlem prawdy, a KAZDY zapis idzie ROWNIEZ do CSV
    //            (backup w tym samym formacie). Wymaga uzupelnienia danych ponizej
    //            i utworzenia tabel (patrz engine/schema.sql).
    //  Mozna nadpisac zmienna srodowiskowa STORAGE_DRIVER.
    // ============================================================================
    public static function storageDriver(): string
    {
        $d = getenv('STORAGE_DRIVER') ?: 'csv';
        return in_array($d, ['csv', 'mysql'], true) ? $d : 'csv';
    }

    // CORS dla API. Domyslnie WYLACZONE (API dziala same-origin, jak dotychczas).
    // Gdy w przyszlosci urzadzenia beda laczyc sie zdalnie z innego originu, ustaw tu
    // konkretny origin (np. 'https://panel.twojadomena.pl') albo '*' — SWIADOMIE, bo API
    // nie ma jeszcze uwierzytelniania. Mozna nadpisac zmienna srodowiskowa CORS_ORIGIN.
    // Pusty string => brak naglowkow CORS.
    public static function corsOrigin(): string
    {
        $v = getenv('CORS_ORIGIN');
        return $v === false ? '' : $v;
    }

    // --- Polaczenie MySQL (uzupelnij na serwerze; w cPanel: sekcja "Bazy danych MySQL") ---
    public static function mysql(): array
    {
        return [
            'host'    => getenv('DB_HOST') ?: 'localhost',
            'port'    => (int)(getenv('DB_PORT') ?: 3306),
            'name'    => getenv('DB_NAME') ?: '',   // np. login_aleksander
            'user'    => getenv('DB_USER') ?: '',   // np. login_panel
            'pass'    => getenv('DB_PASS') ?: '',
            'charset' => 'utf8mb4',
            // Nazwy tabel (mozna zostawic domyslne).
            'table_entries'  => 'entries',
            'table_settings' => 'settings',
        ];
    }

    // ------------------------------- Sciezki plikow --------------------------------
    // Katalog z danymi: 1) zmienna srodowiskowa, 2) stala PANEL_DATA_DIR (z ui/paths.php),
    // 3) domyslnie ../data obok engine/ (dziala lokalnie i przy jednolitym ukladzie).
    private static function dataDir(): string
    {
        if (defined('PANEL_DATA_DIR')) {
            return rtrim(PANEL_DATA_DIR, '/');
        }
        return __DIR__ . '/../data';
    }
    public static function dataFile(): string    { return getenv('DATA_FILE') ?: (self::dataDir() . '/karmienia.csv'); }
    public static function backupFile(): string  { return getenv('BACKUP_FILE') ?: (self::dataDir() . '/karmienia_backup.csv'); }
    public static function settingsFile(): string{ return getenv('SETTINGS_FILE') ?: (self::dataDir() . '/ustawienia.cfg'); }

    // Sciezka do kopii zapasowej z aktualnym czasem: DATA-GODZINA-SEKUNDA.bakap
    // (format YYYY-MM-DD-HH-MM-SS.bakap), zapisywana OBOK pliku danych
    // (ten sam katalog co dataFile — dziala tez gdy DATA_FILE wskazuje inne miejsce).
    public static function timestampedBackupFile(?DateTimeImmutable $when = null): string
    {
        $when = $when ?? new DateTimeImmutable();
        return dirname(self::dataFile()) . '/' . $when->format('Y-m-d-H-i-s') . '.bakap';
    }

    // Opcjonalny token dla przyjmowania pliku CSV z zewnatrz (POST /api/upload-data).
    // Gdy pusty => endpoint otwarty (jak dotychczasowy import). Gdy ustawiony (env
    // UPLOAD_TOKEN), zadanie MUSI podac ten sam token (naglowek X-Upload-Token lub ?token=).
    public static function uploadToken(): string
    {
        $v = getenv('UPLOAD_TOKEN');
        return $v === false ? '' : (string)$v;
    }
}
