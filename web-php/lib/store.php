<?php
// Warstwa danych + logika obliczeniowa odtworzona 1:1 z firmware
// (OfficialWaveshareHelloWorld.ino). Zrodlem danych jest plik CSV; wszystkie czasy
// liczymy w czasie LOKALNYM (Europe/Warsaw, ustawione w config.php), tak jak urzadzenie.
declare(strict_types=1);

require_once __DIR__ . '/config.php';

final class Store
{
    // ------------------------------- Pomocnicze czasu -------------------------------
    public static function dateIso(DateTimeImmutable $d): string
    {
        return $d->format('Y-m-d');
    }

    public static function webDateTime(DateTimeImmutable $d): string
    {
        return $d->format('Y-m-d\TH:i');
    }

    public static function formatDateTime(DateTimeImmutable $d): string
    {
        return $d->format('d.m.Y H:i');
    }

    private static function beginningOfDay(DateTimeImmutable $d): DateTimeImmutable
    {
        return $d->setTime(0, 0, 0);
    }

    // Dzien przesuniety o N wstecz, zakotwiczony na 12:00 -> poczatek tego dnia.
    public static function dayOffsetFromToday(int $daysBack, DateTimeImmutable $now): DateTimeImmutable
    {
        $d = $now->setTime(12, 0, 0)->modify("-{$daysBack} day");
        return self::beginningOfDay($d);
    }

    // "YYYY-MM-DD","HH:MM" -> DateTimeImmutable (lokalnie) lub null.
    public static function csvDateTimeToDate(string $dateStr, string $timeStr): ?DateTimeImmutable
    {
        if (strlen($dateStr) !== 10 || strlen($timeStr) < 5) {
            return null;
        }
        $y = (int)substr($dateStr, 0, 4);
        $mo = (int)substr($dateStr, 5, 2);
        $da = (int)substr($dateStr, 8, 2);
        $hh = (int)substr($timeStr, 0, 2);
        $mm = (int)substr($timeStr, 3, 2);
        $d = (new DateTimeImmutable())->setDate($y, $mo, $da)->setTime($hh, $mm, 0);
        return $d;
    }

    // "YYYY-MM-DDTHH:MM" -> DateTimeImmutable z walidacja (odpowiednik parseWebDateTime).
    public static function parseWebDateTime(string $value): ?DateTimeImmutable
    {
        if (strlen($value) !== 16) {
            return null;
        }
        if ($value[4] !== '-' || $value[7] !== '-' || $value[10] !== 'T' || $value[13] !== ':') {
            return null;
        }
        foreach ([0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15] as $p) {
            if ($value[$p] < '0' || $value[$p] > '9') {
                return null;
            }
        }
        $y = (int)substr($value, 0, 4);
        $mo = (int)substr($value, 5, 2);
        $da = (int)substr($value, 8, 2);
        $hh = (int)substr($value, 11, 2);
        $mm = (int)substr($value, 14, 2);
        // Walidacja poprawnosci daty (odpowiednik round-trip localtime_r).
        if (!checkdate($mo, $da, $y) || $hh > 23 || $mm > 59) {
            return null;
        }
        $d = (new DateTimeImmutable())->setDate($y, $mo, $da)->setTime($hh, $mm, 0);
        // Odrzucamy czas przed 2025-01-01 00:00 UTC — dokladnie jak firmware `< 1735689600`.
        if ($d->getTimestamp() < 1735689600) {
            return null;
        }
        return $d;
    }

    // ------------------------------- Wiek dziecka -----------------------------------
    private static function birthDate(): DateTimeImmutable
    {
        return (new DateTimeImmutable())->setDate(Config::BIRTH_YEAR, Config::BIRTH_MONTH, Config::BIRTH_DAY)->setTime(12, 0, 0);
    }

    public static function calculateAgeDays(DateTimeImmutable $now): int
    {
        $nowNoon = $now->setTime(12, 0, 0);
        $diff = ($nowNoon->getTimestamp() - self::birthDate()->getTimestamp()) / 86400.0;
        return (int)round($diff);
    }

