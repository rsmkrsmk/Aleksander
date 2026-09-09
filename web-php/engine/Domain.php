<?php
/* ============================================================================
   SILNIK DANYCH — logika domenowa (czysta, bez magazynu)
   Operuje na wpisach dostarczonych przez repozytorium. Te same reguly co firmware:
   parser CSV, wiek dziecka, statystyki dnia, sen wg Napper, rytm karmien.
   Wpis (entry) = tablica: date, time, type, ml, piersLeft, piersRight [, lineIndex].
   ============================================================================ */
declare(strict_types=1);

require_once __DIR__ . '/config.php';

final class Domain
{
    const STATS_DAY_COUNT = 8;

    /* ------------------------------- Czas / format ------------------------------- */
    public static function dateIso(DateTimeImmutable $d): string { return $d->format('Y-m-d'); }
    public static function webDateTime(DateTimeImmutable $d): string { return $d->format('Y-m-d\TH:i'); }
    public static function formatDateTime(DateTimeImmutable $d): string { return $d->format('d.m.Y H:i'); }
    private static function beginningOfDay(DateTimeImmutable $d): DateTimeImmutable { return $d->setTime(0, 0, 0); }

    public static function dayOffsetFromToday(int $daysBack, DateTimeImmutable $now): DateTimeImmutable
    {
        return self::beginningOfDay($now->setTime(12, 0, 0)->modify("-{$daysBack} day"));
    }

    public static function csvDateTimeToDate(string $dateStr, string $timeStr): ?DateTimeImmutable
    {
        if (strlen($dateStr) !== 10 || strlen($timeStr) < 5) return null;
        $y = (int)substr($dateStr, 0, 4); $mo = (int)substr($dateStr, 5, 2); $da = (int)substr($dateStr, 8, 2);
        $hh = (int)substr($timeStr, 0, 2); $mm = (int)substr($timeStr, 3, 2);
        return (new DateTimeImmutable())->setDate($y, $mo, $da)->setTime($hh, $mm, 0);
    }

    public static function parseWebDateTime(string $value): ?DateTimeImmutable
    {
        if (strlen($value) !== 16) return null;
        if ($value[4] !== '-' || $value[7] !== '-' || $value[10] !== 'T' || $value[13] !== ':') return null;
        foreach ([0,1,2,3,5,6,8,9,11,12,14,15] as $p) { if ($value[$p] < '0' || $value[$p] > '9') return null; }
        $y = (int)substr($value,0,4); $mo = (int)substr($value,5,2); $da = (int)substr($value,8,2);
        $hh = (int)substr($value,11,2); $mm = (int)substr($value,14,2);
        if (!checkdate($mo, $da, $y) || $hh > 23 || $mm > 59) return null;
        $d = (new DateTimeImmutable())->setDate($y, $mo, $da)->setTime($hh, $mm, 0);
        if ($d->getTimestamp() < 1735689600) return null; // < 2025-01-01 UTC (jak firmware)
        return $d;
    }

    /* ------------------------------- Wiek dziecka -------------------------------- */
    private static function birthDate(): DateTimeImmutable
    {
        return (new DateTimeImmutable())->setDate(Config::BIRTH_YEAR, Config::BIRTH_MONTH, Config::BIRTH_DAY)->setTime(12, 0, 0);
    }
    public static function calculateAgeDays(DateTimeImmutable $now): int
    {
        $diff = ($now->setTime(12,0,0)->getTimestamp() - self::birthDate()->getTimestamp()) / 86400.0;
        return (int)round($diff);
    }
    public static function calculateAgeText(DateTimeImmutable $now): string
    {
        $days = self::calculateAgeDays($now);
        if ($days < 0) return 'Wiek: data urodzenia jest w przyszlosci';
        $nowNoon = $now->setTime(12,0,0); $b = self::birthDate();
        $fullMonths = ((int)$nowNoon->format('Y') - (int)$b->format('Y')) * 12 + ((int)$nowNoon->format('n') - (int)$b->format('n'));
        if ((int)$nowNoon->format('j') < (int)$b->format('j')) $fullMonths--;
        if ($fullMonths < 0) $fullMonths = 0;
        $weeks = intdiv($days, 7); $extra = $days % 7;
        return "Aleksander ma {$days} dni\n{$weeks} tyg. i {$extra} dni | {$fullMonths} mies.";
    }
    public static function developmentTipForToday(DateTimeImmutable $now): string
    {
        $days = self::calculateAgeDays($now);
        if ($days < 0) return 'Rozwoj: oczekiwanie na prawidlowy czas';
        return "Dzien {$days}: obserwuj rozwoj, zapewnij bliskosc, ruch i spokojny rytm dnia. Kazde dziecko rozwija sie we wlasnym tempie.";
    }
    public static function dayOfLifeForDate(string $isoDate): int
    {
        if (strlen($isoDate) !== 10) return -1;
        $y = (int)substr($isoDate,0,4); $mo = (int)substr($isoDate,5,2); $da = (int)substr($isoDate,8,2);
        $d = (new DateTimeImmutable())->setDate($y, $mo, $da)->setTime(12,0,0);
        return (int)round(($d->getTimestamp() - self::birthDate()->getTimestamp()) / 86400.0);
    }

