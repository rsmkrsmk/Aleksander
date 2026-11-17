<?php
/* ============================================================================
   wiewiorka.mom — KONTA I LOGOWANIE
   Proste konta bez e-maila: login + haslo (bcrypt przez password_hash).
   Pliki w katalogu danych portalu (PANEL_DATA_ROOT = panel-data-wiewiorka):
     portal.cfg   — kod zaproszenia (zaproszenie=...) i sekret podpisu ciasteczek
                    (sekret=...); tworzony sam przy pierwszym uruchomieniu
     konta.json   — konta: login, skrot hasla, numer konta, wersja (zmiana hasla
                    uniewaznia stare logowania)
     proby.json   — nieudane proby logowania / kodu zaproszenia (blokada 15 min)
     u_<nr>/      — dane konta: karmienia.csv, profil.json, ustawienia, rewizja
   Zalogowanie = podpisane ciasteczko (HMAC-SHA256): nr konta, waznosc, wersja.
   Bez plikow sesji — dziala na kazdym hostingu, przezywa restarty serwera.
   ============================================================================ */
declare(strict_types=1);

final class Auth
{
    const COOKIE = 'wm_auth';
    const LOGIN_RE = '/^[A-Za-z0-9._-]{3,32}$/';
    const PASS_MIN = 8;
    const REMEMBER_DAYS = 30;       // "Zapamietaj mnie"
    const SESSION_DAYS = 1;         // bez "zapamietaj": waznosc logowania (ciasteczko do zamkniecia przegladarki)
    const LOCK_WINDOW_S = 900;      // 15 min
    const LOCK_LOGIN_FAILS = 5;     // nieudane logowania na jeden login w oknie
    const LOCK_IP_FAILS = 20;       // nieudane logowania z jednego adresu IP w oknie
    const LOCK_INVITE_FAILS = 5;    // bledne kody zaproszenia z jednego IP w oknie

    /* ------------------------------ pliki ------------------------------ */
    public static function root(): string
    {
        if (!defined('PANEL_DATA_ROOT')) throw new RuntimeException('Brak PANEL_DATA_ROOT');
        $r = rtrim(PANEL_DATA_ROOT, '/');
        if (!is_dir($r)) @mkdir($r, 0775, true);
        return $r;
    }
    private static function f(string $name): string { return self::root() . '/' . $name; }

    /** Zapis pliku jednym ruchem (plik tymczasowy + rename). */
    private static function put(string $file, string $content): bool
    {
        $tmp = $file . '.tmp' . getmypid();
        if (@file_put_contents($tmp, $content) === false) { @unlink($tmp); return false; }
        if (!@rename($tmp, $file)) { @unlink($tmp); return false; }
        return true;
    }

    /** Wszystkie zmiany kont i prob pod jedna blokada. */
    private static function locked(callable $fn)
    {
        $h = @fopen(self::f('konta.lock'), 'c');
        if ($h) flock($h, LOCK_EX);
        try { return $fn(); }
        finally { if ($h) { flock($h, LOCK_UN); fclose($h); } }
    }

    /* ------------------------------ portal.cfg ------------------------------ */
    /** ['zaproszenie' => kod, 'sekret' => klucz podpisu]. Brak pliku = utworz z losowymi wartosciami. */
    public static function cfg(): array
    {
        static $c = null;
        if ($c !== null) return $c;
        $file = self::f('portal.cfg');
        if (!is_file($file)) {
            self::locked(function () use ($file) {
                if (is_file($file)) return;
                $inv = 'wiewiorka-' . substr(bin2hex(random_bytes(4)), 0, 6);
                self::put($file, "# wiewiorka.mom — ustawienia portalu (plik tylko na serwerze)\n"
                    . "# Kod, ktory trzeba podac przy zakladaniu konta. Zmien na wlasny; pusty = rejestracja zamknieta.\n"
                    . "zaproszenie=" . $inv . "\n"
                    . "# Klucz podpisu logowan. NIE udostepniaj. Zmiana wyloguje wszystkich.\n"
                    . "sekret=" . bin2hex(random_bytes(32)) . "\n");
            });
        }
        $c = ['zaproszenie' => '', 'sekret' => ''];
        foreach (explode("\n", (string)@file_get_contents($file)) as $ln) {
            $ln = trim($ln);
            if ($ln === '' || $ln[0] === '#') continue;
            $eq = strpos($ln, '=');
            if ($eq === false) continue;
            $k = trim(substr($ln, 0, $eq));
            if (array_key_exists($k, $c)) $c[$k] = trim(substr($ln, $eq + 1));
        }
        if (strlen($c['sekret']) < 32) throw new RuntimeException('portal.cfg: brak lub za krotki sekret');
        return $c;
    }
    public static function registrationOpen(): bool { return self::cfg()['zaproszenie'] !== ''; }

