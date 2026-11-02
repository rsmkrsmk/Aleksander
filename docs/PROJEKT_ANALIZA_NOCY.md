# Projekt: rozszerzona analiza nocy z kamery

Status: **DO ZATWIERDZENIA** (2.11.2026). Nic z tego dokumentu nie jest jeszcze zakodowane.
Dotyczy trzech miejsc: serwer t620 (repo `wiewiornik`), hosting (`web-php/`), urządzenie ESP32 (`v5/`).

## 1. Cel i zasady

Z nagrania całej nocy wyciągnąć: kiedy dziecko zostało położone i po jakim czasie zasnęło, jak wygląda rytm nocy, jak długie są spokojne bloki, jak zmienia się to z tygodnia na tydzień, co na to wpływa (kąpiel, karmienia, temperatura), gdzie i jak się rusza oraz kiedy płacze.

Zasady (bez zmian względem ustaleń):
- **Bez haseł i kluczy** na tym etapie (zabezpieczenie dla całego portalu później).
- Zmiany tylko przez aktualizację repo; instrukcje krok po kroku.
- Zmiany graficzne tylko na nowoczesnej stronie (`index.html`); prosta (`indexesp.html`) bez zmian graficznych.
- To **ocena orientacyjna z ruchu i dźwięku**, nie pomiar faz snu i nie diagnoza medyczna. Strona to zaznacza.
- Obrazy i nagrania **nie wychodzą z t620** (na hosting idą liczby). Wyjątek: próbki dźwięku, patrz decyzja D2.

## 2. Podział odpowiedzialności

Jedna reguła: **t620 wydobywa cechy z obrazu i dźwięku (tylko tu są surowe dane), hosting liczy z nich wszystkie wskaźniki** (tu są dziennik, wiele nocy i łatwa zmiana algorytmu).

| Miejsce | Robi | Dlaczego tu |
|---|---|---|
| **t620** | cechy ruchu co 5 s (bez maski i z maską, środek ruchu, obszar), mapy ruchu w czasie, cechy dźwięku co 1 s, zdarzenia płaczu, próbki audio, temperatura z Open-Meteo, paczka nocy v2 | tylko tu jest obraz i dźwięk |
| **hosting** | zasypianie, rytm, bloki, trend tygodniowy, wpływ dnia, widoki; **przeliczanie wszystkich zapisanych nocy po zmianie progów** | tu jest dziennik (kąpiele, karmienia, drzemki) i historia |
| **urządzenie** | jedna linia podsumowania ostatniej nocy (opcjonalnie) | wyświetla tylko to, co poda hosting |

Skutek: zmiana progu algorytmu to wgranie jednego pliku PHP, a wszystkie dotychczasowe noce zostają przeliczone bez dotykania t620.

## 3. Przepływ danych

```
kamera ─ strumień dodatkowy ─► ruch1.py (v2) ─► /data/cechy/RRRR-MM-DD.jsonl.gz  (cechy co 5 s, całą dobę, 60 dni)
        │                                    └► /data/noce/<noc>/heat.json (mapy w czasie)
        └ strumień główny ─► kamera1 (segmenty 60 s, z dźwiękiem jeśli kamera go ma)
                                  └► dzwiek1.py (nowy) ─► /data/dzwiek/RRRR-MM-DD.jsonl.gz + próbki .ogg
rano, ~3 min po 6:00:  zapisywacz.py składa paczkę nocy v2  (okno 18:00–08:00)
        └► POST /api/sleep-night (multipart) ─► hosting: zapis surowy + przeliczenie wskaźników
hosting: /api/sleep-nights, /api/sleep-stats, /api/sleep-night?night=… ─► strona (wykresy) i /api/status ─► urządzenie
```

## 4. Format paczki nocy v2 (zgodny wstecz)

`"version": 2`. Hosting nadal przyjmuje v1 (wskaźniki z `timeline`, bez zasypiania i dźwięku). Pola v1 zostają bez zmian. Nowe:

