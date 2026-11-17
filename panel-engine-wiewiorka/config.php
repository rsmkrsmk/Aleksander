<?php
/* ============================================================================
   wiewiorka.mom — SILNIK DANYCH: konfiguracja
   Reguly domenowe (normy snu, wagi, zakresy formularzy) + profil dziecka
   zalogowanego uzytkownika (data i waga urodzenia, imie) ustawiany przez
   router strony po zalogowaniu (Config::setProfile). Dane kazdego konta leza
   w osobnym katalogu (PANEL_DATA_DIR = panel-data-wiewiorka/u_<nr>).
   ============================================================================ */
declare(strict_types=1);

// Firmware liczy wszystko w czasie LOKALNYM Polski — trzymamy tak samo.
date_default_timezone_set('Europe/Warsaw');

final class Config
{
    // ------------------------- Profil dziecka (z konta) -----------------------------
    // Ustawiany po zalogowaniu (Auth::profile -> Config::setProfile). Dla bezpieczenstwa
    // domyslnie dzisiejsza data i typowa waga — nigdy nie sa uzywane bez profilu,
    // bo router wymaga wypelnionego profilu przed dostepem do danych.
    private static array $profile = [];
    public static function setProfile(array $p): void { self::$profile = $p; }
    private static function birthParts(): array
    {
        $b = (string)(self::$profile['birthDate'] ?? '');
        if (preg_match('/^(\d{4})-(\d{2})-(\d{2})$/', $b, $m) && checkdate((int)$m[2], (int)$m[3], (int)$m[1])) return [(int)$m[1], (int)$m[2], (int)$m[3]];
        return [(int)date('Y'), (int)date('n'), (int)date('j')];
    }
    public static function birthYear(): int  { return self::birthParts()[0]; }
    public static function birthMonth(): int { return self::birthParts()[1]; }
    public static function birthDay(): int   { return self::birthParts()[2]; }
    public static function birthWeightG(): int
    {
        $w = (int)(self::$profile['birthWeightG'] ?? 0);
        return ($w >= self::BIRTH_WEIGHT_MIN_G && $w <= self::BIRTH_WEIGHT_MAX_G) ? $w : 3300;
    }
    /** Imie dziecka (opcjonalne); pusty tekst = strona mowi „Maluszek”. */
    public static function childName(): string { return trim((string)(self::$profile['name'] ?? '')); }
    const BIRTH_WEIGHT_MIN_G = 500;
    const BIRTH_WEIGHT_MAX_G = 6500;

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
    // Referencyjny przyrost wagi (slupki tygodniowe): od wypisu ze szpitala, granice okresow.
    const GAIN_DISCHARGE_DAY = 2;
    const GAIN_P1_END_DAY = 91;    // ~3 miesiace (3 x 30,44 dnia): 25-30 g/dzien
    const GAIN_P2_END_DAY = 183;   // ~6 miesiecy: 15-20 g/dzien, potem 10-15

    // Planowany odstep miedzy kapielami (dni) — uzywany przez mini kalendarz kapieli.
    const BATH_INTERVAL_DAYS = 3;

    // Progi belki licznika (minuty od ostatniego karmienia).
    const COUNTER_WARN_MIN = 180;
    const COUNTER_BLINK_MIN = 240;

    // -------------------------------- Sen (Napper) ---------------------------------
    const SLEEP_NIGHT_START_HOUR = 21;
    const SLEEP_NIGHT_END_HOUR = 7;
    // Najdluzsza wiarygodna pojedyncza sesja snu (min). Dluzsze = zapomniany STOP -> sesja odrzucana (nie liczymy 25 h snu w dobie).
    const SLEEP_MAX_SESSION_MIN = 840;
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

    // ------------------------------- Sciezki plikow --------------------------------
    // Katalog z danymi: 1) zmienna srodowiskowa, 2) stala PANEL_DATA_DIR (z ui/paths.php),
    // 3) domyslnie ../data obok engine/ (dziala lokalnie i przy jednolitym ukladzie).
    // Katalog danych ZALOGOWANEGO uzytkownika — definiuje go router strony dopiero po
    // sprawdzeniu logowania. Bez niego silnik nie dotknie zadnych danych.
    private static function dataDir(): string
    {
        if (!defined('PANEL_DATA_DIR')) throw new RuntimeException('Brak katalogu danych uzytkownika (niezalogowany).');
        return rtrim(PANEL_DATA_DIR, '/');
    }
    public static function dataFile(): string    { return self::dataDir() . '/karmienia.csv'; }
    public static function backupFile(): string  { return self::dataDir() . '/karmienia_backup.csv'; }
    public static function settingsFile(): string{ return self::dataDir() . '/ustawienia.cfg'; }
    // Plik z licznikiem rewizji danych — uzywa go polling urzadzenia (v4), aby wykryc
    // zmiane bez pobierania calej historii. Mozna nadpisac zmienna REV_FILE.
    public static function revisionFile(): string{ return self::dataDir() . '/karmienia.rev'; }

    // Sciezka do kopii zapasowej z aktualnym czasem: DATA-GODZINA-SEKUNDA.bakap
    // (format YYYY-MM-DD-HH-MM-SS.bakap), zapisywana OBOK pliku danych
    // (ten sam katalog co dataFile — dziala tez gdy DATA_FILE wskazuje inne miejsce).
    public static function timestampedBackupFile(?DateTimeImmutable $when = null): string
    {
        $when = $when ?? new DateTimeImmutable();
        return dirname(self::dataFile()) . '/' . $when->format('Y-m-d-H-i-s') . '.bakap';
    }
}
