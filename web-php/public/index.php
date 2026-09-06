<?php
// Front controller — testowa kopia panelu WWW urzadzenia Aleksander (ESP32) w PHP.
// Odtwarza kontrakt HTTP 1:1 na podstawie pliku CSV (../data/karmienia.csv).
// Pominieto funkcje SPRZETOWE urzadzenia (Telegram, pogoda, watchdog, mDNS) —
// to swiadomie tylko warstwa panelu do testow, baza pod dalsze prace.
declare(strict_types=1);

// Konfiguracja sciezek do lib/ i data/ (edytowalna — patrz public/paths.php).
require_once __DIR__ . '/paths.php';
require_once PANEL_LIB_DIR . '/store.php';

// --------------------------- Pomocnicze odpowiedzi ------------------------------
function sendJson(int $status, array $obj): void
{
    http_response_code($status);
    header('Cache-Control: no-store, max-age=0');
    header('Content-Type: application/json; charset=utf-8');
    // JSON_UNESCAPED_UNICODE + zachowanie \n w stringach (jak jsonEscape firmware).
    echo json_encode($obj, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

function param(string $key, ?string $default = null): ?string
{
    if (isset($_POST[$key])) {
        return (string)$_POST[$key];
    }
    if (isset($_GET[$key])) {
        return (string)$_GET[$key];
    }
    return $default;
}

// Sciezka bez query stringa (router dziala tak samo pod Apache i php -S).
$uri = parse_url($_SERVER['REQUEST_URI'] ?? '/', PHP_URL_PATH) ?: '/';
$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';

// ----------------------------------- GET / --------------------------------------
if ($uri === '/' || $uri === '/index.php' || $uri === '/index.html') {
    header('Cache-Control: no-store, max-age=0');
    header('Content-Type: text/html; charset=utf-8');
    readfile(__DIR__ . '/index.html');
    exit;
}

// ------------------------------- GET /export.csv --------------------------------
if ($uri === '/export.csv' && $method === 'GET') {
    header('Cache-Control: no-store, max-age=0');
    header('Content-Type: text/csv; charset=utf-8');
    header('Content-Disposition: attachment; filename=karmienia.csv');
    echo Store::readRawCsv();
    exit;
}

// ------------------------------ GET /api/status ---------------------------------
if ($uri === '/api/status' && $method === 'GET') {
    $now = new DateTimeImmutable();
    $latest = Store::loadLatestEntries($now);
    $ageDays = Store::calculateAgeDays($now);
    $ww = Store::wakeWindowMinutes($ageDays);
    $need = Store::sleepNeedMinutes($ageDays);
    $built = Store::buildDayStats($now);
    $stats = $built['stats'];
    $today = $stats[0];
    $settings = Store::loadSettings();

    // Sen: stan biezacy + predykcja okna.
    $napStart = null;
    $napEnd = null;
    $sleepState = 'brak';
    if ($latest['sleepInProgress']) {
        $sleepState = 'spi';
    } elseif ($latest['lastWakeTime'] !== null) {
        $napStart = $latest['lastWakeTime']->modify('+' . $ww['minMin'] . ' minutes');
        $napEnd = $latest['lastWakeTime']->modify('+' . $ww['maxMin'] . ' minutes');
        if ($now < $napStart) {
            $sleepState = 'czuwa';
        } elseif ($now <= $napEnd) {
            $sleepState = 'okno';
        } else {
            $sleepState = 'przekroczone';
        }
    }
    $sleepSinceMin = ($latest['sleepInProgress'] && $latest['sleepStartedTime'] !== null)
        ? intdiv($now->getTimestamp() - $latest['sleepStartedTime']->getTimestamp(), 60) : -1;
    $wakeSinceMin = (!$latest['sleepInProgress'] && $latest['lastWakeTime'] !== null)
        ? intdiv($now->getTimestamp() - $latest['lastWakeTime']->getTimestamp(), 60) : -1;

    $h = (int)$now->format('G');
    $nightActive = ($h >= Config::NIGHT_START_HOUR || $h < Config::NIGHT_END_HOUR);

    $calendar = [];
    for ($i = 0; $i < 5; $i++) {
        $d = Store::dayOffsetFromToday($i, $now);
        $s = $stats[$i];
        $calendar[] = [
            'date' => Store::dateIso($d),
            'label' => Store::calendarDayTitle($d, $i),
            'feedingCount' => $s['feedingCount'],
            'milkMl' => $s['milkMl'],
            'motherMilkMl' => $s['motherMilkMl'],
            'modifiedMilkMl' => $s['modifiedMilkMl'],
            'piersLeftMin' => $s['piersLeftMin'],
            'piersRightMin' => $s['piersRightMin'],
            'diaperWet' => $s['diaperWet'],
            'diaperDirty' => $s['diaperDirty'],
            'pumpingMl' => $s['pumpingMl'],
            'vitaminD' => $s['vitaminD'],
            'weightG' => $s['weightG'],
        ];
    }

    sendJson(200, [
        'now' => Store::formatDateTime($now),
        'nowIso' => Store::webDateTime($now),
        'ip' => 'panel-php',
        'age' => Store::calculateAgeText($now),
        'developmentTip' => Store::developmentTipForToday($now),
        'developmentDay' => $ageDays,
        'lastFeeding' => $latest['lastFeeding'],
        'lastMilk' => $latest['lastMilk'],
        'lastFeedingAgo' => $latest['lastFeedingTime'] !== null ? Store::formatAgoText($latest['lastFeedingTime'], $now) : '',
        'lastFeedingAgeMin' => $latest['lastFeedingTime'] !== null
            ? intdiv($now->getTimestamp() - $latest['lastFeedingTime']->getTimestamp(), 60) : -1,
        'avgFeedingGapMin' => $latest['avgFeedingGapMin'],
        'nextFeedingIso' => $latest['nextFeedingEta'] !== null ? Store::webDateTime($latest['nextFeedingEta']) : '',
        'longestFeedingGapMin' => $latest['longestFeedingGapMin'],
        'sleepInProgress' => $latest['sleepInProgress'],
        'sleepState' => $sleepState,
        'sleepSinceMin' => $sleepSinceMin,
        'wakeSinceMin' => $wakeSinceMin,
        'wakeWindowMinMin' => $ww['minMin'],
        'wakeWindowMaxMin' => $ww['maxMin'],
        'nextNapStartIso' => $napStart !== null ? Store::webDateTime($napStart) : '',
        'nextNapEndIso' => $napEnd !== null ? Store::webDateTime($napEnd) : '',
        'sleepDayMin' => $today['sleepDayMin'],
        'sleepNightMin' => $today['sleepNightMin'],
        'sleepNeedDayMin' => $need['day'],
        'sleepNeedNightMin' => $need['night'],
        'napCount' => $today['napCount'],
        'napTarget' => Store::napTargetCount($ageDays),
        'sleepTelegram' => $settings['sleepTelegram'],
        'wifi' => true,
        'storage' => true,
        'dataFileHuge' => false,
        'timeValid' => true,
        'minMl' => Config::ML_MIN,
        'maxMl' => Config::ML_MAX,
        'defaultMl' => Config::DEFAULT_ML,
        'birthWeightG' => Config::BIRTH_WEIGHT_G,
        'lastWeightG' => $latest['lastWeightG'],
        'calendar' => $calendar,
        'night' => $nightActive,
        'mdns' => 'karmienie.local',
        'undoWindowSec' => 60,
        // Sysinfo / diagnostyka — w kopii PHP wartosci zastepcze (nie ma sprzetu ESP32).
        'freeHeap' => 0,
        'totalHeap' => 0,
        'freePsram' => 0,
        'totalPsram' => 0,
        'maxAlloc' => 0,
        'uptimeSec' => 0,
        'cpuLoad' => 0,
        'minFreeHeap' => 0,
        'rssi' => 0,
        'httpRequests' => 0,
        'bootCount' => 0,
        'watchdogResets' => 0,
        'watchdogReady' => false,
        'resetReason' => 'php',
    ]);
}

// ----------------------------- GET /api/entries ---------------------------------
if ($uri === '/api/entries' && $method === 'GET') {
    $date = param('date');
    if ($date === null || $date === '') {
        sendJson(400, ['message' => 'Brakuje daty.']);
    }
    $day = Store::parseWebDateTime($date . 'T12:00');
    if ($day === null) {
        sendJson(400, ['message' => 'Nieprawidłowy format daty.']);
    }
    $targetDate = Store::dateIso($day);
    sendJson(200, ['date' => $targetDate, 'entries' => Store::entriesForDate($targetDate)]);
}

// -------------------------- GET /api/weight-series ------------------------------
if ($uri === '/api/weight-series' && $method === 'GET') {
    sendJson(200, ['birthWeightG' => Config::BIRTH_WEIGHT_G, 'points' => Store::weightSeries()]);
}

// ------------------------------ POST /api/entry ---------------------------------
if ($uri === '/api/entry' && $method === 'POST') {
    $type = param('type');
    if ($type === null || param('when') === null || param('ml') === null) {
        sendJson(400, ['message' => 'Niepelne dane formularza.']);
    }
    $ml = (int)param('ml', '0');
    $when = Store::parseWebDateTime((string)param('when'));
    if ($when === null) {
        sendJson(400, ['message' => 'Nieprawidlowy czas wpisu.']);
    }

    if (Store::isMilkType($type)) {
        if ($ml < Config::ML_MIN || $ml > Config::ML_MAX) {
            sendJson(400, ['message' => 'Nieprawidlowa ilosc mleka.']);
        }
        Store::appendEntry($type, $when, $ml);
        sendJson(201, ['message' => 'Wpis mleka zapisany w pamieci urzadzenia.']);
    }
    if ($type === 'WAGA') {
        if ($ml < Config::WEIGHT_MIN_G || $ml > Config::WEIGHT_MAX_G) {
            sendJson(400, ['message' => 'Nieprawidlowa waga (gramy).']);
        }
        Store::appendEntry('WAGA', $when, $ml);
        sendJson(201, ['message' => 'Zapisano wage.']);
    }
    if (!Store::isMilkType($type) && $type !== 'KARMIENIE') {
        $validType = in_array($type, ['ODCIAGANIE', 'PIELUCHA_MOKRA', 'PIELUCHA_BRUDNA', 'WITAMINA_D'], true);
        if (!$validType) {
            sendJson(400, ['message' => 'Nieznany typ zdarzenia.']);
        }
        if ($type === 'WITAMINA_D') {
            $todayS = Store::dayStatsForOffset(0, $when);
            if ($todayS['vitaminD']) {
                sendJson(200, ['message' => 'Witamina D juz zapisana dzisiaj.']);
            }
        }
        Store::appendEntry($type, $when, $ml);
        sendJson(201, ['message' => 'Zapisano zdarzenie.']);
    }
    // KARMIENIE
    if ($type !== 'KARMIENIE' || $ml !== 0) {
        sendJson(400, ['message' => 'Karmienie nie wymaga ilosci ml; podaj ja tylko dla Butelki.']);
    }
    $extraMilk = param('extraMilk') === '1';
    $milkType = '';
    $milkMl = 0;
    if ($extraMilk) {
        if (param('milkType') === null || param('milkMl') === null) {
            sendJson(400, ['message' => 'Brakuje typu lub ilosci dodatkowego mleka.']);
        }
        $milkType = (string)param('milkType');
        $milkMl = (int)param('milkMl', '0');
        if (($milkType !== 'MLEKO_MATKI' && $milkType !== 'MLEKO_MODYFIKOWANE') || $milkMl < Config::ML_MIN || $milkMl > Config::ML_MAX) {
            sendJson(400, ['message' => 'Nieprawidlowe dodatkowe mleko.']);
        }
    }
    $clamp = static fn($v, $lo, $hi) => max($lo, min((int)$v, $hi));
    $piersLeftMin = $clamp(param('lewaMin', '0'), 0, 120);
    $piersRightMin = $clamp(param('prawaMin', '0'), 0, 120);
    Store::appendEntry('KARMIENIE', $when, $ml, $piersLeftMin, $piersRightMin);
    if ($extraMilk) {
        Store::appendEntry($milkType, $when, $milkMl);
    }
    sendJson(201, $extraMilk
        ? ['message' => 'Zapisano karmienie i dodatkowe mleko.']
        : ['message' => 'Karmienie zapisane w pamieci urzadzenia.']);
}

// -------------------------- POST /api/delete-entry ------------------------------
if ($uri === '/api/delete-entry' && $method === 'POST') {
    if (param('line') === null) {
        sendJson(400, ['message' => 'Brakuje indeksu linii do usuniecia.']);
    }
    $lineIndex = (int)param('line');
    $result = Store::deleteEntryByIndex($lineIndex);
    if (!$result['ok']) {
        sendJson(400, ['message' => 'Nie udalo sie usunac wpisu.']);
    }
    sendJson(200, ['message' => 'Usunieto wpis.', 'removed' => $result['removed']]);
}

// ------------------------------ POST /api/event ---------------------------------
if ($uri === '/api/event' && $method === 'POST') {
    $type = param('type', '');
    $validType = in_array($type, ['PIELUCHA_MOKRA', 'PIELUCHA_BRUDNA', 'WITAMINA_D', 'ODCIAGANIE', 'WAGA', 'SEN_START', 'SEN_STOP'], true);
    if (!$validType) {
        sendJson(400, ['message' => 'Nieznany typ zdarzenia.']);
    }
    $when = new DateTimeImmutable();
    if (param('when') !== null) {
        $parsed = Store::parseWebDateTime((string)param('when'));
        if ($parsed === null) {
            sendJson(400, ['message' => 'Nieprawidlowy czas zdarzenia.']);
        }
        $when = $parsed;
    }
    if ($type === 'WAGA') {
        $grams = (int)param('ml', '0');
        if ($grams < Config::WEIGHT_MIN_G || $grams > Config::WEIGHT_MAX_G) {
            sendJson(400, ['message' => 'Nieprawidlowa waga (gramy).']);
        }
        Store::appendEntry('WAGA', $when, $grams);
        sendJson(201, ['message' => 'Zapisano wage.']);
    }
    $ml = max(0, min((int)param('ml', '0'), Config::ML_MAX));
    if ($type === 'ODCIAGANIE' && $ml < Config::ML_MIN) {
        sendJson(400, ['message' => 'Podaj ilosc odciagnietego mleka.']);
    }
    if ($type === 'WITAMINA_D') {
        $todayS = Store::dayStatsForOffset(0, $when);
        if ($todayS['vitaminD']) {
            sendJson(200, ['message' => 'Witamina D juz zapisana dzisiaj.']);
        }
        $ml = 0;
    }
    Store::appendEntry($type, $when, $ml);
    sendJson(201, ['message' => 'Zapisano zdarzenie.']);
}

// --------------------------- POST /api/send-backup ------------------------------
// W kopii PHP nie ma Telegrama — zwracamy komunikat zgodny z kontraktem.
if ($uri === '/api/send-backup' && $method === 'POST') {
    sendJson(400, ['message' => 'Telegram nie jest skonfigurowany (kopia testowa PHP).']);
}

// ------------------------------ POST /api/import --------------------------------
if ($uri === '/api/import' && $method === 'POST') {
    $raw = file_get_contents('php://input');
    if ($raw === false) {
        $raw = '';
    }
    if ($raw === '') {
        sendJson(400, ['message' => 'Pusty plik importu.']);
    }
    if (strlen($raw) > 512 * 1024) {
        sendJson(400, ['message' => 'Plik jest za duzy (limit 512 KB).']);
    }
    $result = Store::importCsv($raw);
    if (!$result['ok']) {
        sendJson(400, ['message' => 'Brak poprawnych wierszy do importu.']);
    }
    $msg = $result['skipped'] > 0
        ? "Zaimportowano {$result['imported']} wpisow. Pominieto {$result['skipped']} niepoprawnych."
        : "Zaimportowano {$result['imported']} wpisow.";
    sendJson(200, ['message' => $msg]);
}

// ------------------------------ POST /api/setting -------------------------------
if ($uri === '/api/setting' && $method === 'POST') {
    $key = param('key');
    if ($key !== 'sleepTelegram') {
        sendJson(400, ['message' => 'Nieznane ustawienie.']);
    }
    $settings = Store::loadSettings();
    $settings['sleepTelegram'] = ((int)param('value', '0')) !== 0;
    Store::saveSettings($settings);
    sendJson(200, ['message' => 'Zapisano.', 'sleepTelegram' => $settings['sleepTelegram']]);
}

// --------------------------------- 404 ------------------------------------------
if (strncmp($uri, '/api/', 5) === 0) {
    sendJson(404, ['message' => 'Nie znaleziono adresu API.']);
}
http_response_code(404);
header('Content-Type: text/plain; charset=utf-8');
echo 'Nie znaleziono strony.';
