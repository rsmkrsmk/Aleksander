<?php
/* ============================================================================
   WARSTWA WIZUALNA — konfiguracja ścieżek do SILNIKA i DANYCH.
   Edytuj TEN plik przy wgrywaniu na hosting. UI nie przetwarza danych —
   deleguje wszystko do silnika (engine/), który jako jedyny dotyka CSV/MySQL.
   ============================================================================ */

// -----------------------------------------------------------------------------
//  DOMYŚLNIE (lokalnie / gdy cały katalog web-php jest w jednym miejscu):
//  engine/ i data/ są o poziom wyżej niż ui/. Nic nie zmieniasz.
// -----------------------------------------------------------------------------
define('PANEL_ENGINE_DIR', __DIR__ . '/../engine');
define('PANEL_DATA_DIR',   __DIR__ . '/../data');

// -----------------------------------------------------------------------------
//  WARIANT cPanel / webd.pl (ZALECANY): ui/ trafia do public_html, a engine/
//  i data/ do katalogu domowego OBOK public_html (poza zasięgiem WWW). Wtedy
//  ZAKOMENTUJ dwie linie powyżej i ODKOMENTUJ poniższe:
//
//    /home/TWOJLOGIN/public_html/  <- zawartość web-php/ui/   (ten plik też)
//    /home/TWOJLOGIN/panel-engine/ <- zawartość web-php/engine/
//    /home/TWOJLOGIN/panel-data/   <- zawartość web-php/data/  (zapisywalny!)
// -----------------------------------------------------------------------------
// define('PANEL_ENGINE_DIR', dirname(__DIR__) . '/panel-engine');
// define('PANEL_DATA_DIR',   dirname(__DIR__) . '/panel-data');
