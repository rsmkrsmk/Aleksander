<?php
/* ============================================================================
   wiewiorka.mom — punkt wejścia / router z logowaniem
     • GET  /                       -> index.html (ekran logowania pokazuje sama strona)
     • /api/auth, /api/register, /api/login, /api/logout, /api/profile, /api/password
                                    -> konta (engine/Auth.php)
     • pozostałe /api/* i /export.csv -> TYLKO po zalogowaniu i uzupełnieniu profilu;
                                    silnik dostaje katalog danych tego jednego konta
     • statyki (app.js, fonts, svg) -> z tego katalogu
   Działa od korzenia domeny (wiewiorka.mom/) i w podkatalogu (…/wiewiorka/).
   ============================================================================ */
declare(strict_types=1);

require_once __DIR__ . '/paths.php';

$uri = parse_url($_SERVER['REQUEST_URI'] ?? '/', PHP_URL_PATH) ?: '/';
$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
// Ścieżka bazowa strony ("/" na domenie, "/wiewiorka/" w podkatalogu) — dla ciasteczka.
$script = str_replace('\\', '/', (string)($_SERVER['SCRIPT_NAME'] ?? '/index.php'));
$base = basename($script) === 'index.php' ? rtrim(dirname($script), '/') . '/' : '/';

$route = null;
if (preg_match('#^(?:/[^/]+)*/export\.csv$#', $uri)) $route = 'export.csv';
elseif (preg_match('#^(?:/[^/]+)*/api/([a-z0-9\-]+)$#', $uri, $m)) $route = $m[1];

