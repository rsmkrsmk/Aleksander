# Rejestr karmienia Aleksandra — ESP32-S3 Touch LCD 4B

Projekt prowadzi dotykowy **dziennik opieki** dla płytki Waveshare ESP32-S3-Touch-LCD-4B: karmienia piersią (minuty osobno LEWA/PRAWA), butelki (mleko matki/modyfikowane), pieluchy, odciąganie pokarmu i witaminę D. Urządzenie synchronizuje czas przez Wi‑Fi i NTP, dane zapisuje w pliku CSV w systemie plików **LittleFS** (wewnętrzny Flash), a równolegle serwuje **panel WWW** będący kopią interfejsu — dostępny pod `http://karmienie.local` lub adresem IP. Ekran główny pokazuje zegar, diody statusu, wiek Aleksandra (od 08.08.2026), wskazówkę dnia oraz licznik czasu od ostatniego karmienia z progami kolorów.

> **Nie trzeba używać ani podłączać karty microSD.** Program nie wykorzystuje złącza H3 do przechowywania danych i nie wymaga żadnego dodatkowego modułu ani przewodu.

Przed pierwszym uruchomieniem należy uzupełnić nazwę oraz hasło domowej sieci Wi‑Fi w pliku `OfficialWaveshareHelloWorld/secrets.h`. Nie należy przesyłać tego pliku z prawdziwym hasłem do publicznego repozytorium.

## Zawartość projektu

| Plik | Rola |
|---|---|
| `OfficialWaveshareHelloWorld/OfficialWaveshareHelloWorld.ino` | Główny program Arduino: ekran, dotyk, Wi‑Fi/NTP, formularze oraz zapis LittleFS. |
| `OfficialWaveshareHelloWorld/config.h` | Parametry daty urodzenia, zakresu ml, motywu nocnego, Telegramu i OTA. |
| `OfficialWaveshareHelloWorld/web_panel.h` | Panel WWW osadzony w pamięci programu (PROGMEM). |
| `OfficialWaveshareHelloWorld/secrets.h` | Lokalne poświadczenia Wi‑Fi do uzupełnienia przez użytkownika. |
| `OfficialWaveshareHelloWorld/secrets.h.example` | Bezpieczna kopia wzorcowa pliku Wi‑Fi. |
| `LVGL_CONFIGURATION/lv_conf.h` | Konfiguracja LVGL 9.3 wymagana przez bibliotekę podczas kompilacji. |
| `LCD_Official_Config_Test/LCD_Official_Config_Test.ino` | Jedyny zalecany test LCD: konfiguracja producenta z poprawionymi stałymi RGB565, bez LVGL ani Wi‑Fi. |
| `LCD_TEST_INSTRUCTIONS.md` | Krótka instrukcja kompilacji i interpretacji wyniku jedynego testu LCD. |
| `PROJECT_DESIGN.md` | Dokumentacja ekranów, modelu danych i zasad działania. |

## Wymagane oprogramowanie

Należy zainstalować aktualne Arduino IDE, a następnie dodać w Menedżerze płytek pakiet **esp32 by Espressif Systems** w wersji **3.2.0 lub nowszej**. Producent płytki dla projektów Arduino wskazuje bibliotekę **GFX_Library_for_Arduino** w wersji 1.6.0 lub nowszej oraz **LVGL** 9.3.0. [1]

| Składnik | Wymaganie |
|---|---|
| Płytka w Arduino IDE | `ESP32S3 Dev Module` z pakietu `esp32 by Espressif Systems` >= 3.2.0. |
| Biblioteka pomocnicza panelu | `GFX_Library_for_Arduino` >= 1.6.0 (ekspander i komendy inicjalizacyjne ST7701; obraz obsługuje natywne `esp_lcd`). |
| Biblioteka interfejsu | `lvgl` 9.3.0. |
| Dane | Wewnętrzna pamięć Flash ESP32 — LittleFS, bez karty microSD. |