| Pole | Treść |
|---|---|
| `window` | `{start_ts, end_ts}` okno cech: **18:00–08:00** (żeby złapać położenie przed 20:30) |
| `bin_s` | 5 |
| `feat` | tablice równoległe po `bin_s`: `m` (% obrazu zmienionego w masce łóżeczka ×10), `t` (% całego obrazu ×10), `c` (liczba klatek z ruchem 0–10), `cx`, `cy` (środek zmiany 0–255), `a` (obszar zmiany, % maski ×10), `z` (4 liczby na komórkę: udział stref A–D) |
| `light` | zdarzenia zmiany światła/IR (ignorowane jako ruch) |
| `heat` | `night`: mapa 80×45 (0–255, skala log), `slices`: mapy 40×23 co 60 min, `grid`: opis stref |
| `pose` | wyniki porównań spokojnych bloków: `[{t, score}]` (zmiany pozycji) |
| `audio` | `present` (czy kamera ma dźwięk), `level_1min` (dB nad tłem, co minutę), `events` (`start_ts`, `dur_s`, `class`, `level_db`, `score`, `sample`), `samples` (nazwy próbek) |
| `weather` | `night_min`, `night_mean`, `day_mean` (12–20), `source` |
| `calib` | wersje progów użytych przy wydobywaniu cech |

Rozmiar ok. 250–400 kB; limit odbioru podnosimy z 1 MB do 2 MB (sprawdzimy `post_max_size` hostingu).

## 5. Algorytmy

Wszystkie progi to **wartości startowe do kalibracji** na prawdziwych nocach (rozdział 10). Trzymane w jednym miejscu (`SleepNights.php`, tablica `TUNE`).

### 5.1 Położenie i czas zasypiania (punkt 1)

**Wykrycie położenia.** Dorosły przy łóżeczku daje ruch o dużej powierzchni i trwający dłużej niż drgnięcie dziecka.
1. `dorosły` = przedział, w którym `m` ≥ 6% lub `t` ≥ 12% (pomijając zdarzenia `light`) przez ≥ 10 s; przedziały dzielone przerwą ≤ 20 s łączymy.
2. **Położenie** = koniec takiego przedziału, po którym przez 3 minuty nie ma kolejnego ruchu dorosłego.
3. Każde położenie opisujemy: czas, źródło (`wykryte` / `dziennik`), a z dziennika: karmienie w ciągu 30 min przed nim → „po karmieniu”, wpis `SEN_START` w ±15 min → „potwierdzone dziennikiem”.
4. Brak wykrytego położenia, a jest `SEN_START` w oknie 18:00–23:00 → używamy wpisu z dziennika (pewność „z dziennika”).

**Zaśnięcie.** Minuta „aktywna” = `c` w tej minucie ≥ 4 (≥ 4 z 120 klatek z ruchem). Od położenia szukamy pierwszego okna **10 kolejnych minut**, w którym ≤ 2 minuty są aktywne i żadna nie ma dużego ruchu (`m` ≥ 6%). Początek tego okna = **moment zaśnięcia**; `czas zasypiania = zaśnięcie − położenie`.

**Warunek „cały czas ruch” (wymóg z punktu 1).** Jeżeli w ciągu **60 minut od położenia** nie ma takiego okna *oraz* ≥ 80% minut jest aktywnych, status = **`aktywny`** („sen aktywny, nie da się oszacować”); czasu nie liczymy i **noc nie trafia do średnich czasu zasypiania**. Pozostałe statusy: `ok`, `rodzic-przy-dziecku` (ruch dorosłego trwa dalej, liczymy od jego końca), `brak-położenia`, `niepewne` (mało danych: pokrycie < 60%).

**Wynik:** lista położeń nocy (wieczorne główne + po nocnych karmieniach), dla każdego: `placedAt`, `settledAt`, `minutes`, `status`, `source`. Klasyfikacja: < 10 min szybko, 10–30 normalnie, 30–60 długo.

### 5.2 Rytm nocy (punkt 2)
- Aktywność w oknach 5 min po wygładzeniu; **szczyty** = maksima lokalne ponad progiem, oddalone ≥ 25 min. Odstępy między szczytami → **typowy cykl** (mediana).
- Autokorelacja aktywności 10-minutowej dla opóźnień 30–120 min → długość cyklu i „siła” rytmu `r`. Gdy `r` < 0,25 piszemy „brak wyraźnego rytmu”.
- Pierwszy i ostatni ruch, godzina największej aktywności, rozkład aktywności na godziny.
- Widok zbiorczy „**kobierzec nocy**”: wiersze = noce, kolumny = 30-minutowe komórki, kolor = aktywność. Rytm i dryf pory pobudek widać od razu.

