<?php
/* ============================================================================
   ANALIZA NOCY Z KAMERY (jedna noc) — docs/PROJEKT_ANALIZA_NOCY.md, rozdział 5

   Wejście: plik nocy z serwera domowego (wersja 1 albo 2) + wpisy dziennika z okolicy tej nocy.
   Wyjście: położenia i zasypianie, okres snu, spokojne bloki, przebudzenia, rytm, kobierzec,
   zmiany pozycji, strefy, dźwięk, pogoda. Wszystkie progi w self::T (zmiana progu = nowa
   TUNE_VERSION = przeliczenie wszystkich nocy przy następnym odczycie).

   To ocena orientacyjna z ruchu i dźwięku, nie pomiar faz snu.
   ============================================================================ */
declare(strict_types=1);

final class SleepAnalysis
{
    const TUNE_VERSION = 2;
    const T = [
        'activeC' => 4,          // minuta aktywna: >= 4 klatki z ruchem (z 120)
        'quietC' => 1,           // minuta spokojna: <= 1 klatka z ruchem
        'bigM' => 60,            // duży ruch: >= 6,0% maski łóżeczka (m x10)
        'adultM' => 60,          // ruch dorosłego: >= 6,0% maski ...
        'adultT' => 120,         // ... albo >= 12,0% całego kadru
        'adultMinS' => 10,       // ... przez co najmniej 10 s
        'adultGapS' => 20,       // przerwy <= 20 s łączą ruch dorosłego w jeden
        'placeQuietS' => 180,    // położenie = koniec ruchu dorosłego, po którym 3 min bez kolejnego
        'settleWin' => 10,       // zaśnięcie = pierwsze 10 min ...
        'settleMaxActive' => 2,  // ... z najwyżej 2 aktywnymi minutami i bez dużego ruchu
        'activeLimitMin' => 60,  // godzina po położeniu bez zaśnięcia ...
        'activeLimitPct' => 80,  // ... i >= 80% aktywnych minut = "sen aktywny, nie da się oszacować"
        'settleSearchMin' => 120,
        'twitch' => 3,           // pojedyncza aktywna minuta między >= 3 spokojnymi nie przerywa bloku
        'peakSepMin' => 25,      // szczyty aktywności oddalone >= 25 min
        'rhythmR' => 0.25,       // minimalna siła rytmu (autokorelacja)
        'poseThr' => 6.0,        // zmiana pozycji: różnica obrazu spokojnych bloków >= 6 (0-255)
        'emptyThr' => 8.0,       // puste łóżeczko: różnica od wzorca < 8
        'feedBeforeMin' => 30,   // położenie "po karmieniu": karmienie do 30 min wcześniej
        'diaryMatchMin' => 15,   // potwierdzenie dziennikiem: SEN_START w ±15 min
        'wakeActiveMin' => 3,    // przebudzenie: epizod z >= 3 aktywnymi minutami
        'wakeGapMin' => 5,       // aktywne minuty dzielone przerwą <= 5 min = jeden epizod
        'finalWakeH' => 2.5,     // pobudka poranna: ruch dorosłego w ostatnich 2,5 h okna
        'eveningUntil' => 23,    // dziennik jako zapas położenia wieczornego: SEN_START przed 23:00
    ];

    /** Wpisy dziennika (date,time,type,ml) jako [ts, type, ml] w zakresie [a, b]. */
    public static function diaryRows(array $entries, int $a, int $b): array
    {
        $out = [];
        foreach ($entries as $e) {
            $d = (string)($e['date'] ?? ''); $t = (string)($e['time'] ?? '');
            if (strlen($d) !== 10 || strlen($t) < 5) continue;
            $ts = strtotime($d . ' ' . substr($t, 0, 5));
            if ($ts === false || $ts < $a || $ts > $b) continue;
            $out[] = [$ts, (string)$e['type'], (int)($e['ml'] ?? 0)];
        }
        usort($out, static fn($x, $y) => $x[0] <=> $y[0]);
        return $out;
    }

    public static function isFeed(string $type): bool
    {
        return $type === 'KARMIENIE' || strncmp($type, 'MLEKO', 5) === 0;
    }