Oficjalne demo producenta zawiera również biblioteki pomocnicze, ale ten projekt nie wymaga jego modułów RTC, IMU, audio ani zewnętrznej pamięci SD. [1]

## Obowiązkowa konfiguracja LVGL — naprawa błędu `lv_conf.h`

Błąd `fatal error: ../../lv_conf.h: No such file or directory` oznacza, że biblioteka LVGL nie ma własnego pliku konfiguracyjnego. Samo umieszczenie `lv_conf.h` obok pliku `.ino` **nie wystarcza**. Zgodnie z dokumentacją LVGL plik musi znajdować się **obok katalogu `lvgl`**, bezpośrednio w katalogu bibliotek Arduino. [2]

Należy skopiować plik `LVGL_CONFIGURATION/lv_conf.h` z dostarczonego archiwum do dokładnie tej lokalizacji:

```text
C:\\Users\\pc\\Documents\\Arduino\\libraries\\lv_conf.h
```

Po wykonaniu kopiowania układ katalogów ma wyglądać następująco:

```text
C:\\Users\\pc\\Documents\\Arduino\\libraries\\
├── lvgl\\
│   ├── src\\
│   └── lvgl.h
├── GFX_Library_for_Arduino\\
└── lv_conf.h
```

Nie należy umieszczać pliku w `C:\\Users\\pc\\Documents\\Arduino\\libraries\\lvgl\\lv_conf.h`, ponieważ taka lokalizacja nie usuwa zgłoszonego błędu. Należy zamknąć Arduino IDE, skopiować plik, uruchomić IDE ponownie i dopiero wtedy rozpocząć kompilację. Plik dostarczony z projektem ma aktywną konfigurację dla **LVGL 9.3**, głębię koloru RGB565 (`LV_COLOR_DEPTH 16`) oraz włączone komponenty używane przez program, w tym przyciski, etykiety i suwak.

## Wewnętrzna pamięć danych

Program podczas pierwszego uruchomienia tworzy w LittleFS plik `/karmienia.csv` z nagłówkiem `data,godzina,typ,ml,piers_lewa_min,piers_prawa_min`. Każde następne użycie przycisku **ZAPISZ** dopisuje nowy wiersz; poprzednie rekordy nie są nadpisywane. Kolumny minut piersi są dopisywane dla karmień piersią; starsze/pozostałe wpisy używają tylko czterech pierwszych kolumn.

| Element CSV | Przykład |
|---|---|
| Nagłówek | `data,godzina,typ,ml,piers_lewa_min,piers_prawa_min` |
| Wpis karmienia piersią | `2026-08-18,11:45,KARMIENIE,0,10,8` |
| Wpis mleka (butelka) | `2026-08-18,12:30,MLEKO_MATKI,90` |
| Zdarzenie snu | `2026-08-18,13:10,SEN_START,0` / `...,SEN_STOP,0` |
| Waga (gramy w kolumnie ml) | `2026-08-18,09:00,WAGA,4200` |

Obsługiwane typy wpisów: `KARMIENIE`, `MLEKO_MATKI`, `MLEKO_MODYFIKOWANE`, `PIELUCHA_MOKRA`, `PIELUCHA_BRUDNA`, `ODCIAGANIE`, `WITAMINA_D`, `WAGA`, `SEN_START`, `SEN_STOP`.

Dane pozostają w wewnętrznej pamięci Flash również po zwykłym wyłączeniu urządzenia. Należy jednak zachować ostrożność przy wgrywaniu nowego szkicu lub zmianie ustawień partycji Flash: operacja wymazania Flash może usunąć historię. Cały plik można wyeksportować z panelu WWW (`/export.csv`) oraz zaimportować z powrotem (import robi najpierw kopię bezpieczeństwa). Gdy plik przekroczy 256 KB, urządzenie tworzy jednorazowo kopię archiwalną i sygnalizuje to w diagnostyce (dane nie są usuwane).

