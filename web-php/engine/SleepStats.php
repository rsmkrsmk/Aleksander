<?php
/* ============================================================================
   STATYSTYKI NOCY Z KAMERY (wiele nocy) — docs/PROJEKT_ANALIZA_NOCY.md, rozdziały 5.4–5.5

   - analyses(): analiza każdej nocy (SleepAnalysis) z pamięcią podręczną data/noce/analiza/<noc>.json.
     Noc jest liczona ponownie, gdy zmieni się jej plik, wersja progów albo wpisy dziennika z tej doby.
   - stats(): ostatnia noc, trend tygodniowy, seria nocy, kobierzec, wpływ dnia na noc.
   ============================================================================ */
declare(strict_types=1);

final class SleepStats
{
    /** Próg pokazania zmiany tydzień do tygodnia (różnica średnich) i czy więcej = lepiej. */
    const WEEK_METRICS = [
        'quietH' => ['label' => 'Spokojny sen', 'unit' => 'h', 'thr' => 0.3, 'better' => 1, 'dec' => 1],
        'longestBlock' => ['label' => 'Najdłuższy blok', 'unit' => 'min', 'thr' => 15, 'better' => 1, 'dec' => 0],
        'settleMin' => ['label' => 'Zasypianie', 'unit' => 'min', 'thr' => 5, 'better' => -1, 'dec' => 0],
        'wakeCount' => ['label' => 'Przebudzenia', 'unit' => '', 'thr' => 0.7, 'better' => -1, 'dec' => 1],
        'firstWakeClock' => ['label' => 'Pierwsze przebudzenie', 'unit' => 'clock', 'thr' => 20, 'better' => 1, 'dec' => 0],
        'cryMin' => ['label' => 'Płacz', 'unit' => 'min', 'thr' => 3, 'better' => -1, 'dec' => 1],
        'nightFeeds' => ['label' => 'Karmienia w nocy', 'unit' => '', 'thr' => 0.5, 'better' => -1, 'dec' => 1],
        'positions' => ['label' => 'Zmiany pozycji', 'unit' => '', 'thr' => 1.5, 'better' => 0, 'dec' => 1],
    ];
    const MIN_WEEK_NIGHTS = 4;
    const INF_MIN_GROUP = 10;      // wpływ dnia: pokazujemy od 10 nocy w mniejszej grupie
    const INF_FIRM_GROUP = 20;     // do 20 nocy napis "wstępne"

    private static function cacheDir(): string { return SleepNights::dir() . '/analiza'; }

    /** Analizy nocy od najstarszej: [noc => analiza]; $only = jedna noc. */
    public static function analyses(array $entries, int $days = 400, ?string $only = null): array
    {
        $files = glob(SleepNights::dir() . '/noc_*.json') ?: [];
        sort($files);
        if ($only !== null) $files = array_values(array_filter($files, static fn($f) => substr(basename($f), 4, 10) === $only));
        else $files = array_slice($files, -max(1, $days));
        $out = [];
        foreach ($files as $f) {
            $night = substr(basename($f), 4, 10);
            $base = strtotime($night . ' 00:00');
            $diary = SleepAnalysis::diaryRows($entries, $base - 86400, $base + 36 * 3600);
            $sig = md5(filemtime($f) . ':' . filesize($f) . ':' . SleepAnalysis::TUNE_VERSION . ':' . json_encode($diary));
            $cf = self::cacheDir() . '/' . $night . '.json';
            $hit = is_file($cf) ? json_decode((string)file_get_contents($cf), true) : null;
            if (is_array($hit) && ($hit['sig'] ?? '') === $sig) { $out[$night] = $hit['a']; continue; }
            $d = json_decode((string)file_get_contents($f), true);
            if (!is_array($d)) continue;
            $a = SleepAnalysis::analyze($d, $diary);
            $a['night'] = $night;
            $a['day'] = self::dayFeatures($night, $diary, $a);
            $a['metrics'] = SleepNights::metrics($d);
            $out[$night] = $a;
            if (!is_dir(self::cacheDir())) @mkdir(self::cacheDir(), 0775, true);
            @file_put_contents($cf . '.tmp', json_encode(['sig' => $sig, 'a' => $a], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES), LOCK_EX);
            @rename($cf . '.tmp', $cf);
        }
        ksort($out);
        return $out;
    }

