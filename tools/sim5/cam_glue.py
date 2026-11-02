#!/usr/bin/env python3
"""Wycina z v5/host_sync.cpp parser obiektu camDev (jsonNum, jsonStr, parseCamDev, hostCamDev) do cam_glue.h,
zeby symulator (scen_v5.h, -DSIM_CAM) i testy na PC uzywaly dokladnie tego samego kodu co firmware.
Uzycie: python3 cam_glue.py <repo>/v5/host_sync.cpp > cam_glue.h"""
import re, sys
s = open(sys.argv[1]).read()
def cut(start, end):
    a = s.index(start); b = s.index(end, a)
    return s[a:b]
out = ["// wygenerowane przez cam_glue.py — nie edytowac", "#include <string.h>", "#include <stdio.h>", "#include <stdlib.h>",
       "#define portENTER_CRITICAL(x) (void)0", "#define portEXIT_CRITICAL(x) (void)0", "static int camMux;"]
out.append(cut("static bool jsonNum(", "// \"camNight\""))
out.append(cut("// \"camDev\":{...}", "bool hostCamNight("))
print("\n".join(out))
