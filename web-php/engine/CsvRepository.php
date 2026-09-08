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
            $e = Domain::parseCsvLine($trim);
            if ($e === null) continue;
            $e['lineIndex'] = $idx;
            $out[] = $e;
        }
        return $out;
    }

    public function append(string $type, DateTimeImmutable $when, int $ml, int $piersLeft = -1, int $piersRight = -1): bool
    {
        $this->ensureFile();
        $row = Domain::toCsvRow($type, $when, $ml, $piersLeft, $piersRight);
        return file_put_contents($this->file, $row, FILE_APPEND | LOCK_EX) !== false;
    }

    public function deleteByIndex(int $entryIndex): array
    {
        if ($entryIndex < 0 || !is_file($this->file)) return ['ok' => false];
        $lines = explode("\n", (string)file_get_contents($this->file));
        $header = array_shift($lines);
        if (!empty($lines) && end($lines) === '') array_pop($lines); // koncowy pusty po \n
        $kept = []; $removed = ''; $found = false;
        foreach ($lines as $idx => $raw) {
            $raw = rtrim($raw, "\r");
            if ($idx === $entryIndex) {
                $e = Domain::parseCsvLine(trim($raw));
                if ($e !== null) $removed = Domain::describeCsvEntry($e);
                $found = true;
            } else {
                $kept[] = $raw;
            }
        }
        if (!$found || $entryIndex >= count($lines)) return ['ok' => false];
        $body = implode("\n", $kept);
        file_put_contents($this->file, $header . "\n" . $body . ($body !== '' ? "\n" : ''), LOCK_EX);
        return ['ok' => true, 'removed' => $removed];
    }

    public function importCsv(string $rawText): array
    {
        $this->ensureFile();
        @copy($this->file, Config::backupFile());
        $lines = preg_split('/\r?\n/', $rawText);
        $out = [Config::CSV_HEADER]; $imported = 0; $skipped = 0; $sawHeader = false;
        foreach ($lines as $line) {
            $t = trim($line);
            if ($t === '') continue;
            if (!$sawHeader) { if (strncmp($t, 'data,', 5) === 0) $sawHeader = true; continue; }
            if (strlen($t) > 160) { $skipped++; continue; }
            if (Domain::parseCsvLine($t) === null) { $skipped++; continue; }
            $out[] = $t; $imported++;
        }
        if (!$sawHeader || $imported === 0) return ['ok' => false, 'imported' => 0, 'skipped' => $skipped];
        file_put_contents($this->file, implode("\n", $out) . "\n", LOCK_EX);
        return ['ok' => true, 'imported' => $imported, 'skipped' => $skipped];
    }

    public function replaceRawCsv(string $rawText): array
    {
        $this->ensureFile();
        // 1) Walidacja przeslanego pliku: musi miec naglowek i >=1 poprawny wiersz.
        $lines = preg_split('/\r?\n/', $rawText);
        $out = [Config::CSV_HEADER]; $valid = 0; $skipped = 0; $sawHeader = false;
        foreach ($lines as $line) {
            $t = trim($line);
            if ($t === '') continue;
            if (!$sawHeader) { if (strncmp($t, 'data,', 5) === 0) { $sawHeader = true; continue; } }
            if (strncmp($t, 'data,', 5) === 0) continue; // pomin ewentualne kolejne naglowki
            if (strlen($t) > 160) { $skipped++; continue; }
            if (Domain::parseCsvLine($t) === null) { $skipped++; continue; }
            $out[] = $t; $valid++;
        }
        if (!$sawHeader) return ['ok' => false, 'backup' => '', 'lines' => 0, 'message' => 'Brak naglowka CSV (spodziewano "data,...").'];
        if ($valid === 0) return ['ok' => false, 'backup' => '', 'lines' => 0, 'message' => 'Brak poprawnych wierszy danych.'];

        // 2) Kopia zapasowa DOTYCHCZASOWYCH danych z aktualnym czasem: YYYY-MM-DD-HH-MM-SS.bakap
        $backupPath = Config::timestampedBackupFile();
        $backupName = '';
        if (is_file($this->file) && filesize($this->file) > 0) {
            if (@copy($this->file, $backupPath)) $backupName = basename($backupPath);
        }

        // 3) Podmiana danych na przeslane (znormalizowane: naglowek + poprawne wiersze).
        $ok = file_put_contents($this->file, implode("\n", $out) . "\n", LOCK_EX) !== false;
        if (!$ok) return ['ok' => false, 'backup' => $backupName, 'lines' => 0, 'message' => 'Nie udalo sie zapisac danych.'];

        return ['ok' => true, 'backup' => $backupName, 'lines' => $valid,
                'message' => "Przyjeto plik: {$valid} wpisow." . ($skipped > 0 ? " Pominieto {$skipped} niepoprawnych." : '') . ($backupName !== '' ? " Kopia: {$backupName}." : '')];
    }

    public function rawCsv(): string
    {
        if (!is_file($this->file)) return Config::CSV_HEADER . "\n";
        return (string)file_get_contents($this->file);
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
        file_put_contents(Config::settingsFile(), 'sleepTelegram=' . (!empty($settings['sleepTelegram']) ? 1 : 0) . "\n", LOCK_EX);
    }
}
