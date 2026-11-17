<?php
/* ============================================================================
   SILNIK DANYCH — magazyn CSV (zrodlo prawdy w trybie 'csv')
   Plik CSV w formacie firmware. Ta implementacja przenosi 1:1 dotychczasowa
   logike pracy na pliku (append-only, fizyczny indeks wiersza).
   ============================================================================ */
declare(strict_types=1);

require_once __DIR__ . '/config.php';
require_once __DIR__ . '/Domain.php';
require_once __DIR__ . '/Repository.php';

final class CsvRepository implements Repository
{
    private string $file;

    public function __construct(?string $file = null)
    {
        $this->file = $file ?? Config::dataFile();
    }

    public function ensureFile(): void
    {
        $dir = dirname($this->file);
        if (!is_dir($dir)) @mkdir($dir, 0775, true);
        if (!is_file($this->file)) file_put_contents($this->file, Config::CSV_HEADER . "\n");
    }

    /* A6 (5.5.0): kazda zmiana pliku (dopisanie, usuniecie, edycja, import, podmiana) i podbicie
       rewizji idzie pod JEDNA blokada (plik .lock obok danych) — rownoczesny zapis z drugiego
       telefonu czeka, zamiast zginac. Przepisanie calego pliku idzie przez plik tymczasowy
       i rename(), wiec czytajacy (urzadzenie, /api/status) widzi stary albo nowy plik, nigdy pusty. */
    private function locked(callable $fn)
    {
        $this->ensureFile();
        $h = @fopen($this->file . '.lock', 'c');
        if ($h) flock($h, LOCK_EX);
        try { return $fn(); }
        finally { if ($h) { flock($h, LOCK_UN); fclose($h); } }
    }

    /** Atomowa podmiana pliku: zapis do .tmp + rename (rename na tym samym dysku jest atomowe). */
    public static function atomicWrite(string $file, string $content): bool
    {
        $tmp = $file . '.tmp' . getmypid();
        if (@file_put_contents($tmp, $content) === false) { @unlink($tmp); return false; }
        if (is_file($file)) @chmod($tmp, fileperms($file) & 0777);
        if (!@rename($tmp, $file)) { @unlink($tmp); return false; }
        return true;
    }

    /** Linie pliku: [naglowek, [linie bez koncowej pustej]]. */
    private function readLines(): array
    {
        $lines = explode("\n", (string)file_get_contents($this->file));
        $header = array_shift($lines);
        if (!empty($lines) && end($lines) === '') array_pop($lines);
        return [$header, $lines];
    }

    private function writeLines(string $header, array $lines): bool
    {
        $body = implode("\n", $lines);
        return self::atomicWrite($this->file, $header . "\n" . $body . ($body !== '' ? "\n" : ''));
    }

    // Fizyczny indeks = pozycja wiersza po naglowku (liczona dla KAZDEJ linii,
    // takze pustej/niepoprawnej) — spojnie z firmware i /api/entries.
    public function allEntries(): array
    {
        if (!is_file($this->file)) return [];
        $lines = explode("\n", (string)file_get_contents($this->file));
        array_shift($lines); // naglowek
        $out = [];
        foreach ($lines as $idx => $raw) {
            $raw = rtrim($raw, "\r\n");
            $trim = trim($raw);
            if ($trim === '') continue;
            // D2 (5.5.0): tylko poprawne wiersze (data, godzina, znany typ) trafiaja do strony i obliczen
            if (!Domain::validCsvRow($trim)) continue;
            $e = Domain::parseCsvLine($trim);
            if ($e === null) continue;
            $e['lineIndex'] = $idx;
            $out[] = $e;
        }
        return $out;
    }

    public function append(string $type, DateTimeImmutable $when, int $ml, int $piersLeft = -1, int $piersRight = -1): bool
    {
        $row = Domain::toCsvRow($type, $when, $ml, $piersLeft, $piersRight);
        return $this->locked(function () use ($row) {
            $ok = file_put_contents($this->file, $row, FILE_APPEND) !== false;
            if ($ok) $this->bumpRevision();
            return $ok;
        });
    }

