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
#endif
