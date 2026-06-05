/**
 * @file DisplayManager_FmtFloat.h
 * @brief Inline helpers to format float into buffer (without snprintf %f).
 * @details snprintf("%.1f"/"%.2f") pulls the full vfprintf from newlib (~9 KB)
 * into the link, and the earlephilhower toolchain has no newlib-nano
 * available. fmtFloat1/2 do the conversion without float-printf,
 * saving KB of flash.
 *
 * Inline header so multiple TUs (DisplayManager.cpp and
 * DisplayManager_Graph.cpp) can use it without per-TU duplication.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

/** Formats float with 1 decimal place into buffer. NaN -> "--.-". */
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

/** Formats float with 2 decimal places into buffer. NaN -> "--.--". */
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
