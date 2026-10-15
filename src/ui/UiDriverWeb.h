/**
 * @file UiDriverWeb.h
 * @brief The UI driver's routes in class WebManager. Included INSIDE the class (src/WebManager.h), where the
 *        per-feature route declarations sit.
 *
 * No include guard, for the reason UiDriverMembers.h has none. The original has no route of its own: its hook is
 * empty and always_inline, so the call in WebManager_Core.cpp leaves nothing in the image.
 */
#include "UiCaps.h"

#if SIMUT_UI_ANGULO
#include "angulo/WebMembers.h"
#else
	UI_HOOK void registerUiDriverRoutes( ) { }
#endif