    /** Pełna analiza jednej nocy. */
    public static function analyze(array $d, array $diary): array
    {
        $T = self::T;
        $v2 = isset($d['feat']['c']) && is_array($d['feat']['c']);
        $bin = (int)($d['bin_s'] ?? 5) ?: 5;
        if ($v2) {
            $ws = (int)$d['window']['start_ts']; $we = (int)$d['window']['end_ts'];
            $fc = array_map('intval', $d['feat']['c']); $fm = array_map('intval', $d['feat']['m']);
            $ft = array_map('intval', $d['feat']['t'] ?? []);
            $per = intdiv(60, $bin); $n = intdiv(count($fc), $per);
            $c = []; $m = [];
            for ($i = 0; $i < $n; $i++) {
                $sum = 0; $have = false; $mx = 0;
                for ($k = $i * $per; $k < ($i + 1) * $per; $k++) {
                    if ($fc[$k] >= 0) { $sum += $fc[$k]; $have = true; }
                    $mx = max($mx, $fm[$k] ?? 0);
                }
                $c[] = $have ? $sum : -1; $m[] = $mx;
            }
        } else {
            $ws = (int)round((float)($d['start_ts'] ?? 0)); $we = (int)round((float)($d['end_ts'] ?? 0));
            $c = array_map('intval', array_values((array)($d['timeline'] ?? [])));
            $n = count($c); $m = array_fill(0, $n, 0); $fc = []; $fm = []; $ft = [];
        }
        $at = static fn(int $i) => $ws + 60 * $i;
        $idx = static fn(int $ts) => (int)floor(($ts - $ws) / 60);

        // ---- ruch dorosłego (tylko v2: cechy co 5 s)
        $bursts = [];
        if ($v2) {
            $light = [];
            foreach ((array)($d['light'] ?? []) as $lt) $light[(int)floor(((int)$lt - $ws) / $bin)] = true;
            $gapB = intdiv($T['adultGapS'], $bin); $cur = null;
            foreach ($fc as $k => $cc) {
                $big = (($fm[$k] ?? 0) >= $T['adultM'] || ($ft[$k] ?? 0) >= $T['adultT']) && empty($light[$k]);
                if (!$big) continue;
                if ($cur !== null && $k - $cur[1] <= $gapB + 1) { $cur[1] = $k; continue; }
                if ($cur !== null) $bursts[] = $cur;
                $cur = [$k, $k];
            }
            if ($cur !== null) $bursts[] = $cur;
            $bursts = array_values(array_filter($bursts, static fn($b) => ($b[1] - $b[0] + 1) * $bin >= $T['adultMinS']));
            $bursts = array_map(static fn($b) => ['start' => $ws + $b[0] * $bin, 'end' => $ws + ($b[1] + 1) * $bin], $bursts);
        }

        $feeds = array_values(array_filter($diary, static fn($r) => self::isFeed($r[1])));
        $sleepStarts = array_values(array_filter($diary, static fn($r) => $r[1] === 'SEN_START'));

        // ---- położenia i zasypianie
        $placements = []; $removals = [];
        $emptyFrom = [];
        foreach ((array)($d['pose']['blocks'] ?? []) as $pb) {
            $pb = array_pad(array_map('floatval', (array)$pb), 5, 0);
            if ($pb[3] >= 0 && $pb[3] < $T['emptyThr']) $emptyFrom[] = [(int)$pb[0], (int)$pb[1]];
        }
        foreach ($bursts as $j => $b) {
            $next = $bursts[$j + 1]['start'] ?? PHP_INT_MAX;
            if ($next - $b['end'] < $T['placeQuietS']) continue;
            // wyjęcie z łóżeczka (nie położenie): karmienie z dziennika między tym a następnym ruchem dorosłego
            // albo puste łóżeczko (wzorzec) zaraz po nim
            $out = false;
            foreach ($feeds as $r) if ($r[0] >= $b['end'] - 600 && $r[0] < min($next, $b['end'] + 3 * 3600)) { $out = true; break; }
            foreach ($emptyFrom as [$es, $ee]) if ($es >= $b['end'] && $es - $b['end'] <= 600) { $out = true; break; }
            if ($out && $next !== PHP_INT_MAX) { $removals[] = ['at' => $b['start'], 'kind' => 'karmienie']; continue; }
            // ostatni ruch dorosłego nad ranem = wyjęcie po pobudce
            if ($next === PHP_INT_MAX && $b['start'] >= $we - $T['finalWakeH'] * 3600 && $j > 0) { $removals[] = ['at' => $b['start'], 'kind' => 'pobudka']; continue; }
            $placements[] = self::settle($b['end'], $next, 'wykryte', $c, $m, $ws, $n);
        }
        $hasEvening = false;
        foreach ($placements as $p) if ($p['status'] !== 'przerwane') { $hasEvening = true; break; }
        if (!$hasEvening) {
            foreach ($sleepStarts as $r) {
                $hr = (int)date('G', $r[0]);
                if ($r[0] < $ws || $hr < 12 || $hr >= $T['eveningUntil']) continue;
                $next = PHP_INT_MAX;
                foreach ($bursts as $b) if ($b['start'] > $r[0]) { $next = $b['start']; break; }
                array_unshift($placements, self::settle($r[0], $next, 'dziennik', $c, $m, $ws, $n));
                break;
            }
        }
        $first = true;
        foreach ($placements as &$p) {
            $p['confirmed'] = false;
            foreach ($sleepStarts as $r) if (abs($r[0] - $p['placedAt']) <= $T['diaryMatchMin'] * 60) $p['confirmed'] = true;
            $fed = null;
            foreach ($feeds as $r) if ($r[0] <= $p['placedAt'] && $p['placedAt'] - $r[0] <= $T['feedBeforeMin'] * 60) $fed = $r[0];
            if ($p['status'] === 'przerwane') $p['kind'] = 'przerwane';
            elseif ($first) { $p['kind'] = 'wieczorne'; $first = false; }
            else $p['kind'] = $fed !== null ? 'po-karmieniu' : 'nocne';
            $p['afterFeedAt'] = $fed;
        }
        unset($p);
        $main = null;
        foreach ($placements as $p) if ($p['kind'] === 'wieczorne') { $main = $p; break; }

        // ---- poza łóżeczkiem (od wyjęcia do karmienia do kolejnego położenia): te minuty nie liczą się do snu
        $out = [];
        foreach ($removals as $r) {
            if ($r['kind'] !== 'karmienie') continue;
            $back = $we;
            foreach ($placements as $p) if ($p['placedAt'] > $r['at']) { $back = $p['placedAt']; break; }
            $out[] = ['from' => $r['at'], 'to' => $back];
            for ($i = max(0, $idx($r['at'])); $i < min($n, $idx($back)); $i++) $c[$i] = -1;
        }

        // ---- okres snu
        $firstPresent = 0; while ($firstPresent < $n && $c[$firstPresent] < 0) $firstPresent++;
        $sleepStart = $main ? ($main['settledAt'] ?? $main['placedAt']) : ($v2 ? $at($firstPresent) : $ws);
        $sleepEnd = $we;   // koniec snu = wyjęcie po porannej pobudce (ostatni ruch dorosłego), inaczej koniec okna
        foreach ($removals as $r) if ($r['kind'] === 'pobudka' && $r['at'] > $sleepStart) $sleepEnd = $r['at'];
        $s0 = max(0, $idx($sleepStart)); $s1 = min($n, max($s0, $idx($sleepEnd)));

        // ---- spokojne bloki
        $q = [];
        for ($i = $s0; $i < $s1; $i++) $q[$i] = $c[$i] >= 0 && $c[$i] <= $T['quietC'];
        $tw = $T['twitch']; $q2 = $q;
        for ($i = $s0; $i < $s1; $i++) {
            if ($q[$i] || $c[$i] < 0) continue;
            $ok = true;
            for ($k = 1; $k <= $tw && $ok; $k++) $ok = !empty($q[$i - $k]) && !empty($q[$i + $k]);
            if ($ok) $q2[$i] = true;
        }
        $blocks = []; $run = null; $quietTotal = 0;
        for ($i = $s0; $i <= $s1; $i++) {
            $isQ = $i < $s1 && !empty($q2[$i]);
            if ($isQ) { $quietTotal++; if ($run === null) $run = $i; continue; }
            if ($run !== null) { $blocks[] = ['start' => $at($run), 'min' => $i - $run]; $run = null; }
        }
        $sorted = $blocks; usort($sorted, static fn($a, $b) => $b['min'] <=> $a['min']);
        $mid = (int)(($sleepStart + $sleepEnd) / 2);
        $half1 = 0; $half2 = 0;
        foreach ($blocks as $b) { if ($b['start'] < $mid) $half1 = max($half1, $b['min']); else $half2 = max($half2, $b['min']); }

        // ---- przebudzenia (epizody aktywnych minut w okresie snu)
        $wakes = []; $ep = null; $gap = 0;
        for ($i = $s0; $i < $s1; $i++) {
            $act = $c[$i] >= $T['activeC'];
            if ($act) {
                if ($ep !== null && $gap <= $T['wakeGapMin']) { $ep['to'] = $i; $ep['act']++; }
                else { if ($ep !== null) $wakes[] = $ep; $ep = ['from' => $i, 'to' => $i, 'act' => 1]; }
                $gap = 0;
            } else $gap++;
        }
        if ($ep !== null) $wakes[] = $ep;
        $wakes = array_values(array_filter($wakes, static fn($e) => $e['act'] >= $T['wakeActiveMin']));
        $wakeList = array_map(static fn($e) => ['start' => $at($e['from']), 'min' => $e['to'] - $e['from'] + 1, 'activeMin' => $e['act']], $wakes);

        // ---- rytm: aktywność co 5 min, szczyty, autokorelacja co 10 min
        $a5 = []; for ($i = 0; $i + 5 <= $n; $i += 5) { $s = 0; for ($k = $i; $k < $i + 5; $k++) $s += max(0, $c[$k]); $a5[] = $s; }
        $peaks = []; $from5 = intdiv($s0, 5); $to5 = intdiv($s1, 5);
        $sm = [];
        for ($i = 0; $i < count($a5); $i++) $sm[$i] = (($a5[$i - 1] ?? $a5[$i]) + 2 * $a5[$i] + ($a5[$i + 1] ?? $a5[$i])) / 4;
        $mxs = $sm ? max(array_slice($sm, $from5, max(1, $to5 - $from5)) ?: [0]) : 0;
        $thr = max(10, 0.25 * $mxs); $last = -1000;
        for ($i = max(1, $from5); $i < min(count($sm) - 1, $to5); $i++) {
            if ($sm[$i] >= $thr && $sm[$i] >= $sm[$i - 1] && $sm[$i] > $sm[$i + 1]) {
                if (($i - $last) * 5 >= $T['peakSepMin']) { $peaks[] = $i; $last = $i; }
                elseif ($sm[$i] > $sm[$last]) { $peaks[count($peaks) - 1] = $i; $last = $i; }
            }
        }
        $intervals = [];
        for ($k = 1; $k < count($peaks); $k++) { $iv = ($peaks[$k] - $peaks[$k - 1]) * 5; if ($iv >= 30 && $iv <= 120) $intervals[] = $iv; }
        sort($intervals);
        $a10 = []; for ($i = $s0; $i + 10 <= $s1; $i += 10) { $s = 0; for ($k = $i; $k < $i + 10; $k++) $s += max(0, $c[$k]); $a10[] = $s; }
        $bestLag = null; $bestR = 0.0;
        if (count($a10) >= 12) {
            $mu = array_sum($a10) / count($a10); $den = 0.0;
            foreach ($a10 as $x) $den += ($x - $mu) ** 2;
            if ($den > 0) for ($lag = 4; $lag <= 12 && $lag < count($a10) - 3; $lag++) {
                $num = 0.0; for ($i = 0; $i + $lag < count($a10); $i++) $num += ($a10[$i] - $mu) * ($a10[$i + $lag] - $mu);
                $r = $num / $den; if ($r > $bestR) { $bestR = $r; $bestLag = $lag; }
            }
        }
        $cycle = $bestLag !== null && $bestR >= $T['rhythmR'] ? $bestLag * 10 : (count($intervals) >= 3 ? $intervals[intdiv(count($intervals), 2)] : null);
        $hourly = [];
        for ($i = 0; $i < $n; $i++) {
            if ($c[$i] < 0) continue;
            $h = date('H', $at($i)); $hourly[$h] = ($hourly[$h] ?? 0) + ($c[$i] >= $T['activeC'] ? 1 : 0);
        }
        $firstMove = null; $lastMove = null;
        for ($i = $s0; $i < $s1; $i++) if ($c[$i] >= $T['activeC']) { $firstMove ??= $at($i); $lastMove = $at($i); }

        // ---- kobierzec: 30-min komórki od 18:30 do 06:00 (23), wartość = aktywne minuty, -1 = brak danych
        $carpet = array_fill(0, 23, -1);
        for ($i = 0; $i < $n; $i++) {
            if ($c[$i] < 0) continue;
            $mod = ((int)date('G', $at($i)) * 60 + (int)date('i', $at($i)) - 1110 + 1440) % 1440;
            $cell = intdiv($mod, 30); if ($cell >= 23) continue;
            $carpet[$cell] = max(0, $carpet[$cell]) + ($c[$i] >= $T['activeC'] ? 1 : 0);
        }

        // ---- zmiany pozycji i puste łóżeczko
        $pose = (array)($d['pose']['blocks'] ?? []);
        $changes = []; $empty = [];
        foreach ($pose as $k => $b) {
            [$bs, $be, $sp, $se, $lt] = array_pad(array_map('floatval', (array)$b), 5, 0);
            if ($k > 0 && $sp >= $T['poseThr'] && !$lt) $changes[] = ['at' => (int)$bs, 'score' => $sp];
            if ($se >= 0 && $se < $T['emptyThr']) $empty[] = ['start' => (int)$bs, 'end' => (int)$be];
        }

        // ---- strefy
        $z = (array)($d['zones'] ?? []);
        $zones = null;
        if (!empty($z['defined'])) {
            $tot = array_map('intval', (array)($z['total'] ?? [0, 0, 0, 0])); $sum = array_sum($tot);
            $zones = ['names' => $z['names'] ?? ['A', 'B', 'C', 'D'], 'cells' => $z['cells'] ?? [],
                      'pct' => array_map(static fn($v) => $sum ? round($v / $sum * 100, 1) : 0, $tot)];
            $hours = [];
            foreach ((array)($z['per5min'] ?? []) as $k => $row) {
                $h = date('H', $ws + 300 * $k);
                $hours[$h] ??= [0, 0, 0, 0];
                foreach ((array)$row as $q3 => $v) $hours[$h][$q3] += (int)$v;
            }
            $zones['hours'] = $hours;
        }

        // ---- dźwięk
        $au = (array)($d['audio'] ?? []);
        $audio = ['present' => !empty($au['present'])];
        if ($audio['present']) {
            $ev = [];
            foreach ((array)($au['events'] ?? []) as $e) {
                $s = (int)($e['start_ts'] ?? 0); $en = (int)($e['end_ts'] ?? $s);
                $parent = false;
                foreach ($bursts as $b) if ($s <= $b['end'] + 30 && $en >= $b['start'] - 30) { $parent = true; break; }
                $ev[] = ['start' => $s, 'dur' => (int)($e['dur_s'] ?? 0), 'kind' => (string)($e['kind'] ?? ''),
                         'db' => (float)($e['max_db'] ?? 0), 'parent' => $parent];
            }
            $cry = array_values(array_filter($ev, static fn($e) => $e['kind'] === 'placz' && !$e['parent']));
            $audio += [
                'events' => $ev,
                'cryCount' => count($cry),
                'cryMin' => round(array_sum(array_column($cry, 'dur')) / 60, 1),
                'voiceCount' => count(array_filter($ev, static fn($e) => $e['kind'] === 'glos')),
                'firstCry' => $cry ? $cry[0]['start'] : null,
                'level' => array_map('intval', (array)($au['level_1min'] ?? [])),
                'cls' => array_map('intval', (array)($au['class_1min'] ?? [])),
                'samples' => count((array)($au['samples'] ?? [])),
            ];
        }

        $onsetOk = $main && $main['status'] === 'ok';
        return [
            'tune' => self::TUNE_VERSION, 'v2' => $v2,
            'windowStart' => $ws, 'windowEnd' => $we,
            'placements' => $placements, 'removals' => $removals, 'outOfCrib' => $out, 'bursts' => $bursts,
            'onset' => $main ? [
                'placedAt' => $main['placedAt'], 'settledAt' => $main['settledAt'], 'minutes' => $main['minutes'],
                'status' => $main['status'], 'source' => $main['source'], 'confirmed' => $main['confirmed'],
                'class' => $onsetOk ? ($main['minutes'] < 10 ? 'szybko' : ($main['minutes'] <= 30 ? 'normalnie' : 'długo')) : null,
            ] : null,
            'sleepStart' => $sleepStart, 'sleepEnd' => $sleepEnd, 'sleepMin' => intdiv(max(0, $sleepEnd - $sleepStart), 60),
            'quietMin' => $quietTotal,
            'blocks' => array_slice($sorted, 0, 3), 'blockHalf1' => $half1, 'blockHalf2' => $half2,
            'longestBlock' => $sorted[0]['min'] ?? 0,
            'blocks30' => count(array_filter($blocks, static fn($b) => $b['min'] >= 30)),
            'blocks60' => count(array_filter($blocks, static fn($b) => $b['min'] >= 60)),
            'wakes' => $wakeList, 'wakeCount' => count($wakeList), 'firstWake' => $wakeList[0]['start'] ?? null,
            'rhythm' => ['curve5' => $a5, 'peaks' => $peaks, 'cycleMin' => $cycle, 'r' => round($bestR, 2),
                         'firstMove' => $firstMove, 'lastMove' => $lastMove, 'hourly' => $hourly],
            'carpet' => $carpet,
            'cMin' => $c,          // klatki z ruchem w każdej minucie okna (-1 = brak danych / poza łóżeczkiem)
            'positions' => ['changes' => $changes, 'count' => count($changes), 'blocks' => count($pose),
                            'empty' => $empty, 'emptyRef' => !empty($d['pose']['empty_ref'])],
            'zones' => $zones,
            'audio' => $audio,
            'weather' => $d['weather'] ?? null,
        ];
    }