## Konfiguracja Wi‑Fi i daty urodzenia

W katalogu `OfficialWaveshareHelloWorld` należy otworzyć plik `secrets.h`, a następnie wpisać dane własnej sieci.

```cpp
constexpr char WIFI_SSID[] = "NAZWA_TWOJEJ_SIECI";
constexpr char WIFI_PASSWORD[] = "TWOJE_HASLO";
```

Data urodzenia jest ustawiona na 08.08.2026. Można ją zmienić wyłącznie w `config.h`.

```cpp
constexpr int BIRTH_DAY = 8;
constexpr int BIRTH_MONTH = 8;
constexpr int BIRTH_YEAR = 2026;
```

Program używa reguły czasowej dla Polski, włączając automatyczne przejście pomiędzy czasem zimowym a letnim. Dla innej lokalizacji należy zmienić `TIMEZONE_RULE` w `config.h`.

## Kompilacja i wgranie programu

1. W Arduino IDE należy wybrać płytkę **ESP32S3 Dev Module** oraz port USB urządzenia.
2. W ustawieniach płyty trzeba ustawić **PSRAM: OPI PSRAM** oraz **Flash Size: 16MB (128Mb)**. Jeżeli używany jest natywny port USB-OTG, warto również włączyć USB CDC przy starcie.
3. W polu **Partition Scheme** należy wybrać wariant dla pamięci 16 MB zawierający **SPIFFS**, np. `16M Flash (3MB APP/9.9MB SPIFFS)`. LittleFS korzysta z tej partycji danych. Nie wybieraj wariantu FATFS.
4. Należy otworzyć `OfficialWaveshareHelloWorld/OfficialWaveshareHelloWorld.ino` i skompilować szkic. Arduino IDE załaduje pliki `.h` z tego samego katalogu automatycznie.
5. Bezpośrednio po wgraniu ekran przez chwilę pokaże biały ekran kontrolny z tekstem `Rejestr karmienia / Uruchamianie...`, a następnie program połączy się z Wi‑Fi, pobierze czas z NTP, zainicjalizuje LittleFS i wyświetli ekran główny.

Jeśli płytka nie rozpocznie wgrywania, należy przytrzymać `BOOT`, krótko nacisnąć `RST` albo podłączyć USB ponownie — zależnie od trybu uruchomienia urządzenia.

## Obsługa na ekranie

**Ekran główny** — pasek górny z tytułem, diodami statusu (`W` Wi‑Fi, `P` Pamięć, `C` Czas; zielona = OK, czerwona = błąd) i zegarem. Poniżej: karta **OSTATNIE KARMIENIE** (lewa) oraz wiek Aleksandra (prawa). Pod nimi pasek licznika **OSTATNIE KARMIENIE: X temu** — zielony <3 h, żółty 3–4 h, czerwony ≥4 h od karmienia. Dalej karta **OSTATNIA BUTELKA** (pełna szerokość) oraz przyciski nawigacji w układzie 3×2: **SEN**, **INNE**, **WAGA** (górny rząd) i **KALENDARZ**, **PODSUMOWANIE** (dolny rząd); nad nimi duży przycisk **KARMIENIE**. (Codzienna wskazówka rozwojowa nie jest już pokazywana na ekranie głównym — pozostaje dostępna w panelu WWW oraz na wygaszaczu.)

**Formularz KARMIENIE** — czas z zegara urządzenia przesuwany o ±5 min, minuty piersi LEWA/PRAWA krok po 5 (0–90), a po rozwinięciu SZCZEGÓŁY: rodzaj mleka i ilość suwakiem 10–120 ml. ZAPISZ dopisuje wpis do CSV.

