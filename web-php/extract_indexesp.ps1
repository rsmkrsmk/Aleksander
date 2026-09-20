# ============================================================================
#  extract_indexesp.ps1 — generuje ui/indexesp.html (kopia strony urządzenia)
#  z web_panel.h (WEB_APP_HTML) z usunięciem elementów sprzętowych:
#    • sysinfo (RAM/PSRAM/CPU/uptime)
#    • cała sekcja DIAGNOSTYKA (przycisk + karta)
#    • wywołania renderDiag/setText('sysinfo',...) w JS
#
#  Uruchomienie:  powershell -File web-php/extract_indexesp.ps1
#  (z katalogu repo v3). Po każdej zmianie strony urządzenia (web_panel.h)
#  uruchom ten skrypt, aby zsynchronizować kopię na hostingu.
# ============================================================================
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourcePath = Join-Path (Join-Path $scriptDir '..') 'web_panel.h'
$outPath = Join-Path (Join-Path $scriptDir 'ui') 'indexesp.html'

$sourcePath = (Resolve-Path $sourcePath).Path
Write-Host "Zrodlo:  $sourcePath"
Write-Host "Wynik:   $outPath"

$raw = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8

# Wyciągnij zawartość raw stringa R"WEBPANEL( ... )WEBPANEL"
$open = $raw.indexOf('R"WEBPANEL(')
$close = $raw.indexOf(')WEBPANEL"', $open)
if ($open -lt 0 -or $close -lt 0) {
    Write-Error 'Nie znaleziono R"WEBPANEL( ... )WEBPANEL" w web_panel.h.'
    exit 1
}
$htmlStart = $open + 11   # po 'R"WEBPANEL('
$html = $raw.Substring($htmlStart, $close - $htmlStart)

# --- Usuniecia elementów sprzętowych ---
# 1. Pasek sysinfo (HTML)
$html = $html.Replace('<p class="sysinfo" id="sysinfo"></p>', '')

# 2. Przycisk DIAGNOSTYKA (HTML)
$html = $html.Replace('<button type="button" class="small muted" data-action="toggle-diag">DIAGNOSTYKA</button>', '')

# 3. Karta DIAGNOSTYKA (HTML) — usuwamy ATOMOWO cały blok od <section ... diagCard>
#    do jego zamykającego </section>. NIE używamy Replace('</section>',''), bo to
#    usunęłoby WSZYSTKIE sekcje w dokumencie i rozsypało układ.
$diagStart = $html.indexOf('<section class="card diag-card hidden" id="diagCard">')
if ($diagStart -ge 0) {
    $diagEnd = $html.indexOf('</section>', $diagStart)
    if ($diagEnd -ge 0) {
        $html = $html.Substring(0, $diagStart) + $html.Substring($diagEnd + 10)   # +10 za '</section>'
    }
}

# 4. JS: blok sysinfo (const heapPct ... setText('sysinfo',...);) — usuwamy fragment od "const heapPct=" do ";renderNextFeed("
$needleStart = 'const heapPct='
$needleEnd = ';renderNextFeed('
$i1 = $html.indexOf($needleStart)
if ($i1 -ge 0) {
    $i2 = $html.indexOf($needleEnd, $i1)
    if ($i2 -ge 0) {
        $html = $html.Substring(0, $i1) + $html.Substring($i2 + 1)   # zostawiamy ";renderNextFeed("
    }
}

# 5. JS: renderDiag(data); (wywołanie w render()) — usuwamy sam fragment
$html = $html.Replace('renderDiag(data);', '')

# 5b. JS: handler akcji 'toggle-diag' (przycisk usuniety; $('diagCard') juz nie istnieje)
$tdStart = $html.indexOf("if(action==='toggle-diag')")
if ($tdStart -ge 0) {
    $tdEnd = $html.indexOf('}if(action==', $tdStart)
    if ($tdEnd -ge 0) {
        $html = $html.Substring(0, $tdStart) + $html.Substring($tdEnd + 1)
    }
}

# 6. Usuń puste linie, które mogły powstać po wycięciu karty diag (linie z samym białym znakiem między sekcjami)
$linesOut = New-Object System.Collections.Generic.List[string]
foreach ($line in ($html -split "`r?`n")) {
    # pomiń całkowicie puste linie (pozostawiamy strukturę HTML, tylko czyste wiersze)
    if ($line.Trim().Length -eq 0) { continue }
    $linesOut.Add($line)
}
$html = [string]::Join("`n", $linesOut) + "`n"

# Zapis UTF-8 bez BOM (System.IO.File to .NET API dostepne w PowerShell 5.1).
$bytes = [System.Text.Encoding]::UTF8.GetBytes($html)
[System.IO.File]::WriteAllBytes($outPath, $bytes)
Write-Host "Zapisano $($bytes.Length) B (UTF-8 bez BOM)."

Write-Host "OK: wygenerowano $outPath."