    /** Zasypianie od położenia `placed` do najbliższego ruchu dorosłego `next`. */
    private static function settle(int $placed, int $next, string $source, array $c, array $m, int $ws, int $n): array
    {
        $T = self::T;
        $p = max(0, (int)ceil(($placed - $ws) / 60));
        $limit = $next === PHP_INT_MAX ? $n : min($n, (int)floor(($next - $ws) / 60));
        $res = ['placedAt' => $placed, 'settledAt' => null, 'minutes' => null, 'status' => 'niepewne', 'source' => $source];
        $checkedActive = false;
        for ($k = $p; $k <= $p + $T['settleSearchMin']; $k++) {
            // godzina od położenia bez zaśnięcia: gdy prawie cały czas ruch - "sen aktywny, nie da się oszacować"
            if (!$checkedActive && $k > $p + $T['activeLimitMin'] - $T['settleWin']) {
                $checkedActive = true;
                if (self::activeShare($c, $p, $n) >= $T['activeLimitPct']) { $res['status'] = 'aktywny'; return $res; }
            }
            if ($k + $T['settleWin'] > $limit) {
                if ($limit < $n && $k + $T['settleWin'] <= $p + $T['activeLimitMin']) $res['status'] = 'przerwane';
                break;
            }
            $ok = true; $act = 0;
            for ($i = $k; $i < $k + $T['settleWin']; $i++) {
                if ($c[$i] < 0 || $m[$i] >= $T['bigM']) { $ok = false; break; }
                if ($c[$i] >= $T['activeC']) $act++;
            }
            if ($ok && $act <= $T['settleMaxActive']) {
                $res['settledAt'] = $ws + 60 * $k;
                $res['minutes'] = max(0, (int)round(($res['settledAt'] - $placed) / 60));
                $res['status'] = 'ok';
                return $res;
            }
        }
        if ($res['status'] === 'przerwane') return $res;
        if (self::activeShare($c, $p, $n) >= $T['activeLimitPct']) $res['status'] = 'aktywny';
        return $res;
    }

    /** % aktywnych minut w godzinie od minuty $p (0, gdy danych mniej niż pół godziny). */
    private static function activeShare(array $c, int $p, int $n): float
    {
        $T = self::T; $act = 0; $have = 0;
        for ($i = $p; $i < min($n, $p + $T['activeLimitMin']); $i++) {
            if ($c[$i] < 0) continue;
            $have++; if ($c[$i] >= $T['activeC']) $act++;
        }
        return $have >= $T['activeLimitMin'] / 2 ? $act / $have * 100 : 0.0;
    }
}
