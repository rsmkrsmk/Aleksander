#!/usr/bin/env bash
# ============================================================================
# TEST API HOSTINGU (v4)
# Weryfikuje kontrakt API, ktorego uzywa urzadzenie (polling rewizji + mirror)
# oraz strony WWW. Uruchom na maszynie z curl (i najlepiej python3).
#
# Sposob uruchomienia:
#   ./test_api.sh                                  # testy tylko-odczyt
#   TEST_WRITE=1 ./test_api.sh                     # + cykl zapisu/usuniecia
#   BASE="https://panel.twojadomena.pl" ./test_api.sh
#
# TEST_WRITE=1 dopisuje testowy wpis (typ WAGA, ml=3700, "when"=teraz), a nastepnie
# USUWA go po weryfikacji, wiec dane produkcyjne nie sa trwale zmieniane.
# TOKEN — opcjonalny naglowek X-Upload-Token dla endpointow zapisu.
# UWAGA: -k (insecure) odpowiada setInsecure() na urzadzeniu — hosting webd.pro
# ma certyfikat, ktoremu Windows Schannel nie ufa, a urzadzenie i tak nie weryfikuje.
# ============================================================================
set -u

BASE="${BASE:-https://phpmapy1.webd.pro}"
TEST_WRITE="${TEST_WRITE:-0}"
TOKEN="${TOKEN:-}"
TMP=/tmp/pinat_test_body
INSECURE="${INSECURE:--k}"   # -k = nie weryfikuj certyfikatu (jak urzadzenie)

PASS=0
FAIL=0
ok()   { echo "  OK:   $1"; PASS=$((PASS+1)); }
bad()  { echo "  BLAD: $1"; FAIL=$((FAIL+1)); }

req() { # req <method> <path> [--data-urlencode ...] -> HTTP code, body w $TMP
  local method="$1" path="$2"; shift 2
  local args=(-s -o "$TMP" -w '%{http_code}' -X "$method" $INSECURE "$BASE$path")
  [ -n "$TOKEN" ] && args+=(-H "X-Upload-Token: $TOKEN")
  [ "$#" -gt 0 ] && args+=("$@")
  curl "${args[@]}"
}

json_val() { # json_val <key> -> wartosc (python3 z fallbackiem na sed)
  if command -v python3 >/dev/null 2>&1; then
    local r
    r=$(python3 -c "import json,sys; d=json.load(open('$TMP')); print(d.get('$1',''))" 2>/dev/null)
    if [ -n "$r" ]; then echo "$r"; return; fi
  fi
  sed -n "s/.*\"$1\"[[:space:]]*:[[:space:]]*\"\{0,1\}\([^,\"}\"]*\)\"\{0,1\}.*/\1/p" "$TMP" | head -1
}

echo "==> Testy API (BASE=$BASE, TEST_WRITE=$TEST_WRITE)"

# --- 1. Rewizja (v4) --------------------------------------------------------
echo "==> /api/revision"
code=$(req GET /api/revision)
echo "    HTTP $code: $(cat "$TMP" 2>/dev/null)"
[ "$code" = "200" ] && ok "/api/revision HTTP 200" || bad "/api/revision HTTP $code"
grep -q '"rev"' "$TMP" 2>/dev/null && ok "ma 'rev'" || bad "brak 'rev'"
grep -q '"count"' "$TMP" 2>/dev/null && ok "ma 'count'" || bad "brak 'count'"
grep -q '"updatedAt"' "$TMP" 2>/dev/null && ok "ma 'updatedAt'" || bad "brak 'updatedAt'"
REV1=$(json_val rev)

# --- 2. Eksport CSV (mirror urzadzenia) -------------------------------------
echo "==> /api/export.csv"
code=$(req GET /api/export.csv)
bytes=$(wc -c < "$TMP" 2>/dev/null || echo 0)
echo "    HTTP $code, $bytes bajtow"
[ "$code" = "200" ] && ok "/api/export.csv HTTP 200" || bad "/api/export.csv HTTP $code"
head -1 "$TMP" 2>/dev/null | grep -q '^data,godzina,typ' && ok "naglowek CSV poprawny" || bad "zly naglowek CSV"