    /**
     * Usuwa wiersz po fizycznym indeksie. A5: gdy podano $expect (date, time, type), wiersz musi sie
     * zgadzac — inaczej ['ok'=>false,'stale'=>true] (lista na stronie byla nieaktualna, nic nie usuwamy).
     * A4: $withMilk i wiersz KARMIENIE => usuwamy tez mleko (MLEKO_*) z ta sama data i godzina.
     */
    public function deleteByIndex(int $entryIndex, ?array $expect = null, bool $withMilk = false): array
    {
        if ($entryIndex < 0 || !is_file($this->file)) return ['ok' => false];
        return $this->locked(function () use ($entryIndex, $expect, $withMilk) {
            [$header, $lines] = $this->readLines();
            if ($entryIndex >= count($lines)) return ['ok' => false, 'stale' => $expect !== null];
            $target = Domain::parseCsvLine(trim(rtrim((string)$lines[$entryIndex], "\r")));
            if ($expect !== null && ($target === null || !Domain::entryMatches($target, $expect))) return ['ok' => false, 'stale' => true];
            $drop = [$entryIndex => true];
            $removed = $target !== null ? [Domain::describeCsvEntry($target)] : [];
            if ($withMilk && $target !== null && $target['type'] === 'KARMIENIE') {
                foreach ($lines as $idx => $raw) {
                    if ($idx === $entryIndex) continue;
                    $e = Domain::parseCsvLine(trim(rtrim((string)$raw, "\r")));
                    if ($e !== null && Domain::isMilkType($e['type']) && $e['date'] === $target['date'] && $e['time'] === $target['time']) {
                        $drop[$idx] = true; $removed[] = Domain::describeCsvEntry($e);
                    }
                }
            }
            $kept = [];
            foreach ($lines as $idx => $raw) if (!isset($drop[$idx])) $kept[] = rtrim((string)$raw, "\r");
            if (!$this->writeLines($header, $kept)) return ['ok' => false];
            $this->bumpRevision();
            return ['ok' => true, 'removed' => implode(', ', $removed), 'count' => count($drop)];
        });
    }

    public function updateFeeding(int $feedLineIndex, DateTimeImmutable $when, int $motherMl, int $modifiedMl): array
    {
        if ($feedLineIndex < 0 || !is_file($this->file)) return ['ok' => false, 'message' => 'Brak wpisu.'];
        return $this->locked(fn() => $this->updateFeedingLocked($feedLineIndex, $when, $motherMl, $modifiedMl));
    }