    public static function calculateAgeText(DateTimeImmutable $now): string
    {
        $days = self::calculateAgeDays($now);
        if ($days < 0) {
            return 'Wiek: data urodzenia jest w przyszlosci';
        }
        $nowNoon = $now->setTime(12, 0, 0);
        $b = self::birthDate();
        $fullMonths = ((int)$nowNoon->format('Y') - (int)$b->format('Y')) * 12
            + ((int)$nowNoon->format('n') - (int)$b->format('n'));
        if ((int)$nowNoon->format('j') < (int)$b->format('j')) {
            $fullMonths -= 1;
        }
        if ($fullMonths < 0) {
            $fullMonths = 0;
        }
        $weeks = intdiv($days, 7);
        $extra = $days % 7;
        return "Aleksander ma {$days} dni\n{$weeks} tyg. i {$extra} dni | {$fullMonths} mies.";
    }

    public static function developmentTipForToday(DateTimeImmutable $now): string
    {
        $days = self::calculateAgeDays($now);
        if ($days < 0) {
            return 'Rozwoj: oczekiwanie na prawidlowy czas';
        }
        return "Dzien {$days}: obserwuj rozwoj, zapewnij bliskosc, ruch i spokojny rytm dnia. Kazde dziecko rozwija sie we wlasnym tempie.";
    }

    // Dzien zycia (0 = dzien urodzenia) dla daty CSV "YYYY-MM-DD".
    public static function dayOfLifeForDate(string $isoDate): int
    {
        if (strlen($isoDate) !== 10) {
            return -1;
        }
        $y = (int)substr($isoDate, 0, 4);
        $mo = (int)substr($isoDate, 5, 2);
        $da = (int)substr($isoDate, 8, 2);
        $d = (new DateTimeImmutable())->setDate($y, $mo, $da)->setTime(12, 0, 0);
        $diff = ($d->getTimestamp() - self::birthDate()->getTimestamp()) / 86400.0;
        return (int)round($diff);
    }

    // ------------------------------- Typy wpisow -----------------------------------
    public static function isMilkType(string $t): bool
    {
        return $t === 'MLEKO' || $t === 'MLEKO_MATKI' || $t === 'MLEKO_MODYFIKOWANE';
    }

    public static function milkTypeLabel(string $t): string
    {
        if ($t === 'MLEKO_MATKI') {
            return 'MLEKO MATKI';
        }
        if ($t === 'MLEKO_MODYFIKOWANE') {
            return 'MLEKO MODYFIKOWANE';
        }
        return 'MLEKO';
    }

    // -------------------------- Parser wiersza CSV (4 lub 6 kolumn) -----------------
    public static function parseCsvLine(string $line): ?array
    {
        $first = strpos($line, ',');
        if ($first === false) {
            return null;
        }
        $second = strpos($line, ',', $first + 1);
        if ($second === false) {
            return null;
        }
        $third = strpos($line, ',', $second + 1);
        if ($third === false) {
            return null;
        }
        $entry = [
            'date' => substr($line, 0, $first),
            'time' => substr($line, $first + 1, $second - $first - 1),
            'type' => substr($line, $second + 1, $third - $second - 1),
            'ml' => 0,
            'piersLeft' => 0,
            'piersRight' => 0,
        ];
        $fourth = strpos($line, ',', $third + 1);
        if ($fourth === false) {
            $entry['ml'] = (int)substr($line, $third + 1);
            return $entry;
        }
        $entry['ml'] = (int)substr($line, $third + 1, $fourth - $third - 1);
        $fifth = strpos($line, ',', $fourth + 1);
        if ($fifth === false) {
            $entry['piersLeft'] = (int)substr($line, $fourth + 1);
            return $entry;
        }
        $entry['piersLeft'] = (int)substr($line, $fourth + 1, $fifth - $fourth - 1);
        $entry['piersRight'] = (int)substr($line, $fifth + 1);
        return $entry;
    }

