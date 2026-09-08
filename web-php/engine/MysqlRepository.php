<?php
/* ============================================================================
   SILNIK DANYCH — magazyn MySQL (zrodlo prawdy w trybie 'mysql')
   + KAZDY zapis idzie ROWNIEZ do CSV (backup w formacie firmware).

   Aktywacja: Config::storageDriver()==='mysql' + dane w Config::mysql()
   + utworzone tabele (engine/schema.sql). Do czasu aktywacji uzywany jest CSV.

   Docelowo z tego samego API/bazy korzystaja: strona WWW oraz urzadzenia (zdalnie).
   ============================================================================ */
declare(strict_types=1);

require_once __DIR__ . '/config.php';
require_once __DIR__ . '/Domain.php';
require_once __DIR__ . '/Repository.php';
require_once __DIR__ . '/CsvRepository.php';

final class MysqlRepository implements Repository
{
    private PDO $db;
    private array $cfg;
    private CsvRepository $csv;   // rownolegly backup — ZAWSZE zapisywany

    public function __construct()
    {
        $this->cfg = Config::mysql();
        $this->csv = new CsvRepository(); // ten sam plik CSV co w trybie csv
        $dsn = sprintf('mysql:host=%s;port=%d;dbname=%s;charset=%s',
            $this->cfg['host'], $this->cfg['port'], $this->cfg['name'], $this->cfg['charset']);
        $this->db = new PDO($dsn, $this->cfg['user'], $this->cfg['pass'], [
            PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
            PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
            PDO::ATTR_EMULATE_PREPARES => false,
        ]);
    }

    private function tEntries(): string  { return $this->cfg['table_entries']; }
    private function tSettings(): string { return $this->cfg['table_settings']; }

    // Wpisy chronologicznie. id (autoinkrement) sluzy jako stabilny lineIndex.
    public function allEntries(): array
    {
        $rows = $this->db->query(
            "SELECT id, entry_date, entry_time, type, ml, piers_left, piers_right
             FROM `{$this->tEntries()}` ORDER BY entry_date, entry_time, id"
        )->fetchAll();
        $out = [];
        foreach ($rows as $r) {
            $out[] = [
                'date' => $r['entry_date'], 'time' => substr((string)$r['entry_time'], 0, 5),
                'type' => $r['type'], 'ml' => (int)$r['ml'],
                'piersLeft' => (int)$r['piers_left'], 'piersRight' => (int)$r['piers_right'],
                'lineIndex' => (int)$r['id'],
            ];
        }
        return $out;
    }

    public function append(string $type, DateTimeImmutable $when, int $ml, int $piersLeft = -1, int $piersRight = -1): bool
    {
        $stmt = $this->db->prepare(
            "INSERT INTO `{$this->tEntries()}` (entry_date, entry_time, type, ml, piers_left, piers_right)
             VALUES (:d, :t, :ty, :ml, :pl, :pr)"
        );
        $ok = $stmt->execute([
            ':d' => $when->format('Y-m-d'), ':t' => $when->format('H:i:s'), ':ty' => $type,
            ':ml' => $ml, ':pl' => max($piersLeft, 0), ':pr' => max($piersRight, 0),
        ]);
        // Backup CSV — ZAWSZE wiernym lustrem bazy (przepisujemy z aktualnego stanu,
        // by kolejnosc i lineIndex zgadzaly sie z baza takze przy wpisach wstecz).
        $this->rewriteCsvBackup();
        return $ok;
    }

    public function deleteByIndex(int $entryIndex): array
    {
        $sel = $this->db->prepare("SELECT * FROM `{$this->tEntries()}` WHERE id = :id");
        $sel->execute([':id' => $entryIndex]);
        $r = $sel->fetch();
        if (!$r) return ['ok' => false];
        $removed = Domain::describeCsvEntry([
            'date' => $r['entry_date'], 'time' => substr((string)$r['entry_time'], 0, 5),
            'type' => $r['type'], 'ml' => (int)$r['ml'], 'piersLeft' => (int)$r['piers_left'], 'piersRight' => (int)$r['piers_right'],
        ]);
        $this->db->prepare("DELETE FROM `{$this->tEntries()}` WHERE id = :id")->execute([':id' => $entryIndex]);
        // Odswiez backup CSV z aktualnego stanu bazy.
        $this->rewriteCsvBackup();
        return ['ok' => true, 'removed' => $removed];
    }