    private function updateFeedingLocked(int $feedLineIndex, DateTimeImmutable $when, int $motherMl, int $modifiedMl): array
    {
        [$header, $lines] = $this->readLines();

        // 1) Zlokalizuj wiersz KARMIENIA po fizycznym indeksie.
        if ($feedLineIndex >= count($lines)) return ['ok' => false, 'message' => 'Nie znaleziono wpisu.'];
        $feedRaw = rtrim((string)$lines[$feedLineIndex], "\r");
        $feed = Domain::parseCsvLine(trim($feedRaw));
        if ($feed === null || $feed['type'] !== 'KARMIENIE') {
            return ['ok' => false, 'message' => 'Wskazany wpis nie jest karmieniem.'];
        }
        $oldDate = $feed['date']; $oldTime = $feed['time'];

        // 2) Zaznacz WSZYSTKIE sparowane wiersze mleka o TYM SAMYM starym czasie do usuniecia.
        //    Model mleka mieszanego = DWA wiersze (MLEKO_MATKI + MLEKO_MODYFIKOWANE), a stare
        //    dane moga miec jednowierszowe MLEKO_MIESZANE — kazde takie mleko pary usuwamy i
        //    odtwarzamy z nowych ilosci ponizej.
        $milkIdxToDrop = [];
        foreach ($lines as $idx => $raw) {
            if ($idx === $feedLineIndex) continue;
            $e = Domain::parseCsvLine(trim(rtrim((string)$raw, "\r")));
            if ($e === null) continue;
            if ($e['date'] === $oldDate && $e['time'] === $oldTime && Domain::isMilkType($e['type'])) $milkIdxToDrop[$idx] = true;
        }

        // 3) Zbuduj nowe wiersze (zachowujac minuty piersi karmienia). Mleko rozbite na
        //    osobne ilosci: >0 => wiersz danego rodzaju. Oba 0 => brak mleka.
        $newFeedRow = rtrim(Domain::toCsvRow('KARMIENIE', $when, 0, $feed['piersLeft'], $feed['piersRight']), "\r\n");
        $newMilkRows = [];
        if ($motherMl > 0)   $newMilkRows[] = rtrim(Domain::toCsvRow('MLEKO_MATKI', $when, $motherMl), "\r\n");
        if ($modifiedMl > 0) $newMilkRows[] = rtrim(Domain::toCsvRow('MLEKO_MODYFIKOWANE', $when, $modifiedMl), "\r\n");

        // 4) Przepisz plik W MIEJSCU — karmienie zostaje na swojej pozycji, stare mleko pary
        //    znika, nowe wiersze mleka wstawiamy TUZ ZA karmieniem.
        $out = [];
        foreach ($lines as $idx => $raw) {
            $raw = rtrim((string)$raw, "\r");
            if ($idx === $feedLineIndex) {
                $out[] = $newFeedRow;
                foreach ($newMilkRows as $r) $out[] = $r;
            } elseif (isset($milkIdxToDrop[$idx])) {
                // stary wiersz mleka pary — pomijamy (odtworzony powyzej)
            } else {
                $out[] = $raw;
            }
        }
        if (!$this->writeLines($header, $out)) return ['ok' => false, 'message' => 'Nie udalo sie zapisac pliku.'];
        $this->bumpRevision();
        return ['ok' => true, 'message' => 'Zapisano zmiany karmienia.'];
    }

    public function importCsv(string $rawText): array
    {
        $lines = preg_split('/\r?\n/', $rawText);
        $out = [Config::CSV_HEADER]; $imported = 0; $skipped = 0; $sawHeader = false;
        foreach ($lines as $line) {
            $t = trim($line);
            if ($t === '') continue;
            if (!$sawHeader) { if (strncmp($t, 'data,', 5) === 0) $sawHeader = true; continue; }
            if (strlen($t) > 160) { $skipped++; continue; }
            if (!Domain::validCsvRow($t)) { $skipped++; continue; }   // D2: tylko znane typy i poprawna data
            $out[] = $t; $imported++;
        }
        if (!$sawHeader || $imported === 0) return ['ok' => false, 'imported' => 0, 'skipped' => $skipped];
        $ok = $this->locked(function () use ($out) {
            @copy($this->file, Config::backupFile());
            if (!self::atomicWrite($this->file, implode("\n", $out) . "\n")) return false;
            $this->bumpRevision();
            return true;
        });
        if (!$ok) return ['ok' => false, 'imported' => 0, 'skipped' => $skipped];
        return ['ok' => true, 'imported' => $imported, 'skipped' => $skipped];
    }