    public static function describeCsvEntry(array $e): string
    {
        $shortDate = substr($e['date'], 8, 2) . '.' . substr($e['date'], 5, 2) . '. ';
        $t = $shortDate . $e['time'] . ' ';
        if (self::isMilkType($e['type'])) {
            $t .= self::milkTypeLabel($e['type']) . ' ' . $e['ml'] . ' ml';
        } elseif ($e['type'] === 'KARMIENIE') {
            $t .= 'KARMIENIE';
            if ($e['piersLeft'] > 0 || $e['piersRight'] > 0) {
                $t .= ' L' . $e['piersLeft'] . '/P' . $e['piersRight'];
            }
        } else {
            $t .= $e['type'];
        }
        return $t;
    }

    private static function formatEntryForUi(array $e): string
    {
        $datePl = substr($e['date'], 8, 2) . '.' . substr($e['date'], 5, 2) . '.' . substr($e['date'], 0, 4);
        if ($e['type'] === 'KARMIENIE' && $e['ml'] === 0) {
            return "{$datePl}  {$e['time']}\nKARMIENIE";
        }
        $prefix = self::isMilkType($e['type']) ? self::milkTypeLabel($e['type']) . ' | ' : '';
        return "{$datePl}  {$e['time']}\n{$prefix}{$e['ml']} ml";
    }

    // --------------------------- Interpolacja tabel Napper --------------------------
    private static function interpTable(int $x, array $xs, array $ys): int
    {
        $n = count($xs);
        if ($n <= 0) {
            return 0;
        }
        if ($x <= $xs[0]) {
            return $ys[0];
        }
        if ($x >= $xs[$n - 1]) {
            return $ys[$n - 1];
        }
        for ($i = 1; $i < $n; $i++) {
            if ($x <= $xs[$i]) {
                $x0 = $xs[$i - 1];
                $x1 = $xs[$i];
                $y0 = $ys[$i - 1];
                $y1 = $ys[$i];
                if ($x1 === $x0) {
                    return (int)$y0;
                }
                return (int)($y0 + intdiv(($y1 - $y0) * ($x - $x0), ($x1 - $x0)));
            }
        }
        return $ys[$n - 1];
    }

    public static function wakeWindowMinutes(int $ageDays): array
    {
        if ($ageDays < 0) {
            $ageDays = 0;
        }
        $minMin = self::interpTable($ageDays, Config::WAKE_WIN_AGE_DAYS, Config::WAKE_WIN_MIN_MINUTES);
        $maxMin = self::interpTable($ageDays, Config::WAKE_WIN_AGE_DAYS, Config::WAKE_WIN_MAX_MINUTES);
        if ($maxMin < $minMin) {
            $maxMin = $minMin;
        }
        return ['minMin' => $minMin, 'maxMin' => $maxMin];
    }

    public static function sleepNeedMinutes(int $ageDays): array
    {
        if ($ageDays < 0) {
            $ageDays = 0;
        }
        return [
            'night' => self::interpTable($ageDays, Config::SLEEP_NEED_AGE_DAYS, Config::SLEEP_NEED_NIGHT_MIN),
            'day' => self::interpTable($ageDays, Config::SLEEP_NEED_AGE_DAYS, Config::SLEEP_NEED_DAY_MIN),
        ];
    }

    public static function napTargetCount(int $ageDays): int
    {
        if ($ageDays < 0) {
            $ageDays = 0;
        }
        return self::interpTable($ageDays, Config::NAP_TARGET_AGE_DAYS, Config::NAP_TARGET_NAPS);
    }

    private static function sleepHourIsNight(int $hour): bool
    {
        return $hour >= Config::SLEEP_NIGHT_START_HOUR || $hour < Config::SLEEP_NIGHT_END_HOUR;
    }

    // ------------------------------- Odczyt pliku CSV -------------------------------
    // Zwraca wpisy z FIZYCZNYM indeksem linii (dataIndex liczony dla KAZDEJ linii po
    // naglowku — takze pustych/niepoprawnych), spojnie z /api/entries i deleteEntryByIndex.
    private static function readEntriesWithIndex(): array
    {
        $file = Config::dataFile();
        if (!is_file($file)) {
            return [];
        }
        $text = (string)file_get_contents($file);
        $lines = explode("\n", $text);
        array_shift($lines); // naglowek
        $out = [];
        foreach ($lines as $dataIndex => $raw) {
            $raw = rtrim($raw, "\r\n");
            $trimmed = trim($raw);
            if ($trimmed === '') {
                continue;
            }
            $e = self::parseCsvLine($trimmed);
            if ($e === null) {
                continue;
            }
            $e['lineIndex'] = $dataIndex;
            $out[] = $e;
        }
        return $out;
    }

