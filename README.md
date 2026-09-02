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

Program podczas pierwszego uruchomienia tworzy w LittleFS plik `/karmienia.csv` z nagłówkiem `data,godzina,typ,ml`. Każde następne użycie przycisku **ZAPISZ** dopisuje nowy wiersz; poprzednie rekordy nie są nadpisywane.

| Element CSV | Przykład |
|---|---|
| Nagłówek | `data,godzina,typ,ml` |
| Wpis karmienia | `2026-08-18,11:45,KARMIENIE,75` |
| Wpis mleka | `2026-08-18,12:30,MLEKO,90` |

Dane pozostają w wewnętrznej pamięci Flash również po zwykłym wyłączeniu urządzenia. Należy jednak zachować ostrożność przy wgrywaniu nowego szkicu lub zmianie ustawień partycji Flash: operacja wymazania Flash może usunąć historię. Obecna wersja wyświetla ostatni wpis każdego typu na ekranie; eksport całego CSV do komputera można dodać w następnym kroku, na przykład przez Wi‑Fi lub port USB.

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

**Ekran główny** — pasek górny z tytułem, diodami statusu (`W` Wi‑Fi, `P` Pamięć, `C` Czas; zielona = OK, czerwona = błąd) i zegarem. Poniżej: wskazówka rozwojowa dnia (lewa) oraz wiek Aleksandra (prawa). Pod nimi pasek **OSTATNIE KARMIENIE: X temu** — zielony <3 h, żółty 3–4 h, czerwony ≥4 h od karmienia. Dalej karty ostatniego karmienia i butelki oraz przyciski: **KARMIENIE** (formularz), **PIELUCHA**, **ODCIAG POKARMU**, **KALENDARZ**, **PODSUMOWANIE**.

**Formularz KARMIENIE** — czas z zegara urządzenia przesuwany o ±5 min, minuty piersi LEWA/PRAWA krok po 5 (0–90), a po rozwinięciu SZCZEGÓŁY: rodzaj mleka i ilość suwakiem 10–120 ml. ZAPISZ dopisuje wpis do CSV.

**PODSUMOWANIE** — dziś + wczoraj z każdym karmieniem osobno (godzina, typ, minuty L/P lub ml); przycisk „+ WCZYTAJ STARSZE DNI" dokłada historię po 7 dni. Na pasku: **COFNIJ** (dwuetapowe usunięcie ostatniego wpisu) i **WIT.D** (dawka dzienna).

Pełny opis ekranów i formatu danych znajduje się w `PROJECT_DESIGN.md`.

## Funkcje dodatkowe

| Funkcja | Opis |
|---|---|
| Panel WWW | Kopie wszystkich widoków jako popupy; eksport CSV (`/export.csv`); polling co 10 s |
| Motyw nocny | 21:00–7:00 ciemna paleta + przyciemnione podświetlenie (na urządzeniu i w WWW) |
| Cofnij | Bezpieczne, atomowe usunięcie ostatniego wpisu (urządzenie i WWW) |
| Backup | Automatyczna dzienna kopia `/karmienia_backup.csv` |
| Telegram | Powiadomienia o wpisach do drugiego rodzica — uzupełnij `TELEGRAM_BOT_TOKEN` i `TELEGRAM_CHAT_ID` w `config.h` (puste = wyłączone) |
| OTA | Wgrywanie szkicu przez Wi‑Fi — ustaw `OTA_PASSWORD` w `config.h` (puste = wyłączone) |
| mDNS | `http://karmienie.local` |
| NTP | Re-synchronizacja zegara co 6 h |
| Wygaszacz | Po 2 min bezczynności: duży zegar, data, ostatnie karmienie (kolor), pogoda z Open-Meteo z ikoną, opisem, min/max, 3h prognozą i poradą ubioru |
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

## Źródła

[1] [Waveshare — ESP32-S3-Touch-LCD-4B Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4B)

[2] [LVGL 9.3 — Arduino: konfiguracja lv_conf.h](https://lvgl.io/docs/open/9.3/details/integration/framework/arduino)