### 5.3 Najdłuższe spokojne bloki (punkt 3)
- Minuta spokojna: `c` ≤ 1. Pojedyncze aktywne minuty między ≥ 3 spokojnymi po obu stronach nie przerywają bloku (drgnięcia).
- Wynik: **3 najdłuższe bloki** (początek, długość), najdłuższy w **pierwszej** i w **drugiej** połowie snu (od zaśnięcia do ostatniej pobudki), liczba bloków ≥ 30 i ≥ 60 min, łączny spokój.
- Działa już na istniejących nocach (z `timeline`).

### 5.4 Trend tygodniowy (punkt 4)
- Wskaźniki na noc: spokój %, najdłuższy blok, mediana czasu zasypiania (tylko `ok`), liczba dłuższych epizodów, pierwsze przebudzenie, minuty płaczu, nocne karmienia (dziennik).
- Agregaty: tygodnie kalendarzowe i średnia krocząca 7 nocy; zmiana względem poprzedniego tygodnia strzałką (▲▼ tylko gdy różnica > próg istotności i ≥ 4 noce w obu tygodniach, inaczej „mało danych”).
- Zaznaczone „zakłócenia”: noc `brak-danych`, noc `aktywny`, choroba/szczepienie jeśli kiedyś dodamy znacznik (opcja, poza zakresem).

### 5.5 Wpływ dnia na noc (punkt 5)
Zmienne dnia: **kąpiel** (czy była i o której), **liczba karmień** (doba, wieczór 18–20:30, nocne), **suma i długość drzemek** (dziennik), **temperatura** zewnętrzna (średnia dzienna 12–20, minimum nocne). Zmienne wyniku: ilość snu (spokój %, łączne minuty snu), jakość (najdłuższy blok, czas zasypiania, epizody).

Metoda ostrożna, bo dziecko z wiekiem śpi coraz lepiej (zakłóca każdy wynik):
1. Z każdego wskaźnika odejmujemy trend wieku (średnia krocząca 14 nocy).
2. Dla zmiennych 0/1 (kąpiel) porównanie dwóch grup: różnica median + test permutacyjny, pokazujemy przedział niepewności.
3. Dla ciągłych (temperatura, liczba karmień) korelacja rangowa Spearmana.
4. **Pokazujemy dopiero od 10 nocy w mniejszej grupie**, do 20 nocy z napisem „wstępne”. Zawsze „zależność, nie przyczyna”.

Kąpiel jest co ok. 3 dni (`BATH_INTERVAL_DAYS`), więc grupa „kąpiel” zbierze się po ok. 4–5 tygodniach. Temperaturę dla starych nocy t620 pobierze jednorazowo z archiwum Open-Meteo (współrzędne jak na urządzeniu).

### 5.6 Mapa ruchu: gdzie, jak, kiedy (punkt 6 — główny nacisk)

Obecnie jest jedna suma na całą noc, bez czasu, bez rozróżnienia rodzaju ruchu. Rozbudowa w czterech warstwach:

**A. Mapy w czasie.** Mapa nocy 80×45 oraz mapy godzinowe 40×23 (od położenia do pobudki ok. 10 plasterków). Widok: suwak godziny + animacja „odtwórz noc”; przełącznik miary: *czas ruchu* / *intensywność*.

**B. Strefy łóżeczka (A–D).** Na stronie kamer (edytor stref, który już jest) dochodzi pędzel „strefa”: rysujesz 4 obszary łóżeczka i nadajesz im nazwy (np. *góra*, *środek*, *dół*, *brzeg*). Wynik: udział ruchu w każdej strefie, szereg czasowy ruchu w strefach, „gdzie dziecko spędza noc”. Nazwy anatomiczne (głowa/nogi) wpisujesz sam, jeśli ułożenie jest stałe; algorytm niczego anatomicznie nie rozpoznaje.

**C. Rodzaj ruchu** (z obszaru i czasu trwania; progi do kalibracji):
| Rodzaj | Cecha |
|---|---|
| drgnięcie | obszar < 0,5% maski, < 3 s |
| ruch | obszar 0,5–3%, 3–60 s |
| zmiana pozycji | obszar > 3% **i** zmiana obrazu spokojnego bloku (patrz D) |
| dorosły | patrz 5.1 |