    // ---------------------------- Ostatnie wpisy / rytm -----------------------------
    public static function loadLatestEntries(DateTimeImmutable $now): array
    {
        $res = [
            'lastFeeding' => 'Brak zapisanego wpisu',
            'lastMilk' => 'Brak zapisanego wpisu',
            'lastFeedingTime' => null,
            'lastMilkTime' => null,
            'lastWeightG' => 0,
            'sleepInProgress' => false,
            'sleepStartedTime' => null,
            'lastWakeTime' => null,
            'avgFeedingGapMin' => 0,
            'longestFeedingGapMin' => 0,
            'todayFeedingCount' => 0,
            'nextFeedingEta' => null,
        ];
        $entries = self::readEntriesWithIndex();
        $today = self::dateIso(self::dayOffsetFromToday(0, $now));
        $prevFeedingToday = null;
        $sumGapMin = 0;
        $gapCount = 0;
        $sawSleepStart = false;

        foreach ($entries as $e) {
            $stamp = self::csvDateTimeToDate($e['date'], $e['time']);
            if ($e['type'] === 'KARMIENIE') {
                $res['lastFeeding'] = self::formatEntryForUi($e);
                $res['lastFeedingTime'] = $stamp;
                if ($e['date'] === $today) {
                    $res['todayFeedingCount']++;
                    if ($prevFeedingToday !== null && $stamp !== null) {
                        $gap = intdiv($stamp->getTimestamp() - $prevFeedingToday->getTimestamp(), 60);
                        if ($gap > 0) {
                            $sumGapMin += $gap;
                            $gapCount++;
                            if ($gap > $res['longestFeedingGapMin']) {
                                $res['longestFeedingGapMin'] = $gap;
                            }
                        }
                    }
                    $prevFeedingToday = $stamp;
                }
            }
            if (self::isMilkType($e['type'])) {
                $res['lastMilk'] = self::formatEntryForUi($e);
                $res['lastMilkTime'] = $stamp;
            }
            if ($e['type'] === 'WAGA') {
                $res['lastWeightG'] = $e['ml'];
            }
            if ($e['type'] === 'SEN_START') {
                $res['sleepInProgress'] = true;
                $res['sleepStartedTime'] = $stamp;
                $sawSleepStart = true;
            } elseif ($e['type'] === 'SEN_STOP') {
                $res['sleepInProgress'] = false;
                $res['sleepStartedTime'] = null;
                if ($sawSleepStart) {
                    $res['lastWakeTime'] = $stamp;
                    $sawSleepStart = false;
                }
            }
        }
        if ($gapCount >= 1) {
            $res['avgFeedingGapMin'] = intdiv($sumGapMin, $gapCount);
        }
        if ($res['lastFeedingTime'] !== null) {
            $res['nextFeedingEta'] = $res['lastFeedingTime']->modify('+' . Config::COUNTER_BLINK_MIN . ' minutes');
        }
        return $res;
    }

    // ------------------------ Statystyki dnia (jeden przebieg) ----------------------
    private static function emptyDaySummary(): array
    {
        return [
            'feedingCount' => 0, 'milkCount' => 0, 'milkMl' => 0, 'motherMilkMl' => 0, 'modifiedMilkMl' => 0,
            'piersLeftMin' => 0, 'piersRightMin' => 0, 'diaperWet' => 0, 'diaperDirty' => 0, 'pumpingMl' => 0,
            'vitaminD' => false, 'weightG' => 0, 'sleepDayMin' => 0, 'sleepNightMin' => 0, 'napCount' => 0,
        ];
    }