    /** Cechy dnia przed nocą z dziennika (kąpiel, karmienia, drzemki) + temperatura. */
    public static function dayFeatures(string $night, array $diary, array $a): array
    {
        $base = strtotime($night . ' 00:00');
        $evening = $base + 20 * 3600 + 1800;            // 20:30
        $bath = null; $feeds24 = []; $feedsEve = []; $feedsNight = []; $nap = 0; $napStart = null;
        foreach ($diary as [$ts, $type]) {
            if ($type === 'KAPIEL' && $ts >= $base && $ts < $base + 86400) $bath = $ts;
            if (SleepAnalysis::isFeed($type)) {
                $minute = intdiv($ts, 60);          // mleko mieszane = 2 wiersze w tej samej minucie
                if ($ts >= $evening - 86400 && $ts < $evening) $feeds24[$minute] = true;
                if ($ts >= $base + 18 * 3600 && $ts < $evening) $feedsEve[$minute] = true;
                if ($ts >= $a['sleepStart'] && $ts <= $a['sleepEnd']) $feedsNight[$minute] = true;
            }
            if ($ts >= $base + 7 * 3600 && $ts < $base + 19 * 3600) {
                if ($type === 'SEN_START') $napStart = $ts;
                elseif ($type === 'SEN_STOP' && $napStart !== null) { $nap += min(240, intdiv($ts - $napStart, 60)); $napStart = null; }
            }
        }
        $w = (array)($a['weather'] ?? []);
        return [
            'bath' => $bath !== null, 'bathAt' => $bath,
            'feeds24' => count($feeds24), 'feedsEvening' => count($feedsEve), 'nightFeeds' => count($feedsNight),
            'daySleepMin' => $nap,
            'tempDay' => isset($w['day_mean']) ? (float)$w['day_mean'] : null,
            'tempNight' => isset($w['night_min']) ? (float)$w['night_min'] : null,
        ];
    }

    /** Wartości do trendu i wpływu dnia (null = brak). */
    public static function values(array $a): array
    {
        $ok = ($a['onset']['status'] ?? '') === 'ok';
        $fw = $a['firstWake'] ?? null;
        $complete = !empty($a['metrics']['complete']) && ($a['metrics']['rating'] ?? '') !== 'brak-danych';
        if (!$complete) return [];
        return [
            'quietH' => round($a['quietMin'] / 60, 2),
            'longestBlock' => $a['longestBlock'],
            'settleMin' => $ok ? $a['onset']['minutes'] : null,
            'wakeCount' => $a['wakeCount'],
            'firstWakeClock' => $fw ? ((int)date('G', $fw) * 60 + (int)date('i', $fw) - 1110 + 1440) % 1440 : null,
            'cryMin' => $a['audio']['present'] ? $a['audio']['cryMin'] : null,
            'nightFeeds' => $a['day']['nightFeeds'],
            'positions' => $a['v2'] ? $a['positions']['count'] : null,
        ];
    }

    /** Skrót nocy dla listy, /api/status i urządzenia. */
    public static function brief(array $a): array
    {
        $m = $a['metrics'];
        return [
            'night' => $a['night'], 'rating' => $m['rating'], 'quietPct' => $m['quietPct'],
            'complete' => $m['complete'],
            'settleMin' => $a['onset']['minutes'] ?? null, 'settleStatus' => $a['onset']['status'] ?? null,
            'settleClass' => $a['onset']['class'] ?? null,
            'placedAt' => $a['onset']['placedAt'] ?? null, 'settledAt' => $a['onset']['settledAt'] ?? null,
            'longestBlock' => $a['longestBlock'], 'quietMin' => $a['quietMin'], 'sleepMin' => $a['sleepMin'],
            'wakeCount' => $a['wakeCount'], 'positions' => $a['v2'] ? $a['positions']['count'] : null,
            'cryMin' => $a['audio']['present'] ? $a['audio']['cryMin'] : null,
            'cryCount' => $a['audio']['present'] ? $a['audio']['cryCount'] : null,
            'cycleMin' => $a['rhythm']['cycleMin'], 'tempNight' => $a['day']['tempNight'],
        ];
    }