# --- 3. Status (strony WWW) -------------------------------------------------
echo "==> /api/status"
code=$(req GET /api/status)
[ "$code" = "200" ] && ok "/api/status HTTP 200" || bad "/api/status HTTP $code"
grep -q '"lastFeeding"' "$TMP" 2>/dev/null && ok "ma lastFeeding" || bad "brak lastFeeding"

# --- 4. Wpisy dnia -----------------------------------------------------------
echo "==> /api/entries"
TODAY=$(date +%F)
code=$(req GET "/api/entries?date=$TODAY")
[ "$code" = "200" ] && ok "/api/entries HTTP 200" || bad "/api/entries HTTP $code"

# --- 5. Cykl zapisu/usuniecia (tylko gdy TEST_WRITE=1) -----------------------
if [ "$TEST_WRITE" = "1" ]; then
  echo "==> Cykl zapisu/usuniecia"
  # 'when' w formacie YYYY-MM-DDTHH:MM (strefa Europy/Warszawy), jak przyjmuje API.
  WHEN=$(TZ='Europe/Warsaw' date +'%Y-%m-%dT%H:%M')
  code=$(req POST /api/entry --data-urlencode "type=WAGA" --data-urlencode "when=$WHEN" --data-urlencode "ml=3700")
  if [ "$code" = "201" ]; then
    ok "POST /api/entry HTTP 201 (when=$WHEN)"

    code=$(req GET /api/revision)
    REV2=$(json_val rev)
    echo "    rev przed: $REV1, po zapisie: $REV2"
    [ -n "$REV1" ] && [ -n "$REV2" ] && [ "$REV2" -gt "$REV1" ] && ok "rewizja wzrosla po zapisie" || bad "rewizja nie wzrosla ($REV1 -> $REV2)"

    # Znajdz lineIndex DOPISANEGO wpisu: WAGA o ml=3700 (znacznik testowy) o najwiekszym
    # lineIndex. UWAGA: usuwamy TYLKO wpis o ml=3700, nigdy inny (realne wagi sa inne).
    code=$(req GET "/api/entries?date=$TODAY")
    LINE=""
    if command -v python3 >/dev/null 2>&1 && python3 -c "import sys" 2>/dev/null; then
      LINE=$(python3 - "$TMP" <<'EOF'
import json,sys
data=json.load(open(sys.argv[1]))
cand=None
for e in data.get('entries',[]):
    if e.get('type')=='WAGA' and e.get('ml')==3700:
        cand=e.get('lineIndex')
print(cand if cand is not None else -1)
EOF
)
    else
      LINE=$(grep -oE '"type":"WAGA"[^}]*"ml":3700[^}]*"lineIndex":[0-9]+' "$TMP" | tail -1 | grep -oE '[0-9]+$')
      [ -z "$LINE" ] && LINE=-1
    fi
    echo "    lineIndex testowego wpisu: $LINE"
    if [ -n "$LINE" ] && [ "$LINE" -ge 0 ]; then
      code=$(req POST /api/delete-entry --data-urlencode "line=$LINE")
      [ "$code" = "200" ] && ok "POST /api/delete-entry HTTP 200" || bad "POST /api/delete-entry HTTP $code ($(cat "$TMP" 2>/dev/null))"
      # weryfikacja: wpis 3700 nie powinien juz istniec
      code=$(req GET "/api/entries?date=$TODAY")
      LEFT=$(grep -c '"ml":3700' "$TMP" 2>/dev/null)
      [ -z "$LEFT" ] && LEFT=0
      [ "$LEFT" = "0" ] && ok "testowy wpis usuniety z danych" || bad "testowy wpis 3700 pozostal ($LEFT)"
    else
      bad "nie znaleziono lineIndex testowego wpisu (ml=3700) — NIE usuwam zadnego innego wpisu"
    fi

    code=$(req GET /api/revision)
    REV3=$(json_val rev)
    echo "    rev po usunieciu: $REV3"
    [ -n "$REV2" ] && [ -n "$REV3" ] && [ "$REV3" -gt "$REV2" ] && ok "rewizja wzrosla po usunieciu" || bad "rewizja nie wzrosla po usunieciu ($REV2 -> $REV3)"
  else
    bad "POST /api/entry HTTP $code (when=$WHEN) — pomijam usuwanie"
  fi
else
  echo "==> Pominieto cykl zapisu (TEST_WRITE=1 aby wykonac)"
fi

echo
echo "==> WYNIK: $PASS OK, $FAIL bledow"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