    /* ------------------------------ konta ------------------------------ */
    private static function accounts(): array
    {
        $a = json_decode((string)@file_get_contents(self::f('konta.json')), true);
        if (!is_array($a)) $a = [];
        $a += ['next' => 1, 'users' => []];
        return $a;
    }
    private static function saveAccounts(array $a): bool
    {
        return self::put(self::f('konta.json'), json_encode($a, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES));
    }
    private static function byId(int $id): ?array
    {
        foreach (self::accounts()['users'] as $u) if ((int)$u['id'] === $id) return $u;
        return null;
    }

    /* ------------------------------ blokady ------------------------------ */
    private static function fails(string $key): int
    {
        $p = json_decode((string)@file_get_contents(self::f('proby.json')), true);
        if (!is_array($p) || empty($p[$key])) return 0;
        $now = time();
        return count(array_filter((array)$p[$key], static fn($t) => $now - (int)$t < self::LOCK_WINDOW_S));
    }
    /** Minuty do konca blokady (0 = brak blokady). */
    private static function lockMinutes(string $key, int $limit): int
    {
        if (self::fails($key) < $limit) return 0;
        $p = json_decode((string)@file_get_contents(self::f('proby.json')), true);
        $ts = array_map('intval', (array)($p[$key] ?? []));
        sort($ts);
        $recent = array_values(array_filter($ts, static fn($t) => time() - $t < self::LOCK_WINDOW_S));
        $oldestBlocking = $recent[count($recent) - $limit] ?? time();
        return max(1, (int)ceil(($oldestBlocking + self::LOCK_WINDOW_S - time()) / 60));
    }
    private static function addFail(string ...$keys): void
    {
        self::locked(function () use ($keys) {
            $p = json_decode((string)@file_get_contents(self::f('proby.json')), true);
            if (!is_array($p)) $p = [];
            $now = time();
            foreach ($p as $k => $ts) {   // sprzatanie starych wpisow
                $ts = array_values(array_filter((array)$ts, static fn($t) => $now - (int)$t < self::LOCK_WINDOW_S));
                if ($ts) $p[$k] = $ts; else unset($p[$k]);
            }
            foreach ($keys as $k) { $p[$k][] = $now; $p[$k] = array_slice($p[$k], -50); }
            self::put(self::f('proby.json'), json_encode($p));
        });
    }
    private static function clearFails(string $key): void
    {
        self::locked(function () use ($key) {
            $p = json_decode((string)@file_get_contents(self::f('proby.json')), true);
            if (!is_array($p) || !isset($p[$key])) return;
            unset($p[$key]);
            self::put(self::f('proby.json'), json_encode($p));
        });
    }

