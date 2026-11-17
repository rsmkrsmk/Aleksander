<?php
/* ============================================================================
   SILNIK DANYCH — kontrakt magazynu (Repository)
   Abstrakcja nad zrodlem danych. Implementacje: CsvRepository, MysqlRepository.
   Domain (logika) nie wie, skad pochodza wpisy — dostaje je stad.
   ============================================================================ */
declare(strict_types=1);

interface Repository
{
    /**
     * Wszystkie wpisy w kolejnosci chronologicznej (jak plik append-only).
     * Kazdy wpis: ['date','time','type','ml','piersLeft','piersRight','lineIndex'].
     * lineIndex = FIZYCZNY, stabilny identyfikator wiersza uzywany do usuwania.
     */
    public function allEntries(): array;

    /** Dopisuje wpis. ZAWSZE zapisuje tez do CSV (backup). Zwraca true/false. */
    public function append(string $type, DateTimeImmutable $when, int $ml, int $piersLeft = -1, int $piersRight = -1): bool;

    /**
     * Usuwa wpis po lineIndex. Zwraca ['ok'=>bool, 'removed'=>string, 'stale'=>bool].
     * $expect (date, time, type): wpis musi sie zgadzac, inaczej nic nie usuwa i zwraca stale=true.
     * $withMilk: dla KARMIENIA usuwa tez mleko z ta sama data i godzina.
     */
    public function deleteByIndex(int $lineIndex, ?array $expect = null, bool $withMilk = false): array;

    /**
     * Edytuje wpis KARMIENIA (i jego sparowane mleko) W MIEJSCU — bez zmiany
     * kolejnosci wierszy. $feedLineIndex wskazuje wiersz typu KARMIENIE. Nowy czas
     * $when zapisywany jest na tym wierszu (minuty piersi zachowane).
     *
     * Mleko opisuja DWIE osobne ilosci (mleko mieszane = obie > 0, zapisywane jako
     * DWA wiersze MLEKO_MATKI + MLEKO_MODYFIKOWANE o tej samej godzinie):
     *   - $motherMl   > 0 => wiersz MLEKO_MATKI z ta iloscia,
     *   - $modifiedMl > 0 => wiersz MLEKO_MODYFIKOWANE z ta iloscia,
     *   - obie == 0       => brak mleka (usun WSZYSTKIE sparowane wiersze mleka).
     * Wszystkie dotychczasowe wiersze mleka o starym czasie karmienia sa usuwane,
     * a nowe (wg powyzszych ilosci) wstawiane TUZ ZA wierszem KARMIENIE. Obsluguje
     * takze STARE wpisy jednowierszowe MLEKO_MIESZANE (są usuwane jak kazde mleko pary).
     * Zwraca ['ok'=>bool, 'message'=>string].
     */
    public function updateFeeding(int $feedLineIndex, DateTimeImmutable $when, int $motherMl, int $modifiedMl): array;

    /** Import: zastepuje caly zbior danych zawartoscia CSV. Zwraca ['ok','imported','skipped']. */
    public function importCsv(string $rawText): array;

    /**
     * Przyjmuje z ZEWNATRZ caly plik CSV i czyni go biezacymi danymi.
     * Najpierw zapisuje kopie zapasowa DOTYCHCZASOWYCH danych pod nazwa
     * "YYYY-MM-DD-HH-MM-SS.bakap", potem podmienia dane na przeslane.
     * Zwraca ['ok'=>bool,'backup'=>string,'lines'=>int,'message'=>string].
     */
    public function replaceRawCsv(string $rawText): array;

    /** Surowy CSV (do /export.csv) — zawsze aktualny zrzut danych. */
    public function rawCsv(): string;

    /**
     * Rewizja danych: monotoniczny licznik zmian + znacznik pliku (czas modyfikacji i rozmiar)
     * + czas. Urzadzenie i strona sprawdzaja ja, aby wykryc zmiane bez pobierania calego CSV.
     * Zwraca ['rev'=>int,'stamp'=>string,'count'=>-1,'updatedAt'=>string].
     */
    public function revision(): array;

    /** Ustawienia klucz=>wartosc (na teraz: sleepTelegram). */
    public function loadSettings(): array;
    public function saveSettings(array $settings): void;
}
