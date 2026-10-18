# Wycina z v3.ino kod modelu danych UI (uiFillLive/uiFillCharts) do glue_extract.h,
# zeby uruchomic go w symulatorze PC (scen_fw.h) na wygenerowanych danych CSV.
import os
root = os.path.join(os.path.dirname(__file__), '..', '..')
s = open(os.path.join(root, 'v3.ino'), encoding='utf-8').read()
i = s.index('static UiModel uiModel;')
j = s.index('// ----------------------------- Ekran glowny (panel)')
open(os.path.join(os.path.dirname(__file__), 'glue_extract.h'), 'w', encoding='utf-8').write(s[i:j])