    public function importCsv(string $rawText): array
    {
        // Backup dotychczasowego stanu BAZY (nie pliku — plik moglby byc nieaktualny).
        @file_put_contents(Config::backupFile(), $this->rawCsv(), LOCK_EX);
        $lines = preg_split('/\r?\n/', $rawText);
        $rows = []; $imported = 0; $skipped = 0; $sawHeader = false;
        foreach ($lines as $line) {
            $t = trim($line);
            if ($t === '') continue;
            if (!$sawHeader) { if (strncmp($t, 'data,', 5) === 0) $sawHeader = true; continue; }
            if (strlen($t) > 160) { $skipped++; continue; }
            $e = Domain::parseCsvLine($t);
            if ($e === null) { $skipped++; continue; }
            $rows[] = $e; $imported++;
        }
        if (!$sawHeader || $imported === 0) return ['ok' => false, 'imported' => 0, 'skipped' => $skipped];

        $this->db->beginTransaction();
        try {
            $this->db->exec("DELETE FROM `{$this->tEntries()}`");
            $stmt = $this->db->prepare(
                "INSERT INTO `{$this->tEntries()}` (entry_date, entry_time, type, ml, piers_left, piers_right)
                 VALUES (:d,:t,:ty,:ml,:pl,:pr)"
            );
            foreach ($rows as $e) {
                $stmt->execute([
                    ':d' => $e['date'], ':t' => $e['time'] . ':00', ':ty' => $e['type'],
                    ':ml' => $e['ml'], ':pl' => $e['piersLeft'], ':pr' => $e['piersRight'],
                ]);
            }
            $this->db->commit();
        } catch (Throwable $ex) {
            $this->db->rollBack();
            return ['ok' => false, 'imported' => 0, 'skipped' => $skipped];
        }
        $this->rewriteCsvBackup();
        return ['ok' => true, 'imported' => $imported, 'skipped' => $skipped];
    }

    public function replaceRawCsv(string $rawText): array
    {
        // 1) Kopia zapasowa DOTYCHCZASOWYCH danych (zrzut BAZY) z aktualnym czasem.
        $backupPath = Config::timestampedBackupFile();
        $backupName = '';
        if (@file_put_contents($backupPath, $this->rawCsv(), LOCK_EX) !== false) $backupName = basename($backupPath);

        // 2) Zaladuj przeslany plik do bazy (importCsv robi walidacje + transakcje + odswieza CSV).
        $res = $this->importCsv($rawText);
        if (!$res['ok']) return ['ok' => false, 'backup' => $backupName, 'lines' => 0, 'message' => 'Brak poprawnych wierszy danych.'];

        return ['ok' => true, 'backup' => $backupName, 'lines' => $res['imported'],
                'message' => "Przyjeto plik: {$res['imported']} wpisow." . ((int)$res['skipped'] > 0 ? " Pominieto {$res['skipped']} niepoprawnych." : '') . ($backupName !== '' ? " Kopia: {$backupName}." : '')];
    }

    // Zrzut bazy do CSV (backup zawsze odzwierciedla stan danych).
    private function rewriteCsvBackup(): void
    {
        $out = [Config::CSV_HEADER];
        foreach ($this->allEntries() as $e) {
            $when = Domain::csvDateTimeToDate($e['date'], $e['time']);
            if ($when === null) continue;
            $out[] = rtrim(Domain::toCsvRow($e['type'], $when, $e['ml'], $e['piersLeft'], $e['piersRight']), "\n");
        }
        @file_put_contents(Config::dataFile(), implode("\n", $out) . "\n", LOCK_EX);
    }

    public function rawCsv(): string
    {
        $out = [Config::CSV_HEADER];
        foreach ($this->allEntries() as $e) {
            $when = Domain::csvDateTimeToDate($e['date'], $e['time']);
            if ($when === null) continue;
            $out[] = rtrim(Domain::toCsvRow($e['type'], $when, $e['ml'], $e['piersLeft'], $e['piersRight']), "\n");
        }
        return implode("\n", $out) . "\n";
    }

    public function loadSettings(): array
    {
        $s = ['sleepTelegram' => false];
        try {
            $row = $this->db->query("SELECT `value` FROM `{$this->tSettings()}` WHERE `key`='sleepTelegram'")->fetch();
            if ($row) $s['sleepTelegram'] = ((int)$row['value']) !== 0;
        } catch (Throwable $ex) { /* brak tabeli/wiersza = domyslne */ }
        return $s;
    }

    public function saveSettings(array $settings): void
    {
        $stmt = $this->db->prepare(
            "INSERT INTO `{$this->tSettings()}` (`key`,`value`) VALUES ('sleepTelegram', :v)
             ON DUPLICATE KEY UPDATE `value` = :v"
        );
        $stmt->execute([':v' => !empty($settings['sleepTelegram']) ? '1' : '0']);
    }
}