**SEN** — dedykowany ekran śledzenia snu wzorowany na aplikacji Napper (patrz sekcja *Funkcje dodatkowe*): przycisk **ZASNĄŁ / OBUDZIŁ SIĘ** (opisuje fakt), przewidywane okno drzemki wg wieku (kolorowy pasek stanu), oraz bilans dnia (drzemki, sen dzień/noc względem orientacyjnego celu).

**INNE** — szybkie akcje: **PIELUCHA**, **ODCIAG POKARMU**, **DIAGNOSTYKA**.

**PODSUMOWANIE** — dziś + wczoraj z każdym karmieniem osobno (godzina, typ, minuty L/P lub ml); przycisk „+ WCZYTAJ STARSZE DNI" dokłada historię po 7 dni. Na pasku: **COFNIJ** (dwuetapowe usunięcie ostatniego wpisu) i **WIT.D** (dawka dzienna).

Pełny opis ekranów i formatu danych znajduje się w `PROJECT_DESIGN.md`.

## Funkcje dodatkowe

| Funkcja | Opis |
|---|---|
| **SEN (wake windows)** | Śledzenie snu wzorowane na Napper: okna czuwania i zapotrzebowanie na sen wg wieku, przewidywanie następnej drzemki (pasek stanu: za wcześnie / okno drzemki / przekroczone), bilans dobowy snu (drzemki, sen dzień/noc vs cel). Dostępne na urządzeniu (przycisk **SEN**), w panelu WWW i skrótowo na wygaszaczu |
| Powiadomienia snu | Telegram raz na okno czuwania (wejście w okno drzemki i przekroczenie). Włącznik **„Sen: powiadomienia Telegram"** w DIAGNOSTYCE (urządzenie i WWW), zapisywany trwale w `/ustawienia.cfg` |
| Wykres wagi (WWW) | Krzywa pomiarów na tle mediany WHO i oczekiwanego zakresu (min–max) oraz **pasma przyrostu od wagi wypisowej** (2850 g od 10.08.2026: 25–30 g/d do ~3 mies., 15–20 g/d do ~6 mies.) |
| Panel WWW | Kopie wszystkich widoków jako popupy (w tym **SEN**); eksport CSV (`/export.csv`); import CSV; polling co 10 s |
| Motyw nocny | 21:00–7:00 ciemna paleta + przyciemnione podświetlenie (na urządzeniu i w WWW) |
| Cofnij | Bezpieczne, atomowe usunięcie ostatniego wpisu (urządzenie i WWW) |
| Backup | Automatyczna dzienna kopia `/karmienia_backup.csv`; miękka rotacja (archiwum) po przekroczeniu 256 KB |
| Telegram | Powiadomienia o wpisach do drugiego rodzica — uzupełnij `TELEGRAM_BOT_TOKEN` i `TELEGRAM_CHAT_ID` w `config.h` (puste = wyłączone). Wysyłka w osobnym zadaniu (nie blokuje UI) |
| OTA | Wgrywanie szkicu przez Wi‑Fi — ustaw `OTA_PASSWORD` w `config.h` (puste = wyłączone) |
| mDNS | `http://karmienie.local` |
| NTP | Re-synchronizacja zegara co 6 h |
| Wygaszacz | Po 2 min bezczynności: duży zegar z datą, ostatnie karmienie (kolor) + informacja o oknie drzemki, wskazówka rozwojowa, statystyki dnia, pogoda z Open-Meteo z ikoną, opisem, min/max, 3h prognozą i poradą ubioru |
| Pogoda | Open-Meteo (HTTP, darmowe, bez klucza API), `apparent_temperature`, cache w LittleFS |

Przy starcie w Monitorze Portu Serial dostępna jest inwentaryzacja partycji i wolnego miejsca (aplikacja, dane, Flash/Heap/PSRAM, zajętość LittleFS).

## Diagnostyka

