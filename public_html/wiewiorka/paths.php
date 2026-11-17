<?php
/* ============================================================================
   wiewiorka.mom — ścieżki do SILNIKA i DANYCH.

   Hosting (webd.pl), układ na serwerze (ten sam co w repozytorium):
     /home/phpmapy1/public_html/wiewiorka/     <- ta strona (domena wiewiorka.mom)
     /home/phpmapy1/panel-engine-wiewiorka/    <- silnik (poza katalogiem publicznym)
     /home/phpmapy1/panel-data-wiewiorka/      <- dane kont (poza katalogiem publicznym, zapisywalny)
   Katalog strony leży dwa poziomy niżej niż silnik i dane, więc dirname(__DIR__, 2).
   Strona Leśnego Dziennika (public_html, panel-engine, panel-data) nie jest używana.
   ============================================================================ */
define('PANEL_ENGINE_DIR', dirname(__DIR__, 2) . '/panel-engine-wiewiorka');
define('PANEL_DATA_ROOT',  dirname(__DIR__, 2) . '/panel-data-wiewiorka');