    private static function accrueSleepInterval(DateTimeImmutable $start, DateTimeImmutable $stop, array $iso, array &$stats): void
    {
        if ($stop->getTimestamp() <= $start->getTimestamp()) {
            return;
        }
        $cur = $start;
        $guard = 0;
        while ($cur->getTimestamp() < $stop->getTimestamp() && $guard++ < 4000) {
            $night = self::sleepHourIsNight((int)$cur->format('G'));
            // krok do najblizszej pelnej godziny
            $min = (int)$cur->format('i');
            $sec = (int)$cur->format('s');
            if ($min === 0 && $sec === 0) {
                $nextHour = $cur->modify('+3600 seconds');
            } else {
                $nextHour = $cur->modify('+' . (3600 - ($min * 60 + $sec)) . ' seconds');
            }
            $segEnd = $nextHour->getTimestamp() < $stop->getTimestamp() ? $nextHour : $stop;
            $segMin = intdiv($segEnd->getTimestamp() - $cur->getTimestamp(), 60);
            if ($segMin > 0) {
                $segDate = self::dateIso(self::beginningOfDay($cur));
                foreach ($iso as $i => $isoDate) {
                    if ($segDate === $isoDate) {
                        if ($night) {
                            $stats[$i]['sleepNightMin'] += $segMin;
                        } else {
                            $stats[$i]['sleepDayMin'] += $segMin;
                        }
                        break;
                    }
                }
            }
            $cur = $segEnd;
        }
    }

    private static function accrueNapCount(?DateTimeImmutable $start, array $iso, array &$stats): void
    {
        if ($start === null) {
            return;
        }
        if (self::sleepHourIsNight((int)$start->format('G'))) {
            return; // sen nocny nie jest drzemka
        }
        $startDate = self::dateIso(self::beginningOfDay($start));
        foreach ($iso as $i => $isoDate) {
            if ($startDate === $isoDate) {
                $stats[$i]['napCount']++;
                return;
            }
        }
    }

    const STATS_DAY_COUNT = 8;

    // Buduje statystyki dla okna 8 dni (dzis + 7 wstecz).
    public static function buildDayStats(DateTimeImmutable $now): array
    {
        $iso = [];
        $days = [];
        $stats = [];
        for ($i = 0; $i < self::STATS_DAY_COUNT; $i++) {
            $d = self::dayOffsetFromToday($i, $now);
            $days[$i] = $d;
            $iso[$i] = self::dateIso($d);
            $stats[$i] = self::emptyDaySummary();
        }
        $entries = self::readEntriesWithIndex();
        $openSleepStart = null;
        foreach ($entries as $e) {
            if ($e['type'] === 'SEN_START') {
                $openSleepStart = self::csvDateTimeToDate($e['date'], $e['time']);
                self::accrueNapCount($openSleepStart, $iso, $stats);
                continue;
            } elseif ($e['type'] === 'SEN_STOP') {
                if ($openSleepStart !== null) {
                    $stop = self::csvDateTimeToDate($e['date'], $e['time']);
                    if ($stop !== null) {
                        self::accrueSleepInterval($openSleepStart, $stop, $iso, $stats);
                    }
                    $openSleepStart = null;
                }
                continue;
            }
            foreach ($iso as $i => $isoDate) {
                if ($e['date'] !== $isoDate) {
                    continue;
                }
                $s = &$stats[$i];
                if ($e['type'] === 'KARMIENIE') {
                    $s['feedingCount']++;
                    $s['piersLeftMin'] += $e['piersLeft'];
                    $s['piersRightMin'] += $e['piersRight'];
                } elseif (self::isMilkType($e['type'])) {
                    $s['milkCount']++;
                    $s['milkMl'] += $e['ml'];
                    if ($e['type'] === 'MLEKO_MATKI') {
                        $s['motherMilkMl'] += $e['ml'];
                    } elseif ($e['type'] === 'MLEKO_MODYFIKOWANE') {
                        $s['modifiedMilkMl'] += $e['ml'];
                    }
                } elseif ($e['type'] === 'PIELUCHA_MOKRA') {
                    $s['diaperWet']++;
                } elseif ($e['type'] === 'PIELUCHA_BRUDNA') {
                    $s['diaperDirty']++;
                } elseif ($e['type'] === 'ODCIAGANIE') {
                    $s['pumpingMl'] += $e['ml'];
                } elseif ($e['type'] === 'WITAMINA_D') {
                    $s['vitaminD'] = true;
                } elseif ($e['type'] === 'WAGA') {
                    $s['weightG'] = $e['ml'];
                }
                unset($s);
                break;
            }
        }
        // Sen trwajacy do teraz (brak STOP): dolicz do biezacej chwili.
        if ($openSleepStart !== null) {
            self::accrueSleepInterval($openSleepStart, $now, $iso, $stats);
        }
        return ['iso' => $iso, 'days' => $days, 'stats' => $stats];
    }