    /* ------------------------------- Typy wpisow --------------------------------- */
    public static function isMilkType(string $t): bool { return $t === 'MLEKO' || $t === 'MLEKO_MATKI' || $t === 'MLEKO_MODYFIKOWANE' || $t === 'MLEKO_MIESZANE'; }
    public static function milkTypeLabel(string $t): string
    {
        if ($t === 'MLEKO_MATKI') return 'MLEKO MATKI';
        if ($t === 'MLEKO_MODYFIKOWANE') return 'MLEKO MODYFIKOWANE';
        if ($t === 'MLEKO_MIESZANE') return 'MLEKO MIESZANE';
        return 'MLEKO';
    }

    /* ---------------------- Parser / serializacja wiersza CSV -------------------- */
    public static function parseCsvLine(string $line): ?array
    {
        $first = strpos($line, ','); if ($first === false) return null;
        $second = strpos($line, ',', $first + 1); if ($second === false) return null;
        $third = strpos($line, ',', $second + 1); if ($third === false) return null;
        $e = [
            'date' => substr($line, 0, $first),
            'time' => substr($line, $first + 1, $second - $first - 1),
            'type' => substr($line, $second + 1, $third - $second - 1),
            'ml' => 0, 'piersLeft' => 0, 'piersRight' => 0,
        ];
        $fourth = strpos($line, ',', $third + 1);
        if ($fourth === false) { $e['ml'] = (int)substr($line, $third + 1); return $e; }
        $e['ml'] = (int)substr($line, $third + 1, $fourth - $third - 1);
        $fifth = strpos($line, ',', $fourth + 1);
        if ($fifth === false) { $e['piersLeft'] = (int)substr($line, $fourth + 1); return $e; }
        $e['piersLeft'] = (int)substr($line, $fourth + 1, $fifth - $fourth - 1);
        $e['piersRight'] = (int)substr($line, $fifth + 1);
        return $e;
    }

    // Buduje wiersz CSV z pol wpisu (4 lub 6 kolumn) — wspolne dla CSV i backupu MySQL.
    public static function toCsvRow(string $type, DateTimeImmutable $when, int $ml, int $piersLeft = -1, int $piersRight = -1): string
    {
        $date = $when->format('Y-m-d'); $time = $when->format('H:i');
        if ($piersLeft >= 0 || $piersRight >= 0) {
            return sprintf("%s,%s,%s,%d,%d,%d\n", $date, $time, $type, $ml, max($piersLeft, 0), max($piersRight, 0));
        }
        return sprintf("%s,%s,%s,%d\n", $date, $time, $type, $ml);
    }

