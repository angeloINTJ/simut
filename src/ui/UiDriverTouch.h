/**
 * @file UiDriverTouch.h
 * @brief The UI driver's part of handleTouch( ) (src/DisplayManager_Touch.cpp): what a touch does once the early gates (calibration,
 *        sensitivity, the web client's hold) have let it through. Included INSIDE the function body, where the original's mode chain stood,
 *        so the original's handleTouch( ) is the same token sequence it was and the Ângulo's ends in one call. The fifth selection header
 *        (tools/check_ui_boundary.py: SELECTION_HEADERS).
 *
 * No include guard, for the reason UiDriverMembers.h has none: the one include sits in a function body, and the fragments it picks name
 * handleTouch( )'s locals.
 */
#include "UiCaps.h"

#if SIMUT_UI_ANGULO
#include "angulo/UiTouchChain.h"
#else
#include "original/UiTouchChain.h"
#endif