    /** Skrót dla urządzenia ESP32 (widok "Noc z kamery" na karcie Sen): płaski obiekt z samymi
     *  liczbami i krótkimi tekstami, bo firmware czyta go prostym wyszukiwaniem kluczy.
     *  strip: 23 znaki = pół godziny od 18:30 do 06:00: '-' brak danych, 'o' poza łóżeczkiem,
     *  '0'..'9' ruch (minuty z ruchem / 2, maks. 9); cry: 23 znaki '0'/'1' (płacz w tym półgodzinie);
     *  wk: do 7 ostatnich nocy "d.mm:zasypianie:spokój_h*10:ocena" rozdzielone ';'
     *  (zasypianie -1 brak, -2 sen aktywny; ocena 1 spokojna, 2 umiarkowana, 3 niespokojna, 4 mało danych). */
    public static function device(array $entries, ?array $an16 = null): ?array
    {
        $all = array_values($an16 ?? self::analyses($entries, 16));
        $done = array_values(array_filter($all, static fn($a) => !empty($a['metrics']['complete'])));
        if (!$done) return null;
        $a = $done[count($done) - 1];
        $hm = static fn($ts) => $ts ? date('H:i', (int)$ts) : '';
        $rate = static fn($r) => ['spokojna' => 1, 'umiarkowana' => 2, 'niespokojna' => 3][$r] ?? 4;
        $cell = static fn($ts) => intdiv(((int)date('G', $ts) * 60 + (int)date('i', $ts) - 1110 + 1440) % 1440, 30);

        $strip = '';
        foreach ($a['carpet'] as $v) $strip .= $v < 0 ? '-' : (string)min(9, (int)ceil($v / 2));
        $outMin = array_fill(0, 23, 0);
        foreach ((array)$a['outOfCrib'] as $o) {
            for ($t = (int)$o['from']; $t < (int)$o['to']; $t += 60) { $k = $cell($t); if ($k < 23) $outMin[$k]++; }
        }
        foreach ($outMin as $k => $m) if ($m >= 10) $strip[$k] = 'o';
        $cry = str_repeat('0', 23);
        if (!empty($a['audio']['present'])) foreach ($a['audio']['events'] as $e) {
            if ($e['kind'] !== 'placz' || $e['parent']) continue;
            $k = $cell($e['start']); if ($k < 23) $cry[$k] = '1';
        }

        // ostatnie 7 nocy i porównanie z 7 wcześniejszymi
        $rows = [];
        foreach ($done as $n) {
            $v = self::values($n);
            if (!$v) continue;
            $st = $n['onset']['status'] ?? '';
            $rows[] = ['label' => (int)substr($n['night'], 8, 2) . '.' . substr($n['night'], 5, 2),
                       'settle' => $v['settleMin'] !== null ? (int)round($v['settleMin']) : ($st === 'aktywny' ? -2 : -1),
                       'quietH' => (float)$v['quietH'], 'rating' => $rate($n['metrics']['rating'] ?? '')];
        }
        $wkRows = array_slice($rows, -7);
        $prevRows = array_slice($rows, -14, max(0, count($rows) - 7));
        $wk = implode(';', array_map(static fn($r) => $r['label'] . ':' . $r['settle'] . ':' . (int)round($r['quietH'] * 10) . ':' . $r['rating'], $wkRows));
        $set = static fn($rs) => array_values(array_map(static fn($r) => $r['settle'], array_filter($rs, static fn($r) => $r['settle'] >= 0)));
        $qh = static fn($rs) => array_map(static fn($r) => $r['quietH'], $rs);
        $fmtH = static fn($h) => str_replace('.', ',', number_format($h, 1));
        $parts = [];
        [$s1, $s0] = [$set($wkRows), $set($prevRows)];
        if (count($s1) >= self::MIN_WEEK_NIGHTS && count($s0) >= self::MIN_WEEK_NIGHTS)
            $parts[] = 'zasypia ' . (int)round(self::median($s1)) . ' min (wcześniej ' . (int)round(self::median($s0)) . ')';
        [$q1, $q0] = [$qh($wkRows), $qh($prevRows)];
        if (count($q1) >= self::MIN_WEEK_NIGHTS && count($q0) >= self::MIN_WEEK_NIGHTS)
            $parts[] = 'spokój ' . $fmtH(array_sum($q1) / count($q1)) . ' h (' . $fmtH(array_sum($q0) / count($q0)) . ' h)';
        $cmp = $parts ? '7 nocy: ' . implode(', ', $parts)
                      : 'Porównanie tygodni po 14 nocach (jest ' . count($rows) . ')';

        $on = $a['onset'] ?? null;
        return [
            'night' => $a['night'], 'rating' => $rate($a['metrics']['rating'] ?? ''),
            'settleStatus' => $on['status'] ?? '', 'settleMin' => ($on['status'] ?? '') === 'ok' ? (int)$on['minutes'] : -1,
            'placed' => $hm($on['placedAt'] ?? null), 'settled' => $hm($on['settledAt'] ?? null),
            'sleepFrom' => $hm($a['sleepStart']), 'sleepTo' => $hm($a['sleepEnd']),
            'quietPct' => (int)round($a['metrics']['quietPct'] ?? 0), 'longestBlock' => (int)$a['longestBlock'],
            'wakeCount' => (int)$a['wakeCount'],
            'cryCount' => !empty($a['audio']['present']) ? (int)$a['audio']['cryCount'] : -1,
            'strip' => $strip, 'cry' => $cry, 'wk' => $wk, 'cmp' => $cmp,
        ];
    }