    /* ------------------------------ rejestracja / logowanie ------------------------------ */
    /** @return array [ok, komunikat, uzytkownik|null] */
    public static function register(string $invite, string $login, string $pass, string $pass2, string $ip): array
    {
        if (!self::registrationOpen()) return [false, 'Zakładanie kont jest teraz wyłączone.', null];
        if (($m = self::lockMinutes('inv:' . $ip, self::LOCK_INVITE_FAILS)) > 0) return [false, "Zbyt wiele prób. Spróbuj ponownie za {$m} min.", null];
        if (!hash_equals(self::cfg()['zaproszenie'], trim($invite))) {
            self::addFail('inv:' . $ip);
            return [false, 'Nieprawidłowy kod zaproszenia.', null];
        }
        if (!preg_match(self::LOGIN_RE, $login)) return [false, 'Login: 3–32 znaki, litery bez polskich znaków, cyfry, kropka, myślnik lub podkreślnik.', null];
        if (mb_strlen($pass) < self::PASS_MIN) return [false, 'Hasło musi mieć co najmniej ' . self::PASS_MIN . ' znaków.', null];
        if (mb_strlen($pass) > 200) return [false, 'Hasło jest za długie.', null];
        if ($pass !== $pass2) return [false, 'Hasła nie są takie same.', null];
        $hash = password_hash($pass, PASSWORD_DEFAULT);
        return self::locked(function () use ($login, $hash) {
            $a = self::accounts();
            $key = strtolower($login);
            if (isset($a['users'][$key])) return [false, 'Taki login już istnieje — wybierz inny.', null];
            $id = (int)$a['next'];
            $u = ['id' => $id, 'login' => $login, 'hash' => $hash, 'ver' => 1, 'created' => date('Y-m-d H:i:s')];
            $a['users'][$key] = $u;
            $a['next'] = $id + 1;
            if (!self::saveAccounts($a)) return [false, 'Nie udało się zapisać konta (prawa zapisu katalogu danych?).', null];
            self::userDir($id);
            return [true, 'Konto założone.', $u];
        });
    }

    /** @return array [ok, komunikat, uzytkownik|null] */
    public static function login(string $login, string $pass, string $ip): array
    {
        $key = strtolower(trim($login));
        if (($m = self::lockMinutes('ip:' . $ip, self::LOCK_IP_FAILS)) > 0 || ($m = self::lockMinutes('l:' . $key, self::LOCK_LOGIN_FAILS)) > 0) {
            return [false, "Zbyt wiele nieudanych prób. Spróbuj ponownie za {$m} min.", null];
        }
        $u = self::accounts()['users'][$key] ?? null;
        // ten sam czas odpowiedzi dla nieistniejacego loginu (bez zgadywania loginow po czasie)
        $ok = password_verify($pass, $u['hash'] ?? '$2y$10$nouDZG.gQNlqIASSbsSYjORbIJmFJZR9NfSiOoQDFA/TgZmu04kFi');
        if (!$u || !$ok) {
            self::addFail('l:' . $key, 'ip:' . $ip);
            return [false, 'Nieprawidłowy login lub hasło.', null];
        }
        self::clearFails('l:' . $key);
        if (password_needs_rehash($u['hash'], PASSWORD_DEFAULT)) {
            self::locked(function () use ($key, $pass) {
                $a = self::accounts();
                if (isset($a['users'][$key])) { $a['users'][$key]['hash'] = password_hash($pass, PASSWORD_DEFAULT); self::saveAccounts($a); }
            });
        }
        return [true, 'Zalogowano.', $u];
    }

    /** Zmiana hasla: podnosi wersje konta (inne zalogowane urzadzenia zostaja wylogowane). */
    public static function changePassword(int $id, string $old, string $new, string $new2): array
    {
        $u = self::byId($id);
        if (!$u || !password_verify($old, $u['hash'])) return [false, 'Obecne hasło jest nieprawidłowe.', null];
        if (mb_strlen($new) < self::PASS_MIN) return [false, 'Nowe hasło musi mieć co najmniej ' . self::PASS_MIN . ' znaków.', null];
        if ($new !== $new2) return [false, 'Nowe hasła nie są takie same.', null];
        $hash = password_hash($new, PASSWORD_DEFAULT);
        return self::locked(function () use ($u, $hash) {
            $a = self::accounts();
            $key = strtolower($u['login']);
            if (!isset($a['users'][$key])) return [false, 'Konto nie istnieje.', null];
            $a['users'][$key]['hash'] = $hash;
            $a['users'][$key]['ver'] = (int)$a['users'][$key]['ver'] + 1;
            if (!self::saveAccounts($a)) return [false, 'Nie udało się zapisać hasła.', null];
            return [true, 'Hasło zmienione. Inne urządzenia zostały wylogowane.', $a['users'][$key]];
        });
    }

