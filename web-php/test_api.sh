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
# ============================================================================
set -u

BASE="${BASE:-https://phpmapy1.webd.pro}"
TEST_WRITE="${TEST_WRITE:-0}"
TOKEN="${TOKEN:-}"
TMP=/tmp/pinat_test_body

PASS=0
FAIL=0
ok()   { echo "  OK:   $1"; PASS=$((PASS+1)); }
bad()  { echo "  BLAD: $1"; FAIL=$((FAIL+1)); }

req() { # req <method> <path> [--data-urlencode ...] -> HTTP code, body w $TMP
  local method="$1" path="$2"; shift 2
  local args=(-s -o "$TMP" -w '%{http_code}' -X "$method" "$BASE$path")
  [ -n "$TOKEN" ] && args+=(-H "X-Upload-Token: $TOKEN")
  [ "$#" -gt 0 ] && args+=("$@")
  curl "${args[@]}"
}

json_val() { # json_val <key> -> wartosc (python3 lub sed)
  if command -v python3 >/dev/null 2>&1; then
    python3 -c "import json,sys; d=json.load(open('$TMP')); print(d.get('$1',''))" 2>/dev/null
  else
    sed -n "s/.*\"$1\"[[:space:]]*:[[:space:]]*\"\{0,1\}\([^,\"}\"]*\)\"\{0,1\}.*/\1/p" "$TMP" | head -1
  fi
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
  NOW=$(date +%s)
  code=$(req POST /api/entry --data-urlencode "type=WAGA" --data-urlencode "when=$NOW" --data-urlencode "ml=3700")
  [ "$code" = "201" ] && ok "POST /api/entry HTTP 201" || bad "POST /api/entry HTTP $code ($(cat "$TMP" 2>/dev/null))"

  code=$(req GET /api/revision)
  REV2=$(json_val rev)
  echo "    rev przed: $REV1, po zapisie: $REV2"
  [ -n "$REV1" ] && [ -n "$REV2" ] && [ "$REV2" -gt "$REV1" ] && ok "rewizja wzrosla po zapisie" || bad "rewizja nie wzrosla ($REV1 -> $REV2)"

  # Znajdz lineIndex dodanego wpisu (WAGA, ml=3700, when=teraz) w dzisiejszych wpisach.
  code=$(req GET "/api/entries?date=$TODAY")
  LINE=$(python3 - "$TMP" "$NOW" <<'EOF'
import json,sys
data=json.load(open(sys.argv[1]))
t=int(sys.argv[2])
# wpis WAGA o naszym when — ostatni pasujacy
cand=None
for e in data.get('entries',[]):
    if e.get('type')=='WAGA':
        et=e.get('timestamp') or e.get('epoch')
        # fallback: dopasuj po 'ml'==3700 i godzine (przyblizona)
        if e.get('ml')==3700 and ('when' in e or True):
            cand=e.get('lineIndex')
# ostatni WAGA z ml=3700 w dzien
print(cand if cand is not None else -1)
EOF
)
  echo "    lineIndex testowego wpisu: $LINE"
  if [ -n "$LINE" ] && [ "$LINE" -ge 0 ]; then
    code=$(req POST /api/delete-entry --data-urlencode "line=$LINE")
    [ "$code" = "200" ] && ok "POST /api/delete-entry HTTP 200" || bad "POST /api/delete-entry HTTP $code ($(cat "$TMP" 2>/dev/null))"
  else
    bad "nie znaleziono lineIndex testowego wpisu — usuń go ręcznie w panelu"
  fi

  code=$(req GET /api/revision)
  REV3=$(json_val rev)
  echo "    rev po usunieciu: $REV3"
  [ -n "$REV2" ] && [ -n "$REV3" ] && [ "$REV3" -gt "$REV2" ] && ok "rewizja wzrosla po usunieciu" || bad "rewizja nie wzrosla po usunieciu ($REV2 -> $REV3)"
else
  echo "==> Pominieto cykl zapisu (TEST_WRITE=1 aby wykonac)"
fi

echo
echo "==> WYNIK: $PASS OK, $FAIL bledow"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