    /** stats() z pamięcią (B1): wynik liczony ponownie tylko po nowej/zmienionej nocy, zmianie dziennika,
     *  wersji progów albo zmianie dnia. Podpis: pliki nocy z zakresu (nazwa, mtime, rozmiar) + dziennik. */
    public static function stats(array $entries, int $days = 120): array
    {
        $files = glob(SleepNights::dir() . '/noc_*.json') ?: [];
        sort($files);
        $sigParts = [SleepAnalysis::TUNE_VERSION, self::STATS_VERSION, $days, date('Y-m-d'), md5(json_encode($entries))];
        foreach (array_slice($files, -max(1, $days)) as $f) $sigParts[] = basename($f) . ':' . @filemtime($f) . ':' . @filesize($f);
        $sig = md5(implode('|', $sigParts));
        $cf = self::cacheDir() . '/stats_' . $days . '.json';
        $hit = is_file($cf) ? json_decode((string)file_get_contents($cf), true) : null;
        if (is_array($hit) && ($hit['sig'] ?? '') === $sig && isset($hit['s'])) return $hit['s'];
        $out = self::computeStats($entries, $days);
        if (!is_dir(self::cacheDir())) @mkdir(self::cacheDir(), 0775, true);
        @file_put_contents($cf . '.tmp', json_encode(['sig' => $sig, 's' => $out], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES), LOCK_EX);
        @rename($cf . '.tmp', $cf);
        return $out;
    }

    const STATS_VERSION = 2;   // zmiana sposobu liczenia stats() unieważnia pamięć

