<?php
/* ============================================================================
   WARSTWA WIZUALNA — punkt wejścia / router
   Odpowiada TYLKO za prezentację i przekierowanie żądań danych do silnika:
     • GET /                      -> serwuje index.html (UI)
     • GET/POST /api/* , /export.csv -> deleguje do silnika (engine/api.php)
     • statyki (app.js, css, ...) -> serwuje z katalogu ui/
   Cała logika danych (CSV/MySQL, backup) żyje w engine/. UI jej nie dotyka.
   ============================================================================ */
declare(strict_types=1);

require_once __DIR__ . '/paths.php';

$uri = parse_url($_SERVER['REQUEST_URI'] ?? '/', PHP_URL_PATH) ?: '/';
$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';

/* ---- API + eksport -> silnik ---- */
// Trasa zakotwiczona dokladnie: /export.csv albo /api/<endpoint> (jak dotychczas),
// z opcjonalnym prefiksem podkatalogu instalacji.
$route = null;
if (preg_match('#^(?:/[^/]+)*/export\.csv$#', $uri)) $route = 'export.csv';
elseif (preg_match('#^(?:/[^/]+)*/api/([a-z0-9\-]+)$#', $uri, $m)) $route = $m[1];

if ($route !== null) {
    require_once PANEL_ENGINE_DIR . '/api.php';
    if ($method === 'OPTIONS') { // preflight CORS (tylko gdy skonfigurowany)
        if (Config::corsOrigin() !== '') {
            apiCorsHeader();
            header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
            header('Access-Control-Allow-Headers: Content-Type');
        }
        http_response_code(204); exit;
    }
    handle_api($route, $method, make_repository());
    exit;
}

/* ---- statyki UI ---- */
if ($method === 'GET' && preg_match('#^/([A-Za-z0-9_\-]+\.(js|css|svg|png|jpg|jpeg|gif|webp|ico|woff2?|map))$#', $uri, $mm)) {
    $file = __DIR__ . '/' . $mm[1];
    if (is_file($file)) {
        $ext = strtolower(pathinfo($file, PATHINFO_EXTENSION));
        $types = [
            'js' => 'text/javascript; charset=utf-8', 'css' => 'text/css; charset=utf-8',
            'svg' => 'image/svg+xml', 'png' => 'image/png', 'jpg' => 'image/jpeg', 'jpeg' => 'image/jpeg',
            'gif' => 'image/gif', 'webp' => 'image/webp', 'ico' => 'image/x-icon',
            'woff' => 'font/woff', 'woff2' => 'font/woff2', 'map' => 'application/json',
        ];
        header('Content-Type: ' . ($types[$ext] ?? 'application/octet-stream'));
        readfile($file); exit;
    }
}

/* ---- strona główna (UI) ---- */
if ($uri === '/' || $uri === '/index.php' || $uri === '/index.html') {
    header('Cache-Control: no-store, max-age=0');
    header('Content-Type: text/html; charset=utf-8');
    readfile(__DIR__ . '/index.html');
    exit;
}

/* ---- 404 ---- */
http_response_code(404);
header('Content-Type: text/plain; charset=utf-8');
echo 'Nie znaleziono strony.';
