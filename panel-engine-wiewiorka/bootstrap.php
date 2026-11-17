<?php
/* ============================================================================
   wiewiorka.mom — SILNIK DANYCH: bootstrap
   Laduje silnik. Dane zawsze w CSV, osobny plik dla kazdego konta.
   ============================================================================ */
declare(strict_types=1);

require_once __DIR__ . '/config.php';
require_once __DIR__ . '/Domain.php';
require_once __DIR__ . '/Repository.php';
require_once __DIR__ . '/CsvRepository.php';
require_once __DIR__ . '/Auth.php';
require_once __DIR__ . '/SleepNights.php';
require_once __DIR__ . '/SleepAnalysis.php';
require_once __DIR__ . '/SleepStats.php';

function make_repository(): Repository
{
    return new CsvRepository();
}