    public static function describeCsvEntry(array $e): string
    {
        $shortDate = substr($e['date'],8,2) . '.' . substr($e['date'],5,2) . '. ';
        $t = $shortDate . $e['time'] . ' ';
        if (self::isMilkType($e['type'])) $t .= self::milkTypeLabel($e['type']) . ' ' . $e['ml'] . ' ml';
        elseif ($e['type'] === 'KARMIENIE') { $t .= 'KARMIENIE'; if ($e['piersLeft'] > 0 || $e['piersRight'] > 0) $t .= ' L' . $e['piersLeft'] . '/P' . $e['piersRight']; }
        else $t .= $e['type'];
        return $t;
    }
    public static function formatEntryForUi(array $e): string
    {
        $datePl = substr($e['date'],8,2) . '.' . substr($e['date'],5,2) . '.' . substr($e['date'],0,4);
        if ($e['type'] === 'KARMIENIE' && $e['ml'] === 0) return "{$datePl}  {$e['time']}\nKARMIENIE";
        $prefix = self::isMilkType($e['type']) ? self::milkTypeLabel($e['type']) . ' | ' : '';
        return "{$datePl}  {$e['time']}\n{$prefix}{$e['ml']} ml";
    }

    /* --------------------------- Interpolacja Napper ----------------------------- */
    private static function interpTable(int $x, array $xs, array $ys): int
    {
        $n = count($xs); if ($n <= 0) return 0;
        if ($x <= $xs[0]) return $ys[0];
        if ($x >= $xs[$n-1]) return $ys[$n-1];
        for ($i = 1; $i < $n; $i++) {
            if ($x <= $xs[$i]) {
                $x0 = $xs[$i-1]; $x1 = $xs[$i]; $y0 = $ys[$i-1]; $y1 = $ys[$i];
                if ($x1 === $x0) return (int)$y0;
                return (int)($y0 + intdiv(($y1 - $y0) * ($x - $x0), ($x1 - $x0)));
            }
        }
        return $ys[$n-1];
    }
    public static function wakeWindowMinutes(int $ageDays): array
    {
        if ($ageDays < 0) $ageDays = 0;
        $min = self::interpTable($ageDays, Config::WAKE_WIN_AGE_DAYS, Config::WAKE_WIN_MIN_MINUTES);
        $max = self::interpTable($ageDays, Config::WAKE_WIN_AGE_DAYS, Config::WAKE_WIN_MAX_MINUTES);
        if ($max < $min) $max = $min;
        return ['minMin' => $min, 'maxMin' => $max];
    }
    public static function sleepNeedMinutes(int $ageDays): array
    {
        if ($ageDays < 0) $ageDays = 0;
        return [
            'night' => self::interpTable($ageDays, Config::SLEEP_NEED_AGE_DAYS, Config::SLEEP_NEED_NIGHT_MIN),
            'day'   => self::interpTable($ageDays, Config::SLEEP_NEED_AGE_DAYS, Config::SLEEP_NEED_DAY_MIN),
        ];
    }
    public static function napTargetCount(int $ageDays): int
    {
        if ($ageDays < 0) $ageDays = 0;
        return self::interpTable($ageDays, Config::NAP_TARGET_AGE_DAYS, Config::NAP_TARGET_NAPS);
    }
    private static function sleepHourIsNight(int $hour): bool { return $hour >= Config::SLEEP_NIGHT_START_HOUR || $hour < Config::SLEEP_NIGHT_END_HOUR; }