**D. Zmiany pozycji.** Dla każdego spokojnego bloku ≥ 5 min t620 liczy uśredniony mały obraz (40×23, szary). Różnica między kolejnymi blokami w obrębie maski = `score`. Wysoki `score` = dziecko leży inaczej (obróciło się, przesunęło). Do hostingu idą **same liczby**, obrazy zostają na t620. To także jedyny sposób na wykrycie pustego łóżeczka (wzorzec „puste łóżeczko” zapisywany raz z poziomu strony kamer).

**E. Dodatkowo:** tor środka ruchu (`cx`,`cy`) w nocy, mapa „dziś względem średniej z 7 nocy” (różnica), liczba zmian pozycji na noc w trendzie.

### 5.7 Dźwięk i płacz (punkt 7)

**Warunek wstępny.** Trzeba sprawdzić, czy kamera Tenda CP7 w ogóle wysyła dźwięk w RTSP (D1). Nagrywanie idzie `-c copy`, więc dźwięk jest w plikach `.mkv`, jeśli kamera go ma.

**Przetwarzanie (usługa `dzwiek1.py`).** Po zamknięciu każdego segmentu 60 s w `/data/bufor` (starszego niż 70 s) wydobywa sam dźwięk (`ffmpeg -vn -ac 1 -ar 16000`) i liczy co 1 s. Nie otwiera dodatkowej sesji RTSP kamery, jest odporne na restarty, opóźnienie ≈ 1–2 min.

Cechy na sekundę: poziom dB ponad tło (tło = 10. percentyl z ostatnich 10 min), środek ciężkości widma, **harmoniczność** (autokorelacja w zakresie F0 250–700 Hz typowym dla płaczu niemowlęcia; głosy dorosłych mają F0 ok. 85–255 Hz).

| Klasa sekundy | Warunek startowy |
|---|---|
| cisza | < 6 dB nad tłem |
| dźwięk | ≥ 6 dB, niehamoniczny (szelest, kaszlnięcie, hałas) |
| głos | harmoniczny, cichy (kwilenie, gaworzenie) |
| płacz | harmoniczny, ≥ 12 dB nad tłem, ≥ 3 s z ≥ 60% sekund „głosowych” w oknie 8 s |

Zdarzenie płaczu: łączone przy przerwie ≤ 10 s; zapisujemy początek, czas, poziom, wynik pewności. Zdarzenia zbiegające się z ruchem dorosłego są oznaczone (to może być rozmowa).

**Próbki do osłuchania.** Do 3 na noc: najgłośniejszy płacz, najdłuższy płacz i jeden „głos/niepewny” do kontroli (10–15 s, ogg/opus, ok. 20–40 kB). Przy każdej przyciski **„to płacz / to nie płacz”**; etykiety zapisują się i służą do dostrojenia progów (ocena trafności w ustawieniach).

**Na wykresach:** pasek głośności pod paskiem ruchu, czerwone znaczniki płaczu (klik = próbka), na wykresie 14 nocy liczba minut płaczu, kafelek „płacz”.

**Rozszerzenie (faza późniejsza):** model YAMNet (klasa „Baby cry”) na t620 uruchamiany tylko dla fragmentów oznaczonych przez regułę; przydatność procesora (AMD GX-217GA) do sprawdzenia przed decyzją.

## 6. Hosting (`web-php/`)

| Zmiana | Szczegóły |
|---|---|
| `SleepNights.php` | przyjmuje v1 i v2, limit 2 MB; zapis surowy bez zmian; wskaźniki liczone **przy odczycie** z tablicy `TUNE` (cache w `index.json`, unieważniany zmianą `TUNE_VERSION`); nowe: `onset`, `rhythm`, `blocks`, `positions`, `zones`, `audio` |
| nowa klasa `SleepStats.php` | trend tygodniowy, kobierzec nocy, wpływ dnia (łączy noce z dziennikiem: kąpiele, karmienia, drzemki), korelacje |
| API | `GET /api/sleep-stats?days=60`; rozszerzone `GET /api/sleep-night?night=` (mapy, strefy, ruchy, zdarzenia dźwięku); `POST /api/sleep-audio-label` (etykiety „płacz/nie płacz”); `GET /api/sleep-audio?file=` (tylko jeśli D2 = tak); `GET /api/status` dostaje obiekt `night` (krótkie podsumowanie dla urządzenia) |
| `ui/index.html`, `ui/app.js` | nowe widoki (rozdział 7). Prosta strona bez zmian graficznych |
| `README.md` | nowe rozdziały, opis algorytmów i progów |

