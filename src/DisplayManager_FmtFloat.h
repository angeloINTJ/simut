/**
 * @file DisplayManager_FmtFloat.h
 * @brief Helpers inline para formatar float em buffer (sem snprintf %f).
 * @details snprintf("%.1f"/"%.2f") puxa o vfprintf full do newlib (~9 KB)
 * para o link, e o earlephilhower toolchain não tem newlib-nano
 * disponível. fmtFloat1/2 fazem a conversão sem float-printf,
 * economizando KB de flash.
 *
 * Header inline para que múltiplos TUs (DisplayManager.cpp e
 * DisplayManager_Graph.cpp) possam usar sem duplicação por TU.
 *
 * REF-001 / F17 .
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

/** Formata float com 1 casa decimal em buffer. NaN → "--.-". */
inline char* fmtFloat1(char* buf, size_t size, float val) {
 if (isnan(val)) { snprintf(buf, size, "--.-"); return buf; }
 int neg = (val < 0.0f);
 if (neg) val = -val;
 int intPart = (int)val;
 int decPart = (int)((val - (float)intPart) * 10.0f + 0.5f);
 if (decPart >= 10) { intPart++; decPart = 0; }
 if (neg) snprintf(buf, size, "-%d.%d", intPart, decPart);
 else snprintf(buf, size, "%d.%d", intPart, decPart);
 return buf;
}

/** Formata float com 2 casas decimais em buffer. NaN → "--.--". */
inline char* fmtFloat2(char* buf, size_t size, float val) {
 if (isnan(val)) { snprintf(buf, size, "--.--"); return buf; }
 int neg = (val < 0.0f);
 if (neg) val = -val;
 int intPart = (int)val;
 int decPart = (int)((val - (float)intPart) * 100.0f + 0.5f);
 if (decPart >= 100) { intPart++; decPart = 0; }
 if (neg) snprintf(buf, size, "-%d.%02d", intPart, decPart);
 else snprintf(buf, size, "%d.%02d", intPart, decPart);
 return buf;
}
