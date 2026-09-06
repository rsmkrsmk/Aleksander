<?php
/* ============================================================================
   SILNIK DANYCH — publiczne API HTTP (JSON)
   Wspolny punkt wejscia dla warstwy wizualnej (WWW) oraz — docelowo — urzadzen.
   Kontrakt endpointow 1:1 jak dotychczas. Zrodlo danych: Repository (CSV lub MySQL).

   Obslugiwane sciezki (dowolny prefiks jest obcinany do ostatniego segmentu):
     GET  /api/status | /api/entries?date= | /api/weight-series | /export.csv
     POST /api/entry | /api/delete-entry | /api/event | /api/send-backup
          /api/import | /api/setting
   ============================================================================ */
declare(strict_types=1);

require_once __DIR__ . '/bootstrap.php';

function apiCorsHeader(): void
{
    // Naglowek CORS tylko gdy SWIADOMIE skonfigurowany (Config::corsOrigin()).
    // Domyslnie pusty => API dziala same-origin (bez otwierania na inne originy).
    $origin = Config::corsOrigin();
    if ($origin !== '') header('Access-Control-Allow-Origin: ' . $origin);
}

function sendJson(int $status, array $obj): void
{
    http_response_code($status);
    header('Cache-Control: no-store, max-age=0');
    header('Content-Type: application/json; charset=utf-8');
    apiCorsHeader();
    echo json_encode($obj, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

function param(string $key, ?string $default = null): ?string
{
    if (isset($_POST[$key])) return (string)$_POST[$key];
    if (isset($_GET[$key]))  return (string)$_GET[$key];
    return $default;
}

/**
 * Obsluguje jedno zadanie API. $route to logiczna nazwa endpointu, np.
 * 'status','entries','weight-series','entry','delete-entry','event',
 * 'send-backup','import','setting','export.csv'. Router (ui/index.php lub
 * bezposredni engine/api.php) ustala $route i wola te funkcje.
 */
function handle_api(string $route, string $method, Repository $repo): void
{
    $now = new DateTimeImmutable();

    if ($route === 'export.csv' && $method === 'GET') {
        header('Cache-Control: no-store, max-age=0');
        header('Content-Type: text/csv; charset=utf-8');
        header('Content-Disposition: attachment; filename=karmienia.csv');
        echo $repo->rawCsv();
        exit;
    }

    if ($route === 'status' && $method === 'GET') {
        $entries = $repo->allEntries();
        $latest = Domain::loadLatestEntries($entries, $now);
        $ageDays = Domain::calculateAgeDays($now);
        $ww = Domain::wakeWindowMinutes($ageDays);
        $need = Domain::sleepNeedMinutes($ageDays);
        $built = Domain::buildDayStats($entries, $now);
        $stats = $built['stats'];
        $today = $stats[0];
        $settings = $repo->loadSettings();

        $napStart = null; $napEnd = null; $sleepState = 'brak';
        if ($latest['sleepInProgress']) {
            $sleepState = 'spi';
        } elseif ($latest['lastWakeTime'] !== null) {
            $napStart = $latest['lastWakeTime']->modify('+' . $ww['minMin'] . ' minutes');
            $napEnd = $latest['lastWakeTime']->modify('+' . $ww['maxMin'] . ' minutes');
            if ($now < $napStart) $sleepState = 'czuwa';
            elseif ($now <= $napEnd) $sleepState = 'okno';
            else $sleepState = 'przekroczone';
        }
        $sleepSinceMin = ($latest['sleepInProgress'] && $latest['sleepStartedTime'] !== null)
            ? intdiv($now->getTimestamp() - $latest['sleepStartedTime']->getTimestamp(), 60) : -1;
        $wakeSinceMin = (!$latest['sleepInProgress'] && $latest['lastWakeTime'] !== null)
            ? intdiv($now->getTimestamp() - $latest['lastWakeTime']->getTimestamp(), 60) : -1;

        $h = (int)$now->format('G');
        $nightActive = ($h >= Config::NIGHT_START_HOUR || $h < Config::NIGHT_END_HOUR);

        $calendar = [];
        for ($i = 0; $i < 5; $i++) {
            $d = Domain::dayOffsetFromToday($i, $now); $s = $stats[$i];
            $calendar[] = [
                'date' => Domain::dateIso($d), 'label' => Domain::calendarDayTitle($d, $i),
                'feedingCount' => $s['feedingCount'], 'milkMl' => $s['milkMl'],
                'motherMilkMl' => $s['motherMilkMl'], 'modifiedMilkMl' => $s['modifiedMilkMl'],
                'piersLeftMin' => $s['piersLeftMin'], 'piersRightMin' => $s['piersRightMin'],
                'diaperWet' => $s['diaperWet'], 'diaperDirty' => $s['diaperDirty'],
                'pumpingMl' => $s['pumpingMl'], 'vitaminD' => $s['vitaminD'], 'weightG' => $s['weightG'],
            ];
        }

        sendJson(200, [
            'now' => Domain::formatDateTime($now),
            'nowIso' => Domain::webDateTime($now),
            'ip' => 'panel-php',
            'age' => Domain::calculateAgeText($now),
            'developmentTip' => Domain::developmentTipForToday($now),
            'developmentDay' => $ageDays,
            'lastFeeding' => $latest['lastFeeding'],
            'lastMilk' => $latest['lastMilk'],
            'lastFeedingAgo' => $latest['lastFeedingTime'] !== null ? Domain::formatAgoText($latest['lastFeedingTime'], $now) : '',
            'lastFeedingAgeMin' => $latest['lastFeedingTime'] !== null ? intdiv($now->getTimestamp() - $latest['lastFeedingTime']->getTimestamp(), 60) : -1,
            'avgFeedingGapMin' => $latest['avgFeedingGapMin'],
            'nextFeedingIso' => $latest['nextFeedingEta'] !== null ? Domain::webDateTime($latest['nextFeedingEta']) : '',
            'longestFeedingGapMin' => $latest['longestFeedingGapMin'],
            'sleepInProgress' => $latest['sleepInProgress'],
            'sleepState' => $sleepState,
            'sleepSinceMin' => $sleepSinceMin,
            'wakeSinceMin' => $wakeSinceMin,
            'wakeWindowMinMin' => $ww['minMin'],
            'wakeWindowMaxMin' => $ww['maxMin'],
            'nextNapStartIso' => $napStart !== null ? Domain::webDateTime($napStart) : '',
            'nextNapEndIso' => $napEnd !== null ? Domain::webDateTime($napEnd) : '',
            'sleepDayMin' => $today['sleepDayMin'],
            'sleepNightMin' => $today['sleepNightMin'],
            'sleepNeedDayMin' => $need['day'],
            'sleepNeedNightMin' => $need['night'],
            'napCount' => $today['napCount'],
            'napTarget' => Domain::napTargetCount($ageDays),
            'sleepTelegram' => $settings['sleepTelegram'],
            'wifi' => true, 'storage' => true, 'dataFileHuge' => false, 'timeValid' => true,
            'minMl' => Config::ML_MIN, 'maxMl' => Config::ML_MAX, 'defaultMl' => Config::DEFAULT_ML,
            'birthWeightG' => Config::BIRTH_WEIGHT_G, 'lastWeightG' => $latest['lastWeightG'],
            'calendar' => $calendar, 'night' => $nightActive,
            'mdns' => 'karmienie.local', 'undoWindowSec' => 60,
            'freeHeap' => 0, 'totalHeap' => 0, 'freePsram' => 0, 'totalPsram' => 0, 'maxAlloc' => 0,
            'uptimeSec' => 0, 'cpuLoad' => 0, 'minFreeHeap' => 0, 'rssi' => 0, 'httpRequests' => 0,
            'bootCount' => 0, 'watchdogResets' => 0, 'watchdogReady' => false,
            'storageDriver' => Config::storageDriver(),
            'resetReason' => 'php',
        ]);
    }

    if ($route === 'entries' && $method === 'GET') {
        $date = param('date');
        if ($date === null || $date === '') sendJson(400, ['message' => 'Brakuje daty.']);
        $day = Domain::parseWebDateTime($date . 'T12:00');
        if ($day === null) sendJson(400, ['message' => 'Nieprawidłowy format daty.']);
        $target = Domain::dateIso($day);
        sendJson(200, ['date' => $target, 'entries' => Domain::entriesForDate($repo->allEntries(), $target)]);
    }

    if ($route === 'weight-series' && $method === 'GET') {
        sendJson(200, ['birthWeightG' => Config::BIRTH_WEIGHT_G, 'points' => Domain::weightSeries($repo->allEntries())]);
    }

    if ($route === 'entry' && $method === 'POST') {
        $type = param('type');
        if ($type === null || param('when') === null || param('ml') === null) sendJson(400, ['message' => 'Niepelne dane formularza.']);
        $ml = (int)param('ml', '0');
        $when = Domain::parseWebDateTime((string)param('when'));
        if ($when === null) sendJson(400, ['message' => 'Nieprawidlowy czas wpisu.']);

        if (Domain::isMilkType($type)) {
            if ($ml < Config::ML_MIN || $ml > Config::ML_MAX) sendJson(400, ['message' => 'Nieprawidlowa ilosc mleka.']);
            $repo->append($type, $when, $ml);
            sendJson(201, ['message' => 'Wpis mleka zapisany w pamieci urzadzenia.']);
        }
        if ($type === 'WAGA') {
            if ($ml < Config::WEIGHT_MIN_G || $ml > Config::WEIGHT_MAX_G) sendJson(400, ['message' => 'Nieprawidlowa waga (gramy).']);
            $repo->append('WAGA', $when, $ml);
            sendJson(201, ['message' => 'Zapisano wage.']);
        }
        if (!Domain::isMilkType($type) && $type !== 'KARMIENIE') {
            if (!in_array($type, ['ODCIAGANIE', 'PIELUCHA_MOKRA', 'PIELUCHA_BRUDNA', 'WITAMINA_D'], true)) sendJson(400, ['message' => 'Nieznany typ zdarzenia.']);
            if ($type === 'WITAMINA_D') {
                $todayS = Domain::dayStatsForOffset($repo->allEntries(), 0, $when);
                if ($todayS['vitaminD']) sendJson(200, ['message' => 'Witamina D juz zapisana dzisiaj.']);
            }
            $repo->append($type, $when, $ml);
            sendJson(201, ['message' => 'Zapisano zdarzenie.']);
        }
        if ($type !== 'KARMIENIE' || $ml !== 0) sendJson(400, ['message' => 'Karmienie nie wymaga ilosci ml; podaj ja tylko dla Butelki.']);
        $extraMilk = param('extraMilk') === '1'; $milkType = ''; $milkMl = 0;
        if ($extraMilk) {
            if (param('milkType') === null || param('milkMl') === null) sendJson(400, ['message' => 'Brakuje typu lub ilosci dodatkowego mleka.']);
            $milkType = (string)param('milkType'); $milkMl = (int)param('milkMl', '0');
            if (($milkType !== 'MLEKO_MATKI' && $milkType !== 'MLEKO_MODYFIKOWANE') || $milkMl < Config::ML_MIN || $milkMl > Config::ML_MAX) sendJson(400, ['message' => 'Nieprawidlowe dodatkowe mleko.']);
        }
        $clamp = static fn($v, $lo, $hi) => max($lo, min((int)$v, $hi));
        $repo->append('KARMIENIE', $when, $ml, $clamp(param('lewaMin', '0'), 0, 120), $clamp(param('prawaMin', '0'), 0, 120));
        if ($extraMilk) $repo->append($milkType, $when, $milkMl);
        sendJson(201, $extraMilk ? ['message' => 'Zapisano karmienie i dodatkowe mleko.'] : ['message' => 'Karmienie zapisane w pamieci urzadzenia.']);
    }

    if ($route === 'delete-entry' && $method === 'POST') {
        if (param('line') === null) sendJson(400, ['message' => 'Brakuje indeksu linii do usuniecia.']);
        $result = $repo->deleteByIndex((int)param('line'));
        if (!$result['ok']) sendJson(400, ['message' => 'Nie udalo sie usunac wpisu.']);
        sendJson(200, ['message' => 'Usunieto wpis.', 'removed' => $result['removed'] ?? '']);
    }

    if ($route === 'event' && $method === 'POST') {
        $type = param('type', '');
        if (!in_array($type, ['PIELUCHA_MOKRA', 'PIELUCHA_BRUDNA', 'WITAMINA_D', 'ODCIAGANIE', 'WAGA', 'SEN_START', 'SEN_STOP'], true)) sendJson(400, ['message' => 'Nieznany typ zdarzenia.']);
        $when = new DateTimeImmutable();
        if (param('when') !== null) {
            $parsed = Domain::parseWebDateTime((string)param('when'));
            if ($parsed === null) sendJson(400, ['message' => 'Nieprawidlowy czas zdarzenia.']);
            $when = $parsed;
        }
        if ($type === 'WAGA') {
            $grams = (int)param('ml', '0');
            if ($grams < Config::WEIGHT_MIN_G || $grams > Config::WEIGHT_MAX_G) sendJson(400, ['message' => 'Nieprawidlowa waga (gramy).']);
            $repo->append('WAGA', $when, $grams);
            sendJson(201, ['message' => 'Zapisano wage.']);
        }
        $ml = max(0, min((int)param('ml', '0'), Config::ML_MAX));
        if ($type === 'ODCIAGANIE' && $ml < Config::ML_MIN) sendJson(400, ['message' => 'Podaj ilosc odciagnietego mleka.']);
        if ($type === 'WITAMINA_D') {
            $todayS = Domain::dayStatsForOffset($repo->allEntries(), 0, $when);
            if ($todayS['vitaminD']) sendJson(200, ['message' => 'Witamina D juz zapisana dzisiaj.']);
            $ml = 0;
        }
        $repo->append($type, $when, $ml);
        sendJson(201, ['message' => 'Zapisano zdarzenie.']);
    }

    if ($route === 'send-backup' && $method === 'POST') {
        sendJson(400, ['message' => 'Telegram nie jest skonfigurowany (kopia testowa PHP).']);
    }

    if ($route === 'import' && $method === 'POST') {
        $raw = file_get_contents('php://input'); if ($raw === false) $raw = '';
        if ($raw === '') sendJson(400, ['message' => 'Pusty plik importu.']);
        if (strlen($raw) > 512 * 1024) sendJson(400, ['message' => 'Plik jest za duzy (limit 512 KB).']);
        $result = $repo->importCsv($raw);
        if (!$result['ok']) sendJson(400, ['message' => 'Brak poprawnych wierszy do importu.']);
        $msg = $result['skipped'] > 0
            ? "Zaimportowano {$result['imported']} wpisow. Pominieto {$result['skipped']} niepoprawnych."
            : "Zaimportowano {$result['imported']} wpisow.";
        sendJson(200, ['message' => $msg]);
    }

    if ($route === 'setting' && $method === 'POST') {
        if (param('key') !== 'sleepTelegram') sendJson(400, ['message' => 'Nieznane ustawienie.']);
        $settings = $repo->loadSettings();
        $settings['sleepTelegram'] = ((int)param('value', '0')) !== 0;
        $repo->saveSettings($settings);
        sendJson(200, ['message' => 'Zapisano.', 'sleepTelegram' => $settings['sleepTelegram']]);
    }

    sendJson(404, ['message' => 'Nie znaleziono adresu API.']);
}

/* ------------------------------------------------------------------------------
   Tryb SAMODZIELNY: jesli plik wywolano bezposrednio (np. DocumentRoot=engine/
   albo urzadzenie uderza wprost w engine/api.php?route=status), obsluz zadanie.
   Gdy plik jest tylko dolaczany (require) przez ui/index.php — nie robimy nic.
   ------------------------------------------------------------------------------ */
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === realpath(__FILE__)) {
    $method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
    if ($method === 'OPTIONS') { // preflight CORS (tylko gdy CORS skonfigurowany)
        if (Config::corsOrigin() !== '') {
            apiCorsHeader();
            header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
            header('Access-Control-Allow-Headers: Content-Type');
        }
        http_response_code(204); exit;
    }
    $uri = parse_url($_SERVER['REQUEST_URI'] ?? '/', PHP_URL_PATH) ?: '/';
    // route z ?route= lub z ostatniego segmentu sciezki (…/api/status -> status)
    $route = param('route');
    if ($route === null) {
        if (preg_match('#/export\.csv$#', $uri)) $route = 'export.csv';
        elseif (preg_match('#/api/([a-z0-9\-]+)$#', $uri, $m)) $route = $m[1];
        else $route = '';
    }
    handle_api($route, $method, make_repository());
}