## 7. Widoki na stronie (nowoczesna, zakładka Sen)

1. **Noc** (istniejąca sekcja rozszerzona): karta „Zasypianie” (położono 20:41 → zasnął 21:03, 22 min, „normalnie”; albo „sen aktywny, nie da się oszacować”), pasek nocy z ruchem, głośnością i znacznikami płaczu, karmienia z dziennika, linie położeń.
2. **Mapa ruchu:** schemat łóżeczka z 4 strefami, mapa cieplna, suwak godziny/odtwarzanie, miara (czas/intensywność), porównanie z średnią 7 nocy, szereg ruchu w strefach.
3. **Rytm:** wykres aktywności z zaznaczonymi cyklami, kobierzec nocy (noce × czas).
4. **Trend:** tygodnie ze strzałkami zmian dla wskaźników z 5.4.
5. **Co wpływa:** karty „kąpiel / karmienia / drzemki / temperatura” z wynikiem lub „zbyt mało nocy (jest 6 z 10)”.
6. **Dźwięk:** lista zdarzeń nocy, odtwarzacz próbek z przyciskami etykiet.

Wygląd: spójny z obecnym (szkło, dzień/noc), kolory ocen jak dziś, ten sam sposób odświeżania co 5 min.

## 8. Urządzenie (`v5/`, wersja 5.2.0) — opcjonalne

Zmiana minimalna: `fetchStatusSettings()` czyta już `/api/status`; dochodzi parsowanie obiektu `night` (`rating`, `settleMin`, `settleStatus`, `quietPct`, `longestQuietMin`, `cryMin`) i **jedna linia na karcie Sen**: „Noc: zasnął po 22 min · spokój 91% · blok 3 h 10 min” z kolorową kropką oceny. Bez nowych zapytań sieciowych i bez wpływu na wyświetlanie obrazu. **Wymaga zgody na zmianę graficzną urządzenia (D3).** Brak zmian po stronie firmware jest równoważnie akceptowalny.

## 9. Wdrożenie na t620 (repo `wiewiornik`)

| Plik | Zmiana |
|---|---|
| `kamery/ruch1.py` | cechy 5 s całą dobę, mapy czasowe, strefy, wzorce spokojnych bloków i `score` pozycji, zdarzenia światła; dotychczasowe klipy i `heat.json` bez zmian |
| `kamery/dzwiek1.py` + `.service` | nowa usługa dźwięku (rozdział 5.7) |
| `kamery/zapisywacz.py` | paczka v2 (okno 18:00–08:00, `audio`, `weather`, `pose`), pobranie temperatury z Open-Meteo, endpointy `/noce/dzwiek/…` (próbki w sieci domowej), `/noce/wyslij` bez zmian |
| `kamery/index.html` | pędzel stref, zapis wzorca „puste łóżeczko”, przeglądarka próbek dźwięku |
| `kamery/wdroz-ruch.sh` | instalator: nowe pliki i usługa, katalogi `/data/cechy`, `/data/dzwiek` |
| `INSTRUKCJA.md`, `README.md` | kroki wdrożenia, kontrola, rozwiązywanie problemów |

Zużycie miejsca: cechy ok. 100 kB dziennie (gz, 60 dni ≈ 6 MB), próbki dźwięku ok. 100 kB na noc (30 dni). Obciążenie procesora: cechy to rozszerzenie istniejącej pętli (dodatek rzędu kilku %), dźwięk to dekodowanie samego audio co minutę (pomijalne).

## 10. Kalibracja i testy

- **Najpierw zbieranie.** Etap 1 (rozdział 11) wdrażamy jako pierwszy, bo cech z minionych nocy nie da się odtworzyć.
- **Prawda terenowa:** wpisy `SEN_START`/`SEN_STOP` i karmienia z dziennika; etykiety płaczu z próbek. Dla zasypiania liczymy zgodność wykrytych położeń z `SEN_START` (cel: ≥ 80% w ±10 min).
- **Strojenie progów:** po 3–5 nocach przesyłasz mi pliki nocy v2 (z t620 lub z hostingu), ja przestawiam `TUNE` i wgrywasz jeden plik PHP. Przeliczą się wszystkie noce.
- **Testy automatyczne** (jak dotąd): syntetyczne noce z wymuszonymi położeniami, aktywnym snem, płaczem; sprawdzenie granic progów, noce v1 i v2, noce bez dźwięku; zrzuty ekranów nowych widoków.
- **Kryteria odbioru:** patrz rozdział 11.