    private static function computeStats(array $entries, int $days): array
    {
        $all = self::analyses($entries, $days);
        $nights = array_values($all);
        $last = null;
        for ($i = count($nights) - 1; $i >= 0; $i--) if (!empty($nights[$i]['metrics']['complete'])) { $last = $nights[$i]; break; }
        $last ??= $nights ? $nights[count($nights) - 1] : null;

        // seria nocy
        $series = [];
        foreach ($nights as $a) $series[] = ['night' => $a['night'], 'v' => self::values($a), 'rating' => $a['metrics']['rating'],
                                             'carpet' => $a['carpet'], 'day' => $a['day']];

        // tygodnie (ISO, poniedziałek-niedziela wg daty wieczoru)
        $weeks = [];
        foreach ($series as $s) {
            if (!$s['v']) continue;
            $ts = strtotime($s['night']); $key = date('o-\WW', $ts);
            $weeks[$key] ??= ['week' => $key, 'from' => date('Y-m-d', strtotime('monday this week', $ts)), 'n' => 0, 'vals' => []];
            $weeks[$key]['n']++;
            foreach ($s['v'] as $k => $v) if ($v !== null) $weeks[$key]['vals'][$k][] = $v;
        }
        ksort($weeks); $weeks = array_values($weeks);
        foreach ($weeks as $i => &$w) {
            $w['mean'] = [];
            foreach (self::WEEK_METRICS as $k => $meta) {
                $arr = $w['vals'][$k] ?? [];
                $w['mean'][$k] = $arr ? round(($k === 'settleMin' ? self::median($arr) : array_sum($arr) / count($arr)), 2) : null;
                $w['count'][$k] = count($arr);
            }
            $w['delta'] = [];
            if ($i > 0) {
                $p = $weeks[$i - 1];
                foreach (self::WEEK_METRICS as $k => $meta) {
                    $a = $w['mean'][$k]; $b = $p['mean'][$k] ?? null;
                    if ($a === null || $b === null) continue;
                    $enough = ($w['count'][$k] ?? 0) >= self::MIN_WEEK_NIGHTS && ($p['count'][$k] ?? 0) >= self::MIN_WEEK_NIGHTS;
                    $d = round($a - $b, 2);
                    $dir = !$enough ? 'malo' : (abs($d) < $meta['thr'] ? 'bez-zmian' : ($d > 0 ? 'w-gore' : 'w-dol'));
                    $good = ($dir === 'w-gore' || $dir === 'w-dol') && $meta['better'] !== 0 ? (($d > 0) === ($meta['better'] > 0)) : null;
                    $w['delta'][$k] = ['d' => $d, 'dir' => $dir, 'good' => $good];
                }
            }
            unset($w['vals']);
        }
        unset($w);

        return [
            'last' => $last ? self::lastView($last) : null,
            'weeks' => array_slice($weeks, -6),
            'weekMetrics' => self::WEEK_METRICS,
            'series' => $series,
            'influence' => self::influence($series),
            'nights' => count($nights),
            'tune' => SleepAnalysis::TUNE_VERSION,
        ];
    }

    private static function lastView(array $a): array
    {
        return self::brief($a) + ['blocks' => $a['blocks'], 'blockHalf1' => $a['blockHalf1'], 'blockHalf2' => $a['blockHalf2'],
            'firstWake' => $a['firstWake'], 'rhythmR' => $a['rhythm']['r'], 'day' => $a['day'], 'v2' => $a['v2'],
            'audioPresent' => $a['audio']['present'], 'weather' => $a['weather']];
    }

    /* ---------------- wpływ dnia na noc ---------------- */

    const FACTORS = [
        'bath' => ['label' => 'Kąpiel tego dnia', 'type' => 'bin', 'yes' => 'z kąpielą', 'no' => 'bez kąpieli'],
        'feeds24' => ['label' => 'Liczba karmień w ciągu doby', 'type' => 'num', 'unit' => ''],
        'feedsEvening' => ['label' => 'Karmienia wieczorem (18:00–20:30)', 'type' => 'num', 'unit' => ''],
        'daySleepMin' => ['label' => 'Drzemki w dzień', 'type' => 'num', 'unit' => 'min'],
        'tempDay' => ['label' => 'Temperatura na zewnątrz w dzień', 'type' => 'num', 'unit' => '°C'],
        'tempNight' => ['label' => 'Temperatura na zewnątrz w nocy (min.)', 'type' => 'num', 'unit' => '°C'],
    ];
    const OUTCOMES = [
        'quietH' => ['label' => 'spokojny sen', 'unit' => 'h', 'better' => 1],
        'longestBlock' => ['label' => 'najdłuższy blok', 'unit' => 'min', 'better' => 1],
        'settleMin' => ['label' => 'zasypianie', 'unit' => 'min', 'better' => -1],
        'wakeCount' => ['label' => 'przebudzenia', 'unit' => '', 'better' => -1],
    ];

