/**
 * @file UiTouchChain.h
 * @brief The Ângulo's touch: the point lands on the frame on the glass. Included INSIDE handleTouch( ) by src/ui/UiDriverTouch.h,
 *        after the early gates, where the original runs its mode chain; x and y are its locals. uiTouch( ) (DisplayManager_UiA.cpp)
 *        takes the touch whatever the mode: the one screen that still needed the old chain, the update's, calls its action itself.
 *
 * Not self-contained and no include guard: it is a fragment of a function body.
 */
uiTouch( x, y );