    /* ------------------ Ostatnie wpisy / rytm (na liscie wpisow) ----------------- */
    // $entries: uporzadkowana chronologicznie lista wpisow (jak w pliku/append-only).
    public static function loadLatestEntries(array $entries, DateTimeImmutable $now): array
    {
        $res = [
            'lastFeeding' => 'Brak zapisanego wpisu', 'lastMilk' => 'Brak zapisanego wpisu',
            'lastFeedingTime' => null, 'lastMilkTime' => null, 'lastWeightG' => 0,
            'sleepInProgress' => false, 'sleepStartedTime' => null, 'lastWakeTime' => null,
            'avgFeedingGapMin' => 0, 'longestFeedingGapMin' => 0, 'todayFeedingCount' => 0, 'nextFeedingEta' => null,
        ];
        $today = self::dateIso(self::dayOffsetFromToday(0, $now));
        $prevFeedingToday = null; $sumGap = 0; $gapCount = 0; $sawStart = false;
        foreach ($entries as $e) {
            $stamp = self::csvDateTimeToDate($e['date'], $e['time']);
            if ($e['type'] === 'KARMIENIE') {
                $res['lastFeeding'] = self::formatEntryForUi($e); $res['lastFeedingTime'] = $stamp;
                if ($e['date'] === $today) {
                    $res['todayFeedingCount']++;
                    if ($prevFeedingToday !== null && $stamp !== null) {
                        $gap = intdiv($stamp->getTimestamp() - $prevFeedingToday->getTimestamp(), 60);
                        if ($gap > 0) { $sumGap += $gap; $gapCount++; if ($gap > $res['longestFeedingGapMin']) $res['longestFeedingGapMin'] = $gap; }
                    }
                    $prevFeedingToday = $stamp;
                }
            }
            if (self::isMilkType($e['type'])) { $res['lastMilk'] = self::formatEntryForUi($e); $res['lastMilkTime'] = $stamp; }
            if ($e['type'] === 'WAGA') $res['lastWeightG'] = $e['ml'];
            if ($e['type'] === 'SEN_START') { $res['sleepInProgress'] = true; $res['sleepStartedTime'] = $stamp; $sawStart = true; }
            elseif ($e['type'] === 'SEN_STOP') { $res['sleepInProgress'] = false; $res['sleepStartedTime'] = null; if ($sawStart) { $res['lastWakeTime'] = $stamp; $sawStart = false; } }
        }
        if ($gapCount >= 1) $res['avgFeedingGapMin'] = intdiv($sumGap, $gapCount);
        if ($res['lastFeedingTime'] !== null) $res['nextFeedingEta'] = $res['lastFeedingTime']->modify('+' . Config::COUNTER_BLINK_MIN . ' minutes');
        return $res;
    }