    /* ------------------------------ ciasteczko ------------------------------ */
    private static function sign(string $data): string
    {
        return hash_hmac('sha256', $data, self::cfg()['sekret']);
    }
    private static function isHttps(): bool
    {
        return (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off')
            || (($_SERVER['HTTP_X_FORWARDED_PROTO'] ?? '') === 'https')
            || ((int)($_SERVER['SERVER_PORT'] ?? 0) === 443);
    }
    /** Ustawia ciasteczko logowania. $remember=false: znika po zamknieciu przegladarki. */
    public static function issue(array $u, bool $remember, string $path): void
    {
        $exp = time() + 86400 * ($remember ? self::REMEMBER_DAYS : self::SESSION_DAYS);
        $data = (int)$u['id'] . '.' . $exp . '.' . (int)$u['ver'] . '.' . ($remember ? 1 : 0);
        setcookie(self::COOKIE, $data . '.' . self::sign($data), [
            'expires' => $remember ? $exp : 0, 'path' => $path, 'secure' => self::isHttps(),
            'httponly' => true, 'samesite' => 'Lax',
        ]);
    }
    public static function clear(string $path): void
    {
        setcookie(self::COOKIE, '', ['expires' => time() - 3600, 'path' => $path, 'secure' => self::isHttps(), 'httponly' => true, 'samesite' => 'Lax']);
    }
    /** Zalogowany uzytkownik albo null. Odswieza ciasteczko "zapamietaj", gdy minela polowa waznosci. */
    public static function current(string $path): ?array
    {
        $c = (string)($_COOKIE[self::COOKIE] ?? '');
        $p = explode('.', $c);
        if (count($p) !== 5) return null;
        [$id, $exp, $ver, $rem, $sig] = $p;
        if (!hash_equals(self::sign("$id.$exp.$ver.$rem"), $sig)) return null;
        if ((int)$exp < time()) return null;
        $u = self::byId((int)$id);
        if (!$u || (int)$u['ver'] !== (int)$ver) return null;
        if ($rem === '1' && (int)$exp - time() < 86400 * self::REMEMBER_DAYS / 2) self::issue($u, true, $path);
        return $u;
    }

    /* ------------------------------ dane konta ------------------------------ */
    public static function userDir(int $id): string
    {
        $d = self::root() . '/u_' . $id;
        if (!is_dir($d)) @mkdir($d, 0775, true);
        return $d;
    }
    public static function profile(int $id): array
    {
        $p = json_decode((string)@file_get_contents(self::userDir($id) . '/profil.json'), true);
        return is_array($p) ? $p : [];
    }
    public static function profileComplete(array $p): bool
    {
        return !empty($p['birthDate']) && !empty($p['birthWeightG']);
    }
    /** @return array [ok, komunikat, profil] */
    public static function saveProfile(int $id, string $name, string $birthDate, string $weight): array
    {
        $name = trim(preg_replace('/\s+/u', ' ', $name));
        if (mb_strlen($name) > 30) return [false, 'Imię może mieć najwyżej 30 znaków.', null];
        if ($name !== '' && !preg_match('/^[\p{L}][\p{L} \'\-]*$/u', $name)) return [false, 'Imię może zawierać tylko litery, spację, myślnik i apostrof.', null];
        if (!preg_match('/^(\d{4})-(\d{2})-(\d{2})$/', $birthDate, $m) || !checkdate((int)$m[2], (int)$m[3], (int)$m[1])) return [false, 'Podaj datę urodzenia.', null];
        if ($birthDate > date('Y-m-d')) return [false, 'Data urodzenia nie może być w przyszłości.', null];
        if ($birthDate < date('Y-m-d', strtotime('-6 years'))) return [false, 'Data urodzenia jest zbyt odległa (strona jest dla maluszków do ok. 2 lat).', null];
        $w = (int)$weight;
        if ($w < Config::BIRTH_WEIGHT_MIN_G || $w > Config::BIRTH_WEIGHT_MAX_G) return [false, 'Waga urodzeniowa w gramach: ' . Config::BIRTH_WEIGHT_MIN_G . '–' . Config::BIRTH_WEIGHT_MAX_G . '.', null];
        $p = ['name' => $name, 'birthDate' => $birthDate, 'birthWeightG' => $w, 'updated' => date('Y-m-d H:i:s')];
        if (!self::put(self::userDir($id) . '/profil.json', json_encode($p, JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT))) return [false, 'Nie udało się zapisać profilu.', null];
        return [true, 'Zapisano.', $p];
    }
}