    public function replaceRawCsv(string $rawText): array
    {
        // 1) Walidacja przeslanego pliku: musi miec naglowek i >=1 poprawny wiersz.
        $lines = preg_split('/\r?\n/', $rawText);
        $out = [Config::CSV_HEADER]; $valid = 0; $skipped = 0; $sawHeader = false;
        foreach ($lines as $line) {
            $t = trim($line);
            if ($t === '') continue;
            if (!$sawHeader) { if (strncmp($t, 'data,', 5) === 0) { $sawHeader = true; continue; } }
            if (strncmp($t, 'data,', 5) === 0) continue; // pomin ewentualne kolejne naglowki
            if (strlen($t) > 160) { $skipped++; continue; }
            if (!Domain::validCsvRow($t)) { $skipped++; continue; }   // D2: tylko znane typy i poprawna data
            $out[] = $t; $valid++;
        }
        if (!$sawHeader) return ['ok' => false, 'backup' => '', 'lines' => 0, 'message' => 'Brak naglowka CSV (spodziewano "data,...").'];
        if ($valid === 0) return ['ok' => false, 'backup' => '', 'lines' => 0, 'message' => 'Brak poprawnych wierszy danych.'];

        // 2) Kopia zapasowa DOTYCHCZASOWYCH danych (YYYY-MM-DD-HH-MM-SS.bakap) i 3) podmiana — pod blokada.
        [$ok, $backupName] = $this->locked(function () use ($out) {
            $backupPath = Config::timestampedBackupFile();
            $name = '';
            if (is_file($this->file) && filesize($this->file) > 0 && @copy($this->file, $backupPath)) $name = basename($backupPath);
            if (!self::atomicWrite($this->file, implode("\n", $out) . "\n")) return [false, $name];
            $this->bumpRevision();
            return [true, $name];
        });
        if (!$ok) return ['ok' => false, 'backup' => $backupName, 'lines' => 0, 'message' => 'Nie udalo sie zapisac danych.'];

        return ['ok' => true, 'backup' => $backupName, 'lines' => $valid,
                'message' => "Przyjeto plik: {$valid} wpisow." . ($skipped > 0 ? " Pominieto {$skipped} niepoprawnych." : '') . ($backupName !== '' ? " Kopia: {$backupName}." : '')];
    }

    public function rawCsv(): string
    {
        if (!is_file($this->file)) return Config::CSV_HEADER . "\n";
        return (string)file_get_contents($this->file);
    }

    // --- Rewizja danych ----------------------------------------------------------
    // Licznik monotoniczny w osobnym pliku meta. Urzadzenie i strona sprawdzaja go co 10-60 s
    // (GET /api/revision); gdy sie zmieni — pobieraja dane. C1 (5.5.0): bez czytania CSV —
    // 'stamp' = czas modyfikacji + rozmiar pliku danych (wylapuje tez podmiane przez cPanel),
    // ('nrev' — znacznik nocy z kamery i ustawien, B3 — dokleja api.php.)
    // 'count' zostaje dla zgodnosci ze starszymi stronami (-1 = nieliczone).
    public function revision(): array
    {
        $rev = 0; $f = Config::revisionFile();
        if (is_file($f)) $rev = (int)trim((string)@file_get_contents($f));
        clearstatcache();
        $stamp = is_file($this->file) ? (filemtime($this->file) . '-' . filesize($this->file)) : '0';
        return ['rev' => $rev, 'stamp' => $stamp, 'count' => -1,
                'updatedAt' => (new DateTimeImmutable())->format('Y-m-d H:i:s')];
    }

    /** Wywolywane POD blokada locked(). */
    private function bumpRevision(): void
    {
        $f = Config::revisionFile();
        $cur = 0;
        if (is_file($f)) $cur = (int)trim((string)file_get_contents($f));
        $dir = dirname($f);
        if (!is_dir($dir)) @mkdir($dir, 0775, true);
        self::atomicWrite($f, (string)($cur + 1) . "\n");
    }

    public function loadSettings(): array
    {
        $s = ['sleepTelegram' => false];
        $f = Config::settingsFile();
        if (!is_file($f)) return $s;
        foreach (explode("\n", (string)file_get_contents($f)) as $line) {
            $eq = strpos($line, '='); if ($eq === false || $eq <= 0) continue;
            if (trim(substr($line, 0, $eq)) === 'sleepTelegram') $s['sleepTelegram'] = ((int)trim(substr($line, $eq + 1))) !== 0;
        }
        return $s;
    }

    public function saveSettings(array $settings): void
    {
        $dir = dirname(Config::settingsFile());
        if (!is_dir($dir)) @mkdir($dir, 0775, true);
        self::atomicWrite(Config::settingsFile(), 'sleepTelegram=' . (!empty($settings['sleepTelegram']) ? 1 : 0) . "\n");
    }
}