    public static function influence(array $series): array
    {
        // reszty: wartość minus mediana nocy z ±7 dni (usuwa trend wieku dziecka)
        $res = [];
        foreach (array_keys(self::OUTCOMES) as $k) {
            foreach ($series as $i => $s) {
                $v = $s['v'][$k] ?? null; if ($v === null) continue;
                $t = strtotime($s['night']); $near = [];
                foreach ($series as $s2) {
                    $v2 = $s2['v'][$k] ?? null;
                    if ($v2 !== null && abs(strtotime($s2['night']) - $t) <= 7 * 86400) $near[] = $v2;
                }
                $res[$k][$i] = $v - self::median($near);
            }
        }
        mt_srand(20261102);
        $out = [];
        foreach (self::FACTORS as $f => $meta) {
            $item = ['factor' => $f, 'label' => $meta['label'], 'type' => $meta['type'], 'outcomes' => []];
            $minN = PHP_INT_MAX;
            foreach (self::OUTCOMES as $k => $om) {
                $xs = []; $ys = [];
                foreach ($series as $i => $s) {
                    if (!isset($res[$k][$i])) continue;
                    $x = $s['day'][$f] ?? null; if ($x === null) continue;
                    $xs[] = $meta['type'] === 'bin' ? ($x ? 1 : 0) : (float)$x; $ys[] = $res[$k][$i];
                }
                if ($meta['type'] === 'bin') {
                    $n1 = array_sum($xs); $n0 = count($xs) - $n1; $small = min($n1, $n0);
                    $minN = min($minN, $small);
                    $o = ['outcome' => $k, 'label' => $om['label'], 'unit' => $om['unit'], 'better' => $om['better'], 'n1' => $n1, 'n0' => $n0];
                    if ($small >= self::INF_MIN_GROUP) {
                        $diff = self::groupDiff($xs, $ys);
                        $o += ['diff' => round($diff, 2), 'p' => self::permP($xs, $ys, $diff, 'bin')];
                    }
                } else {
                    $minN = min($minN, count($xs));
                    $o = ['outcome' => $k, 'label' => $om['label'], 'unit' => $om['unit'], 'better' => $om['better'], 'n' => count($xs)];
                    if (count($xs) >= self::INF_MIN_GROUP) {
                        $rho = self::spearman($xs, $ys);
                        $o += ['rho' => round($rho, 2), 'p' => self::permP($xs, $ys, $rho, 'num')];
                    }
                }
                $item['outcomes'][] = $o;
            }
            $minN = $minN === PHP_INT_MAX ? 0 : $minN;
            $item['n'] = $minN;
            $item['status'] = $minN < self::INF_MIN_GROUP ? 'za-malo' : ($minN < self::INF_FIRM_GROUP ? 'wstepne' : 'ok');
            if ($meta['type'] === 'num') {   // ta sama wartość prawie każdej nocy - nie ma czego porównywać
                $vals = [];
                foreach ($series as $s) if (($s['day'][$f] ?? null) !== null) $vals[(string)$s['day'][$f]] = true;
                if (count($vals) < 3) { $item['status'] = 'brak-zmiennosci'; foreach ($item['outcomes'] as &$o) unset($o['rho'], $o['p']); unset($o); }
            }
            $item['need'] = self::INF_MIN_GROUP;
            $out[] = $item;
        }
        return $out;
    }

    private static function groupDiff(array $xs, array $ys): float
    {
        $a = []; $b = [];
        foreach ($xs as $i => $x) { if ($x) $a[] = $ys[$i]; else $b[] = $ys[$i]; }
        return ($a ? array_sum($a) / count($a) : 0) - ($b ? array_sum($b) / count($b) : 0);
    }

