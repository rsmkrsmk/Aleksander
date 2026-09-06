<?php
// =============================================================================
//  KONFIGURACJA SCIEZEK — edytuj ten JEDEN plik przy wgrywaniu na hosting.
// =============================================================================
//
// Panel sklada sie z trzech czesci:
//   • public/  -> katalog publiczny (to, co trafia do web-roota, np. public_html)
//   • lib/     -> kod PHP (logika) — POWINIEN byc poza web-rootem
//   • data/    -> plik CSV z danymi — POWINIEN byc poza web-rootem
//
// Ponizej wskazujesz, gdzie na serwerze leza katalogi lib/ i data/.
//
// -----------------------------------------------------------------------------
//  WARIANT DOMYSLNY (lokalny / gdy caly katalog web-php jest w jednym miejscu):
//  lib/ i data/ sa o poziom wyzej niz public/. Nic nie musisz zmieniac.
// -----------------------------------------------------------------------------
define('PANEL_LIB_DIR',  __DIR__ . '/../lib');
define('PANEL_DATA_DIR', __DIR__ . '/../data');

// -----------------------------------------------------------------------------
//  WARIANT cPanel / webd.pl (ZALECANY dla bezpieczenstwa):
//  public/ trafia do ~/public_html, a lib/ i data/ do katalogu domowego OBOK
//  public_html (poza zasiegiem WWW). Wtedy ZAKOMENTUJ dwie linie powyzej,
//  a ODKOMENTUJ ponizsze i — jesli trzeba — popraw sciezke katalogu domowego.
//
//  Przyklad struktury na serwerze:
//    /home/TWOJLOGIN/public_html/   <- zawartosc web-php/public/ (ten plik tez)
//    /home/TWOJLOGIN/panel-lib/     <- zawartosc web-php/lib/
//    /home/TWOJLOGIN/panel-data/    <- zawartosc web-php/data/ (zapisywalny!)
//
//  Nie musisz wpisywac loginu recznie — dirname(__DIR__) to katalog nad
//  public_html, czyli Twoj katalog domowy.
// -----------------------------------------------------------------------------
// define('PANEL_LIB_DIR',  dirname(__DIR__) . '/panel-lib');
// define('PANEL_DATA_DIR', dirname(__DIR__) . '/panel-data');
