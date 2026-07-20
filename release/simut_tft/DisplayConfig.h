/**
 * @file display/DisplayConfig.h
 * @brief Compile-time display feature flags — delegates to simut_config.h.
 *
 * All user-configurable display options are now centralized in
 * `src/simut_config.h`. This header is kept for backward compatibility:
 * existing code that includes DisplayConfig.h continues to work.
 *
 * To customize your display:
 *   1. Edit src/simut_config.h
 *   2. Or override via platformio.ini build_flags (e.g. -DSIMUT_DISPLAY_TFT=0)
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */
#pragma once

#include "simut_config.h"
