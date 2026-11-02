<?php
/* ============================================================================
   WARSTWA WIZUALNA — ścieżki do SILNIKA i DANYCH (wykrywane automatycznie).

   Hosting (webd.pl), układ na serwerze:
     /home/phpmapy1/public_html/  <- zawartość web-php/ui/   (ten plik też)
     /home/phpmapy1/panel-engine/ <- zawartość web-php/engine/
     /home/phpmapy1/panel-data/   <- dane (karmienia.csv, zapisywalny)
   Lokalnie / w repozytorium: engine/ i data/ obok ui/.

   Gdy obok public_html istnieje katalog panel-engine, używany jest układ hostingu,
   w przeciwnym razie układ z repozytorium — ten sam plik działa w obu miejscach.
   ============================================================================ */
if (is_dir(dirname(__DIR__) . '/panel-engine')) {
    define('PANEL_ENGINE_DIR', dirname(__DIR__) . '/panel-engine');
    define('PANEL_DATA_DIR',   dirname(__DIR__) . '/panel-data');
} else {
    define('PANEL_ENGINE_DIR', __DIR__ . '/../engine');
    define('PANEL_DATA_DIR',   __DIR__ . '/../data');
}
