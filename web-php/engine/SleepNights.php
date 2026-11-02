<?php
/* ============================================================================
   SILNIK DANYCH — noce z kamery (jakość snu wg ruchu w łóżeczku)

   Źródło: serwer domowy t620 (kontener "kamery", usługa zapisywacz.py) co rano wysyła
   plik danych zakończonej nocy: POST /api/sleep-night (multipart, pole "file" = JSON
   albo surowy JSON), klucz w nagłówku "Authorization: Bearer KLUCZ", "X-Klucz: KLUCZ"
   albo w polu formularza "klucz".

   Plik nocy (wersja 1) zawiera m.in.: night (data wieczoru), start/end, start_ts/end_ts,
   minutes, timeline (klatki z ruchem w każdej minucie, 0..120 przy 2 kl./s), events
   (start_ts, end_ts, dur_s), hours, summary (events, motion_s, longest_quiet_s,
   coverage_pct, ...). Zapisujemy go bez zmian w data/noce/noc_RRRR-MM-DD.json, a
   wskaźniki nocy (spokój, epizody ruchu, ocena) liczymy tutaj i trzymamy w index.json.

   To NIE jest pomiar faz snu — kamera widzi tylko ruch. Wskaźniki są orientacyjne.
   ============================================================================ */
declare(strict_types=1);

final class SleepNights
{
    const MAX_BYTES = 1048576;           // limit pliku nocy (1 MB)
    const EPISODE_GAP_MIN = 5;           // minuty ruchu dzielone krótszą przerwą = jeden epizod
    const LONG_EPISODE_MIN = 3;          // epizod "dłuższy" = co najmniej tyle minut z ruchem
    const LOW_COVERAGE_PCT = 60;         // poniżej: kamera/detektor nie działały większość nocy

    public static function dir(): string
    {
        $d = getenv('NOCE_DIR') ?: (dirname(Config::dataFile()) . '/noce');
        return rtrim($d, '/');
    }

    /** Klucz odbioru: zmienna środowiskowa NOCE_KLUCZ albo pierwsza linia pliku data/noce_klucz.txt. */
    public static function key(): string
    {
        $v = getenv('NOCE_KLUCZ');
        if ($v !== false && trim($v) !== '') return trim($v);
        $f = dirname(Config::dataFile()) . '/noce_klucz.txt';
        if (is_file($f)) {
            $line = strtok((string)file_get_contents($f), "\r\n");
            return $line === false ? '' : trim($line);
        }
        return '';
    }

    /** Klucz podany w żądaniu (kilka dróg, bo część hostingów obcina nagłówek Authorization). */
    public static function requestKey(): string
    {
        $auth = $_SERVER['HTTP_AUTHORIZATION'] ?? $_SERVER['REDIRECT_HTTP_AUTHORIZATION'] ?? '';
        if ($auth === '' && function_exists('getallheaders')) {
            foreach ((array)getallheaders() as $k => $v) if (strcasecmp((string)$k, 'Authorization') === 0) $auth = (string)$v;
        }
        if (preg_match('/^Bearer\s+(\S+)/i', $auth, $m)) return $m[1];
        if (!empty($_SERVER['HTTP_X_KLUCZ'])) return trim((string)$_SERVER['HTTP_X_KLUCZ']);
        if (isset($_POST['klucz'])) return trim((string)$_POST['klucz']);
        return '';
    }

    /** Przyjmuje surowy JSON nocy. Zwraca [kod HTTP, odpowiedź]. */
    public static function receive(string $raw): array
    {
        if ($raw === '') return [400, ['message' => 'Pusty plik nocy.']];
        if (strlen($raw) > self::MAX_BYTES) return [400, ['message' => 'Plik nocy jest za duży (limit 1 MB).']];
        $d = json_decode($raw, true);
        if (!is_array($d)) return [400, ['message' => 'To nie jest poprawny JSON.']];
        $night = (string)($d['night'] ?? '');
        if (!preg_match('/^\d{4}-\d{2}-\d{2}$/', $night) || !checkdate((int)substr($night, 5, 2), (int)substr($night, 8, 2), (int)substr($night, 0, 4))) {
            return [400, ['message' => 'Brak lub zła data nocy (pole "night").']];
        }
        if (!isset($d['timeline']) || !is_array($d['timeline']) || !isset($d['summary']) || !is_array($d['summary'])) {
            return [400, ['message' => 'Brak osi czasu nocy (timeline) lub podsumowania (summary).']];
        }
        $m = self::metrics($d);
        $dir = self::dir();
        if (!is_dir($dir) && !@mkdir($dir, 0775, true)) return [500, ['message' => 'Nie można utworzyć katalogu na noce.']];
        $file = $dir . '/noc_' . $night . '.json';
        $tmp = $file . '.tmp';
        if (file_put_contents($tmp, $raw, LOCK_EX) === false || !rename($tmp, $file)) {
            @unlink($tmp);
            return [500, ['message' => 'Nie udało się zapisać nocy.']];
        }
        self::updateIndex($night, $m);
        return [200, ['message' => 'Zapisano noc ' . $night . '.', 'night' => $night, 'metrics' => $m]];
    }