    /** Test permutacyjny (B1): rangi i sumy liczone raz, w pętli tylko tasowanie i iloczyn skalarny O(n)
     *  (dla czynników 0/1 suma w grupie "tak"). Tasowanie jak wcześniej (Fisher-Yates od tej samej kopii),
     *  więc wyniki są te same. Wczesne zatrzymanie (Besag-Clifford): partie po 100; gdy po >= 200 losowaniach
     *  odsetek trafień >= 0,15 (p wyraźnie > 0,1, próg strony), dalsze losowania niczego nie zmienią. */
    private static function permP(array $xs, array $ys, float $obs, string $type, int $iter = 1500): float
    {
        $xs = array_values($xs); $ys = array_values($ys);
        $n = count($ys); $thr = abs($obs) - 1e-12; $hit = 0; $k = 0;
        if ($type === 'bin') {
            $n1 = 0; foreach ($xs as $x) if ($x) $n1++;
            $n0 = $n - $n1; $all = array_sum($ys); $base = $ys;
        } else {
            if ($n < 3) return 1.0;
            $rx = self::ranks($xs); $ry = self::ranks($ys);
            $mx = array_sum($rx) / $n; $my = array_sum($ry) / $n;
            $dx = []; $base = []; $sxx = 0.0; $syy = 0.0;
            for ($i = 0; $i < $n; $i++) { $dx[$i] = $rx[$i] - $mx; $base[$i] = $ry[$i] - $my; $sxx += $dx[$i] ** 2; $syy += $base[$i] ** 2; }
            $den = ($sxx > 0 && $syy > 0) ? sqrt($sxx * $syy) : 0.0;
        }
        while ($k < $iter) {
            $end = min($iter, $k + 100);
            for (; $k < $end; $k++) {
                $sh = $base;
                for ($i = $n - 1; $i > 0; $i--) { $j = mt_rand(0, $i); $t = $sh[$i]; $sh[$i] = $sh[$j]; $sh[$j] = $t; }
                if ($type === 'bin') {
                    $s1 = 0.0; for ($i = 0; $i < $n; $i++) if ($xs[$i]) $s1 += $sh[$i];
                    $v = ($n1 ? $s1 / $n1 : 0) - ($n0 ? ($all - $s1) / $n0 : 0);
                } else {
                    if ($den == 0.0) $v = 0.0;
                    else { $sxy = 0.0; for ($i = 0; $i < $n; $i++) $sxy += $dx[$i] * $sh[$i]; $v = $sxy / $den; }
                }
                if (abs($v) >= $thr) $hit++;
            }
            if ($k >= 200 && $hit >= 0.15 * $k) break;
        }
        return round(($hit + 1) / ($k + 1), 3);
    }

    private static function ranks(array $v): array
    {
        $idx = array_keys($v); usort($idx, static fn($a, $b) => $v[$a] <=> $v[$b]);
        $r = []; $i = 0; $n = count($idx);
        while ($i < $n) {
            $j = $i; while ($j + 1 < $n && $v[$idx[$j + 1]] == $v[$idx[$i]]) $j++;
            for ($k = $i; $k <= $j; $k++) $r[$idx[$k]] = ($i + $j) / 2 + 1;
            $i = $j + 1;
        }
        ksort($r); return array_values($r);
    }

    public static function spearman(array $x, array $y): float
    {
        $n = count($x); if ($n < 3) return 0.0;
        $rx = self::ranks($x); $ry = self::ranks($y);
        $mx = array_sum($rx) / $n; $my = array_sum($ry) / $n; $sxy = 0; $sxx = 0; $syy = 0;
        for ($i = 0; $i < $n; $i++) { $sxy += ($rx[$i] - $mx) * ($ry[$i] - $my); $sxx += ($rx[$i] - $mx) ** 2; $syy += ($ry[$i] - $my) ** 2; }
        return $sxx > 0 && $syy > 0 ? $sxy / sqrt($sxx * $syy) : 0.0;
    }

    public static function median(array $a): float
    {
        if (!$a) return 0.0;
        sort($a); $n = count($a);
        return $n % 2 ? (float)$a[intdiv($n, 2)] : ($a[$n / 2 - 1] + $a[$n / 2]) / 2;
    }

    /** Średnia mapa nocy 80x45 z do 7 nocy przed `night` (do porównania "dziś vs zwykle"). */
    public static function avgHeat(string $night): ?array
    {
        $files = glob(SleepNights::dir() . '/noc_*.json') ?: []; sort($files);
        $prev = array_values(array_filter($files, static fn($f) => substr(basename($f), 4, 10) < $night));
        $prev = array_slice($prev, -7);
        $sum = null; $n = 0;
        foreach ($prev as $f) {
            $d = json_decode((string)file_get_contents($f), true);
            $h = $d['heat']['night80']['data'] ?? null;
            if (!is_array($h) || count($h) !== 3600) continue;
            if ($sum === null) $sum = array_fill(0, 3600, 0);
            foreach ($h as $i => $v) $sum[$i] += (int)$v;
            $n++;
        }
        if (!$n) return null;
        return ['w' => 80, 'h' => 45, 'n' => $n, 'data' => array_map(static fn($v) => (int)round($v / $n), $sum)];
    }
}