| Objaw | Najbardziej prawdopodobna przyczyna i rozwiązanie |
|---|---|
| Zegar pokazuje „Brak potwierdzonego czasu” | Sprawdź `secrets.h`, zasięg Wi‑Fi oraz czy sieć ma dostęp do internetu. |
| Status `Pamiec: BLAD` | Sprawdź, czy `Partition Scheme` zawiera `SPIFFS` i wgraj szkic ponownie. Jeżeli błąd pozostaje, wymaż Flash w Arduino IDE i wgraj program ponownie; spowoduje to usunięcie danych. |
| Kompilacja kończy się błędem braku biblioteki | Zainstaluj `GFX_Library_for_Arduino` oraz `lvgl` w podanej wersji. |
  | Ekran jest czarny po wgraniu starszej wersji szkicu | Wgraj obecną wersję. Poprzednie szkice pomijały wymaganą sekwencję pinów `5` i `6` ekspandera TCA9554 przed uruchomieniem panelu. Oficjalny przykład Waveshare wykonuje tę sekwencję, aby uruchomić panel ST7701. |
  | Ekran nadal jest czarny po wgraniu obecnej wersji | Wgraj wyłącznie `LCD_Official_Config_Test/LCD_Official_Config_Test.ino` (szkic diagnostyczny z sekwencją uruchomienia oficjalnego przykładu `01_HelloWorld`). Prześlij pełny log z monitora portu, zwłaszcza komunikaty rozpoczynające się od `LCD:`. |
| Po aktualizacji programu brakuje historii | Sprawdź ustawienia wgrywania; wymazanie Flash lub zmiana schematu partycji usuwa dane LittleFS. |

## Historia zmian

Poniżej chronologiczny wykaz wprowadzonych zmian (od najnowszych). Każda pozycja opisuje **co zmieniła** i **co dodała**.

### Wygaszacz — przebudowa karty zegara (PR #17)
- **Zmieniło:** układ karty zegara na wygaszaczu na dwukolumnowy — zegar (mniejsza czcionka) z datą i dniem tygodnia pod spodem po lewej; po prawej dwa wiersze: ostatnie karmienie i osobny wiersz o drzemce.
- **Naprawiło:** nachodzenie zawijającego się tekstu (karmienie + drzemka) na zegar, które pojawiło się po dodaniu informacji o śnie.

### SEN — poprawki UX i nazewnictwa (PR #16)
- **Zmieniło:** nazwy przycisków snu na opisujące **fakt** (co dziecko właśnie zrobiło): **ZASNĄŁ / OBUDZIŁ SIĘ** zamiast trybu rozkazującego. Przycisk na ekranie głównym pokazuje „SEN (śpi)”, gdy dziecko śpi.
- **Zmieniło:** ekran główny urządzenia — usunięto kartę codziennej wskazówki; w jej miejsce trafiła czytelna karta **OSTATNIE KARMIENIE**, a **OSTATNIA BUTELKA** zajmuje pełną szerokość. Wskazówka rozwojowa pozostaje w panelu WWW i na wygaszaczu.
- **Dodało:** przycisk **SEN** na stronie głównej panelu WWW.
- **Usunęło:** duplikat sekcji SEN z ekranu/modala **INNE** (cała obsługa snu jest teraz pod głównym przyciskiem SEN).