## 11. Etapy i kolejność

| Etap | Zakres | Gdzie | Kryterium odbioru |
|---|---|---|---|
| **0** | sprawdzenie, czy kamera ma dźwięk (2 polecenia) | t620 | wiemy: jest / nie ma |
| **1** | zbieranie cech v2 (ruch co 5 s, okno 18:00–08:00, mapy czasowe), paczka v2, odbiór v2 | t620, hosting | po nocy na stronie jest noc v2, stary wykres działa jak dotąd |
| **2** | bloki, rytm, kobierzec, trend tygodniowy (na dotychczasowych i nowych nocach) | hosting | widoki rysują się dla wszystkich nocy; ▲▼ tylko przy ≥ 4 nocach/tydzień |
| **3** | położenie i zasypianie (+ status „sen aktywny”) | hosting | karta „Zasypianie”; zgodność z `SEN_START` zmierzona na realnych nocach |
| **4** | mapy czasowe, strefy, rodzaje ruchu, zmiany pozycji | t620, hosting | suwak godziny, strefy, lista zmian pozycji |
| **5** | dźwięk i płacz + próbki + etykiety | t620, hosting | znaczniki płaczu na wykresach, 2–3 próbki do posłuchania |
| **6** | pogoda i wpływ dnia | t620, hosting | karty „co wpływa” (po 10+ nocach w grupie) |
| **7** | podsumowanie na urządzeniu (opcjonalnie) | firmware 5.2.0 | linia na karcie Sen |

Każdy etap kończy się aktualizacją repo i opisem krok po kroku (bez zip, bez „czytaj README”). Po etapach 1, 3 i 5 potrzebuję od Ciebie kilku nocy do kalibracji.

## 12. Ryzyka i ograniczenia

- **Detekcja dorosłego** po samej wielkości ruchu może pomylić się w obie strony (np. duży ruch dziecka vs. ręka w łóżeczku). Dlatego łączymy ją z dziennikiem i kalibrujemy; wynik ma pole „pewność”.
- **Czy dziecko w ogóle jest w łóżeczku** nie wynika z samego ruchu; pomaga wzorzec „puste łóżeczko” i dziennik.
- **Dźwięk:** reguła akustyczna będzie mieć fałszywe alarmy (rozmowy, telewizor, pies). Próbki z etykietami są po to, żeby to mierzyć i poprawiać; model YAMNet to druga linia.
- **Brak dźwięku w kamerze** wyklucza etap 5; wtedy rozważymy osobny mikrofon USB w kontenerze (osobne wdrożenie).
- **Wnioski z korelacji** przy kilkudziesięciu nocach są słabe i zakłócone wiekiem dziecka; dlatego progi liczby nocy i napisy „wstępne”.
- **Prywatność:** nagrania i obrazy zostają na t620. Próbki dźwięku na hostingu byłyby dostępne bez hasła, jak całe API (D2).
- Hosting: zwiększony limit 2 MB musi mieścić się w `post_max_size`/`upload_max_filesize` serwera (sprawdzimy w etapie 1).

## 13. Decyzje do zatwierdzenia

| # | Pytanie | Moja rekomendacja |
|---|---|---|
| **D1** | Wykonać etap 0 (czy kamera ma dźwięk) przed kodowaniem dźwięku | tak; dwa polecenia podam po zatwierdzeniu |
| **D2** | Próbki dźwięku: tylko na t620 (słuchasz w domu przez stronę kamer) czy także na hostingu bez hasła | **tylko t620**, a na hostingu dopiero po wdrożeniu haseł dla portalu; na wykresie hostingu znaczniki płaczu bez odtwarzania |
| **D3** | Linia „Noc: …” na karcie Sen urządzenia (firmware 5.2.0) | tak, na końcu (etap 7) |
| **D4** | Logowanie cech od 18:00 (a nie tylko 20:30–6:00) | tak, bo położenie często przed 20:30 |
| **D5** | Cztery strefy łóżeczka A–D rysowane przez Ciebie na stronie kamer | tak |
| **D6** | Kolejność etapów 0 → 1 → 2 → 3 → 4 → 5 → 6 → 7 | tak: najpierw zbieranie danych |