    /* --------------------------- Statystyki dnia --------------------------------- */
    private static function emptyDaySummary(): array
    {
        return ['feedingCount'=>0,'milkCount'=>0,'milkMl'=>0,'motherMilkMl'=>0,'modifiedMilkMl'=>0,'mixedMilkMl'=>0,
            'piersLeftMin'=>0,'piersRightMin'=>0,'diaperWet'=>0,'diaperDirty'=>0,'pumpingMl'=>0,
            'vitaminD'=>false,'weightG'=>0,'sleepDayMin'=>0,'sleepNightMin'=>0,'napCount'=>0];
    }
    private static function accrueSleepInterval(DateTimeImmutable $start, DateTimeImmutable $stop, array $iso, array &$stats): void
    {
        if ($stop->getTimestamp() <= $start->getTimestamp()) return;
        $cur = $start; $guard = 0;
        while ($cur->getTimestamp() < $stop->getTimestamp() && $guard++ < 4000) {
            $night = self::sleepHourIsNight((int)$cur->format('G'));
            $min = (int)$cur->format('i'); $sec = (int)$cur->format('s');
            $nextHour = ($min === 0 && $sec === 0) ? $cur->modify('+3600 seconds') : $cur->modify('+' . (3600 - ($min * 60 + $sec)) . ' seconds');
            $segEnd = $nextHour->getTimestamp() < $stop->getTimestamp() ? $nextHour : $stop;
            $segMin = intdiv($segEnd->getTimestamp() - $cur->getTimestamp(), 60);
            if ($segMin > 0) {
                $segDate = self::dateIso(self::beginningOfDay($cur));
                foreach ($iso as $i => $isoDate) {
                    if ($segDate === $isoDate) { if ($night) $stats[$i]['sleepNightMin'] += $segMin; else $stats[$i]['sleepDayMin'] += $segMin; break; }
                }
            }
            $cur = $segEnd;
        }
    }
    private static function accrueNapCount(?DateTimeImmutable $start, array $iso, array &$stats): void
    {
        if ($start === null) return;
        if (self::sleepHourIsNight((int)$start->format('G'))) return;
        $startDate = self::dateIso(self::beginningOfDay($start));
        foreach ($iso as $i => $isoDate) { if ($startDate === $isoDate) { $stats[$i]['napCount']++; return; } }
    }
    public static function buildDayStats(array $entries, DateTimeImmutable $now): array
    {
        $iso = []; $days = []; $stats = [];
        for ($i = 0; $i < self::STATS_DAY_COUNT; $i++) {
            $d = self::dayOffsetFromToday($i, $now); $days[$i] = $d; $iso[$i] = self::dateIso($d); $stats[$i] = self::emptyDaySummary();
        }
        $openSleepStart = null;
        foreach ($entries as $e) {
            if ($e['type'] === 'SEN_START') { $openSleepStart = self::csvDateTimeToDate($e['date'], $e['time']); self::accrueNapCount($openSleepStart, $iso, $stats); continue; }
            if ($e['type'] === 'SEN_STOP') { if ($openSleepStart !== null) { $stop = self::csvDateTimeToDate($e['date'], $e['time']); if ($stop !== null) self::accrueSleepInterval($openSleepStart, $stop, $iso, $stats); $openSleepStart = null; } continue; }
            foreach ($iso as $i => $isoDate) {
                if ($e['date'] !== $isoDate) continue;
                $s = &$stats[$i];
                if ($e['type'] === 'KARMIENIE') { $s['feedingCount']++; $s['piersLeftMin'] += $e['piersLeft']; $s['piersRightMin'] += $e['piersRight']; }
                elseif (self::isMilkType($e['type'])) { $s['milkCount']++; $s['milkMl'] += $e['ml']; if ($e['type'] === 'MLEKO_MATKI') $s['motherMilkMl'] += $e['ml']; elseif ($e['type'] === 'MLEKO_MODYFIKOWANE') $s['modifiedMilkMl'] += $e['ml']; elseif ($e['type'] === 'MLEKO_MIESZANE') $s['mixedMilkMl'] += $e['ml']; }
                elseif ($e['type'] === 'PIELUCHA_MOKRA') $s['diaperWet']++;
                elseif ($e['type'] === 'PIELUCHA_BRUDNA') $s['diaperDirty']++;
                elseif ($e['type'] === 'ODCIAGANIE') $s['pumpingMl'] += $e['ml'];
                elseif ($e['type'] === 'WITAMINA_D') $s['vitaminD'] = true;
                elseif ($e['type'] === 'WAGA') $s['weightG'] = $e['ml'];
                unset($s); break;
            }
        }
        if ($openSleepStart !== null) self::accrueSleepInterval($openSleepStart, $now, $iso, $stats);
        return ['iso' => $iso, 'days' => $days, 'stats' => $stats];
    }
    public static function dayStatsForOffset(array $entries, int $offset, DateTimeImmutable $now): array
    {
        $r = self::buildDayStats($entries, $now);
        return $r['stats'][$offset] ?? self::emptyDaySummary();
    }
    public static function calendarDayTitle(DateTimeImmutable $d, int $index): string
    {
        $dd = $d->format('d.m.Y');
        if ($index === 0) return "DZISIAJ - {$dd}";
        if ($index === 1) return "WCZORAJ - {$dd}";
        if ($index === 2) return "2 DNI TEMU - {$dd}";
        return "{$index} DNI TEMU - {$dd}";
    }
    public static function formatAgoText(?DateTimeImmutable $then, DateTimeImmutable $now): string
    {
        if ($then === null) return '';
        $delta = $now->getTimestamp() - $then->getTimestamp();
        if ($delta < -60) return 'w przyszlosci';
        if ($delta < 0) $delta = 0;
        $h = intdiv($delta, 3600); $m = intdiv($delta % 3600, 60);
        return $h === 0 ? "{$m} min temu" : "{$h} godz. {$m} min temu";
    }

    // Buduje wpisy pod /api/entries (label + fizyczny lineIndex).
    public static function entriesForDate(array $entries, string $targetDate): array
    {
        $out = [];
        foreach ($entries as $e) {
            if (strncmp($e['date'], $targetDate, strlen($targetDate)) !== 0) continue;
            $out[] = [
                'time' => $e['time'], 'type' => $e['type'],
                'label' => self::isMilkType($e['type']) ? self::milkTypeLabel($e['type']) : $e['type'],
                'ml' => $e['ml'], 'piersLeftMin' => $e['piersLeft'], 'piersRightMin' => $e['piersRight'],
                'lineIndex' => $e['lineIndex'] ?? -1,
            ];
        }
        return $out;
    }
    public static function weightSeries(array $entries): array
    {
        $points = [];
        foreach ($entries as $e) {
            if ($e['type'] !== 'WAGA') continue;
            $dol = self::dayOfLifeForDate($e['date']); if ($dol < 0) continue;
            $points[] = ['day' => $dol, 'date' => $e['date'], 'g' => $e['ml']];
        }
        return $points;
    }
}