    public static function dayStatsForOffset(int $offset, DateTimeImmutable $now): array
    {
        $res = self::buildDayStats($now);
        return $res['stats'][$offset] ?? self::emptyDaySummary();
    }

    public static function calendarDayTitle(DateTimeImmutable $d, int $index): string
    {
        $dd = $d->format('d.m.Y');
        if ($index === 0) {
            return "DZISIAJ - {$dd}";
        }
        if ($index === 1) {
            return "WCZORAJ - {$dd}";
        }
        if ($index === 2) {
            return "2 DNI TEMU - {$dd}";
        }
        return "{$index} DNI TEMU - {$dd}";
    }

    public static function formatAgoText(?DateTimeImmutable $then, DateTimeImmutable $now): string
    {
        if ($then === null) {
            return '';
        }
        $delta = $now->getTimestamp() - $then->getTimestamp();
        if ($delta < -60) {
            return 'w przyszlosci';
        }
        if ($delta < 0) {
            $delta = 0;
        }
        $hours = intdiv($delta, 3600);
        $minutes = intdiv($delta % 3600, 60);
        if ($hours === 0) {
            return "{$minutes} min temu";
        }
        return "{$hours} godz. {$minutes} min temu";
    }

    // -------------------------------- Zapis / import --------------------------------
    public static function ensureDataFile(): void
    {
        $file = Config::dataFile();
        $dir = dirname($file);
        if (!is_dir($dir)) {
            @mkdir($dir, 0775, true);
        }
        if (!is_file($file)) {
            file_put_contents($file, Config::CSV_HEADER . "\n");
        }
    }

    // Buforowany zapis wiersza (append) — odpowiednik appendEntry.
    public static function appendEntry(string $type, DateTimeImmutable $when, int $ml, int $piersLeft = -1, int $piersRight = -1): bool
    {
        self::ensureDataFile();
        $date = $when->format('Y-m-d');
        $time = $when->format('H:i');
        if ($piersLeft >= 0 || $piersRight >= 0) {
            $row = sprintf("%s,%s,%s,%d,%d,%d\n", $date, $time, $type, $ml, max($piersLeft, 0), max($piersRight, 0));
        } else {
            $row = sprintf("%s,%s,%s,%d\n", $date, $time, $type, $ml);
        }
        return file_put_contents(Config::dataFile(), $row, FILE_APPEND | LOCK_EX) !== false;
    }

    // Usuwa wpis o fizycznym indeksie (dataIndex) — spojnie z /api/entries.
    public static function deleteEntryByIndex(int $entryIndex): array
    {
        if ($entryIndex < 0) {
            return ['ok' => false];
        }
        $file = Config::dataFile();
        if (!is_file($file)) {
            return ['ok' => false];
        }
        $text = (string)file_get_contents($file);
        $lines = explode("\n", $text);
        $header = array_shift($lines);
        // Plik konczacy sie '\n' daje pusty ostatni element splita — to NIE jest wiersz danych.
        if (!empty($lines) && end($lines) === '') {
            array_pop($lines);
        }
        $kept = [];
        $removedDescription = '';
        $removedFound = false;
        foreach ($lines as $dataIndex => $raw) {
            $raw = rtrim($raw, "\r");
            if ($dataIndex === $entryIndex) {
                $e = self::parseCsvLine(trim($raw));
                if ($e !== null) {
                    $removedDescription = self::describeCsvEntry($e);
                }
                $removedFound = true; // pomijamy ten wiersz
            } else {
                $kept[] = $raw;
            }
        }
        if (!$removedFound || $entryIndex >= count($lines)) {
            return ['ok' => false];
        }
        $body = implode("\n", $kept);
        file_put_contents($file, $header . "\n" . $body . ($body !== '' ? "\n" : ''), LOCK_EX);
        return ['ok' => true, 'removed' => $removedDescription];
    }