### Funkcja SEN — śledzenie snu wzorowane na Napper (PR #12–#15)
Kompletna, nowa funkcjonalność wdrożona w czterech etapach:
- **Etap 1 — fundament (PR #12):** tabele wg wieku (okna czuwania, zapotrzebowanie na sen dzień/noc, orientacyjna liczba drzemek), algorytmy predykcji drzemki i bilansu dobowego snu (liczonego w jednym przebiegu pliku CSV, z podziałem noc/dzień 21–07 i snem przez północ), plik trwałych ustawień `/ustawienia.cfg` w LittleFS oraz rozszerzenie `/api/status` o pola snu.
- **Etap 2 — urządzenie (PR #13):** przycisk **SEN** na ekranie głównym (nowy układ nawigacji 3×2) oraz ekran SEN: stan (śpi/czuwa), przewidywane okno drzemki z kolorowym paskiem stanu, przycisk ZASNĄŁ/OBUDZIŁ SIĘ, bilans dnia.
- **Etap 3 — panel WWW (PR #14):** modal SEN z tym samym zestawem informacji, odświeżany na żywo.
- **Etap 4 — powiadomienia i przełącznik (PR #15):** informacja o oknie drzemki na wygaszaczu, powiadomienia **Telegram** (raz na okno czuwania — wejście w okno i przekroczenie, z zabezpieczeniem przed fałszywym alarmem po restarcie) oraz **przełącznik** „Sen: powiadomienia Telegram” w DIAGNOSTYCE (urządzenie i WWW, zapisywany trwale).

### Wykres wagi — pasmo przyrostu od wagi wypisowej (PR #11)
- **Dodało:** na wykresie wagi w panelu WWW drugie pasmo referencyjne — zakres przyrostu liczony od wagi wyjściowej ze szpitala (2850 g, 10.08.2026): 25–30 g/dobę do ~3 mies., 15–20 g/dobę do ~6 mies. Istniejące pasmo WHO i mediana pozostały bez zmian.

### Przyciski butelki — poprawka layoutu (PR #10)
- **Naprawiło:** przyciski MATKI/MODYFIKOWANE w formularzu karmienia (sekcja SZCZEGÓŁY), które wychodziły poza obszar karty w poziomie i pionie — dopasowano geometrię do wewnętrznego obszaru karty.

### Responsywność dotyku (PR #9)
- **Zmieniło:** sposób odpytywania dotyku — I²C podniesiony do 400 kHz (Fast Mode), sztywne opóźnienie w pętli rozbite na krótsze próbki, a urządzenie wejściowe LVGL przełączone w tryb zdarzeniowy i odpytywane częściej niż pełny render.
- **Efekt:** dotyk reaguje natychmiast, bez konieczności przytrzymywania palca. Dodano też zabezpieczenie przed przypadkowym „przeciekaniem” dotknięcia po wyjściu z wygaszacza.

### Dryf obrazu panelu RGB + błąd kompilacji (PR #8)
- **Naprawiło:** okresowe pionowe przesunięcie całego obrazu („jak na rolce”) — dodano cykliczny restart DMA panelu przy VSYNC (programowy odpowiednik `CONFIG_LCD_RGB_RESTART_IN_VSYNC`), wywoływany profilaktycznie i po zapisie do LittleFS. Naprawiono także błąd kompilacji wynikający z kolejności prototypów w pliku `.ino`.

### Wydajność: Telegram, CSV, hardening (PR #7)
- **Zmieniło:** wysyłkę Telegrama przeniesiono do osobnego zadania FreeRTOS — nie blokuje już interfejsu ani dotyku. Zredukowano wielokrotne skany pliku CSV do jednego przebiegu.
- **Dodało:** miękką rotację pliku danych (archiwum po przekroczeniu progu), rezerwację bufora odpowiedzi API, spójny dostęp do danych pogody przez mutex, walidację czasu zdarzeń i weryfikację zapisu do pliku.

### Bootscreen — poprawka napisu (PR #6)
- **Zmieniło:** napis imienia na ekranie startowym z „ALEKSANDRA” na „ALEKSANDER”.

### Artefakty RGB + responsywność (PR #5)
- **Naprawiło:** minimalne poziome linie po lewej stronie ekranu — zwiększono bufor bounce DMA panelu RGB. Wstępnie poprawiono też częstotliwość odczytu dotyku (pełne rozwiązanie w PR #9).

## Źródła

[1] [Waveshare — ESP32-S3-Touch-LCD-4B Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B)

[2] [LVGL 9.3 — Arduino: konfiguracja lv_conf.h](https://lvgl.io/docs/open/9.3/details/integration/framework/arduino)