function wmJson(int $status, array $obj): void
{
    http_response_code($status);
    header('Cache-Control: no-store, max-age=0');
    header('Content-Type: application/json; charset=utf-8');
    echo json_encode($obj, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}
function wmParam(string $k): string { return (string)($_POST[$k] ?? ''); }
function wmIp(): string { return (string)($_SERVER['REMOTE_ADDR'] ?? '0'); }

if ($route !== null) {
    header('X-Content-Type-Options: nosniff');
    // Ochrona przed CSRF: zapis tylko z tej samej strony (nagłówek Origin/Referer = ten host).
    if ($method === 'POST') {
        $src = (string)($_SERVER['HTTP_ORIGIN'] ?? $_SERVER['HTTP_REFERER'] ?? '');
        $srcHost = $src !== '' ? (string)parse_url($src, PHP_URL_HOST) . ((parse_url($src, PHP_URL_PORT)) ? ':' . parse_url($src, PHP_URL_PORT) : '') : '';
        if ($srcHost === '' || strcasecmp($srcHost, (string)($_SERVER['HTTP_HOST'] ?? '')) !== 0) {
            wmJson(403, ['message' => 'Odrzucono zapis spoza strony.']);
        }
    } elseif ($method !== 'GET') {
        wmJson(405, ['message' => 'Nieobsługiwana metoda.']);
    }

    require_once PANEL_ENGINE_DIR . '/bootstrap.php';
    try { Auth::cfg(); } catch (Throwable $e) { wmJson(500, ['message' => 'Katalog danych portalu jest niezapisywalny albo portal.cfg jest uszkodzony.']); }
    $user = Auth::current($base);

    /* ---------------- konta ---------------- */
    if ($route === 'auth' && $method === 'GET') {
        $p = $user ? Auth::profile((int)$user['id']) : [];
        wmJson(200, [
            'loggedIn' => (bool)$user,
            'login' => $user['login'] ?? null,
            'profileComplete' => $user ? Auth::profileComplete($p) : false,
            'profile' => $user ? ['name' => $p['name'] ?? '', 'birthDate' => $p['birthDate'] ?? '', 'birthWeightG' => $p['birthWeightG'] ?? null] : null,
            'registrationOpen' => Auth::registrationOpen(),
        ]);
    }
    if ($route === 'register' && $method === 'POST') {
        [$ok, $msg, $u] = Auth::register(wmParam('invite'), trim(wmParam('login')), wmParam('password'), wmParam('password2'), wmIp());
        if (!$ok) wmJson(400, ['message' => $msg]);
        Auth::issue($u, wmParam('remember') === '1', $base);
        wmJson(201, ['message' => $msg, 'login' => $u['login']]);
    }
    if ($route === 'login' && $method === 'POST') {
        [$ok, $msg, $u] = Auth::login(wmParam('login'), wmParam('password'), wmIp());
        if (!$ok) wmJson(400, ['message' => $msg]);
        Auth::issue($u, wmParam('remember') === '1', $base);
        wmJson(200, ['message' => $msg, 'login' => $u['login']]);
    }
    if ($route === 'logout' && $method === 'POST') {
        Auth::clear($base);
        wmJson(200, ['message' => 'Wylogowano.']);
    }

    /* ---------------- dalej tylko zalogowani ---------------- */
    if (!$user) wmJson(401, ['message' => 'Zaloguj się, aby zobaczyć swoje dane.', 'auth' => true]);
    $uid = (int)$user['id'];

    if ($route === 'profile' && $method === 'POST') {
        [$ok, $msg, $p] = Auth::saveProfile($uid, wmParam('name'), wmParam('birthDate'), wmParam('birthWeightG'));
        if (!$ok) wmJson(400, ['message' => $msg]);
        wmJson(200, ['message' => $msg, 'profile' => $p]);
    }
    if ($route === 'password' && $method === 'POST') {
        [$ok, $msg, $u] = Auth::changePassword($uid, wmParam('old'), wmParam('password'), wmParam('password2'));
        if (!$ok) wmJson(400, ['message' => $msg]);
        Auth::issue($u, true, $base);   // to urządzenie zostaje zalogowane
        wmJson(200, ['message' => $msg]);
    }

    $profile = Auth::profile($uid);
    if (!Auth::profileComplete($profile)) wmJson(409, ['message' => 'Uzupełnij dane maleństwa.', 'profile' => true]);

    // Silnik pracuje WYŁĄCZNIE na katalogu tego konta.
    define('PANEL_DATA_DIR', Auth::userDir($uid));
    Config::setProfile($profile);
    require_once PANEL_ENGINE_DIR . '/api.php';
    handle_api($route, $method, make_repository());
    exit;
}

/* ---- statyki ---- */
$rel = ltrim(substr($uri, strlen(rtrim($base, '/'))), '/');
if ($method === 'GET' && preg_match('#(?:^|/)((?:fonts/|img/)?[A-Za-z0-9_\-]+\.(js|css|svg|png|webp|ico|woff2?|webmanifest))$#', $rel, $mm)) {
    $file = __DIR__ . '/' . $mm[1];
    if (is_file($file)) {
        $types = [
            'js' => 'text/javascript; charset=utf-8', 'css' => 'text/css; charset=utf-8', 'svg' => 'image/svg+xml',
            'png' => 'image/png', 'webp' => 'image/webp', 'ico' => 'image/x-icon',
            'woff' => 'font/woff', 'woff2' => 'font/woff2', 'webmanifest' => 'application/manifest+json',
        ];
        header('Content-Type: ' . $types[$mm[2]]);
        if ($mm[2] === 'woff' || $mm[2] === 'woff2') header('Cache-Control: public, max-age=31536000');
        readfile($file); exit;
    }
}

/* ---- strona ---- */
if ($rel === '' || $rel === 'index.php' || $rel === 'index.html') {
    header('Cache-Control: no-store, max-age=0');
    header('Content-Type: text/html; charset=utf-8');
    header('X-Content-Type-Options: nosniff');
    header('X-Frame-Options: DENY');
    header('Referrer-Policy: same-origin');
    readfile(__DIR__ . '/index.html');
    exit;
}

http_response_code(404);
header('Content-Type: text/plain; charset=utf-8');
echo 'Nie znaleziono strony.';