    public static function readRawCsv(): string
    {
        $file = Config::dataFile();
        if (!is_file($file)) {
            return Config::CSV_HEADER . "\n";
        }
        return (string)file_get_contents($file);
    }

    // Import: sanityzacja jak w firmware — kanoniczny naglowek, tylko parsowalne wiersze
    // do 160 B. Robi backup obecnego pliku. Zwraca [ok, imported, skipped].
    public static function importCsv(string $rawText): array
    {
        self::ensureDataFile();
        $file = Config::dataFile();
        @copy($file, Config::backupFile());
        $lines = preg_split('/\r?\n/', $rawText);
        $out = [Config::CSV_HEADER];
        $imported = 0;
        $skipped = 0;
        $sawHeader = false;
        foreach ($lines as $line) {
            $t = trim($line);
            if ($t === '') {
                continue;
            }
            if (!$sawHeader) {
                // Do wykrycia naglowka 'data,' pomijamy wszystko (jak firmware).
                if (strncmp($t, 'data,', 5) === 0) {
                    $sawHeader = true;
                }
                continue;
            }
            if (strlen($t) > 160) {
                $skipped++;
                continue;
            }
            if (self::parseCsvLine($t) === null) {
                $skipped++;
                continue;
            }
            $out[] = $t;
            $imported++;
        }
        // Firmware odrzuca plik bez naglowka lub bez poprawnych wierszy (400).
        if (!$sawHeader || $imported === 0) {
            return ['ok' => false, 'imported' => 0, 'skipped' => $skipped];
        }
        file_put_contents($file, implode("\n", $out) . "\n", LOCK_EX);
        return ['ok' => true, 'imported' => $imported, 'skipped' => $skipped];
    }

    // Wpisy dla danego dnia (dla /api/entries) — z fizycznym lineIndex.
    public static function entriesForDate(string $targetDate): array
    {
        $out = [];
        foreach (self::readEntriesWithIndex() as $e) {
            if (strncmp($e['date'], $targetDate, strlen($targetDate)) !== 0) {
                continue;
            }
            $out[] = [
                'time' => $e['time'],
                'type' => $e['type'],
                'label' => self::isMilkType($e['type']) ? self::milkTypeLabel($e['type']) : $e['type'],
                'ml' => $e['ml'],
                'piersLeftMin' => $e['piersLeft'],
                'piersRightMin' => $e['piersRight'],
                'lineIndex' => $e['lineIndex'],
            ];
        }
        return $out;
    }

    // Seria pomiarow wagi (dla /api/weight-series).
    public static function weightSeries(): array
    {
        $points = [];
        foreach (self::readEntriesWithIndex() as $e) {
            if ($e['type'] !== 'WAGA') {
                continue;
            }
            $dol = self::dayOfLifeForDate($e['date']);
            if ($dol < 0) {
                continue;
            }
            $points[] = ['day' => $dol, 'date' => $e['date'], 'g' => $e['ml']];
        }
        return $points;
    }

    // ------------------------------- Ustawienia ------------------------------------
    public static function loadSettings(): array
    {
        $settings = ['sleepTelegram' => false];
        $file = Config::settingsFile();
        if (!is_file($file)) {
            return $settings;
        }
        foreach (explode("\n", (string)file_get_contents($file)) as $line) {
            $eq = strpos($line, '=');
            if ($eq === false || $eq <= 0) {
                continue;
            }
            $key = trim(substr($line, 0, $eq));
            $val = trim(substr($line, $eq + 1));
            if ($key === 'sleepTelegram') {
                $settings['sleepTelegram'] = ((int)$val) !== 0;
            }
        }
        return $settings;
    }

    public static function saveSettings(array $settings): void
    {
        $dir = dirname(Config::settingsFile());
        if (!is_dir($dir)) {
            @mkdir($dir, 0775, true);
        }
        file_put_contents(Config::settingsFile(), 'sleepTelegram=' . ($settings['sleepTelegram'] ? 1 : 0) . "\n", LOCK_EX);
    }
}
