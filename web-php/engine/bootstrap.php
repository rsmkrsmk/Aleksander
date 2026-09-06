<?php
/* ============================================================================
   SILNIK DANYCH — bootstrap
   Laduje silnik i tworzy wlasciwe repozytorium wg Config::storageDriver().
   Bezpieczny fallback: gdy MySQL jest wybrany, ale polaczenie/tabele zawiodą,
   silnik AUTOMATYCZNIE wraca do CSV (backup dziala zawsze) — panel nie padnie.
   ============================================================================ */
declare(strict_types=1);

require_once __DIR__ . '/config.php';
require_once __DIR__ . '/Domain.php';
require_once __DIR__ . '/Repository.php';
require_once __DIR__ . '/CsvRepository.php';
require_once __DIR__ . '/MysqlRepository.php';

function make_repository(): Repository
{
    if (Config::storageDriver() === 'mysql') {
        try {
            return new MysqlRepository();
        } catch (Throwable $e) {
            // Awaria bazy — dzialamy na CSV (dane i tak sa w CSV jako backup).
            error_log('[silnik] MySQL niedostepny, fallback CSV: ' . $e->getMessage());
            return new CsvRepository();
        }
    }
    return new CsvRepository();
}