    /** Wskaźniki nocy z osi czasu (minuty) i podsumowania detektora. */
    public static function metrics(array $d): array
    {
        $tl = array_map('intval', array_values((array)$d['timeline']));
        $minutes = (int)($d['minutes'] ?? count($tl));
        if ($minutes <= 0) $minutes = count($tl);
        $tl = array_slice(array_pad($tl, $minutes, 0), 0, $minutes);
        $s = (array)$d['summary'];

        $active = 0; $episodes = []; $cur = null; $gap = 0;
        foreach ($tl as $i => $v) {
            if ($v > 0) {
                $active++;
                if ($cur !== null && $gap <= self::EPISODE_GAP_MIN) { $cur['to'] = $i; $cur['active']++; }
                else { if ($cur !== null) $episodes[] = $cur; $cur = ['from' => $i, 'to' => $i, 'active' => 1]; }
                $gap = 0;
            } else {
                $gap++;
            }
        }
        if ($cur !== null) $episodes[] = $cur;
        $long = count(array_filter($episodes, static fn($e) => $e['active'] >= self::LONG_EPISODE_MIN));

        // najdłuższy spokój: z detektora (dokładny, w sekundach), awaryjnie z osi minut
        $longestQuiet = isset($s['longest_quiet_s']) ? (int)round($s['longest_quiet_s'] / 60) : 0;
        if ($longestQuiet <= 0) {
            $run = 0; foreach ($tl as $v) { $run = $v > 0 ? 0 : $run + 1; $longestQuiet = max($longestQuiet, $run); }
        }
        $quietPct = $minutes > 0 ? round(($minutes - $active) / $minutes * 100, 1) : 0.0;
        $coverage = (float)($s['coverage_pct'] ?? 100);

        if ($coverage < self::LOW_COVERAGE_PCT) $rating = 'brak-danych';
        elseif ($quietPct >= 90 && $longestQuiet >= 150) $rating = 'spokojna';
        elseif ($quietPct < 75 || $long > 10) $rating = 'niespokojna';
        else $rating = 'umiarkowana';

        return [
            'night' => (string)$d['night'],
            'start' => (string)($d['start'] ?? ''), 'end' => (string)($d['end'] ?? ''),
            'startTs' => (int)round((float)($d['start_ts'] ?? 0)), 'endTs' => (int)round((float)($d['end_ts'] ?? 0)),
            'minutes' => $minutes,
            'activeMin' => $active,
            'quietPct' => $quietPct,
            'longestQuietMin' => $longestQuiet,
            'episodes' => count($episodes),
            'longEpisodes' => $long,
            'events' => (int)($s['events'] ?? 0),
            'motionMin' => (int)round(((float)($s['motion_s'] ?? 0)) / 60),
            'first' => $s['first'] ?? null, 'last' => $s['last'] ?? null,
            'busiestHour' => $s['busiest_hour'] ?? null,
            'coveragePct' => $coverage,
            'complete' => (bool)($d['complete'] ?? false),
            'rating' => $rating,
            'receivedAt' => date('Y-m-d H:i:s'),
        ];
    }

    private static function indexFile(): string { return self::dir() . '/index.json'; }

    private static function updateIndex(string $night, array $m): void
    {
        $f = self::indexFile();
        $h = fopen($f, 'c+');
        if ($h === false) return;
        flock($h, LOCK_EX);
        $idx = json_decode((string)stream_get_contents($h), true);
        if (!is_array($idx)) $idx = [];
        $idx[$night] = $m;
        ksort($idx);
        ftruncate($h, 0); rewind($h);
        fwrite($h, json_encode($idx, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES));
        fflush($h); flock($h, LOCK_UN); fclose($h);
    }

    /** Indeks wszystkich nocy; odbudowywany z plików, gdy go brak lub jest niepełny. */
    public static function index(): array
    {
        $idx = is_file(self::indexFile()) ? json_decode((string)file_get_contents(self::indexFile()), true) : null;
        if (!is_array($idx)) $idx = [];
        $files = glob(self::dir() . '/noc_*.json') ?: [];
        if (count($files) !== count($idx)) {
            $idx = [];
            foreach ($files as $f) {
                $d = json_decode((string)file_get_contents($f), true);
                if (is_array($d) && isset($d['night'], $d['timeline'], $d['summary'])) $idx[$d['night']] = self::metrics($d);
            }
            ksort($idx);
            if ($idx && is_dir(self::dir())) @file_put_contents(self::indexFile(), json_encode($idx, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES), LOCK_EX);
        }
        return $idx;
    }

    /** Ostatnie $days nocy (od najstarszej), do wykresu. */
    public static function recent(int $days): array
    {
        $days = max(1, min($days, 400));
        return array_values(array_slice(self::index(), -$days));
    }

    /** Szczegóły jednej nocy do paska nocy: oś minut, epizody ruchu, godziny. */
    public static function detail(string $night): ?array
    {
        if (!preg_match('/^\d{4}-\d{2}-\d{2}$/', $night)) return null;
        $f = self::dir() . '/noc_' . $night . '.json';
        if (!is_file($f)) return null;
        $d = json_decode((string)file_get_contents($f), true);
        if (!is_array($d)) return null;
        $m = self::metrics($d);
        $tl = array_slice(array_pad(array_map('intval', (array)$d['timeline']), $m['minutes'], 0), 0, $m['minutes']);
        $hours = [];
        foreach ((array)($d['hours'] ?? []) as $h) {
            $hours[] = ['hour' => (string)($h['hour'] ?? ''), 'events' => (int)($h['events'] ?? 0), 'activity' => (int)($h['activity'] ?? 0)];
        }
        $events = [];
        foreach ((array)($d['events'] ?? []) as $e) {
            $events[] = ['start' => substr((string)($e['start'] ?? ''), 0, 8), 'durS' => (int)($e['dur_s'] ?? 0), 'startTs' => (int)round((float)($e['start_ts'] ?? 0))];
        }
        return ['metrics' => $m, 'timeline' => $tl, 'hours' => $hours, 'events' => $events];
    }
}
