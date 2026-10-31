/**
 * @file UiDriverMembers.h
 * @brief The UI driver's members of class DisplayManager. Included INSIDE the class, as the last line before
 *        `};` (src/DisplayManager.h), so what the Ângulo declares there moves no other member.
 *
 * No include guard: the one include sits in a class body, and a second would declare every member twice.
 * The Ângulo's types arrive before the class, from src/ui/UiDriver.h. The original has no members of its own.
 */
#include "UiCaps.h"

#if SIMUT_UI_ANGULO
#include "angulo/Members.h"
#else
/* uiLangLoaded( ) is the hook the core calls once, from outside the class (AppManager_Boot.cpp, right after the language pack is loaded), so it is public; the
 * include sits in the private part, hence the labels. The original has nothing to do there: its body is empty and always_inline, the pattern of
 * registerUiDriverRoutes( ) (UiDriverWeb.h), so the call leaves nothing in the image and the LCD and headless products, whose translation units never include
 * UiDriverHooks.h, still see a body. It returns an int because the Angulo reports a refusal through it and the core logs it (AppManager_Boot.cpp): the original always
 * returns 0, the test on it folds away, and no log call is left in its image. The Angulo declares its own in angulo/Members.h (S10c, B2b). */
public:
	UI_HOOK int uiLangLoaded( ) { return 0; }   /* the language pack has finished loading at boot; 0 = nothing to report */
	/* uiKeypadTargets( ) is the hook GET /api/keypad asks (WebManager::handleApiKeypad( ), Core 0, another unit, so it is public too): the route that lists where the keys
	 * of the PIN keypad are, for a panel whose geometry the route's grid cannot describe. nullptr = the route sends the grid (and the groups keyboard's popup), as it always
	 * did. always_inline and a constant, like the hook above, so the handler's other branch folds out of the original's images and they keep their bytes (S12, F4). The
	 * Angulo declares its own in angulo/Members.h. */
	UI_HOOK const char *uiKeypadTargets( ) const { return nullptr; }
private:
#endif
