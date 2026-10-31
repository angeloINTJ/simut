# UI drivers: the audit and the contract

**State:** Living · **Measured on:** 2026-10-14, tree `192e1ad` (origin/main
after #273), branch `feat/ui-drivers` · **Question:** what has to be separated
so that the TFT panel's look can be chosen at build time, and how many bytes
does that put on the table?

This document is stage S0 of the plan that splits the panel's look into two
build-time UI drivers: **simut-ui-original**, the UI that ships today, and
**simut-ui-angulo**, a new one. It holds two things. The first is the contract
a driver has to meet (section 2). The second is the audit of the tree as it is
(section 3), measured so that later stages can lean on it and so that every
table can be produced again. Nothing under `src/` changes in S0.

Stage S0 is complete as of 2026-10-14: every section below is filled, and every
table in section 3 says how it was produced. In the commands, `$B` is the S0
scratch directory, `/home/angelo/Documentos/simut/scratchpad/ui_drivers_20261014`,
whose `tools/` holds the scripts (section 5), and `$BASE` is `$B/base_192e1ad`,
the nine baseline images. Three of the scripts are versioned in `tools/` since
S2b (2026-10-14), each with its row in `tools/README.md`: `bytes_original.py`
(the sums of section 3.6), and `build9.sh` and `elf_funcoes.py` (the plan's
kit), the last one proven by `tools/test_elf_funcoes.py`. The five readers of
sections 3.1, 3.3 and 3.5 (`ui_funcs.py`, `ui_writes.py`, `ui_constcheck.py`,
`ui_xref.py`, `ui_touch.py`) are still only in `$B/tools/`: the tables they
produced can be produced again where those scripts are, and
`bytes_original.py --src` needs the first of them beside it.

## 1. Words used here

- **Core** is the part of `DisplayManager` that stays whatever the look is:
  state, the Core 1 loop, touch acquisition, calibration math, the PIN and
  users logic, the events to Core 0, the mode machine, the timers that have a
  product meaning, and the language machinery.
- **Original** is the part that makes the panel look the way it does today:
  drawing, layout constants, hit-testing geometry, palette, fonts.
- **Mixed** is a function that holds both. Each one is listed with the line
  ranges of each part.
- **Immediate draw** is a call to a `draw*`, `redraw*` or `blit*` function from
  inside a touch branch, at the point where the touch changed something.

## 2. The contract

A UI driver is the set of `DisplayManager` member-function definitions that give
the TFT panel its look, chosen when the image is built; the core is everything
else. This is the plan's section 3 written as what a driver provides and what it
must honour. What the S0 audit found that the plan lacks is marked **(S0)**, and
section 4 lists where the code and the plan disagree. Line numbers are those of
`192e1ad`; they move as the stages land.

### 2.1 Selection, files, and what stays identical

The driver is chosen like the network driver: `ui = "<id>"` in
`tools/features.toml`; an identity macro in `src/ui/UiCaps.h`, written like
`src/net/NetCaps.h:76-92` (a default, an `#error` unless exactly one driver is
named, an `#error` if a derived macro is passed with `-D`); and the units of
every other driver excluded by the generator, as `gen_features.py:241-248` does
for the network. A driver is a set of member-function definitions in its own
units, which is the house precedent: `DisplayManager_Alpha.cpp` and
`DisplayManager_None.cpp` define the same class (`features.toml:62-95`). The
interface is a fixed list of named hooks declared once in `DisplayManager.h`
and defined by exactly one driver, so the linker enforces it: none gives an
undefined reference, two give a multiple definition.

| file | what it is |
|---|---|
| `src/ui/UiCaps.h` | no includes, no code: `SIMUT_UI_ANGULO` defaults to 0, `SIMUT_UI_ORIGINAL` is its negation, `#error` unless exactly one is 1, `#error` if a capability is passed with `-D`; `SIMUT_UI_CUSTOM_THEMES` equals original |
| `src/simut_config.h` | includes it, and `#error "SIMUT_UI_ANGULO needs SIMUT_DISPLAY_TFT"` |
| `src/ui/UiDriver.h` | this contract in code, and the driver selection; the only file outside a driver that may test the identity macro |
| `src/ui/UiDriverMembers.h` | the last line of the class body: empty for the original (the class layout does not move), the Ângulo's members for the Ângulo |
| `src/DisplayManager_Actions.h`, `src/DisplayManager_Ticks.h`, `src/display/TouchCal.h` | the core actions and ticks (`always_inline`), and the pure calibration and sensitivity math with native tests |
| `src/ui/original/`, `src/ui/angulo/` | the original's 14 units, its inline hook headers (`UiOriginal.h`, `UiTouchChain.h`, `PinKeypadLayout.h`) and its three font tables, all moved there in S11 (2026-10-30); the Ângulo's glue, screens and kit (fonts, icons, tokens) |

The original keeps its bytes. Its hooks, and the core actions pulled out of
`handleTouch`, are `always_inline` functions whose bodies are the existing code
moved word for word, so they inline back into the same unit at the same place.
The Ângulo's hooks are ordinary out-of-line functions, and its touch handler
calls core actions instead of moving the touch onto the old layout. Four other
shapes were rejected: the study's (`UA_DESVIO` plus touch translation; section
3.6 measures it: it does not link the old draw code, it links the translation
and the mixed functions), a virtual `UiDriver` class (no original function would
stay the same, so no sha256 or ELF proof, and it costs flash), two copies of
`DisplayManager` (it forks the Core 1 park, quiet and flash-pause protocol, the
PIN lockout ladder and the calibration math), and moving the original into
`src/ui/original/` first (link order alone changes the bytes,
`features.toml:139-146`).

### 2.2 The hooks

All are private `DisplayManager` members, declared without `#if`, except `uiKeypadTargets( )`
(public: its caller is the web handler, another class; S12). The right
column is where the original's body comes from on `192e1ad`.

| hook | the core calls it | original: body from |
|---|---|---|
| `uiBegin( )` | once, when Core 1 starts or restarts, before the loop | `DisplayManager.cpp:1287-1289`: clear the glass, draw the fixed chrome, reset the render cache **S4 (C3):** the first two lines are the body, in `ui/original/UiOriginal.h`; the cache reset stays in the core after the call. The Ângulo's marks its frame whole |
| `uiThemeChanged( )` | `_themeChanged` was raised | `:1400-1449`. The `_uiMode = MODE_DASHBOARD` at `:1439` is a mode change **(S0)**; **S4 (C4) kept it inside the hook**, in the branch that draws, so the block moved as it was (duty 12). The Ângulo's is its old `uaTemaAplicado`  |
| `uiPass( snap )` | once per loop pass, after the touch read and the OTA tick | the dispatch `:1451-1555` and `:1559-1700`, calling the core ticks where the old code had logic (section 2.4, duty 9) **S4 (C8):** the dispatch from the dashboard branch to `SET_CLOCK` (149 lines) is the body, calling the ticks where the old code had logic. The Ângulo's runs the ticks of the mode, takes the six repaint requests and calls `uaQuadro( )` (section 2.7) |
| `uiPaintOta( )` | the update screen must be on the glass, from either core | `drawOtaScreen`, `DisplayManager_Ota.cpp:150-226` **S4 (C2):** the body is the one call `drawOtaScreen( )`, which stays out of line. The Ângulo's is its old `uaOta` |
| `uiTouch( x, y )` | a touch passed the core's gates | the mode chain of `handleTouch`, `Touch.cpp:398-1682`, and `handleTouchPanelV24`, `Users.cpp:1304-1686` **S10a (D6):** not taken; the chain stayed in `handleTouch( )`. **S10b (C3), a recorded deviation from the plan's section 3.2: the original's touch is a text, not a function.** The chain is the fragment `src/ui/original/UiTouchChain.h`, included inside `handleTouch( )` by the fifth selection header `ui/UiDriverTouch.h`, so that `handleTouch( )` keeps its token sequence and the nine images their sha256 (probe P1, section 2.15). The Ângulo's `void uiTouch( int16_t x, int16_t y )` (declared in `angulo/Members.h`, defined in `DisplayManager_UiA.cpp`) is an ordinary function, and its fragment is the one statement that calls it. `handleTouchPanelV24( )` stays in `Users.cpp`, the original's, and leaves the Ângulo image with the chain |
| `uiTouchUp( )` | the finger left the glass | `Touch.cpp:38-51` (the dashboard's short tap) |
| `uiChanged( kind, arg )` | a core action changed what is on screen | a `switch` over the `UiChange` kinds of section 3.5; for the original a constant `kind` folds it into the old call **S4 (C6):** begun with `ALARM_FLASH` and `ALARM_RESTORE`; the switch folds, and the Ângulo marks its frame. The other kinds come with S5 to S10a **S10c (D3a):** `TOUCH_CAL_ENTER` is the first kind raised on **Core 0**, by `showTouchCalibration( )` before it publishes the mode (the others come from the core's touch actions and ticks, on Core 1): the original answers it with `resetTouchCalCaches( )`, the seven stores of its calibration draw caches, and the Ângulo marks its frame, which is redundant (the same entry raises `_forceSettingsRedraw` and `_repaintSettings`) and harmless. A driver's answer to a kind raised on Core 0 must not draw: the glass belongs to Core 1. **S10c (D3b):** `DISPLAY_OFFSET_ENTER` is the second, raised on Core 0 by `showSettingsDisplayOffset( )` **inside `_stateMutex`**: the original answers it by setting the two sentinels that force the numeric redraw of its offset screen (`_lastOffsetDrawX` and `_lastOffsetDrawY`), and the Ângulo marks its frame (redundant and harmless, as for the first). A driver's answer to it must not take `_stateMutex`, because it is held |
| `uiTopPanel( snap )` | the top card is to be shown (`redrawTopPanel( )`, after it synced the slot and mirrored the data) | **S5 (C2):** the two calls `redrawTopPanel( )` ended with, `drawSlotPanel` of the top card and `drawBottomButtons`; the Ângulo's is `uaSujo( )` |
| `uiBootSkipHit( x, y )` | a touch while the device boots with the skip button showing | **S5 (C7):** `Touch.cpp:364-365`, `y > 190 && x > 80 && x < 240`; the Ângulo asks the boot frame whether the point is on its `BOOT_PULAR` target, and only when the frame on the glass is the boot's (`s_modo` is the dashboard's) |
| `uiSensCancelHit( x, y )` | a touch on the sensitivity screen with enough pressure (`p.z >= 50`), or an injected one (`simNow`: main's #280, 2026-10-16) | **S8b (C7):** `Touch.cpp:199`, `y > 195 && x < 125`; the Ângulo asks the frame on the glass for `SENSIB_CANCELAR`, behind a test of `s_modo` |
| `uiCalVerdictHit( x, y )` | a touch on the calibration once a verdict is on the screen | **S8b (C7):** `Touch.cpp:312`, `y >= 185` (the x is not part of it); the Ângulo asks the frame for `CALIB_BOTAO`, behind a test of `s_modo` |
| `uiPages( list, items )` **(S0)** | the core must bound a page index | the arithmetic now spread over six draw bodies, `(items + 3) / 4`, the License text's wrap count, `buildDashLayout`'s button strip |
| `uiThemeCount( )`, `uiThemeId( i )`, `uiThemeName( i )` | the catalogue is read | `availableThemes` and the custom `.thm` list, `Themes.cpp:433-487` **S10b (C4a), a recorded deviation from the plan's section 3.2: the catalogue is a link-time interface and these three hooks do not exist.** The six functions the core already calls (`getThemeCount`, `getThemeId`, `getThemeIndexByName`, `getThemePalette`, `loadTheme`, `scanCustomThemes`) keep their names, signatures and call sites, are declared in `src/ThemeCatalog.h`, and each driver defines them: the original in `Themes.cpp`, the Ângulo in `ui/angulo/Temas.cpp` over its three rows. The linker enforces one definition, as it does for a hook. A `uint8_t` parameter would add a truncation at the `for (int i...)` call sites, and forwarders change the products unless they are inline in every caller (section 2.15) |
| `licenseOpening( ) const` | the Ângulo's licence screen needs the opening in the pack's language, which Core 0 composes into a buffer of `DisplayManager.cpp` (external linkage since S10c D4b-1, declared in `display/LicenseOpening.h`; it was a file-static) | **S4 (C9):** a member defined next to that buffer, replacing the free function `uaAberturaLicenca` the Ângulo declared weak; no product calls it and the linker drops it |
| `uiKeypadTargets( ) const` **(S12, F4)** | `GET /api/keypad` builds its answer (`WebManager::handleApiKeypad( )`, Core 0; public, because the caller is another class) | **Not a body that moved: a constant on each side.** The original answers `nullptr` (`UI_HOOK`, `src/ui/UiDriverMembers.h`) and the route sends its grid and, for the groups keyboard, its popup, statements untouched; the Ângulo answers `"/api/ui"` (defined in the class, `src/ui/angulo/Members.h`, so that the handler's unit sees the constant) and the route sends `targets` in place of both. The branch an image does not take folds out of it: the nine images that are not the Ângulo's keep their sha256 and the Ângulo's two shrink by 208 and 216 B (the CHANGELOG's Unreleased section) |
| `WebManager::registerUiDriverRoutes( )` | the web server registers routes | empty inline; the Ângulo adds `GET /api/ui` **S4 (C9):** called by every image; declared through `ui/UiDriverWeb.h` |

The core ticks of section 2.4, duty 9, are inline members in `src/DisplayManager_Ticks.h`, `always_inline` for
both drivers (S4): `tickDashboard` (the web-busy sample, the alarm page navigation and the rotation),
`tickAuthLockout`, `tickTouchSens`, `tickAlarmFlash`, `minMaxIdleDue` (the original's `render` asks it; the
Ângulo's own `tickMinMaxIdle` clears the flags), `repairAlarmLimits`, `clampEditFocus` and `pktArrowShown`.
The Ângulo adds `tickWebNotify`, which the original does inside `drawTopBar`. Section 2.7 says where each
is called and how it was proved.

`uiChanged` lets an action call it exactly where the original drew at once, for
example `drawBottomButtons` at `Touch.cpp:566` and `:577`, or `redrawTopPanel` at
`:50` and `:509`. The theme hooks do not cover every caller outside the
display units **(S0)**: section 3.2 lists `getThemeIndexByName` (the CLI),
`scanCustomThemes` (boot, and after a `.thm` is uploaded or deleted),
`loadTheme` and `getThemePalette( i )->displayName` (the web API), so the
catalogue hooks need to grow to an index-by-name lookup, a rescan and an apply. **(S10b, C4a: answered by the link-time catalogue, with no hook.)**

### 2.3 Core actions

The business parts of the per-mode branches of `handleTouch` and
`handleTouchPanelV24` become public `act*( )` members, which both drivers call.
The per-mode table of section 3.3 is the work list: 34 branch rows, 38
`pushUiEvent` calls (33 in `handleTouch`, 5 in `handleTouchPanelV24`) and 19
immediate draws, all in `handleTouch`, counted per handler body with comments
left out (section 3.5 says why a plain grep reports one more). The plan estimates 70 to 90
actions and 15 to 25 kinds of `UiChange`; examples are `actSelectSlot( int8_t )`,
`actTopPanelTap( )`, `actTopPanelHoldFired( )`, `actOpenGraph( int8_t )`,
`actGraphNav( int8_t )`, `actLimitStep( int, int )`, `actLimitsSave( )`,
`actClockStep( int, int )`, `actPinKey( int )`, `actUserSave( )`,
`actMaintStart( )` and `actBack( )`. Reused as they are: `pinAppendChar`,
`pinSubmit`, `pinCancel`, `enterPinFor`, `scramblePinKeys`, `pinFailLadder`
(`DisplayManager.h:1169`), `leavePanelMessage`, `showAlarmAction`,
`forceDashboard`, and the gates `acceptTouch`, `acceptHoldTouch` and
`acceptSlideTouch`. Forty-four functions are reachable from the two handlers
without crossing a paint function (section 3.5), and none of them paints, which is
what makes the cut possible. Cost on the original: none, since the actions are
`always_inline`.

**S5 (2026-10-15):** the Painel group's seven actions are done and live in `src/DisplayManager_Actions.h` (section 2.8), under other names than the examples above: `actTopCardPress`, `actBottomCardTap`, `actNavButton`, `actAlarmSilence`, `actAlarmDeactivate`, `actAlarmShowMinMax` and `actBootSkip`.

**S6 (2026-10-15):** the Base and Menus groups' eight actions are done and live in the same header (section 2.9): `actMuteAllBack`, `actMuteAllConfirm`, `actUserDelCancel`, `actUserDelConfirm`, `actStartAp`, `actMenuSelect`, `actAlarmListOpen` and `actLicensePage`.

**S7, S8a and S8b (2026-10-15 and 16):** their actions are listed in sections 2.10 to 2.12.

**S9 (2026-10-21):** the Identidade group's eighteen actions and one predicate are done and live in the same header (section 2.13), under other names than the examples above (`actPinKey( int )`, `actUserSave( )`): the predicate `pinInputBlocked( )`, `actPinPopupDrop`, `actPinBackspace`, `actPinOk`, `actPinPopupKey`, `actPinOrderedKey`, `actPinCard`, `actUserOpen`, `actUserNew`, `actUserEditActivate`, `actUserEditSave`, `actSensorMenuActivate`, `actPwdResult`, `actPwdLeave`, `actPwdNameAccept`, `actPwdLegacyOk`, `actPwdToggleShow`, `actPwdAppend` and `actPwdBackspace`. `pinAppendChar`, `pinSubmit`, `pinCancel`, `enterPinFor` and `scramblePinKeys` are reused, as this section said.

### 2.4 Duties of a driver's paint

A driver that does not run the original's `render` and `draw*` functions takes
over what those functions did besides drawing. Items 1 to 7 are the plan's;
items 8 to 12 are what the audit found in the draw bodies **(S0)**.

1. **`_bootPaintSeq` rises once a boot frame is on the glass.** Core 0 waits for
   it before it opens the AP-by-touch window (`bootPaintSeq( )`,
   `AppManager_Boot.cpp:543-546`). The original raises it at `DisplayManager.cpp:1815`
   and `:1895`.
2. **`_otaDrawnSeq` holds the request whose update screen is on the glass.** Core 0
   may call `uiPaintOta` while Core 1 is parked, through `otaPaintFromCore0`
   (`Ota.cpp:115-122`), and `showOta` waits up to `OTA_PAINT_WAIT_MS`, 800 ms
   (`DisplayManager.cpp:806-822`), for the number to arrive before a step that
   freezes the panel. The original stores it at `Ota.cpp:225`.
3. **`_lastHeartbeat` is stamped again after a long paint.** The health check
   restarts Core 1 when the stamp is older than 10 s (`AppManager_Loop.cpp:133-139`),
   and `AppManager::isDisplayAlive( )` answers false once it is 5 s old
   (`AppManager_Core.cpp:63-72`).
4. **`_lastRenderedState` holds the last frame's state.** The original stores it at
   four places in `render` (`:1814, 1894, 1915, 1991`); `loopCore1` reads it for the
   alarm rotation and the flash (`:1489, 1514, 1522`), and
   `restoreNormalDashboard` for the restore.
5. **`_langChanged` is consumed.** `setLanguage` raises it (`:280`); `render` clears it
   at `:1791` and forces a full redraw of the boot terminal.
6. **`_loadingDrawn` is set when the graph's loading screen is on the glass.** The
   original sets it in `drawLoadingScreen` (`Graph.cpp:49`); the constructor and
   `requestLoadingScreen` clear it; Core 0 waits on `isLoadingDrawn( )` up to 500 ms
   (`AppManager_Events.cpp:170, 233`). This answers the plan's open question.
7. **No flash access and no heap allocation while painting** (the T1.2 rule, stated at
   `DisplayManager.cpp:1412-1414`). The audit checked the original against it
   and it does not meet it: 16 `String` objects in 9 paint bodies
   (`drawAlarmAction` 3, `drawSettingsDisplayOffset` 2, `drawTouchSensitivity` 2,
   `drawGraphScreen` 1, `drawGraphDetailScreen` 1, `drawSettingsPassword` 1,
   `drawPinDotsInto` 1, `drawPinScreen` 3, `drawUserConfirmDel` 2), and in a
   tenth, `drawSystemStatus`, a temporary `String` from
   `StorageManager::getBoardSerialNumber( )` (`Settings.cpp:1390`). The framework's
   `WString.h` has no small-string buffer (`grep SSO` finds nothing), so each is
   a heap allocation; that was read from the header, not measured. There is no
   flash access: the board id is read once by a constructor before `main`
   (`pico-sdk` `unique_id.c`), and `tr( )` answers from RAM. The Ângulo's paint
   has to be heap-free from the start; the original's debt is its own.
8. **The repaint requests are consumed.** The loop clears `_repaintSettings` before it
   calls a draw function (`DisplayManager.cpp:1571-1685`), each of 25 painters
   clears `_forceSettingsRedraw` when it ends (section 3.1 group C), and
   `:1700` re-raises both if the mode changed underneath a draw. A driver that
   calls none of those functions must consume the two flags itself, and keep
   the rule of `:1687-1700`, or the `show*` entry points pile requests up.
   **(S0)**
9. **Timers that run inside draw bodies move to core ticks.** Four of them have a
   product meaning: the 30 s return of both cards from min/max (`render`
   `:1949-1962`); the web-notification banner's expiry (`drawTopBar`
   `Dashboard.cpp:540-553`); the telemetry arrow's flash-to-fixed transition, the
   same state `setTelemetrySendStatus` writes (`:648-660`); and, in
   `drawAlarmEdit`, the repair of MIN >= MAX in the working copy `_tempAlarmConfig`
   and the clamp of `_editFieldFocus` (`Settings.cpp:234-250`), which is
   configuration data that Core 0 diffs on `EVT_SAVE_ALARMS`. The plan's ticks
   (`tickDashboard`, `tickAlarmFlash`, `tickMinMaxIdle`, `tickAuthLockout`,
   `tickTouchSens`) cover the first of these only. By reading, the study never
   advances the arrow from state 3 to 1, since `UA_DESVIO` returns at the top of
   `drawTopBar`; that was not run on hardware. **(S0)**
10. **Page sizes belong to the driver, and the core asks.** Six draw bodies clamp a
    page index with the original's four rows per page: `_themePage`, `_alarmPage`,
    `_mainMenuPage`, `_usersPage`, `_langPage`, and `_licensePage` against
    `_licenseTotalPages`, which `drawSettingsLicense` computes from a layout of
    50 columns by 17 lines (`DisplayManager.cpp:2033-2048`); `buildDashLayout`
    clamps `_currentPage` to the five-button strip (`Dashboard.cpp:1068`); and
    `handleTouch` repeats "4 per page" in core lines (`Touch.cpp:806, 809, 816,
    820`). A driver with another row height has other pages, so the clamp
    becomes the answer of `uiPages( list, items )`, and the core clamps with it.
    **(S0)**
11. **Screen entry resets the original's draw caches.** Eleven `show*` entry points write
    the original's sentinels (section 3.5, `UiChange::CACHES`): the
    `_last*Page`, `_last*Sel` and `_lastOffsetDraw*` members, and the seven `g_*`
    file-scope caches that `showTouchCalibration` clears at
    `Calibration.cpp:49-55`. In the original they stay as they are; the
    contract calls `uiChanged( CACHES )` at those points so a driver can drop or
    ignore them. **(S0)**
12. **The theme block changes the mode.** `loopCore1`'s `_themeChanged` block draws
    the "applying theme" text and the whole dashboard, and sets `_uiMode =
    MODE_DASHBOARD` and `_themeChanged = false` (`:1400-1449`). The hook
    `uiThemeChanged` takes the drawing and leaves the mode change and the flag
    with the core. **(S0)**

### 2.5 Ownership

- **Core.** The Core 1 lifecycle, park, quiet mode and capture (`DisplayManager.cpp:160-1240`,
  and the core parts of `loopCore1`); the shared state, the mutex and the event
  ring (`DisplayManager.h:766-870`); touch acquisition, the pressure and
  injection gates, calibration and sensitivity sampling, boot, press ownership
  and the web-busy gate (`Touch.cpp:24-396`); the `show*( )` entry points and
  every screen's working state; timers with a product meaning (alarm rotation
  and flash phase, lockout expiry, sensitivity to calibration, the 30 s idle
  return, the min/max reset, and the three of duty 9); `tr( )`,
  `DisplayManager_i18n.cpp` and `_LangParser.cpp`; the OTA state machine
  (`Ota.cpp:115-148`); and the panel primitives `fillWindowDma`, `blitWindowDma`,
  `fastFillRect`, `fastClearScreen` and `blitCanvas` (`Dashboard.cpp:238-400`),
  `TftWithOffset`, and the screenshot readback in `DisplayManager_Auth.cpp`
  (which holds no authentication: it is the GRAM read for screenshots and the
  shuffle's `fastRandom`).
- **Original driver.** Every `draw*` and `redraw*`, `render( )` and the boot paint
  (`:1772-1948`); the geometry `CARD_*` (`DisplayManager.h:1095-1098`), `ALARM_EDIT_*`
  (`:235-255`), `CLOCK_*` (`:267-272`), `GRAPH_PBTN_*` (`:285-287`); `buildDashLayout`,
  the 3 s graph header name, `Themes.cpp`, `DisplayManager_Fonts.*` and the font data
  it includes (`ui/original/FreeSansBold9pt8b_latin1.h`, `ui/original/FreeSansBold12pt8b_latin1.h`,
  `ui/original/FreeSansBold24pt7b_subset.h`: they moved there in S11 A2), `UiWidgets.*`,
  `PasswordKeyboard.h` and `sensors/SensorDrawing.h`.
- **Not the original's, though only display units include them.** `DisplayManager_FmtFloat.h`, `OtaScreen.h`,
  `TextWrap.h`, `display/{ClockEntry,CountInText,DisplayDriver,SettingsMenu}.h` and
  `sensors/SensorPanelDispatch.h` are included only by original or mixed units, and mixed units include them,
  so the cross-driver rule of `tools/check_ui_boundary.py` leaves them out (found by the review's include-graph
  script, 2026-10-15).
- **The mixed units** are settled by section 3.3: of its 371 rows, the 290 that are
  functions split 98 original, 183 core and 9 mixed (the other 81 are the 34
  per-mode branch rows of the two touch handlers and 47 rows of file-scope data).
  S10a cuts along those rows.

### 2.6 Room for a 3.5-inch panel

The panel's size comes from the panel layer. **S3 (2026-10-15) did this**: until
then the size was written as a literal into core primitives (the list S0 made is
in section 4.2), and the layer had only `width` and `height`, which nothing reads.
`Ili9341_320x240` now carries `static constexpr int16_t W = 320` and `H = 240`
(`src/display/ILI9341_320x240.h`), the one place the size is written, and
`DisplayDriver` inherits them in the TFT images. The two drivers with no pixels,
`Hd44780_16x2` and the headless `DisplayDriver` (`src/display/DisplayDriver.h`),
answer `W = H = 0`; that is what lets `DisplayManager.h` name `DisplayDriver::W`
in every image without a new `#if SIMUT_DISPLAY_TFT` (the sprawl watermark holds
that macro at 3 sites in the file). `width` and `height` stay, initialised from
`W` and `H`: they are data members, so removing them would move every later
member of `DisplayManager` and change the code.

What reads `DisplayDriver::W` and `H` now, by function:

| function | what it takes from the panel |
|---|---|
| `loopCore1` (`DisplayManager.cpp`) | the strip canvas, `GFXcanvas16( W, 45 )`; the 45 rows are the strip's height and stay |
| `blitWindowDma`, `fillWindowDma` (`Dashboard.cpp`) | the on-panel check of a DMA window |
| `fastFillRect` | the clamp of a fill to the panel |
| `fastClearScreen` | the whole-panel fill and its library fallback |
| `readRow` (`DisplayManager.h`) | its default width |
| `CAL_SCR_X/Y` (`DisplayManager.h`) | the targets: `CAL_INSET` (20) px inside the corners; `CAL_SPAN_X/Y` (280, 200) is the panel less two insets, the span the touch solve mirrors |
| the calibration solve in `handleTouch` (`Touch.cpp`) | `CAL_INSET` and `CAL_SPAN_X/Y` as float constants, in place of `20.0f`, `280.0f` and `200.0f` |
| `mapTouchPoint` (`Calibration.cpp`) | the map onto `0 .. W` and `0 .. W - 1` (and `H`) |
| `Faixa`, `uaPintar` (`ui/angulo/DisplayManager_UiA.cpp`) | the Ângulo's strips: their width (`LARGURA_FAIXA`), the GFX size, the row pitch, the strip count `H / 40` with a `static_assert` that 40 divides `H` |

Not changed, on purpose: the original's layouts, with their 320 (`commitScreenStrip`,
`blitTitleBar` and `blitFooterMenu` are the width of the original's screens, and
section 3.3 classes them original), and the Ângulo's own layout (`Angulo.cpp`,
`telas/*`, `L_TELA` in `AjustesComum.h`), which is for the visual stage. S3 found
three more places that write the panel's size and are outside its list; they are
left, and a panel of another size has to deal with them: `fillMarginsBlack` in
`TftWithOffset.h` (the panel layer's own, included before the struct that
defines `W`), the screenshot (`WebManager_History.cpp`: the rows' buffers, the
BMP header, the chunk sizes), and the range check of the CLI's `touch sim` and
`touch hold` (`AppManager_Commands.cpp`) with its help text (`CommandManager.cpp`).
(`DisplayManager_Auth.cpp`'s `CHUNK_PX` is not one: it is a read chunk over any
`w*h`.) The original's own layout numbers stay as they are, on purpose: `DASH_W`
and `DASH_H` (`DisplayManager_Dashboard.cpp`, the dashboard's grid),
`drawSettingsLicense`'s page, and the texts of `drawCalibrationMessage` and
`drawTouchCalibration` (`DisplayManager_Calibration.cpp`). Two more things a panel
port must budget for, found in the review of S3 (2026-10-15): the shared strip
canvas is now `W` wide while the original's strip blits stay 320, so on a wider
panel `blitCanvas` takes its in-place compaction branch (it works, slower), and a
480x45 canvas is 43,200 B against 28,800 B today, with the heap's low-water mark
under 40 kB; and the Ângulo's calibration sight (`MIRA_X/Y`, `telas/AjustesTela.h`)
is held equal to `CAL_SCR_X/Y` by a `static_assert` in the glue, so a panel of
another size stops that build until the sight moves with the targets.

How S3 was proved (2026-10-15, tree `7046bf7`, built with the private core that
matches CI, `$B/pio_core_ci`; the outputs are in `$B/stages/s3*`):

- **No byte changed in the nine product images.** `$B/tools/build9.sh` into
  `$B/stages/s3`, then `diff $B/base_192e1ad_ci/sha256.txt` against its nine lines
  prints nothing: hash, size and env equal (`pico_w_release` 1,034,500 B,
  `pico_wiretft` 785,736 B, `pico_w_alpha`, `pico_w_air` and `pico_wire` included,
  which see only declarations).
- **The Ângulo image is the one S2b built.** Its sha256 is `14d254ca…` and its size
  831,560 B; `elf_funcoes.py` against S2b's ELF reads 4,262 functions in each, all
  equal, 453 read-only objects and 2,482 texts equal, exit 0.
- **The change bites.** With `W` = 321 in a scratch copy, `pico_wiretft` differs in
  exactly nine functions: `DisplayManager::DisplayManager( )` (the `width` it
  initialises), `loopCore1`, `blitWindowDma`, `fillWindowDma`, `fastFillRect`,
  `fastClearScreen`, `mapTouchPoint`, `drawTouchCalibration` (the targets) and
  `handleTouch` (the window: `280.0f` becomes `281.0f`). `pico_wiretft_angulo`
  differs in eleven: seven of those nine (`fastFillRect` is not linked in that
  image, and `drawTouchCalibration` is only its `UA_DESVIO( )` stub there), and
  `Faixa::drawPixel`, `Faixa::fillRect`, `Faixa::fillScreen` and `uaPintar`. With `H` = 241,
  `pico_wiretft` differs in eight (the same without `loopCore1`: the canvas's 45
  rows are the strip's height, not the panel's), and the Ângulo build is refused
  by the `static_assert` that 40 divides `H`; with `H` = 280 the Ângulo image differs
  in eight functions, three text pairs and a pointer shift in
  `flash_init_boot2_copyout` (the strip count is one of them; the file lists all). `readRow`'s default is in no list because no caller leaves out
  `w`; only the build covers it. The scratch copy also renamed `width` and `height`:
  it builds, so nothing reads them. Output: `$B/stages/s3.bite_checks.txt`.
- **Gates.** `check_ui_boundary.py`, `check_feature_sprawl.py` (368 sites, the
  watermark as it was), `check_net_boundary.py`, `check_features.py`,
  `check_angulo.py` and `test_ui_caps.py` exit 0; `check_flash_budget.py` exits 0
  for the ten images; `pio test -e native` passes, 288 of 288. Nothing of this ran
  on hardware: the bench was not touched.

Visual changes (contrast, font sizes) are made in the
Ângulo's own files only: `src/ui/angulo/kit/angulo_tokens.h` for colours
(the source of truth is the `simut-rx` tokens), `Angulo.cpp` for the themes,
`Angulo.h` for spacing and the font roles, `kit/fontes/*.h`, `AnguloMeta.h`,
and `telas/*.cpp`, checked by `tools/tft_ui/host_*.py`. `AGENTS.md` section 7
("the TFT panel is not Ângulo") changes when the Ângulo ships.

**The margin is black (2026-10-15, `fix/ui-angulo-margem`, 3ba3ea5 plus this change).** The 4 px around the safe area, the band the display alignment moves the image into, is RGB565 0x0000 in both themes; the light theme left it `#f6f6f4` (0xF7BE), the background, beside the black band the offset uncovers. `MARGEM` and `PRETO` are named once, in `src/ui/angulo/Angulo.h`; `ua::margemPreta( )` paints the four bands through the Pintor, `uaPintar( )` calls it after each strip's draw and clears the glass with `fastClearScreen( ua::PRETO )`, and `host_telas_main.cpp` calls it after each sample, so a host frame is the glass's. `host_telas.py` reads `MARGEM` from the header, as it reads `ALVO_INERTE`, and fails a frame of either theme with a margin pixel that is not 0x0000. Nothing else in `src/ui/angulo/` pushes pixels: `uiPaintOta( )` ends in `uaPintar( )`, the boot is the dashboard's frame, `fastClearScreen( )` has no other caller there (grep of 2026-10-15), and in the Ângulo image `blitCanvas( )`, `fastClearScreen( )` and `fastFillRect( )` return unless `_uaPintando` is set, which only `uaPintar( )` does. Proved on the host (`stages/margem/`): in a tree whose harness lacks the call the check fails all 560 frames of the 280 samples (4,416 of 4,416 margin pixels, 0xF7BE light and 0x10A2 dark), and with one pixel (319, 239) at 0x0001 after the call it fails every frame with exactly 1 of 4,416; with the call it passes 560 of 560. Against S8a's frames, in the dark and the light frame of all 280 samples exactly the 4,416 margin pixels changed (1,236,480 per theme), 0 pixels inside x 4..315, y 4..235 changed and 0 margin pixels are not black (`mascara.py`, bitten by doctored copies: a safe-area pixel, a non-black margin pixel, an overlay pixel, the old frames). The kit's host test gained two cases (`test_cantos.py`, 45 -> 47 checks), failed by four mutants of `margemPreta( )`. Not run on a board: a full repaint now clears the glass to black, so the strips not yet pushed show black where they showed the theme's background.

**The compose is faster by items, each one a commit, none changing a pixel (2026-10-15, `perf/ui-angulo-pintura`, from 89de38d).** On the bench (2026-10-15, study build `study/pintura-medida`) a screen change composed in 69 ms (the chart) to 100 ms (the calendar) and showed 56 ms of black before a full repaint. p1: the draw of the first strip is recorded and the other five replay it (`Pintor::gravar( )`, `parar( )`, `repetir( )`, `src/ui/angulo/Angulo.h`): the screens' draw functions, which worked out their layout, numbers and text widths again for each strip, run once per frame; the record was static, 1024 calls of 14 B and 512 B of text (+14,856 B of RAM, given back by p7 below: the chart's frame needs 903 calls, every other sample 117 or fewer), and a frame that does not fit draws as before. `Faixa.h` holds the strip class `DisplayManager_UiA.cpp` had, so the host draws through it: `tools/tft_ui/perf_telas.py` times the compose in six strips and proves, for every frame, that the strips stitched equal the frame drawn whole (560 of 560, 0 pixels apart; with the replay of one primitive taken out, 208,696 pixels apart in the dashboard group). Host numbers are x86 and show a ratio; the board's are measured with the study counters. p2: the strip's hash is ua::hashFaixa( ), two pixels to the 32-bit word through an xor, an odd multiply and a rotate (3,200 steps of four words a strip, where FNV-1a took 25,600 byte steps); host_perf --hash flips one bit, two bits and the weak spot's two bit-31 flips on a random strip, and the plain FNV over words, the control, collides on every one of the last. p3: the coverage of a rounded corner (radii 1 to 12, fill and 1 px outline, 650 B each) and of a disc (radii 0 to 8, 285 B each) is a constexpr table in flash built from the 16-sample function, in place of computing it for every pixel; a bigger radius computes as before; no RAM. p4: Pintor::bruto( ) tells the Pintor its surface is a raw RGB565 buffer (Faixa::para( ) does it for the strip, host_telas_main for the whole canvas), and a text is then written into the buffer directly, only the glyph rows and columns the buffer holds, where Adafruit GFX walks the whole bitmap and calls the virtual drawPixel( ) for each set bit; without it the GFX path draws as before. The pixels are the same: V-PIX holds the raw frames to the GFX ones, and the strips (raw) to the whole frame (GFX). p5: Faixa::fillRect, the strip's clear (12,800 pixels, six times a frame) and every rectangle a screen fills, stores two pixels to a 32-bit word where the run is on a word, four stores a pass, the ends one pixel at a time; V-PIX 840 of 840 equal, and the strips equal the whole frame (the whole canvas fills by Adafruit GFX's own code). p6: a full repaint (theme change, offset change, first frame, update screen) no longer starts with fastClearScreen( ) (56 ms of black at 24 MHz); it blackens only the band the alignment offset uncovers (fillMarginsBlack( ), nothing at offset 0) and pushes all six strips whatever their hash, which cover the rest of the glass and paint the margin black themselves; host_perf --vidro holds the argument on a model of the glass for every offset from -4 to 4, with the controls (no band, five strips) leaving stale pixels. p7: the record owns no RAM: it lives in the 5 spare rows of the canvas under the strip (3,200 B; `Pintor::lista( )`, lent by `uaPintar( )` after it checks the canvas is 320 wide and at least 45 rows, else nothing is recorded and every strip draws the screen), the records growing up from the start of the list and the bytes they point to (text, the function and argument of a call) growing down from its end; the chart's plot (the band, the second curve and the mean, ~650 calls) is three `Pintor::repetivel( fn, arg )` records that each strip runs again, skipping them when it holds none of the plot's rows; RAM 131,156 -> 116,348 B (-14,808); the largest of the 560 host frames takes 1,935 of the 3,200 B. p8: the strips are 320 x 20, twelve of them, composed in the two halves of the canvas's first 40 rows (strip k in the half k % 2) while the DMA pushes the one before: `DisplayManager::uaEmpurrar( )` starts a burst and returns, `uaEmpurrarEsperar( )` waits for it, and `uaPintar( )` returns with no burst in flight, which the touch read, the GRAM readback and the update screen rely on. The code is the driver's own file, `src/ui/angulo/DisplayManager_UiA_Empurrar.cpp`, with its own DMA channel and a repeat of the core's `dmaTftOpen( )` and `dmaTftClose( )` (static in `DisplayManager_Dashboard.cpp`, which `tools/check_ui_boundary.py` keeps free of the driver's names); the comment at its top lists spi0's users and how each stays off the bus. A strip the alignment offset moves partly off the glass is sent by DMA too, the part on the glass packed in place (`ua::recortar( )`, `ua::compactar( )`, `src/ui/angulo/Recorte.h`; `host_perf --recorte` proves them for every offset from -4 to 4, with a control), where it took the library's `drawRGBBitmap( )` at about 2 us a pixel (154 ms a frame with a column offset); RAM +28 B (six more strip hashes, the driver's channel and the in-flight flag). p9 (2026-10-16, the review's fixes): the record's overflow path is held by `host_perf --lista` (the host lends the record 900 B, then none, of the canvas's 3,200: 78 of the 560 frames, 46 of the 90 chart frames, then all 560 fall back to drawing every strip, each still equal to the frame drawn whole; a mutant that replays an overflowed record fails it by 404,448 px, one that ignores the size it is lent by showing no frame over the list); the plot's guard by `--desloca` (the strips on the 19 other grids) and `--guarda` (a chart whose second curve touches the plot's first and last rows), which a guard narrowed by 6 px a side fails by 12 and 288 px and which every earlier check passed; `uaPintar( )` returns the list it lends, `uaEmpurrar( )` sets its flag before the bus opens; the dates of this paragraph's measurements are 2026-10-15. p10 (2026-10-16): `Pintor::line( )`, `fillRect( )` and `rect( )` return when what they draw lies in rows outside the strip's window, as text, icons, circles and rounded boxes already did: the chart's ~600 short lines and ~250 one-pixel columns ran the surface's code in every strip that runs the plot (on the board, 2026-10-16, a 24 h chart with full data composed in 58 ms, with little data in 37). A 24 h chart frame replayed in the twelve strips sends 21,199 pixels and 2,412 rectangles to the strip surface before and 6,585 and 560 after (a count, not a time); the host's time for the chart group falls 8.8%, the other groups' does not move. The mutants of the new rejects are killed by the main table and by `--desloca`; `pixel( )` is left alone, since no screen calls it. p11a (2026-10-16): the chart's plot spares, by their values and before any division, the buckets and segments a strip's window cannot show (`Limiares`, found by bisection on `yDe( )`, with a margin of one row; `Pintor::janelaY0( )` and `janelaY1( )`); a 24 h chart frame replayed in the twelve strips sends 5,060 divisions through `rdiv( )` instead of 19,356, and the property test `host_perf --guarda` draws 300 random charts on the twenty grids of strips against the frame drawn whole. p11b (2026-10-16): a rounded box's corners paint a run of fully covered cells as one span a side and blend the partly covered ones in place (`Pintor::misturar( )` on the strip's raw buffer: no virtual `ler( )` or `drawPixel( )`), and its body fills in three pieces that meet the corners; the calls that reach the strip surface in a frame replayed in the twelve strips fall from 7,969 to 5,222 on the 24 h chart and from 3,426 to 942 on the dashboard (host count). p12a (2026-10-16): `ua::Faixa::fillRect( )` and `ua::hashFaixa( )` run from RAM (`UA_NA_RAM` in `Faixa.h`, `__no_inline_not_in_flash_func` in `Angulo.cpp`), 368 B of RAM: on the board the chart screens ran with 6.4 to 8.0% XIP cache misses against 1.0 to 1.6% on the dashboard, and the strip clear and the hash with them; in RAM the clears take 1.2 ms a frame on both screens and margin plus hash 2.8 to 2.9 ms (3.6 and 8.2 ms on the chart, 1.5 and 3.5 to 4.1 on the dashboard, before). `tools/check_ui_ram.py` reads the ELF after every build and fails it if either moved back to flash; `tools/test_check_ui_ram.py` holds its cases, negative controls included. p12b (2026-10-16): `host_perf --caixas` holds the rounded boxes (the spans, the in-place blend and the body's pieces of p11b) to a per-pixel reference written from the definition, with its own coverage and mix: 4,000 random boxes on strips of shifted grids and on a whole canvas, and two controls that must differ; `--guarda` now also draws a flat chart with inverted buckets and random charts with them (values outside the scale, where `rdiv( )` wraps), which the domain guard of `Limiares::intervalo( )` alone gets right; the counts no committed tool reproduces left the comments of `cantos( )` and of the plot's thresholds.

### 2.7 The loop's hooks and ticks (S4)

**S4 (2026-10-15, branch `feat/ui-drivers-s4`) did this.** `loopCore1`, `render`, the update screen, `drawAlarmEdit`,
`drawTopBar` and the web server's route setup no longer name the Ângulo: what each knew is a hook the driver defines
(section 2.2) or a core tick both drivers call (section 2.4, duty 9), and the Ângulo has a pass of its own. Four selection
headers under `src/ui/` put each driver's text where C++ lets it stand: `UiDriver.h` before the class (`UI_HOOK`, which is
`inline __attribute__((always_inline))` for the original and empty for the Ângulo; `UiChange`; the two definitions of
`UA_DESVIO`), `UiDriverMembers.h` inside it (the Ângulo's members, `angulo/Members.h`), `UiDriverHooks.h` after it (the
original's hook definitions, `original/UiOriginal.h`, and the ticks, in its non-alpha branch) and `UiDriverWeb.h` inside
`WebManager`. The gate named exactly those four then (**S10b, C3: five now, `UiDriverTouch.h` inside `handleTouch( )`, section 2.15; the two definitions of `UA_DESVIO` left in C5a**) and lets only them include the Ângulo's headers (`SELECTION_HEADERS`).
The allowances went from 124 to 50 over S4 (`tools/ui_boundary.json`; 58 at the merge with `feat/ui-drivers`).

The Ângulo's pass (`uiPass` in `DisplayManager_UiA.cpp`) runs, in this order: in the dashboard mode `tickDashboard`,
`tickAlarmFlash` and `tickMinMaxIdle`, then the snapshot (`_langChanged` cleared while booting, `_lastRenderedState = snap`,
`uaSujo`); in the PIN mode `tickAuthLockout`; in the sensitivity mode `tickTouchSens`; in the alarm-edit mode, when a repaint
was requested, the repair and the clamp; `tickWebNotify`; then the six repaint requests taken (`_repaintSettings`,
`_repaintGraph`, `_repaintLoading`, `_repaintCalendar`, `_alarmStatusDirty`, `_forceSettingsRedraw`, with `uaSujo` if one was
set), `uaQuadro( )`, and the packet arrow's flash step. Left out on purpose: `_headerShowName` (no reader in the Ângulo),
the one-second STATUS and MAINT cadence (its 250 ms look covers them) and the original's caches `_prevAlarmSlotMask` and
`_forceFullRedraw`. The review of C8 found two differences, neither a loss: the calibration screen's paint comes one pass
earlier, and a mode change gets one redundant look at the frame, with no strip sent.

How S4 was proved (the outputs are in `$B/stages/s4_c*`, outside the repository; every image was built with the private core
that matches CI, `$B/pio_core_ci`). A step either left the nine product images byte for byte as they were, or changed bytes
that were decoded and accepted:

- **sha256 equal in the nine:** C1 `cfc1bb7` (the four headers), C2 `44685c3` (`uiPaintOta`), C3 `310bae4` (`uiBegin`), C5a
  `b3efea2` (`tickAuthLockout`, `tickTouchSens`), C7 `d3eafd9` (`minMaxIdleDue`) and C7r `dffb83d` (the review fixes of C1 to
  C4: the gate names the four headers, the alpha condition moves into `UiDriverHooks.h`). The Ângulo image was equal in C1,
  C7 and C7r. In C2 it names `otaTick`, `otaPaintFromCore0` and `uaOta` becoming `uiPaintOta`; in C3 `loopCore1`, `uiBegin`
  and `_fini`, which is one padding word at the end of that section, not a function (the review of C1 to C4 measured 4 B); in
  C5a one byte, a compare with its operands swapped.
- **A byte change accepted with a decoded listing and a source proof:**
  - C4 `85cb97f` (`uiThemeChanged`): six images change 3 bytes (2 on the RP2350), two commuted compares in `loopCore1` with
    the matching condition, flags dead after them (`stages/s4_c4/equivalence.txt`); the reviewer signed it. The message of
    C4 says the moved block was "re-indented one level"; it was two.
  - C5b `15eb13c` (`tickDashboard`), C6 `3bd28af` (`tickAlarmFlash`, `uiChanged`) and C8 `1845742` (`uiPass`): six images
    change in `loopCore1` only, plus position (`_fini`, a literal pool); each has `decoded_vs_<previous>.txt` and a
    `source_proof.txt` of five points (a pure move checked by script, no control flow leaving the block, no local of the
    enclosing function named, the macros, `always_inline`). In C6 the `uiChanged` switch folded: no image has the symbol,
    and each call is the direct one. C8 moved 149 lines, equal after whitespace by script; its images
    grew 72 to 96 B because the compiler put 43 exits of the chain out of reach of a 2-byte branch, which C8c took back.
  - C10 `d46597f` (`repairAlarmLimits`, `clampEditFocus`, `pktArrowShown`) and C11a: six images change in `drawTopBar`, the
    function those steps name, and the repair and the clamp (a pure move) leave `drawAlarmEdit` with the same listing.
    `pktArrowShown` is a rewrite of three lines, so its proof is a case analysis, and the review of C10 found one case C10
    got wrong: Core 0 flips `_pktArrowState` from 2 to 3 between two reads of one frame, after the flash end, and the old
    code drew the accent colour where C10 drew the hot one. C11a made the tick answer `NONE`, `GOING` or `OVER`; a model
    of the old block, C10 and C11a over 480 cases, 360 with the two reads disagreeing, has C11a differ in 0 and C10 in 20
    (`stages/s4_c11a/model_pktarrow.txt`), and the volatile reads are those of the old block.
- **Veneers only:** C9 `9a27acd` (`licenseOpening`, `registerUiDriverRoutes`). Six images differ and no function does: the
  linker's veneers changed order. In `pico_w_release` 361 bytes differ
  (`cmp -l` of the two `.bin`); every one is a call to a same-named veneer or a word inside a veneer (C9's message counts
  the sites one way, the review another; the bytes are the measure), and `licenseOpening` is absent from the nine ELFs.
- **The attribute:** C8c `1958835`, `__attribute__((optimize("no-reorder-blocks")))` on `loopCore1`. The experiment ran on
  `117b17c` in a scratch worktree (`stages/s4x_*`): `noinline` on `uiPass`, `cold`, `no-reorder-blocks` on the hook, on
  `loopCore1`, and an RP2040-only `noinline`. The attribute on `loopCore1` alone did better than all of them; `noinline` helped
  only the RP2040 images and went over the Pico 2 W's budget. The stack frame of `loopCore1` is 800 B (808 B on the RP2350)
  with and without it, and the worst Core 1 path 2,192 of 8,192 B. GCC documents `optimize` as meant for debugging; the sizes
  in `tools/flash_budget.json` hold its effect, so a compiler that ignored it fails the budget gate.
- **The merge `fb75d03`** brought `feat/ui-drivers` into the branch (the review fixes of the gates and the comparator, and the
  static-analysis fix of `Angulo.cpp`) as a merge and not a rebase, so that the hashes the stage folders cite stay.
- **Gates.** `check_ui_boundary.py` 0 findings and `test_ui_boundary.py` 285 of 285; `check_authz.py` and its test (39 checks
  and 11 mutants), with the route's gate (`requirePerm( PERM_SYS_CONFIG )`) unchanged under the new function name;
  `check_feature_sprawl.py` (358 sites, from 368 at the start); the six host screen groups and `host_ui.py` 106 of 106; the
  flash budgets of the ten images and the OTA table. Nothing of this ran on hardware.

**Sizes against the CI base `192e1ad` (before S4), after C11a:** the Pico W images 72 B smaller (`pico_w_asserts` 64 B), the
Pico 2 W and `pico_wiretft` 64 B smaller, `pico_w_air`, `pico_w_alpha` and `pico_wire` equal; the OTA headroom of
`pico_w_release` is 5,715 B (5,643 B before S4) and `pico_w_test` 1,235 B (1,163 B). The Ângulo image went from 831,560 B at the
end of S3 to 827,456 B: `render` and the chain's draw functions left it, and the repair, the notice and the arrow came in.

**The Core 1 phase stamps.** Four things read `g_core1Phase`: the Core 1 block of the console's metrics (the live phase with its
age and sequence, `Last stuck: ... phase=`, and the worst time of each phase that reached 5 ms, in `CommandManager.cpp`), the
soft-panic record, whose scratch word carries the phase's low byte for the reboot autopsy (`LogManager.cpp`), the phase taken
when a flash lockout gives up (`g_core1StuckPhase`, `DisplayManager.cpp`) and the phase at the worst stall age
(`g_core1StallPhase`, `FlashIrqProbe.cpp`). The Ângulo's pass sets `C1P_SNAPSHOT` (dashboard mode), `C1P_DASH_MUTEX` and
`C1P_ALARM_FLASH` (the shared ticks) and `C1P_RENDER` before it paints. Its pass does not set `C1P_LOOP_TAIL`, `C1P_UI_GRAPH`,
`C1P_UI_SETTINGS` or the `C1P_R_*` family, which the chain it replaced set before draw calls that were already stubs
(`UA_DESVIO`, gone since S10b's C5a); the paint itself was under `C1P_RENDER` before S4 and still is. In an Ângulo image `LOOP_TAIL`, `UI_GRAPH`
and the `R_*` phases therefore never appear and their worst times stay 0; `UI_SETTINGS` still does, because `otaTick( )`
stamps it for both drivers before the update screen (`DisplayManager_Ota.cpp`, the `C1_PHASE( C1P_UI_SETTINGS )` before
`uiPaintOta( )`). A stall or a lockout that finds Core 1 painting a screen reads `RENDER` where the original would say
`UI_GRAPH`, `UI_SETTINGS` or an `R_*` region, and a freeze between the paint and the wait reads `RENDER` too. Known limit, kept (the stamps are not changed): a crash report from an Ângulo image places a freeze in the paint, not on a
screen.

**Bench checks pending.** None has run; the bench was off limits during S4.

- The plan's list for the Ângulo's rewritten loop: the boot frame and the AP-by-touch window; the alarm flash and the rotation;
  the 30 s return from min/max; the update screen drawn from both cores; a theme change; the PIN lockout countdown; the
  sensitivity screen moving on to the calibration; the graph loading screen without the 500 ms wait; the web-busy banner.
- Added by C8: each screen still repaints on entry now that `uiPass` takes the six repaint requests; the order of the pass
  (the frame is painted before the rule that restores a full repaint for a mode that changed under it).
- Added by C10 and C11a: an alarm edited so that MIN meets MAX is pulled apart (on the Ângulo's +/- keys by its own
  `passoLimite`, which keeps MIN under MAX; the shared repair runs when a repaint is requested, on entering the screen), and
  not while the record is being copied; the
  focus on a sensor with fewer limits than the last; the packet arrow returns to the steady arrow after a send; the "entrou
  pela web" notice goes away.
- For the original: the `loopCore1` of the six images whose bytes changed (C4, C5b, C6, C8, C8c), which has only been read and
  never run, and the Core 1 stack under load with `optimize("no-reorder-blocks")`, where the measurement so far is the
  compiler's.

### 2.8 The Painel group's touches (S5)

**S5 (2026-10-15, branch `feat/ui-drivers-s5`) did this.** What a touch does in the Painel group is a core action, defined
once in `src/DisplayManager_Actions.h`, and the Ângulo runs the same actions itself: `uaToquePainel( )` no longer moves the
touch into the old layout for `handleTouch( )` to finish (`grep -c 'UaToque::TRADUZIDO'
src/ui/angulo/DisplayManager_UiA_Painel.cpp` is 0; the other groups translate until their stages). There are seven actions,
not the dozen the plan counted, because the control flow that decides them cannot be cut finer without copying it into the
Ângulo: `firstTouch = acceptTouch( 0 )` decides three things after it, and the row's gates change with the kind of the
button.

- `actTopCardPress( onGraphBtn )`: the upper card's zone, gate included: the lock tail of a long press, the first touch, the
  graph button of a card in min/max, the alarm's action screen for a card in alarm, the start of the 3 s hold, the gap that
  restarts it and the long press that pins or releases the card.

- `actBottomCardTap( onGraphBtn )`, behind `acceptTouch( 1 )`: the graph, the alarm's action screen or the other face of the
  card.

- `actNavButton( btnIdx )`: the row of buttons; its gates (`acceptTouch( 14 )` for the page, `acceptSlideTouch( 20 )` for
  settings, `acceptSlideTouch( 10 + slot )` for a slot) are inside.

- `actAlarmSilence( )`, `actAlarmDeactivate( )` and `actAlarmShowMinMax( )`, behind `acceptTouch( 0 )`, `( 1 )` and `( 2 )`:
  the alarm's action screen.

- `actBootSkip( )`: sets the flag the boot terminal polls.

Each is `inline __attribute__((always_inline))`, declared next to the ticks in `DisplayManager.h`, called by `handleTouch(
)` behind its zone test and by the Ângulo's glue behind the gate its old zone held. Two hooks and two kinds of `UiChange`
came with them (section 2.2): `uiTopPanel( snap )`, the two draw calls `redrawTopPanel( )` ended with; `uiBootSkipHit( x, y
)`, which takes the raw rectangle of the boot's skip button out of `handleTouch( )` and the rule "Pular stays inside x
80..240, y 191..239" out of ESPEC_UI_A.md; `BOTTOM_PANEL` and `BOTTOM_BUTTONS`. `redrawTopPanel( )` moved out of the class
to `DisplayManager_Actions.h` as a plain `inline` function: left in the class, every unit that includes `DisplayManager.h`
parses its call to `uiTopPanel( )`, and GCC stopped all nine images on "inline function used but never defined" (`-Werror`)
in the units that do not take the original's hook definitions. The pure part is in `src/display/DashHold.h` (`dashHoldStep(
)`, the clock of the 3 s hold, and `dashNextPage( )`), with nine cases in `test/test_validators` (`pio test -e native`, 297
of 297; the last three came with the review of C9, for three mutants the first six let live) and six negative controls,
each of which fails the cases it should (three at C8, three at C10). The plan's `uiTouchUp( )` was not needed: the lift is
answered at the top of `handleTouch( )`, before any driver code. The allowances stay at 50.

How S5 was proved (the outputs are in `$B/stages/s5_c*`, outside the repository; every image was built with the private core
that matches CI):

- **The nine equal:** C1 `d629903` (the header and two includes), C4 `4c8793b` (the alarm screen's buttons), C5 `4c0eadc`
  (the lower card and the row) and C7 `7ac3567` (the skip). In the Ângulo C4 changes `uaToquePainel` (70 to 129
  instructions), C5 the same function (to 280) and C7 `handleTouch` and the new `uiBootSkipHit`.

- **Placement only:** C2 `14fd267` (`uiTopPanel`). In six images `redrawTopPanel` (268 B) and `PwdKb::popupHit` (252 B) swap
  places and the `bl` operands in `handleTouch` follow; no function changed (`stages/s5_c2/symbol_moves_release.txt`), the
  class of S4's C9 veneers.

- **`handleTouch` only, with a decoded listing and a source proof of five points:** C3 `6957cb1` (`BOTTOM_PANEL`,
  `BOTTOM_BUTTONS`: the slot index travels as the hook's `int16_t` and `SystemState::selectedSlotIdx` is an `int`, so two
  instructions in four images; the switch of `uiChanged` folded, no image has the symbol and `loopCore1` is identical); C6
  `54cdd26` (the upper card; one condition is rewritten, `x >= MM_BTN_X0 && x < MM_BTN_X1` evaluated at the call. `x` is a
  local `int16_t`, so there are no volatile reads before or after and the other core cannot reach it; a model over every `x`
  and the two members it was and-ed with, 262,144 cases, has 0 differences and its controls 1 each; the register allocation
  of a region of `handleTouch` changed, which only the source proof covers); C8 `60eabd6` (the hold and the page turn as
  functions; a model of the old statements against the header, 40,000,000 samples and a 44 x 41 grid of pages, 0
  differences, and 32, 102,227 and 122,564 for the three mutants; the load of `_lastTouchRegion` moves ahead of two plain
  stores, and that member is written on Core 1 only).

- **The review of C9 (C10).** Three mutants of `dashHoldStep( )` lived through the first six cases: `==` for `>=` in the
  fire test, no `fired = false` in the restart, and no `start != 0` in front of the gap test. Every cadence there is a
  multiple of 15 ms and 3000 is 200 of them, and none meets a gap with no hold running. Three cases were added (a 17 ms
  cadence, which first fires at +3009; a hold that fires, meets a 121 ms gap and fires again; a 121 ms gap with `start ==
  0`). With each mutant applied, `pio test -e native` fails that case and no other (`stages/s5_c10/native_m4.txt` to
  `native_m6.txt`); without them 297 of 297 pass (`native_after.txt`).

- **Gates.** `check_ui_boundary.py` 0 findings and `test_ui_boundary.py` 285 of 285; `check_authz.py` and its test;
  `check_feature_sprawl.py` 358 sites, as before: the two `SIMUT_TFT_GRAPH` blocks moved with their actions (Touch.cpp 3 to
  1, `DisplayManager_Actions.h` 0 to 2) and the marks were rewritten with `--update`; the six host screen groups and
  `host_ui.py` 106 of 106; the flash budgets of the ten images and the OTA table; `check_readme_numbers.py` (698 cases in 11
  suites). Nothing of this ran on hardware.

**One call without a `return;`.** The branches of the upper card, the lower card and the boot screen end with the `return;`
the zone always had; the call to `actNavButton( )` is the last statement of its branch, and its early exits (a `btnIdx` out
of range, a gap in the row, a refused gate) became the action's own `return;`s. Harmless today, because nothing follows the
`else if` chain of modes in `handleTouch( )`; code added after the chain would be reached by those old early exits. (The
alarm screen's three branches never had a `return;` after their bodies and have no early exit inside.)

**Where "the same code" is not the whole story.** The Ângulo marks its frame dirty whenever a touch answers `TRATADO`
(`uiTouch( )`, `DisplayManager_UiA.cpp`; `uaToque( )` until S10b's C3), so the alarm screen's three buttons (C4), the lower card and the row (C5) and the
upper card (C6) mark it on every sample of a press, where the translation did so only when the old branch drew: one more
comparison of the frame per sample, no state changes. The bottom card's toggle no longer takes the state mutex for a
snapshot in the Ângulo (C3: the draw that followed was a `UA_DESVIO( )`). The Ângulo's skip button is hit where it is drawn,
so a touch inside the old rectangle but off the button no longer skips the boot (C7). "Ver mínimo e máximo" keeps the defect
the plan found, moved as it was: `_alarmActionSlot < 0` never holds, because `showAlarmAction( )`, the only writer, is
called with a slot checked to be >= 0, so the bottom card always takes the min/max even when the screen was opened from the
top card; fixing it is a decision for later.

**Sizes.** Against the end of S4: `pico_w_release`, `pico_w_test` and `pico_w_test_https` 8 B larger from C3 and
`pico_w_asserts` 8 B larger from C6 (each of the two steps adds two instructions to `handleTouch` in all four; their budgets
and OTA rows moved in a commit of their own, because `verify_step.sh` held only the Ângulo image to its budget until the
review of C5 and C6 found it), `pico2_w_release`, `pico_wiretft`, `pico_w_air`, `pico_w_alpha` and `pico_wire` the same
size. The Ângulo image went from 815,424 B used (827,456 B .bin) to 816,264 B (828,296 B), +840 B: C2 -32, C3 -88
(`drawSlotPanel` and `drawBottomButtons` left the image), C4 +136, C5 +328, C6 +424, C7 +72, C8 0; the review of C9 then
added 16 B (816,280 B, .bin 828,312 B) for the frame check in `uiBootSkipHit`. Until S10b's C3 every action was compiled twice
there, the dead old chain in `handleTouch` and the glue; the OTA headroom of that image was 211,831 B at the end of S5
(217,047 B on the visual branch, `feat/ui-angulo-visual`, once the 7 pt fonts left the kit, 2026-10-15).

**Before the merge to main.** The message of `14fd267` still carries a paragraph written when C2 was not yet accepted:
rewrite it when the stage is squashed.

**Bench checks pending.** None has run; the bench was off limits during S5. For the Ângulo: `roteiro_ui.py` `etapa_painel`;
a hold of 3.5 s on the upper card pins and releases it, and a card pinned on the selected slot makes the bottom card move to
another slot, with EVT_SLOT_SELECT in the binary log; letting go right after a long press changes nothing (the 600 ms lock);
a card in alarm opens the alarm screen of its own slot; silence lasts 120 s; deactivate sends EVT_ALARM_DEACTIVATE; min/max
returns to the dashboard with the bottom card in min/max; the page button returns to the first page; a slide across the slot
buttons selects slot after slot; Pular works during a boot that shows it. For the original: the same list against the
`pico_wiretft` image, the six images whose `handleTouch` changed having only been read and never run.

### 2.9 The Base and Menus groups' touches (S6)

**S6 (2026-10-15, branch `feat/ui-drivers-s6`) did this.** What a touch does in the Base group (the panel message and the
three confirmations) and in the Menus group (the settings list, the alarms list and the licence's pages) is a core action
wherever the original's branch had business logic of its own. The actions are defined in `src/DisplayManager_Actions.h` beside
S5's, and the Ângulo runs them: `uaToqueBase( )` no longer moves a point into the old zones (`grep -c 'UaToque::TRADUZIDO'
src/ui/angulo/DisplayManager_UiA.cpp` is 0, from 3), and `uaToqueMenus( )` no longer keeps its own copy of the settings list's
event, of the alarms list's row or of the licence's page step (`grep -c pushUiEvent src/ui/angulo/DisplayManager_UiA_Menus.cpp`
is 3, from 4, and `EVT_MENU_SELECT` is not named under `src/ui/angulo/`). There are eight actions, not the fifteen the plan
counted: the status pages have none, and four outcomes are core calls that exist already.

- `actMuteAllBack( )`, behind `acceptTouch( 0xC0 )`, and `actMuteAllConfirm( )`: the mute-all confirmation. Confirm's gate is
  hand-written (no touch click, because the confirmation sound plays instead) and moved inside the action with its early
  `return;`, which ends `handleTouch( )` where it ended before, because nothing follows the chain of modes.

- `actUserDelCancel( )`, behind `acceptTouch( 0 )`, and `actUserDelConfirm( )`, behind `acceptTouch( 1 )`: the account-deletion
  confirmation, a branch of `handleTouchPanelV24( )`. The panel only asks: Core 0 checks `PERM_USER_MGR` and refuses the admin,
  an empty slot and the account in use. The Ângulo asks from one place, the dialog's confirm target (`DLG_SIM`).

- `actStartAp( )`, behind `acceptTouch( 0xC3 )`: the AP confirmation, called inside the branch's existing `#if SIMUT_NET_WIFI`,
  so no site is added for the sprawl check. The wired Ângulo image has no frame for the mode and cannot reach it, so the Ângulo
  has no call; a Wi-Fi Ângulo image (S12) will need a `VmDialogo` case and a handler that calls `showSettingsMain( )` and
  `actStartAp( )`. **Since 2026-10-28 (S10c D7, `8dd53c51`):** the Wi-Fi Ângulo draws `MODE_CONFIRM_AP`
  itself, a `VmDialogo` case whose Confirm calls `actStartAp( )` and whose Cancel calls `showSettingsMain( )`; the wired image
  still neither lists the mode (`src/display/SettingsMenu.h:143-145`, under `SIMUT_NET_LAN`) nor draws it, and keeps two stubs
  in place of the frame and the touch (section 2.18).

- `actMenuSelect( idx )`: the settings list's Open button, the test of the row against the length of the list and the event
  that asks Core 0 to open the item the row stands for. The gate is outside: `acceptTouch( 13 )` in the original,
  `acceptTouch( ZONA_LINHA )` in the Ângulo, which tests the row ahead of its gate so that a tap on no row clicks nothing.

- `actAlarmListOpen( idx )`: the alarms list opens the sensor's action menu (the original's second tap, the Ângulo's one tap).

- `actLicensePage( dir )`: one page back or forward on the licence, held at the ends. The text's two halves and the two arrows
  of the original call it with a constant `dir`; `_repaintSettings = true` stays at the call sites, because the text's halves
  share one store of it.

The panel message needs no action: `leavePanelMessage( )` is core code already, and the Ângulo calls it behind `acceptTouch( 0 )`,
the gate the translation reached. Left as they were: the selection, the wrap and the arrows of the settings and alarms lists and
of the status pages, `showSettingsMain( )` and `forceDashboard( )`. The drivers differ there on purpose: the approved look has no
selection (a row is entered with one tap) and its pager stops at the ends where the original's wraps. There is no shared code to
extract, so each driver keeps its navigation, and the original's moves into `uiTouch` in S10a (deferred to S10b by D6, section 2.14; S10b made the original's touch a fragment and not a function, section 2.15). No hook and no `UiChange` came with
S6: nothing in the group draws immediately.

**The pure part, `src/display/ListPager.h`** (C8, `c078ef4`): `listPages( )`, `listClampPage( )` and `listPageStep( )` are the page
arithmetic that the Ângulo's frame and touch of the settings list, the alarms list and the status pages each spelled out for
themselves, and that had to be edited in both places when the look moved to three rows a page. The glue calls them. The header is
used by the glue only: `actLicensePage( )` keeps its own two lines, so the nine product images, which compile `Actions.h` and not the
glue, could not change (calling `listPageStep( )` from the action, which the plan allowed if the nine stayed equal, was not tried).
Three cases in `test/test_validators` pin the functions (`pio test -e native`, 300 of 300; 297 before). The plan says seven killable
mutants and lists ten: each of the ten fails exactly the case named for it, and the equivalent one (`page <= 0` for `page < 0`)
fails none (`stages/s6_c8/native_m1.txt` to `native_m10.txt` and `native_eq.txt`, with the clean runs before and after). A host model
whose oracle is the old code itself, compiled (each old line cut out of the glue at `5da8888` as written, members renamed and
nothing else), against the header, for 3, 4 and 5 rows a page (the glue's real 4 and 3 among them), `n` 0..40, page -2..45 and pages
0..15, finds 0 differences in 37,974 comparisons, and 255 to 12,537 for each killable mutant (`stages/s6_c8/model_pager.txt`).
One departure from "word for word", which the controls found: `listClampPage( )` clamps below first. In the glue's own order the
second line catches the -1 that the `pages > 0` test keeps out, so a header that dropped that test could not fail any case (the model
shows 0 differences for it in that order, and the plan's mutant for it would have survived); in the header's order both lines carry
weight and the value is the glue's for every input the model covers.

How S6 was proved (the outputs are in `$B/stages/s6_c*`, outside the repository; every image was built with the private core that
matches CI):

- **The nine equal, at all eight steps.** Each step's nine product images have the sha256 of the end of S5 (`stages/s5_c10/sha256.txt`):
  C1 `7ee79e9` (Users.cpp takes the hooks and the actions), C2 `a872d93`, C3 `77e89aa`, C4 `390fc6f`, C5 `05da4f0`, C6 `80ecdb0`, C7 `5da8888` and C8 `c078ef4`.
  The actions are `always_inline` and the calls took the place of the statements they carry, so the compiler gave back the same bytes,
  including `handleTouch( )`, `handleTouchPanelV24( )` and `loopCore1( )`; no source proof of five points was needed.

- **The Ângulo, function by function.** C1 and C4 change nothing (C4's branch is compiled only by the Wi-Fi images, so it was proved on
  the `pico_w_release` and `pico_wiretft` pair: `stages/s6_c4/bite_actStartAp.txt` shows a mutant of the action changing the first and
  not the second). C2 changes `uaToqueBase( )` (43 to 93 instructions), C3 `uaToqueBase( )` (93 to 120), `uaToque( )` (one branch
  offset) and `uiPass( )` (see the deviation below), C5 `uaToqueMenus( )` (282 to 281), C6 `uaToqueMenus( )` (281 to 313), C7 no
  instruction (see the constructor below), C8 `uaMontarMenus( )` (840 to 838) and the pager lambda of `uaToqueMenus( )` (32 to 32).
  Every other function of the image is identical at every step, `_fini` (alignment padding) apart.

- **A deviation, accepted (C3).** The Ângulo's `uiPass( )` went from 714 to 674 instructions and from 29 to 30 `bl`: GCC stopped
  inlining `pushUiEvent( )` into it once `actUserDelConfirm( )` added a call site in `uaToqueBase( )`. The out-of-line
  `pushUiEvent( )` is in both images at the same address and size (80 B), three `dmb` left `uiPass( )` with the inlined copy and none
  was added, and the rest of the listing is register allocation and branch polarity around the removed copy. The behaviour is the
  same code reached by a call; only this transitional image is affected, and it got 24 B smaller
  (`stages/s6_c3/accept_main_session.txt`). The same three facts accept the same pattern again.

- **The constructor that C7's diff names.** The verifier's ELF diff of the Ângulo image lists `DisplayManager::DisplayManager( )` at C7
  (779 to 779 instructions, the first difference a `.word`). It is not a change of the function (`stages/s6_c7/constructor_proof.txt`,
  read from the two ELFs): its 598 instructions are identical with the addresses masked, and so are 180 of its 181 data words; the
  one that differs points to a 7-byte constant at an address that moved by +0x74, and the diff tool, which reads 64 bytes at a pointer
  that names no symbol, runs past the constant into a different neighbour. The move is the layout's. `UA_DESVIO( )` (S10b's C5a removed it) expanded to
  `do { uaSujo( ); return; } while ( 0 )` in the Ângulo image and to nothing elsewhere (`src/ui/UiDriver.h`): no string and no
  `__LINE__`, so the macro put no data in the unit. What its `return` did was make the rest of `drawSettingsLicense( )` unreachable.
  With the line gone the function is compiled, it calls `utf8FoldAscii( )` (`DisplayManager.cpp`), and the unit's object file carries
  `utf8FoldNext( )` (`Utf8Fold.h`, an inline function) and its three switch tables. The linker keeps the first copy of a weak function,
  and this unit comes before the language-pack parser's, whose copy was kept until then: the function (180 B) and the tables (116 B) take an
  earlier place in the image, and what lay between moves by their size (87 functions by +0xb4: 88 symbols whose name is unique in both images and whose size did not change, one address carrying two names; ten data objects by +0x74). The image has
  the same size, and C7's message, which says all ten ELFs are byte-equal to C6's, is right for the nine and not for this one.

- **The frames.** The six host screen groups of the Ângulo are 813 of 813 frames equal to the step before, at all eight steps
  (`tools_s6/quadros_iguais.py`, a pixel comparison of every frame the groups render), since no screen of the group draws
  differently: S6 changes what a touch does and not what the panel shows.

- **The allowances (C7).** The nine draw functions of these screens are called only from `UiOriginal.h`, which the Ângulo image
  does not compile; checked on the C6 image, `nm -C` finds none of them (`stages/s6_c7/nm_precondition.txt`). Their `UA_DESVIO( )`
  lines went, and `tools/ui_boundary.json` with them: `DisplayManager_Settings.cpp` 12 to 6, `DisplayManager_Users.cpp` 8 to 6 and
  `DisplayManager.cpp` 1 to none. The allowances are 41, from 50 (earlier than the plan's "ends in S10b"); `identity` stays at 6.
  `check_ui_boundary.py` reports 0 findings and `test_ui_boundary.py` passes 285 of 285.

- **Gates.** `check_authz.py` and its test; `check_feature_sprawl.py` 358 sites, as before (no `#if SIMUT_` was added: the call of
  `actStartAp( )` stays inside the branch's existing block); the flash budgets of the ten images and the OTA table;
  `check_readme_numbers.py` (701 cases in 11 suites). Nothing of this ran on hardware.

**The review of C1 to C4 (C9).** It came back with reservations, and C9 answers them: the budget record names S6's measurements
(`tools/flash_budget.json`); the C3 deviation is written down here and in that record; the constructor that C7's diff names is proved
above; the gap in the checks is stated below, with its three mutants on the bench list; the comments that cited `S5_PLANO.md` and
`S6_PLANO.md`, which are not in the repository, cite this document; `UiDriverHooks.h` says which units take the hooks header; and
the old `Touch.cpp` line numbers are gone from the lists' glue.

**Where "the same code" is not the whole story.** The Ângulo marks its frame dirty whenever a touch answers `TRATADO`, so the
confirmations' buttons (C2, C3), the settings and alarms rows (C5) and the licence's arrows (C6) mark it on every sample of a press, where
the translation did so only when the old branch drew: one more comparison of the frame per sample, no state changes (S5 recorded the
same). The settings list's event leaves with `param` uninitialised, as the original's does: the Ângulo's copy built `UiEvent ev{ }`
(param 0) and the action builds `UiEvent ev;`, because `UiEvent` has no member initialisers; Core 0 reads only `id` for
`EVT_MENU_SELECT` (`AppManager_Events.cpp`). A stale dialog cannot delete an account: `uiTouch( )` (`uaToque( )` until S10b's C3) returns before any group when the
glass shows another mode than the state's. The mute-all actions write the mode and the flags without the state mutex, as the original
did.

**Where `S6_PLANO.md` was wrong or incomplete** (the plan is not in the repository; its corrections, as they stand here):

1. The line numbers of the plan's entry in PLANO.md were stale, and "replace each immediate draw with `uiChanged( )`" does not apply:
   the group draws nothing immediately, so S6 has no hook and no `UiChange`.
2. "About 15 actions" became 8: the status pages have none, and four outcomes are existing core calls.
3. The grep criterion covers only `DisplayManager_UiA.cpp` (3 to 0); `uaToqueMenus( )` had no `TRADUZIDO`, and its fault was copied
   business logic, which the `pushUiEvent` and `EVT_MENU_SELECT` counts of C5 measure.
4. `uaToqueMenus( )` also handles the themes and the language (S8a) and the sensor's alarm menu (S9), with copies of their logic;
   S6 left those alone.
5. The panel message and the account-deletion confirmation are branches of `handleTouchPanelV24( )`, the function S9 owns
   (section 4.1, item 6). S6 touched only those two branches; of the function's other six, MAINT and PIN_POLICY were S8b's (section 2.12) and four are S9's.
6. The AP confirmation has no Ângulo frame, and `Base.h` said it drew one; the comment is corrected.
7. The approved look changed how navigation works (one tap enters a row, the pager does not wrap, there is no selection), so
   "geometry, gate, action" applies to the commit step only, not to navigation.
8. The comments of `DisplayManager_UiA_Menus.cpp` cited old `Touch.cpp` line numbers; they name the branch now (C9). The claim that the
   old licence arrows wrapped went with C6: they clamp, in both drivers.
9. The allowances can go down in S6 (C7), not in S10b.
10. Found, moved as it was: the alarms list's previous arrow with no sensors sets the selection to -1, and the status screen has a
    dead strip at x >= 219. Neither is a decision of S6; the first is only recorded.

**Sizes.** The nine product images are the size they had at the end of S5; their budgets and OTA rows did not move. The Ângulo
image went from 811,064 B used (823,096 B .bin) to 811,232 B (823,264 B), +168 B: C2 +128 (the actions are compiled twice there until
S10b, the dead old chain in `handleTouch( )` and the glue), C3 -24 (the deviation above), C6 +64 (`uaToqueMenus( )` 281 to 313
instructions: the licence's two arrows run the action inline, where they shared `virar( )` with the other list screens), the other steps 0. The OTA headroom of that image
is 216,879 B (217,047 B before S6). Its budget, `"bin"` and OTA row moved in the commit that changed them.

**What no check covers.** The Ângulo's touch glue is not compiled on the host (it needs `DisplayManager.h`, the class with the
Arduino in it), so no native case and no gate reads the dialogs' glue. Three changes to it pass every gate S6 has: dropping
`if ( !a ) return UaToque::TRATADO;` from the account-deletion case (a tap outside both buttons would read `a->tipo` through a null
pointer), returning `NAO_E_MEU` there (the touch would fall through to the groups after Base instead of ending in the dialog), and
swapping the mute-all test to `a->tipo != DLG_SIM` (Voltar would run Confirm and turn every sound off). The nine images do not compile
the glue, V-ELF only says which function of the Ângulo image changed, and V-PIX compares frames, not touches. The delete has a source
check for its single call site (`stages/s6_c3/userdel_check.txt`, three mutants of the glue refused), kept outside the repository. A host
check was weighed and not made: a pure mapping in `src/display/` ("no target is nothing, the confirm target confirms, any other target
cancels") would pin the mapping, but each of the three mutants would then be a change in the arguments the glue passes to it or in the
arms of the `switch` that uses its answer, and the native suite cannot see either; the mapping would guard nothing the three mutants
touch. They stay on the bench list below until the dialogs' touch moves into `uiTouch` (S10a; deferred to S10b by D6, section 2.14; S10b made the original's touch a fragment and not a function, section 2.15), where one test can cover both drivers.

**Before the merge to main.** The message of `77e89aa` records the 24 B but not the deviation (this section does), and the message of
`5da8888` says the verifier holds all ten ELFs byte-equal to C6's: the nine product images are, and the Ângulo's differs in one data word
of its constructor (above). Commits are not rewritten here; the message of the merge should say both.

**Bench checks pending.** None has run; the bench was off limits during S6. For the Ângulo: "Silenciar tudo" plays the confirmation,
turns all seven sounds off and returns to Sounds, and Voltar changes nothing (every sound as it was: a swapped `DLG_SIM` test would
mute all); "Entendi" returns to the screen the message came from, from the main menu, the users, the alarms and a sensor; deleting an
account: Cancelar returns to its editor, Excluir with `PERM_USER_MGR` deletes it and without the bit shows the refusal, and a tap
outside both buttons of the dialog changes nothing (no reset: the null guard) and does not leave the dialog (the `NAO_E_MEU`
fall-through); a menu row sends `EVT_MENU_SELECT` with the right item (binary log); an alarms row on page 2 (3 rows a page) opens the
right slot's sensor menu; the status and licence pagers stop at their ends and a held arrow repeats. For the original (`pico_wiretft`,
plus one Wi-Fi image for the AP confirmation): the same list, plus the settings list's select, enter and wrap, the alarms list's
select-then-open, the status pages' wrap and the licence's text-half taps; the AP confirmation: Voltar goes to the menu, Confirmar
brings the AP up when the session has `PERM_NET_CONFIG`.

### 2.10 The Gráficos group's touches (S7)

**S7 (2026-10-15, branch `feat/ui-drivers-s7`) did this.** What a touch does on the graph, on its detail, on the calendar and on the update screen is a core action wherever the original's branch had logic of its own, and the Ângulo runs the actions in place of its copies. There are thirteen actions in `src/DisplayManager_Actions.h` (the plan said "about ten"), and `UiChange` gets two kinds, `GRAPH_HEADER` and `GRAPH_BUSY`, for the two immediate draws this group had (S0 counted 14 sites, section 3.5). On the plan's grep criteria (its section 5, item 3): `grep -c pushUiEvent src/ui/angulo/DisplayManager_UiA_Graficos.cpp` is 0, from 8; the stores of `_uiMode`, `_graphNavOffset`, `_detailPage`, `_repaintGraph`, `_isDirty` and `_forceFullRedraw` there are 0, from 15; the calls of `drawGraphBusyHint( )` and `drawGraphHeaderBar( )` in `DisplayManager_Touch.cpp` are 0, from 14; and `grep -c 'UaToque::TRADUZIDO'` on the glue is 1, not the 0 the plan ended at (the update screen, below).

- On the chart and the detail: `actGraphClose( )` (the X: the offset back to 0, the dashboard, a full repaint; the caller returns), `actGraphShowName( )` (the header's tap: the stamp the loop reads to put the date back after 3 s, and `uiChanged( GRAPH_HEADER )`; the Ângulo has no header target and does not call it), `actGraphNav( g, dir )` (a step in time: one event with `dir` -1 or +1, which Core 0 coalesces), `actGraphOpenCalendar( g )`, `actGraphPeriod( g, range )` (the original steps from the period on the glass; the Ângulo's chips send their own) and `actGraphDetailOpen( )` (the chart's central zone: the detail, on the temperature).
- On the detail: `actDetailPage( p )`, where the original's cycle and the Ângulo's pager both end, and `actDetailBack( )`.
- On the calendar: `actCalendarBack( )`, `actCalendarMonth( g, dir )`, `actCalendarDay( g, day )` and `actCalendarToday( g )`.
- On the update screen: `actOtaDismiss( )`, with its gate inside: the phase test has to come before `acceptTouch( 0xC6 )`, or a tap during an update would arm the touch-priority window and the apply would be answered with a 503.

The four actions that read the graph's package take it as `const GraphDataPackage &g`, and the callers pass `_graphData`: the member exists only when `SIMUT_TFT_GRAPH` is set, `DisplayManager_Actions.h` has no block for it, and a block would raise the sprawl count. A reference keeps each load where the old body had it. **Where each draw goes.** `GRAPH_HEADER` is raised inside `actGraphShowName( )`, because it shows the action's own result. `GRAPH_BUSY` stays at the original's call sites, between the gate and the action (twelve: the five buttons of the chart and of the detail, a day and Today), because the two drivers disagree on which requests get a busy line: the original paints it before a step, the calendar, a zoom, a day and Today; the Ângulo draws its own from its model (`pedirDados( )`), before a step, the calendar, a period chip and a month arrow, and never before a day or Today, because the loading screen answers those. The Ângulo's `uiChanged( )` only marks the frame, and neither kind reaches it.

**How each step was proved** (`tools_s7/verify_step.sh`; numbers in `stages/s7_c1` to `s7_c9`; the bench was in use, so nothing ran on a board):

- **The nine product images.** Equal to the end of S5 at C1, C2 and C3 (`stages/s5_c10/sha256.txt`). **C4 changed five of them, in `handleTouch( )` only**: `pico_w_release`, `pico_w_test`, `pico_w_test_https`, `pico_w_asserts` and `pico_wiretft`, at the same instruction count (4,667; 4,585 in `pico_wiretft`) and the same `bl` count. The new action reads `g.sensorIdx` and `g.timeRange` through the reference it takes, so the compiler forms the address of `_graphData` once where the copy formed `this+788` on its own, and one alignment `nop` before the literal pool goes. Accepted with `stages/s7_c4/decoded_vs_c3.txt` (the instruction diff, addresses masked), `source_proof.txt` (the five points: a pure move, no `return` out of the block, no local of `handleTouch( )` referenced, the macros used, `always_inline`) and `move_check.txt` (the six hunks are pure moves, by script). From C4 on the reference is `stages/s7_c4/sha256.txt`, and C5, C6 and C7 hold the nine equal to it. Their sizes and budgets did not move.
- **The Ângulo, function by function.** C1 changes nothing, and at C2 `handleTouch( )` and `uaToqueGraficos( )` are identical to C1's. C3: `handleTouch( )` 4,581 -> 4,603 instructions, `uaToqueGraficos( )` 322 -> 346, `drawGraphHeaderBar( )` leaves the image (nothing calls it any more), +80 B. C4: `handleTouch( )` 4,603 -> 4,609, `uaToqueGraficos( )` 346 -> 343, `drawGraphBusyHint( )` leaves, -8 B. C5 is in `stages/s7_c5/accept.txt`. C6 changes no function (below). C7: `uaToqueGraficos( )` 343 -> 341. `loopCore1( )` and `handleTouchPanelV24( )` are identical at every step.
- **The frames.** The six host screen groups of the Ângulo are 813 of 813 frames equal to the step before, at every step.
- **The update screen (C5), and a deviation from the plan.** The plan ends at `TRADUZIDO` = 0 on the glue; the code ended at 1 (0 since S10b's C2, section 2.15). It asked for the case to stay `TRADUZIDO` only if a `TRATADO` answer built a frame on every touch sample while an update runs. `stages/s7_c5/ota_r3.txt` (read from the source, not measured on a board) found that it does not in RECEIVING: Core 1 is parked for the whole upload and runs no `handleTouch( )` and no `uiPass( )`. In READY and in the two endings (REFUSED 12 s, CUT 8 s) Core 1 runs the screen, and there `TRATADO` would make each touched pass build the view model and hash it, for a tap the phase test ignores. The decision rests on those phases, not on RECEIVING; the case stays `TRADUZIDO` and the old chain runs `actOtaDismiss( )`. S10a retires the chain and flips the case, two lines; the bench item for the flip is a finger held on the READY screen for 10 s, with `show metrics` iteration maxima compared before and after.
- **The allowances (C6).** `nm -C` of the C5 Ângulo ELF lists none of `drawLoadingScreen`, `drawGraphBusyHint`, `drawPeriodButtons`, `drawGraphHeaderBar`, `drawGraphScreen`, `drawGraphDetailScreen` and `drawCalendarScreen` (`stages/s7_c6/nm_precondition.txt`); outside the two files that define them their only callers are in `UiOriginal.h` (`callers.txt`). Their `UA_DESVIO( )` lines go: `tools/ui_boundary.json` loses `DisplayManager_Graph.cpp` (6) and `DisplayManager_Calendar.cpp` (1), and the allowances are 34, from 41; `identity` stays at 6.
- **The layout move at C6.** The verifier reports no changed function in the Ângulo image, yet its `.bin` is not byte-equal to C5's: 231 bytes differ. `stages/s7_c6/layout_proof.txt` (made from the two ELFs and `.bin`s) shows that 177 of them lie in 168 `bl` / `b.w` instructions whose target is the same by name and is one of 28 linker veneers (16 B each) that changed places among themselves, the call's displacement changed by exactly the veneer's delta, and 54 inside the veneers' own slots, where one veneer took another's place. No other sized symbol moved (5,163 did not), each veneer's 16 bytes are the same at the old place and the new one, and the image's text, data and bss are the same. What decides the veneers' order was not isolated; S6's C7 saw the same kind of move in the same images.
- **The pure part, `src/display/GraphNav.h`** (C7): `calDayHasHistory( mask, day )` is the calendar cell's test (the day is 1..31 and bit `day` of the month's mask is set), which the original's `handleTouch( )` and the Ângulo's `CAL_DIA` each spelled out; both call it, `always_inline`, with the old `1UL << day`, and the nine images stay equal (the Ângulo's `uaToqueGraficos( )` is the one function that changes). Two native cases pin it (`test_validators` 300 -> 302 cases; the READMEs' 701 and 300 move to 703 and 302; the dated 288, 297 and 300 of the S4 to S6 sections stay). Each of the 13 killable mutants fails exactly the cases named for it, and none of the 3 equivalent ones fails any (`stages/s7_c7/native_m01_d01.txt` to `native_m16_e03.txt`, with the clean runs before and after); two mutants are not in the plan's list, the shift taken modulo 16, which the plan's cases let live, and the mask narrowed to 16 bits. The equivalent ones are `day <= 31` dropped or widened to `<= 32` (on the host unsigned long is 64 bits and bit 32 of a 32-bit mask is clear; on the RP2040 that shift is undefined and the range test keeps it out, so no host case can show the bound is there) and the shift taken `& 31`. A host model whose oracle is the old code, compiled (both old expressions cut out of the sources at C6 as written), against the header, for days -5..40 and 1,066 masks: 0 differences in 98,072 comparisons, and 1,086 to 53,758 for each killable mutant (`model_graphnav.txt`).
- **`detailNextPage( )` was not made (plan section 3, item 2).** The plan allows the original's detail branch to call it only if the nine images stay equal. It was written (`page`, `hasHum`, `hasPress` -> 1, 2 or -1) and called in two shapes, `if ( next < 0 ) back else page( next )` and the three arms on 1, 2 and the rest: both changed `pico_w_release` and `pico_wiretft` at the same size (`pico_w_release` sha256 8436d197 and f1cbcb57, against the reference's ddaf10c1; `pico_wiretft` 9a6e3594 and 28f2b22f, against 6b1aa685; `stages/s7_c7/screen_A_*.log`, `screen_B_*.log`), while the day test alone left both equal (`screen_none_*.log`). The Ângulo's pager has no use for it, so it was dropped whole. The host model of the three arms against the function, run before that, showed 0 differences and a difference for each of 14 killable mutants (`model_with_item2_not_committed.txt`); its native controls were not run. The cycle stays as three arms in `handleTouch( )`, and the header says so.
- **Gates.** `check_ui_boundary.py` and its test; `check_authz.py` and its test; `check_feature_sprawl.py` 358 sites, as before (no `#if SIMUT_` was added); `check_flash_budget.py --table`; `-Werror` on the ten images. **R7**, a TFT build without the graph: `tools/build_custom.py` with the profile `{"v":1,"base":"pico_w_release","set":{"tft_graph":false}}` (the configurator's own case "SIMUT without the panel graph") compiles and links: 1,002,020 B of flash, a `.bin` of 1,014,052 B and 109,200 B of RAM, against the release's 1,022,404 B used with the graph (`stages/s7_c9/r7_build.log`; 7 warning lines, none in the repository's `src/`: the one whose path has a `src/` in it is `lfs.c` in the framework's LittleFS). All 12 raisers of the two kinds in `DisplayManager_Touch.cpp` stand inside its `#if SIMUT_TFT_GRAPH` block (an `awk` over the file), and `actGraphShowName( )`, which `DisplayManager_Actions.h` defines in every build, is called from that block only; so the comment in `UiOriginal.h` that no call site raises these kinds in a build without the graph is measured now: by that build, on the C7 tree (`5e27b04`; C9 changes comments, documents and notes only), and by the `awk`.

**The Ângulo's glue is not compiled on the host, and a script outside the repository reads it.** `uaToqueGraficos( )` needs `DisplayManager.h`, so no native case runs it, and the verifier's ELF diff says only that it changed. `tools_s7/glue_check.py` (kept with the stage's tools, like S6's `userdel_check.py`) reads its source and requires, for each target of each mode, the gate, the call (once), its arguments and its sign, the `▶` and lit-chip tests, the `if ( !a )` guards, and the update screen's decision. It passes on the glue and refuses all 18 mutants it applies, each for its own reason (`stages/s7_c9/glue_check.txt`): the signs of the step in time and of the month arrows swapped, the `▶` test and the lit-chip test dropped, the lit period sent in place of the chip's, the guards dropped, `NAO_E_MEU` from the update screen's case, that case as `TRATADO` and its action called with the old chain run too, the close without its gate, the chart's central zone opening the wrong screen, a busy line on a day, the months' busy line dropped, the pager sending the page it was on, and, on C7's call, the history test dropped, negated, and with its arguments swapped (which compiles). What it does not do is run the code: that is the bench list below.

**Where `S7_PLANO.md` was wrong or incomplete** (the plan is not in the repository; its corrections, as they stand here):

1. Its line numbers had moved; the branches are named in the comments now, not numbered.
2. "About ten" actions is thirteen, and two `UiChange` kinds; unlike S6's group this one draws.
3. The grep criterion catches only the update screen (1 -> 0 in the plan, 1 -> 1 in the code, above). The graph screens already returned `TRATADO`; their problem was copied logic, which the three counts at the top of this section measure.
4. `_graphData` is conditional, which is why four actions take the package as an argument.
5. The drivers differ by design: the Ângulo's detail screen has a pager, no requests and a "Voltar" that goes to the chart, where the original's X goes to the dashboard. Recorded, not changed here: the Ângulo's page set requires `count >= 2`, while the original's touch and draw do not.
6. The header-name state belongs to the original alone; `actGraphShowName( )` may move into the original's `uiTouch` in S10a (deferred to S10b by D6, section 2.14; S10b made the original's touch a fragment and not a function, section 2.15).
7. Plan section 3, item 2 did not hold (above), and its R5 did (the veneers moved at C6, with no instruction changed). Its section 5.8 held: the precondition was 0.
8. Defects near this code, recorded and not planned: `_graphAnchorEnd` is never cleared (F1), and going back in time and then forward shows the same window marked "fim" (F3, the arrow defect). The plan's two questions run on their defaults until the maintainer answers: the arrow defect stays recorded, and the Ângulo keeps its busy line on the month arrows.

**The review of C1 to C5 (C9)** came back with reservations (no blocker), and C9 answers them. (1) The update screen's comment in the glue cited `S7_PLANO.md R3`, a file that is not in the repository, and misstated it; it now says what `ota_r3.txt` found and cites this section. (2) The enumeration of the Gráficos actions in `DisplayManager.h` named 12 of 13: the chart's central zone, which opens the detail, was missing. (3) `UiChange::GRAPH_BUSY` in `UiDriver.h` listed "a step in time, a period, the calendar, a day"; C4 also raises it before Today. (4) The glue's 17 line citations into the old `DisplayManager_Touch.cpp` (`(:725, :729)` among them) and `GraficosPreencher.h`'s two now name the branch they meant. The glue still cites lines of other files (`AppManager_Events.cpp`, `AppManager_Graph.cpp`, `DisplayManager_Graph.cpp`, `DisplayManager_Calendar.cpp`): they were not in the sweep. (5) R7 was not exercised at C5; it is above.

**Sizes.** The nine product images are the size they had at the end of S6; their budgets and OTA rows did not move. The Ângulo image: 811,232 -> 811,304 B used (+72: C3 +80, C4 -8, the others 0), `.bin` 823,264 -> 823,336 B, budget = measured; its OTA headroom 216,879 -> 216,807 B. `tools/flash_budget.json` records it.

**Not done.** C8, the Ângulo's busy wait (`pedirDados( )` and `aguardandoDados( )`) as a pure function with `millis( )` passed in, is optional in the plan and was not taken.

**Bench checks pending.** None has run; the bench was in use during S7. For the Ângulo: holding the previous-period arrow repeats, and the busy line stays until the data arrives; the next-period arrow works only in the past; the Dia button opens the calendar; a period chip loads that period, and the lit chip does nothing; the plot area opens the detail on the temperature; the detail's pager stops at both ends, and "Voltar" returns to the chart; the month arrows show the busy line; a day with history loads its 24 h; a day without history does nothing (no click); Hoje returns to the present with the period it had; the update screen closes on a tap only after a refused or cut ending, never during an update, and no 503 window is armed. For the original (`pico_wiretft` and `pico_w_release`): the sensor name shows for 3 s; the busy hint appears before the arrows, the calendar and the zoom on both chart and detail, and before a day and Today; the detail cycle runs T -> H -> P -> chart, T -> P -> chart on a BMP280 and T -> chart without humidity; both pairs of month arrows; X returns to the dashboard; the update screen's tap-to-dismiss.

### 2.11 The Ajustes 1 group's touches (S8a)

**S8a (2026-10-15, branch `feat/ui-drivers-s8a`) did this.** What a touch does on the themes, the language, the clock entry, the display alignment and the sounds (the list and the melody picker) is a core action wherever the original's branch had logic of its own, and the Ângulo runs the actions in place of its copies and of its translations. There are twenty-two actions in `src/DisplayManager_Actions.h` (the plan said "about 25"), no new `UiChange` kind (nothing in the group draws immediately: the two `setDisplayOffset( )` calls are the panel's, owned by the core), and three pure functions in `src/display/SettingsSteps.h`.

- **Themes and language:** `actApplyTheme( idx )` and `actApplyLang( idx )`, the one event each Apply button sent. The original passes the previewed index, the Ângulo the row that was tapped. Core 0 reads only `id` of both events (the `EVT_APPLY_THEME` and `EVT_APPLY_LANG` branches of `AppManager_Events.cpp`), so the Ângulo's `UiEvent ev{ }`, which zeroed `param`, became the action's `UiEvent ev;`: `param` is left as the stack had it, the pattern S6 recorded for its events.
- **Clock entry:** `actClockLeave( )` (Pular at boot or Voltar: at boot the dashboard, with the three stores under `_stateMutex` because Core 0 also writes `_uiMode`; otherwise the settings list), `actClockSave( )` and `actClockStep( i, delta )`. The Ângulo's clock stopped translating a target into the old branch's point: it calls the actions behind the zones the old branch used (0xC4, 0xC5 and 0xD0 + 2 * column + (delta > 0)), so a held arrow repeats as it did.
- **Display alignment:** `actOffsetUp( )`, `actOffsetDown( )`, `actOffsetLeft( )`, `actOffsetRight( )` and `actOffsetCentre( )` return whether the preview moved; `actOffsetPreview( )` then applies it to the panel (`setDisplayOffset( )` on Core 1, then the repaint requests), `actOffsetBack( )` puts the saved offset back and `actOffsetApply( )` only asks Core 0. The Ângulo's seven `TRADUZIDO` of this screen are gone (zones 20 to 26).
- **Melody picker:** `actMelodyPick( idx )` (the type-to-event switch, written three times in the original and once more as `eventoDoSom( )` in the Ângulo, is written once), `actMelodyBack( )`, `actMelodyUse( )` (its stores come before the confirmation preview, which hands the sound to Core 0) and `actMelodyOpen( type, cur )`.
- **Sounds list:** `actSoundVolume( down )` and `actAlarmVolume( down )` (`_touchSoundPending` is stored before the preview, whose barrier publishes the level), `actSoundToggle( flag )` (the web row with its flag as a reference; the Ângulo's five other switches and the web access go through it, so turning a sound on unmutes in one place), `actMuteToggle( )` and `actSaveSounds( )`.
- **The pure part** (`src/display/SettingsSteps.h`, C8): `volumeStep( v, down )`, `offsetStep( v, dir )` with `DISPLAY_OFFSET_LIMIT` (it replaces the Ângulo's `OFFSET_LIMITE` and the citation that sat on it) and `langSlotCount( packLoaded, langCount )`. The plan's `langSlotCount( packLoaded )` takes the count as an argument: the first version included `SystemDefs_Records.h` for `LANG_COUNT`, and `test_main.cpp` would not compile with it (it declares `dallasCrc8( )` and `HIST_NAN_SENTINEL` itself). The three were screened before the edit was kept: with them in place `pico_w_release`, `pico_wiretft`, `pico2_w_release` and `pico_w_asserts` build to the sha256 of C5 (`stages/s8a_c8/screen_*.log`), and the verifier then held all nine; the Ângulo's functions are equal to C7's as well. **Noted 2026-10-28 (S10c, sync 6, C7):** sync 6's C3 (`459d02e1`) removed `langSlotCount( )`: it is no longer in `src/display/SettingsSteps.h` or anywhere in `src/` and `test/` (`grep -rn langSlotCount src test` finds nothing). Decision D1 of the sync kept one rows helper, main's `langRowsListed( )` in `src/display/LangChoice.h`, and the Ângulo's Idioma touch takes its rows from it. The lines of this record that name `langSlotCount( )` (this one, the C8 line below, correction 3 and 'Not done') describe S8a as it stood, and their figures stay.

**How each step was proved** (`tools_s8a/verify_step.sh`; numbers in `stages/s8a_c1` to `s8a_c8`; nothing ran on a board, the bench is off-limits to this work):

- **The nine product images.** Equal to the base at C1, and moved in `handleTouch( )` only at C2 to C5; C6, C7 and C8 left them as C5 made them. **C2** moved six (`pico_w_release`, `pico_w_test`, `pico_w_test_https`, `pico_w_asserts`, `pico2_w_release`, `pico_wiretft`: two hunks in the first four, four in `pico2_w_release`); **C3** only `pico2_w_release` (one hunk); **C4** five (not `pico2_w_release`; one hunk); **C5** six (one hunk each, and two instructions fewer: `handleTouch( )` 4,667 -> 4,665, 4,585 -> 4,583 in `pico_wiretft`, 3,264 -> 3,262 in `pico2_w_release`; each of the six images 8 B smaller, and their budgets, `.bin` records and OTA rows moved with them in that commit). Everywhere else the instruction count is the same (4,667; 4,585; 3,262 -> 3,264 at C2) and the `bl` count is the same at every step (626, 612, 224): no far jump was added. The decoded diff of each is `decoded_vs_c<N-1>.txt`, the five points of the source proof `source_proof.txt` (point 3 is a pattern scan of `handleTouch( )`'s locals, not a parse), and `move_check.txt` shows by script that every hunk of `DisplayManager_Touch.cpp` is a pure move into the action it names, with the bool actions read as `changed = actX( );`. `loopCore1( )` and `handleTouchPanelV24( )` are identical in all ten images at every step.
- **The Ângulo, function by function.** C2: `handleTouch( )` and `uaToqueMenus( )`; C3: `handleTouch( )` and `uaToqueAjustes( )`; C4: `uaToqueAjustes( )`; C5: `handleTouch( )` and `uaToqueAjustes( )` (`eventoDoSom( )` was inlined, it never showed as a function of its own); C6: `uaToqueAjustes( )`; C7 and C8: nothing. Used: 812,600 -> 812,552 (C2, -48) -> 812,696 (C3, +144) -> 813,016 (C4, +320) -> 813,008 (C5, -8) -> 813,048 B (C6, +40), unchanged at C7 and C8; `.bin` 824,632 -> 825,080 B; budget = measured at every step. The growth is the plan's R4: each action is compiled twice in the Ângulo image (inlined into `handleTouch( )` and into its own glue) until S10b.
- **The frames.** The six host screen groups of the Ângulo are 840 of 840 frames equal to the step before, at every step.
- **The glue check.** `uaToqueAjustes( )` and `uaToqueMenus( )` are not compiled on the host, so `tools_s8a/glue_check.py` (kept with the stage's tools, like S7's) reads their source: for every target of the clock, the alignment, the melody picker and the sounds list, and the themes' and the language's rows, the gate and its zone, the action called once with its argument or sign, the range tests and the `if ( !a )` guards, statement by statement. Its mutants (21, the twentieth in two forms) are each refused for their own reason: 14 at C4 (`stages/s8a_c4/glue_check.txt`), 22 at C6 (`stages/s8a_c6/glue_check.txt`), and again at C7 and C8.
- **Three differences outside `handleTouch( )`'s code** that `tools_s8a/accept.py` took, each a consequence of the layout and not of a change of content (the plan's wording, "only `handleTouch( )` changes", did not foresee them):
  1. **C3, `pico_wiretft_angulo`:** `CLOCK_COL_X` and `CLOCK_COL_W`, constexpr tables of a header, had a copy in each unit that indexes them with a runtime value; `uaToqueAjustes( )` stopped doing it, so the image holds one copy of each instead of two (each object is 10 B, and the bytes of the two copies are equal).
  2. **C5, `pico_w_asserts`:** the census of texts reports `b'Pb'` only in A and `b'PZ'` only in B. They are not strings: `'P'` is the top byte of the literal 0x50100000 (the USB controller's base) at 0x20001137 in `.data`, in the literal pool of the RAM copy of TinyUSB's `dcd_rp2040_irq`, and the next byte is the low byte of the literal after it, 0x100a0062 in C4 and 0x100a005a in C5: a pointer into `.rodata`, which moved by -8 (starts at 269,050,280 and 269,050,272) because `.text` shrank by 8 B (602,536 -> 602,528). The first label of this difference, in `stages/s8a_c5/accept.txt` (a record, left as it was) and in the report of the batch, said "jump-table bytes inside `handleTouch( )`": that was wrong, found by the review, and the rule that took it was too wide (see the last paragraph of this section).
  3. **C5, `pico2_w_release`:** `__binary_info_header_end`, a symbol of five words at 0x10000124 in both images, whose first difference is the word 0x000f3b68 -> 0x000f3b60, the same -8 as `.text` (569,108 -> 569,100 B) and `__binary_info_end` (0x100f0628 -> 0x100f0620).
  The other differences the verifier listed were `_fini`, alignment padding at the end of the Ângulo's text, accepted since S6.
- **The allowances (C7).** `nm -C` of the C6 Ângulo ELF lists none of `drawSettingsThemes`, `drawSettingsLang`, `drawSettingsDisplayOffset`, `drawClockEntry`, `drawClockValues`, `drawSettingsSounds` and `drawMelodySelect` (`stages/s8a_c7/nm_precondition.txt`; `pico_wiretft` links them, 1, 1, 1, 1, 2, 3 and 3 symbols); outside the files that define them their callers are in `UiOriginal.h`, and `drawClockValues( )` is called, twice, by `drawClockEntry( )` alone (`callers.txt`). The six `UA_DESVIO( )` of the draw functions go (`drawClockValues( )` never had one, so C7's subject line, which says seven, counts the names in `retired_draws.txt` and not the lines removed, and the commit stays as it is): `DisplayManager_Settings.cpp` 6 -> 2, `DisplayManager_Calibration.cpp` 3 -> 2, `DisplayManager_i18n.cpp` 1 -> none. The allowances are 32, from 38: the plan said 34 -> 28, but its base lacked the four allowances of the history fix in `AppManager_Events.cpp`, which stay, so `identity` is 10 and not 6 and `driver-hook` goes 28 -> 22. `retired_draws.txt` holds 23 names and the verifier's V-NM gives 0 for all of them. The nine images stayed byte-equal at C7, so no `__LINE__` user moved.
- **The pure part (C8).** `pio test -e native` 316 -> 325 cases (nine new: four for `volumeStep( )`, four for `offsetStep( )`, one for `langSlotCount( )`), and the READMEs' counts follow (717 -> 726, `check_readme_numbers.py`). The 31 mutants of the header were applied in turn (`stages/s8a_c8/native_m*.txt`, with the clean runs before and after): the 28 killable ones fail exactly the cases named for them and the three equivalent ones (`v > 9`, `v < 91` and `dir <= 0`) fail none. A host model with the old code as oracle (`model_settingssteps.txt`: the bodies of the actions as they stood at C7, compiled) compares 0..255 x both directions x both volumes, the preview -8..8 x the four arrows and both slot counts: 0 differences in 1,094 comparisons for the header, more than 0 for every killable mutant. One mutant of `langSlotCount( )` (a constant 2, `LANG_COUNT` today) equals the old code on the model's domain and is killed by the native case with three slots. `langSlotCount( )` does not retire every copy of the slot rule: two remain, `src/DisplayManager_i18n.cpp:187` (`int activeSlots = _activeLangLoaded ? LANG_COUNT : 1;`, in the original's `drawSettingsLang( )`) and `src/ui/angulo/DisplayManager_UiA_Menus.cpp:141` (the Ângulo's draw, `if ( _activeLangLoaded )`, which builds the language rows: English, and the pack when one is loaded). Calling the function there would change compiled code, the original's in the products' images, and C8 was accepted only with the images unchanged, so both stay; S10b retires the Ângulo's copy and the original's draw keeps its own.
- **R1, the `bool &changed` form, one attempt, dropped** (`stages/s8a_r1/r1.txt`). Three images built from C8 with the four arrows and Redefinir taking `bool &changed` and storing true in it, as the branch did (E1), and with the alignment's branch of `handleTouch( )` put back to its text at 9bef346 (E2). E1 is byte for byte C8 (E0) in all three, so the return form was not what moved the bytes at C4; E2 differs from both by one hunk each (two stack-slot loads in the opposite order in the release and asserts images; the instruction and `bl` counts are the same, 4,665 and 626), and none of the three is the image of 9bef346, because C2, C3 and C5 moved `handleTouch( )` too. The form was not kept.
- **Gates.** `check_ui_boundary.py` (0 findings) and its test (285 checks); `check_authz.py` and its test; `check_feature_sprawl.py` 362 sites, as at C0; `check_flash_budget.py --table`; `-Werror` on the ten images; `python3 tools_s4/ci_gates_local.py` over the final tree (`stages/ci_gates_<hash>.txt`).

**The greps of S8A_PLANO.md 5.3, at C0 and after C8:** `UaToque::TRADUZIDO` on the Ajustes glue 11 -> 1 (the raw case of the touch sensitivity and the calibration, S8b's); `pushUiEvent` on the Menus glue 3 -> 1 (the sensor menu's block, S9's); on the Ajustes glue 4 -> 3 (limits, maintenance and PIN policy, S8b's); the five `EVT_` names in `src/ui/angulo` 5 -> 0; `requestPreviewSound`, `requestVolumePreview`, `requestAlarmVolumePreview`, `showMuteConfirm` and `*flag` on the Ajustes glue 7 -> 0; the stores of the core's state in it 21 -> 0; `pushUiEvent` in `DisplayManager_Touch.cpp` 9 -> 4 (the theme's, the language's, the clock's Save, the alignment's Apply and the sounds' Save). `OFFSET_LIMITE`'s comment was the only `Touch.cpp:NNN` citation of S8a's groups left in the Ajustes glue, and it went with the constant; the ones that remain (the limits editor, the maintenance window, the raw touch of the sensitivity and the calibration) are S8b's.

**Where `S8A_PLANO.md` was wrong or incomplete** (the plan is not in the repository; its corrections, as they stand here):

1. The allowances were 38, not 34, at the base (above), and the C7 total is 32.
2. "~25" actions is 22, with no `UiChange`; the `TRADUZIDO` criterion catches only the clock and the alignment, and the other three modes' problem was copied logic, which the greps above measure.
3. `langSlotCount( packLoaded )` could not stay a one-argument function (above).
4. The acceptance wording did not foresee layout-driven differences outside `handleTouch( )` (the three above).
5. R1's premise, that the return form of the alignment's actions might be what moved the bytes, did not hold (above).
6. The Ângulo's per-sample cost of `TRATADO` on the clock and the alignment (a frame comparison for every sample of a held arrow, from the plan) and the new `_repaintSettings` / `_forceSettingsRedraw` stores of the melody picker and the sounds list (taken by its pass as frame requests) are as the plan says and were not measured.
7. Found, not S8a's: the comment in `AppManager_Events.cpp` that says Core 0 "applies the new offset to the TFT" for the alignment's event while the handler never calls `setDisplayOffset( )` (whether a callee does was not checked); `showSettingsDisplayOffset( )` resets the original's `_lastOffsetDraw*` (S10a/b).
8. The alignment's Save (`actOffsetApply( )`: Core 0 zeroes the touch calibration's magic, saves, and calibrates again) and the clock's Save (`actClockSave( )`: `setManualTime( )`, which rewrites this boot's history stamps) must never be pressed on the bench account; the theme's, the language's and the sounds' Save write the configuration, so check and then put the old value back.

**The review of C1 to C6** came back with reservations (no blocker): the code moves and the glue are right (`move_check.py` reads C2 to C6 as pure moves, the gates sit in the original's order, seven mutants of the reviewer's own were refused), and the defects are in the proof tooling and in one recorded explanation. (1) The recorded reason of the `pico_w_asserts` difference at C5 is false (above), and `accept.py`'s rule for short strings was too wide: a doctored listing with other letters was accepted with the same label. (2) `tools/elf_funcoes.py` printed "(N objetos em A, M em B)" for a name also when the surviving object's content differs, and `accept.py` took that label as proof that only the number of copies moved. (3) `glue_check.py` did not compare the three statements that decide `voltar`, `linha` and the row `i` in `uaToqueMenus( )`. (4) The header of `DisplayManager_UiA_Ajustes.cpp` still said that each action cites the old line it reproduces. (5) The theme's and the language's events no longer zero `param` in the Ângulo (above). (6) The walk `tools/tft_ui/roteiro_ui.py` waited for the melody picker with a target type that the sounds list also carries. **C10 answers them.** (1) `tools_s8a/accept.py` no longer reads the strings the tool printed for a difference of texts: given the two stage directories it reads the ELFs, requires every word of `.rodata` and `.data` that differs to have moved by exactly the change in the size of `.text`, puts them back and compares the text census again; for C5 that is 1,596 words moved by -8 B (`stages/s8a_c5/accept_corrected.txt`; `accept.txt` stays as the record it was), and `__binary_info_header_end` must show the same -8 in its differing word. Copies of the ELFs with a word moved by 8 more than the layout moved it, or the header word moved by 16 more, are refused for those reasons, and so is one with a letter of a text changed so that its word moves by anything but the change of `.text` (`stages/s8a_c10/accept_bites.txt`). A letter lowered by exactly 8 in the first byte of an aligned word moves its word by exactly that change, and this first version of the rule accepted it as a moved pointer (found by the review of C7 to C10, answered by C11 below). (2) `tools/elf_funcoes.py` ends the label with `conteúdo igual` or `conteúdo diferente`, the first only when every surplus copy has a twin with the same bytes on the other side and nothing else is unpaired, and `accept.py` takes only the first; `tools/test_elf_funcoes.py` has the cases 5j and 5k and the mutant m27 (it fails exactly 5k), four earlier mutants fail the two new cases as well, and the suite is 39 cases and 27 mutants. C3's `CLOCK_COL_X` and `CLOCK_COL_W` pass the strict rule (`stages/s8a_c3/accept_corrected.txt`), and a copy of the Ângulo image with one byte of the surviving `CLOCK_COL_X` changed is refused. (3) `glue_check.py` compares the statements that decide `voltar`, `linha` and `i`, with three more mutants (25 runs refused in all). (4) The header of `DisplayManager_UiA_Ajustes.cpp` says which cases name the core's actions and which cite old lines. (5) is recorded in the list above. (6) The wait for the picker is `tem( s, MEL_USAR, -1 )` and the tap on its Cancelar takes index -1: the picker's targets carry index -1 and the list's rows their row index; on the host samples the old condition is true on the list pages that have a melody row (`sons_p1`, `sons_p1_desligados`, `sons_p2`) and on every picker page, the new one false on every list page and true on every picker page (`stages/s8a_c10/roteiro_bite.txt`).

**The review of C7 to C10** came back with reservations (no blocker). (1) `accept.py` took a text edited by exactly -8 in the first byte of an aligned word for a moved pointer: in the C5 `pico_w_asserts` image one byte at 0x1009764c, 'write sem memori' to 'orite sem memori', gave 1,597 moved words where the real pair gives 1,596, because the rule asked only for the change of `.text`. (2) Two more copies of the language slot rule remain (above). (3) C7's subject line says seven draw functions lose their `UA_DESVIO( )`; six lines went (above). (4) The docstring of `glue_check.py` said 21 mutants; it refuses 25 runs of 24 mutants. **C11 answers them.** (1) A moved word must also be a flash address at both ends: the old value inside the flash that holds the first image, the new one inside the second's (the stored ranges of the ELF's `PT_LOAD` segments). RAM is left out on purpose, which is stricter than the review asked (flash and RAM): nothing in RAM moves with the size of `.text`, and a string that starts with a space after a two-letter one reads as 0x2000xxxx. `stages/s8a_c11/accept_bites.txt` has the reviewer's copy (accepted by C10's rule, kept as `tools_s8a/accept_v2.py`, with 1,597 words; refused by this one: 0x74697277 -> 0x7469726f is no address), the real C4 -> C5 pair of `pico_w_asserts` (accepted, 1,596 words), the C4 -> C5 pair of `pico2_w_release` and the C2 -> C3 pair of the Ângulo (accepted), the real C5 pair plus a RAM address of `.data` lowered by 8 (accepted by C10's rule with 1,597 words, refused by this one), and the doctored pairs of C10 (refused as before). (2) Recorded in the C8 paragraph above: `src/DisplayManager_i18n.cpp:187` and `src/ui/angulo/DisplayManager_UiA_Menus.cpp:141`. (3) Recorded in the C7 paragraph above; the commit is not rewritten. (4) The docstring says 24 mutants (the 20th in its two forms, 25 runs).

**Not done.** The Ajustes 2 group (limits, maintenance, PIN policy, touch sensitivity and calibration) is S8b's; the sensor's alarm menu is S9's. C8's optional `langSlotCount( )` was made.

**Bench checks pending** (the main session runs them; never press the alignment's Save or the clock's Save). For the Ângulo: the alignment pad moves the image live and stops at +-4, holding an arrow repeats, Redefinir centres, Voltar restores the saved offset; the clock: held arrows repeat, Pular at boot goes to the dashboard, Voltar to the menu; the melody picker: a row plays its preview, Usar turns the sound on with that melody, unmutes and plays the confirmation, Voltar keeps nothing; the volumes step by 10 and preview at 0 and 100; the switches flip and unmute; Silenciar tudo asks first; the theme's, the language's and the sounds' Save: check, then put the old value back. For the original (`pico_wiretft`, `pico_w_release`): the same, plus the preview rows, the wraps of the melody arrows, the clock's dead value band, the halves of the volume rows, and rows 2 to 6 of the sounds list (they open the melody picker when the sound is off and turn it off when it is on); compare `show config` and the binary log's UiEvent sequence (H2).

### 2.12 The Ajustes 2 group's touches (S8b)

**S8b (2026-10-16, branch `feat/ui-drivers-s8b`, base 2af475d) did this, in part.** What a touch does on the limits editor, the maintenance window, the PIN policy, the touch sensitivity and the calibration is a core action wherever the original's branch had logic of its own, and the Ângulo runs the actions in place of its copies; the limits' step, which S8b deferred (B1), was taken in S10a (section 2.14).

- **Limits editor:** `actLimitFocus( idx )` (a tap on a bar, or the Ângulo's pager: the repeat forgotten, the focus, the repaint request), `actLimitsBack( )` and `actLimitsSave( )` (Salvar: since 2026-10-31 it posts a copy of the working copy into the box `_limitsSaved` under `_stateMutex`, and then pushes the event, with `id` and `param` both set; Core 0 takes the copy out under the same lock). `actLimitStep( )` was declared and not defined until S10a took B1 (section 2.14).
- **Maintenance window:** `actMaintFocus( row )`, `actMaintStep( row, delta )` (hours 0..720 without wrapping, minutes in fives wrapping 55 <-> 0) and `actMaintPrimary( open )` (the permission test, the zero test with the error sound, `EVT_MAINT_SET`; it returns where the original returned `true`, which `move_check.py` verifies for every void action with an early return).
- **PIN policy:** `actPolicySet( minLen, keypad, alphabet )` (the clamp, the three stores, the repaint request) and `actPolicySave( )`. The two UIs keep their own controls (decision D2): the original cycles the selected row and wraps, the Ângulo has buttons that stop at the ends.
- **Touch sensitivity and calibration:** the core keeps the two blocks of `handleTouch( )` that read the touch before any driver, and asks the driver where their buttons are: `uiSensCancelHit( x, y )` (original: `y > 195 && x < 125`; Ângulo: the target `SENSIB_CANCELAR` of the frame on the glass, behind a test of `s_modo`, because target types are numbered per screen) and `uiCalVerdictHit( x, y )` (original: `y >= 185`; Ângulo: `CALIB_BOTAO`). What Cancelar and the verdict's button do is `actSensCancel( )` and `actCalVerdictTap( )`, moved word for word; both write `_uiMode` at boot with no `_stateMutex`, as they always did. The Ângulo's raw case answers `TRATADO` (`UaToque::TRADUZIDO` 1 -> 0 on the Ajustes glue), and a tap in the band of the old zone beside the Ângulo's Cancelar is a sample like any other touch (decision D1 on its default).
- **The allowances (C8).** `nm -C` of the C7 Ângulo ELF links none of `drawAlarmEdit`, `drawMaintEntry`, `drawPinPolicy`, `drawTouchSensitivity` and `drawTouchCalibration` (S3's note that `drawTouchCalibration` was a linked stub no longer holds: no caller outside `UiOriginal.h`). Four of the five `UA_DESVIO( );` go (`UA_DESVIO` sites 15 -> 11, allowances 32 -> 28: Settings 2 -> 1, Users 6 -> 5, Calibration removed) and the five names join `retired_draws.txt`. The fifth, `drawMaintEntry( )`'s, stays: its call of the static `maintMinutesLeft( )` is behind the early return, and without it the compiler stops inlining the helper into `handleTouchPanelV24( )` in the Ângulo image alone (1363 -> 1325 instructions, bl 128 -> 126; the nine products stay byte-equal), which the plan's "layout only" does not allow.
- **The pure part (C9).** Q2, Q3 and Q4 (`maintHoursStep( )`, `maintMinutesStep( )`, `maintMinutesLeft( until, now )`) were tried and dropped whole: Q2 moved `handleTouchPanelV24( )` in every image, Q3 in `pico2_w_release` alone, Q4 changed the static `maintMinutesLeft( )` in every product (`stages/s8b_c9_probe.txt`). Q1, `limitStepMag( heldMs )` in `SettingsSteps.h` for the ramp of a limit's arrow, keeps the nine products byte-equal and moves the Ângulo's copy of `handleTouch( )` by two `ldr` that exchange their stack offsets (4 changed lines of the decoded diff, the same 4613 instructions); the plan's list for the step does not allow it, so the commit waited on a side branch until the main session accepted it (2026-10-16); it is part of the branch now. The host model (the old two lines as the oracle) found 0 differences in 10,002 inputs and the five mutants of the header failed exactly their cases (`stages/s8b_c9/`).

**How each step was proved** (`tools_s8b/verify_step.sh`; numbers in `stages/s8b_c1` to `s8b_c8`; nothing ran on a board, the bench is the main session's):

- **The nine product images.** Byte-equal to the base at C1, C2, C5, C7 and C8. **C6** moved `handleTouchPanelV24( )` in seven images (1333 -> 1327 instructions in `pico_w_release`, `pico_w_test`, `pico_w_test_https` and `pico_w_asserts`, 1329 -> 1325 in `pico_wiretft`, 1006 -> 1003 in `pico2_w_release`, 1369 -> 1363 in the Ângulo; the same bl): register allocation and the rematerialisation of stack addresses, accepted by the main session on the strength of the source being a pure move (`move_check.py`), not proved on the control-flow graph. Two layout items came with it: `__binary_info_header_end` of `pico2_w_release`, and `CYW43lwIP::encryptionType( )`, whose table `tools/elf_funcoes.py` now reads as bytes, exactly as long as the code says (the review of that commit found the first version too loose, and the second compares a table with no second chance).
- **The Ângulo, function by function.** C2: `uaToqueAjustes( )`; C5: `uaToqueAjustes( )`; C6: `handleTouchPanelV24( )` and `uaToqueAjustes( )`; C7: `handleTouch( )` (4607 -> 4613 instructions, bl 611 -> 613: the hooks are ordinary functions there), `uaToqueAjustes( )` (1339 -> 1338, bl 108 -> 109) and the two hooks, only in B; C8: nothing. `.bin` 831,152 B at the base, 831,312 B after C8 (+160 B, recorded in the budget and the OTA table with each commit).
- **The frames.** The six host screen groups of the Ângulo are 840 of 840 frames equal to the step before, at every step.
- **The glue check.** `tools_s8b/glue_check.py` reads `uaToqueAjustes( )`, `uaToqueMenus( )` and the two hooks: for every target of the limits, the maintenance window and the PIN policy, the gate and its zone, the action called once with its arguments and sign, the range tests, the stale-frame test and the `if ( !a )` guards; 51 mutants at C7 (items 1 to 19 of the plan's section 6, the 19th in four forms, items 20 and 21 for the hooks, and S8a's 25), each refused for its own reason; two mutants of the C4 builder wait for C4.
- **The review's reservations on C1 to C6**, answered: the C6 message says what the register allocation did and that it was not proved on the graph; `tools/elf_funcoes.py` gives a Thumb-1 case table its exact extent from the `cmp` before the helper's call and compares its bytes with no relocation excuse (43 cases and 30 mutants); section 4.4 states the forms of B1 as they were built.
- **Gates.** `check_ui_boundary.py` and its test, `check_authz.py` and its test, `check_feature_sprawl.py` 363 sites (S8a's 362 plus one from the merge of main), `check_flash_budget.py --table`, `-Werror` on the ten images, `pio test -e native` 337 cases (S8a's 325 plus the merge's), and `python3 tools_s8b/ci_gates_local.py` over the final tree (`stages/s8b_c10/ci_gates.txt`).

**Where `S8B_PLANO.md` was wrong or incomplete** (the plan is not in the repository):

1. The baselines of section 5.3 were 28 stores (not about 25) and 12 citations (not about 14) on the Ajustes glue; the sprawl is 363 and the native suite 337.
2. B1's lambdas inside an always_inline action move bytes in three forms (R1 happened), and C4 depends on B1 because only B1 makes the Ângulo's step raise the repaint request; section 4.4.
3. The acceptance wording did not foresee that a register allocation differs (C6), that a `UA_DESVIO( )` behind which a static helper is called changes the Ângulo's inlining (C8), or that the original's `handleTouch( )` inside the Ângulo moves when its source changes (C9).
4. `move_check.py` read only `Touch.cpp`; it now reads `Users.cpp` too, compares each file's hunks together (a body may stand far from the code it left) and checks the `return` contract of a void action in `handleTouchPanelV24( )`.
5. The mutant "`<` for `<=`" of `maintMinutesLeft( )` is equivalent by reading (until == now gives 59 / 60 = 0), not killed by (100, 100) as the plan says; Q4 was dropped before any run could show it.
6. The plan's section 6 lists 21 glue items; the check runs them as above plus S8a's mutants (labelled S8a-N).
7. Found, not S8b's: `AppManager::panelSaveAlarmLimits( )` reads the live working copy by reference with no copy and no lock (section 4.4).

**B1 was taken in S10a (2026-10-21).** Its C2 (`f30c3c4a`) moved the three lambdas into `limitStepOn( )` and the step into `actLimitStep( )`, in form Fb, one of the three forms that section 4.4 recorded as built and not kept; its C3 (`e979a02f`) made the Ângulo's builder read the working copy only. Section 2.14 has what was built, how it was proved and what the bench saw.

### 2.13 The Identidade group's touches (S9)

**S9 (2026-10-21, branch `feat/ui-drivers-s9`, base d8b6f5f0) did this.** What a touch does on the PIN keypad, the account picker, the users list, the account editor, the sensor menu and the text keyboard is a core action, and the Ângulo runs the actions instead of translating its taps to the old layout's coordinates. There are eighteen actions and one predicate in `src/DisplayManager_Actions.h` (the plan said sixteen, and two more with its optional step, which was taken). The group is authentication, so each step carried more proof than the earlier stages: every store to a lockout field, a session bit or an entry buffer, and every call and event that leads to Core 0, was listed before and after the step (`tools_s9/sec_inventory.py`: the diff may only move statements), and a product image that changed had to keep the same calls in the same order and the same conditional branches (`tools_s9/calls_check.py`). The plan's eight decisions ran on their defaults: the two UIs keep their own navigation (D1) and their own check of a new account's name (D2); the Ângulo keeps two guards of its own (D3); a popup owns every touch, the original's rule (D4); no lockout is tripped on the bench (D5); the legacy keyboard moves unchanged (D6); S8b's deferred limits step goes to S9b (D7); and `PinKeypad.h` is split in S10a, not here (D8, section 4.2).

- **PIN keypad (C2):** `pinInputBlocked( )`, a predicate (a timed rung of the lockout, the permanent one, or Core 0 reading the entry), and six actions, in `src/DisplayManager_PinActions.h`: `actPinPopupDrop( )`, `actPinBackspace( )` (with the re-deal on a dealt keypad), `actPinOk( )`, `actPinPopupKey( mem, n, i )`, `actPinOrderedKey( k, alphabet )` and `actPinCard( g, k )` (the keypad's ceiling, then a new deal on every tap). Both drivers test the predicate after Cancelar and before every other control. The Ângulo calls the actions by target, under the original's zones (0, 1, 2, 10 + k, 20 + k, 40 + the popup key + 1); `ua::pinPontoAntigo( )` and its neutral point go. With a popup up, any touch that is not one of its keys only closes it, the original's rule (D4). A host model proved the old translation an identity on key indices at the base (246 checks); the key-over-popup overlaps it found (16 of 81) were not reachable, since the frame lists no key under a popup. Nothing that makes the keypad safe moved: the lockout ladder, the deal, Core 0's digest comparison and the logging stay where they were. The keypad exists only where a panel proves a PIN, and `SIMUT_PANEL_PIN` follows the TFT panel (`SystemDefs_Limits.h`): the seven are declared with the keypad's state, in `DisplayManager.h`'s `SIMUT_PANEL_PIN` section, and their header has no `#if`, because only TFT-only units include it (`DisplayManager_Users.cpp` and the Ângulo's identity glue; a `static_assert` says so to the compiler). C1 and C2 had put them under two new `#if SIMUT_PANEL_PIN`, which the feature-sprawl gate refuses (`tools/check_feature_sprawl.py`: a shared file's sites only go down); C8 moved them byte for byte, and the sites are the base's 363 again.
- **Account picker, users list and sensor menu (C3):** `actUserOpen( picking )` (the picker opens the account's PIN entry through `enterPinFor( )`, which refuses a locked account; the list opens the account's editor), `actUserNew( )` (behind `PERM_USER_MGR`, the name first on the text keyboard) and `actSensorMenuActivate( row )` (each row behind its permission bit, which Core 0 checks again). The table of the three bits moved into the action, so the Ângulo's copy of it went. The sensor menu keeps the original's closure, which is out of line in the seven TFT images: its body is now the action.
- **Account editor (C4):** `actUserEditActivate( idx, lockedBits )` (rows 0-2 flip a bit of the copy, refused on the admin's account, whose bits are locked; row 3 the account's PIN entry; row 4 asks before the delete, the mode stored under `_stateMutex`) and `actUserEditSave( lockedBits )` (a new account goes on to its PIN entry, the admin's back to the list, any other asks Core 0 to store its bits). The editor's closure stays too, its body the action. The Ângulo keeps its guard on Excluir (D3): the original has no delete row on the admin's account, and the Ângulo lists one on every account that is not new.
- **Text keyboard (C5, C6):** `actPwdResult( )` (the result screen's button: the menu after a saved PIN, or both buffers wiped for another try), `actPwdLeave( )` (the X), `actPwdNameAccept( name )` (a new account's name, then its editor), `actPwdLegacyOk( )` (the legacy PIN's two passes, with `EVT_SAVE_PASSWORD`), `actPwdToggleShow( )` (the boxes, plain or masked), `actPwdAppend( buf, c )` and `actPwdBackspace( buf )` (the buffer's stores). The Ângulo keeps its own keyboard (layers, shift) and its own check of a new account's name (D2: it trims, and refuses an empty name, bad characters, "admin" and a name in use, on the keyboard, the rules Core 0 applies after the PIN) and gives W3 the trimmed name. The plan's `bool actPwdAppend( )` would have tested the room inside and run `popupChar( )` before that test, an order the original does not have; the two typing actions are `void`, and each caller keeps its room test (`_kbCursor < kbMaxLen( )`, `_kbCursor > 0`).
- **The allowances (C7):** `nm -C` of the Ângulo ELF links none of the fourteen identity draws (`drawPinScreen`, `drawPinPadInto`, `drawPinCardInto`, `drawPinPopupInto`, `drawPinDotsInto`, `blitPinCards`, `drawUserList`, `drawUserRowInto`, `drawAuthUser`, `drawSettingsUsers`, `drawUserEdit`, `drawSettingsPassword`, `drawPasswordMessage`, `drawAlarmSensorMenu`). Four of the five `UA_DESVIO( );` that began them go (`drawPinScreen`, `drawUserList`, `drawUserEdit`, `drawSettingsPassword`): sites 11 -> 7, allowances 28 -> 24 (`tools/ui_boundary.json`: Users 5 -> 2, Settings removed; driver-hook 18 -> 14, identity stays 10), and the fourteen names join `retired_draws.txt` (28 -> 42). The fifth, `drawAlarmSensorMenu( )`'s, stays, for S8b's reason (section 2.12): without it the draw's call of the static `maintMinutesLeft( )` comes alive, the compiler stops inlining the helper, and `handleTouchPanelV24( )` goes from 1339 to 1302 instructions in the Ângulo image alone. The Ângulo's `.bin` kept its size and changed its hash: the linker ordered 37 of its 62 veneers differently (262 bytes, all in `bl` to veneers or in the veneers' literal words; `tools/elf_funcoes.py` finds every function equal).

**How each step was proved** (`tools_s9/verify_step.sh`; numbers in `stages/s9_c1` to `s9_c8`; nothing ran on a board, the bench being busy from 2026-10-21):

- **The nine product images.** Byte-equal to the base at C1 and C2. C3 and C4 moved `handleTouchPanelV24( )` in the six TFT images and nowhere else:
  - C3, 8 B smaller each: with the permission table inside its action, the sensor menu's closure captures `this` alone, becomes an IPA-SRA clone and builds the table itself.
  - C4, 8 B smaller in five images and the same size in `pico2_w_release`: the editor's table is built in the row's own path, and the handler's frame is 8 B smaller; on the RP2350 one `cmp r3, #0` + `bne` became `cbnz r3`.

  Both were accepted by the plan's fallback, with the decoded diff, the five-point source proof (`tools_s9/proof_s9.py`), `move_check.py`, `sec_inventory.py` and `calls_check.py`: the same calls in the same order and the same conditional branches (126 calls and 152 branches on the RP2040 TFT images; on `pico2_w_release` 85 calls and 155 conditional entries, 19 of them IT blocks). The three permission bits were read back from the code's literal word and halfword (0x0400, 0x0800, 0x1000, the values of `SystemDefs_Limits.h`). The budgets went down with the images, in the same commits. C5 to C8: byte-equal to C4's images. `pico_w_alpha`, `pico_w_air` and `pico_wire` were byte-equal throughout.
- **The Ângulo, function by function.** C2: `uaToqueIdentidade( )` (870 -> 1,126 instructions) and `_fini`'s alignment; C3: `handleTouchPanelV24( )` and `uaToqueMenus( )` (its static table `kPrecisa` goes); C4: `handleTouchPanelV24( )` and `uaToqueIdentidade( )` (its static table `bits` goes); C5: `uaToqueIdentidade( )`; C6: the glue's typing closure (43 -> 39 instructions); C7: nothing but the veneers' order; C8: nothing (the keypad's header and the comments). Since C3, `verify_step.sh` holds the changed functions to the step's list (`ELF_ALLOWED`) instead of printing them for a reader. `.bin` 833,040 B at the base, 833,656 B after C8 (+616 B, recorded with each commit): the actions are compiled into the glue and into the original's handler, which the Ângulo links until S10b.
- **The frames.** The six host screen groups of the Ângulo are 840 of 840 frames equal to the step before, at every step.
- **The security inventory.** Every step from C1 to C8: "only moves". Its controls (`--selftest`): a hand-made tree that only moves passes, and six doctored trees are each refused for their own reason (a re-deal dropped, a lockout store added, the PIN entry opened without `enterPinFor( )`, a permission test dropped, a wipe dropped, the predicate weakened). One allowance: the Ângulo's Cancelar calls `pinCancel( )` once, in `uaToqueIdentidade( )` (a second call is refused); one alias: W3's `name` reads as the original's `_kbBuffer`, and the inventory checks that the original passes exactly that (its item 6, since the review of C5-C8), as `move_check.py` does.
- **The glue check.** `tools_s9/glue_check.py`: 23 rules on `uaToqueIdentidade( )` and the sensor menu's case, each enforced from the step that moves its group, and 40 mutants, each refused by its own rule (the plan's 23 items, several in more than one form, and eight added during S9: 18c and 18d with rule 18's correction at C3, 14b after the review of C3-C4, 19b to 19d, 22d and 22e after the review of C5-C8); S8a's 25 and S8b's 26 mutants rerun at every step.
- **The greps of the plan, base -> C8:** `pushUiEvent` in the identity glue 2 -> 0, in the sensor menu's 1 -> 0; `EVT_USER_PERMS`, `EVT_SAVE_PASSWORD` and `EVT_ALARM_BLOCK` in `src/ui/angulo` 3 -> 0; the `pushUiEvent(` calls of `Users.cpp` and `Touch.cpp` 4 -> 2 and 3 -> 2; action declarations 61 -> 79 and definitions 60 -> 78; stores to the group's state in the identity glue 0, and `_errorSoundPending` stores 5 -> 2 (D2's and D3's); `file:line` citations in `uaToqueIdentidade( )` 0; `UaToque::TRADUZIDO` 3 -> 1 (the Gráficos glue; S10b's C2 took the last, 0); `_panelAuthFor` in the display and UI units 0. Two of the plan's zeros hold in code and not in comments: the header of `IdentidadeToque.h` names the old translation in the sentence that says it went, and three comments (`IdentidadeToque.h`, `telas/IdentidadePin.h`) number the keys by `PinKb::keyAt( )` and `popupKeyAt( )`, the numbering the zones 10 + k, 20 + k and 40 + j depend on. No code of the Ângulo reads the old geometry.
- **Gates.** The local run of every step of the CI's gates, static-analysis and fuzz jobs over the final tree (`tools_s9/ci_gates_local.py`, `stages/s9_c8/ci_gates.txt`): 52 of 52 pass, and the sanitizers' pass is the CI's alone (it needs its own build flags). Among them `check_ui_boundary.py` (0 findings), `check_authz.py` and its test, `check_feature_sprawl.py` (363 sites, the base's, after C8's first commit), the OTA table (`check_flash_budget.py --table`), cppcheck (clean), the web validators' 60 s fuzz, and the eleven native suites, 742 cases; the validators' suite keeps the base's 341, since S9 has no pure function to hold on the host. `-Werror` held on the ten images at every step.

**The reviews.** C1-C2, on their own (the keypad): COM RESSALVAS, two comments only (fc3c1ea8). C3-C4: COM RESSALVAS; the product code a faithful, gated move, and four holes in the tools, each closed with a control that fails on the old version (below, item 3). C5-C8: COM RESSALVAS; no product defect, and three findings, fixed the same day: glue rule 19 read only the swap of the two Salvar arms, and now also holds the name arm's own `break`, its error path and the trimmed name it gives W3 (mutants 19b to 19d), and rule 22 refuses any `memset` and any store through the active buffer (22d, 22e); the inventory's parameter alias now checks the argument the original passes (its item 6); and four line citations of this section had moved by a line, so they name functions instead.

**Where `S9_PLANO.md` was wrong or incomplete** (the plan is not in the repository):

1. The keypad's declarations need `#if SIMUT_PANEL_PIN`: `PinKb::` exists only in the images with a panel PIN, and the first build of C1 failed on alpha, air and wire.
2. Glue rule 18 first asked for the Ângulo's copy of the sensor menu's permission test, which the plan's own section 3 removes; corrected at C3, with two more mutants.
3. The fallback's tools had holes, found by the main session and by the review of C3-C4: `calls_check.py` did not count the RP2350's `cbz` / `cbnz`, so a condition flipped between them read as `same`, nor did it see a call inside an IT block; `move_check.py`'s early-return contract did not ask what the braces after a call close, so a call in a loop's body passed, and so did an if inside a loop without braces; glue rule 14 did not read E1's gate argument; `verify_step.sh` read a refusal of `elf_funcoes.py` as a pass and skipped an image with no build log. Each fix has a control that fails on the old version; C2 -> C3 and C3 -> C4 were rerun with the fixed tools and stay `same`.
4. C3's first acceptance lacked the five-point source proof the plan asks for; it was written at C4 for both steps.
5. W6's signature (`bool`, the room test inside) would have reordered the original's evaluation; it is `void` (above).
6. The plan's zero for the old geometry's names holds in code only (above).
7. The plan lists the feature sprawl among the stop conditions, but `verify_step.sh` did not run `check_feature_sprawl.py`, and C1 and C2 each added a `SIMUT_PANEL_PIN` site that the local run of every CI gate caught at C8 (above). The verifier runs the gate since.

### 2.14 The limits step and the unit splits (S10a)

**S10a (2026-10-21, branch `feat/ui-drivers-s10a`, base `331266dc`, the sync with main `2f4bc3d3`) did this, in part.** It took B1, the limits editor's step that S8b and S9b had deferred, and it split eight units so that each new file is one class. It left the mode chain of `handleTouch( )` (C4) and the PIN keypad's header (C8) for S10b, and five units whole (D5). Twelve commits, numbered as the plan numbers them (C4, C7, C8 and C11a are not in the branch): C2 `f30c3c4a`, C3 `e979a02f`, the instrument `a26530b9`, the splits C5 `10353792`, C6 `ff88ca77`, C9 `2091eb5b`, C10 `123027cd`, C11b `594d8eae`, C11c `079f18b7`, C11d `99587781`, C11e `7c18d00b`, and C12, the documentation, the table of section 3.3 and the comments. The plan's decisions ran on their defaults, with one recorded deviation (D4, below); D6 was asked on 2026-10-21 and S10a ran on its default.

- **The limits step (C2, C3).** B1 in form Fb. The three lambdas of the limits branch (`adjustVal`, `enforceInterlock`, `stepLimit`) moved word for word into `limitStepOn( )`, a `static inline` function of `src/DisplayManager_Actions.h` (the one substitution: `_tempAlarmConfig` became `cfg`). `actLimitStep( )` holds the ramp, the focus, the call and the repaint request, and both drivers call it after their own gate: the original's arrow, and the Ângulo's step case with `sensorLimitCount( st )`. `passoLimite( )` is gone. C3 made the Ângulo's builder only read: `separarLimites( )` is gone, and `uaMontarAjustes( )` clamps the focus into a local. **Recorded deviation (the one-definition rule):** `limitStepOn( )` has internal linkage and is called from `actLimitStep( )`, an inline member function with external linkage that every unit including the header defines, so the definitions name different entities, which [basic.def.odr] makes ill-formed with no diagnostic required. It is harmless here because `actLimitStep( )` is always_inline and never emitted out of line, GCC checks no ODR without LTO and the branch has none; and the internal linkage is what keeps the closures local, which is why Fb held where S8b's forms (1) to (3) did not. **D2:** `uiTouch( )` was to return `void`, where the plan's section 3.2 wrote `bool`, so that the chain's `return;` statements move word for word (the call is the last statement of `handleTouch( )`); with C4 deferred it does not exist yet.
- **What the step changed in the images.** The six TFT products change in `handleTouch( )` alone (and `_fini`'s pad; the RP2350 image's header words move with `.text`, as in S8a): `pico_w_release` 4665 -> 4712 instructions, `pico2_w_release` 3266 -> 3263, the same calls in the same order on both chips (`calls_check.py`), the conditional branches differing only in the RP2040's unsigned tests (bcc 1 -> 0, bcs 1 -> 2). The three LCD images are byte for byte C0's. The Ângulo at C2: `handleTouch( )` 4571 -> 4642, `uaToqueAjustes( )` 1312 -> 1301 (its step case calls `sensorLimitCount( )` before `millis( )`); at C3 `uaMontarAjustes( )` 748 -> 628 and nothing else, and the image shrinks 256 B (`.bin` 833,720 -> 833,464; C0 was 833,664), its budget, `"bin"` and row of `PLANO_STABLE.md` section 3.1 coming down in the same commit. The host model (`model_limitstep.py`, outside the repository) found 0 differences in 1,265,472 inputs at -O0 and -O2, and caught every mutant of the candidate (seven, among them "the early return dropped", by 205,504 inputs) and the three of the Ângulo's call.
- **The instrument (`a26530b9`).** `tools/elf_funcoes.py` read layout as code on the first splits, in three ways (below, item 6): it now reads a text to its NUL (4 KB at most), ends a window at the next object, compares the RP2350's inline `tbb` tables as one byte string, and ends the flash-text runs at symbol boundaries. Its test holds 53 cases and 37 mutants (43 and 30 before), and the 90 pairs of images recorded during the probes were read again, none worse.
- **The eight splits.** Each new unit has one parent and no header; it joins its parent's exclusions in `features.toml` (`[display.alpha_paralelo]`, `[display.nenhum]`, and the `off_exclude` lists of `[toggles.tft_graph]` for Graph and Calendar, which name them) and generated files were regenerated, not edited. A new unit whose whole content is TFT-only carries no `#if` and is excluded instead, so that the feature sprawl does not rise (copying a parent's frame had raised it to 364 or 365 in the probes): Render and i18nUi.

| step | from -> to | what moved | TSV rows |
|---|---|---|---:|
| C5 | `DisplayManager_Dashboard.cpp` -> `DisplayManager_Panel.cpp` (core) | the eight panel primitives (`dmaTftChannel`, `dmaTftOpen`, `dmaTftClose`, `blitWindowDma`, `fillWindowDma`, `fastFillRect`, `fastClearScreen`, `blitCanvas`) with their three `SIMUT_UI_ANGULO` guards; the allowances follow (identity 3, driver-hook 3) | 8 |
| C6 | `DisplayManager.cpp` -> `DisplayManager_Render.cpp` (original) | `render( )`, `truncateText( )`, `truncateTextKeepSuffix( )` | 3 |
| C9 | `DisplayManager_Settings.cpp` -> `DisplayManager_SettingsCore.cpp` (core) | the `show*` entry points and the working state of the settings screens | 22 |
| C10 | `DisplayManager_Calibration.cpp` -> `DisplayManager_CalibrationCore.cpp` (core) | `loadTouchCalibration`, `fillCalData`, `loadDisplayOffset`, `fillDisplayOffsetData`, `getDisplayOffsetX`, `getDisplayOffsetY`, `resetTouchCalibration`, `showTouchSensitivity`, `mapTouchPoint` | 9 |
| C11b | `DisplayManager_i18n.cpp` -> `DisplayManager_i18nUi.cpp` (original) | `drawSettingsLang( )` | 1 |
| C11c | `DisplayManager_Graph.cpp` -> `DisplayManager_GraphCore.cpp` (core) | `showGraphPlot( )`, `requestLoadingScreen( )` | 2 |
| C11d | `DisplayManager_Ota.cpp` -> `DisplayManager_OtaCore.cpp` (core) | `otaPaintFromCore0( )`, `otaTick( )` and the `static_assert` on the `TR_OTA_WHY_*` order | 3 |
| C11e | `DisplayManager_Calendar.cpp` -> `DisplayManager_CalendarCore.cpp` (core) | `showCalendar( )`, `setCalendarDays( )`, `setGraphNavOffset( )` | 3 |

- **What stayed whole, and why (D5).** A split that changes any function that does not move stays undone; the plan's rule, and each case was measured on a probe of the split alone (`stages/s10a_c1/probes_summary.txt`). **`DisplayManager_Users.cpp`** (c7, the draws to a `UsersUi` unit): `PinKb::orderedFace( )` had one out-of-line `.isra` clone shared by the moved draws and the PIN logic; across two units it is inlined instead, and `pinPolicy( )` goes 31 -> 64 instructions, `scramblePinKeys( )` 157 -> 187 and `pinAppendChar( )` 47 -> 57 (both on the never-change list), +160 B on `pico_w_release`, and `pico_w_test_https` no longer links ("region FLASH overflowed by 60 bytes"). **`DisplayManager_Alarm.cpp`** (c11a): `redrawAlarmFlash( )` 141 -> 143 instructions on the RP2040 products, and `slotAlarmBg( )` and `redrawAlarmFlash( )` recompiled on the RP2350, because `isSlotAlarming( )` was inlined into them inside one unit. **`drawSettingsLicense( )`** stays in `DisplayManager.cpp`: moving it (c6_render_lic) would make `_licOpening` extern, which S10b's licence hook does better. **Calibration's two mixed `show*` functions** stay: `showTouchCalibration( )` writes the seven `g_*` caches that the draws read and write. **`showApConfirm( )`** stays in Settings.cpp, a core entry point beside its draw, with the one network frame (`#if SIMUT_NET_WIFI`) they share. S10b resolves each with a hook or a `UiChange` in place of the write across the cut. **Superseded on 2026-10-28 (S10c, stage D):** none of these stays whole now. The Users draws moved in S10c D6a (`5f04277d`), `drawSettingsLicense( )` in S10c D4b-2 (`6c216cc6`), the alarm draws in S10c D5 (`87875859`), Calibration's two `show*` functions in S10c D3 (`aea1f5f8`, `c67a1cb4`, `79ac927a`) and `showApConfirm( )` in S10c D2 (`9fae2fa7`); section 2.18.
- **What was deferred.** **C4 (D6), the chain as `uiTouch( )`.** Moving the chain behind a function boundary changes `handleTouch( )` by itself (`stages/s10a_c1/probes_summary.txt`). Written as D2 says, by value (P2), the copies of `x` and `y` stop being reloaded after each call, because `handleTouch( )` passes their address to `mapTouchPoint( )` and the Ângulo to `uaToque( )`: `pico_w_release` 4712 -> 4605 instructions, `pico_wiretft` 4608 -> 4537, `pico2_w_release` 3263 -> 3225, the Ângulo 4642 -> 4526, five calls fewer on the first. By const reference (P2r) the size comes back (4712 -> 4712), but one `showSettingsMain( )` tail is cross-jumped into another (4 call sites in P1, 3 in P2r), the branch counts move (`ble` 55 -> 52, `blt` 14 -> 18, `bne` 161 -> 162) and a stack slot swaps (`ldrh [R, #24]` against `[R, #28]`) in all seven images; GCC's early passes run over the inline function before it is inlined and number its blocks and temporaries differently. R1's fallbacks fail the same way: forms Ff and Fe with the chain moved by const reference, each against its own form alone (`pico_w_release`: Ff 4712 -> 4712, Fe 4710 -> 4710, the first difference at instruction 564, a stack slot), and P2f's `pico_w_release` is P2r's byte for byte (sha256 `6b6afec2...`). The cause is the boundary, not the form of B1; no always_inline function reproduces the bytes of the same code written in place. D6 (2026-10-21): (1) defer the chain to S10b, where it becomes the hook with the Ângulo's own `uiTouch( )` (the default, recommended); (2) take C4 now, by const reference, and accept a changed `handleTouch( )` with a bench check; (3) include the text as a fragment inside `handleTouch( )` (unmeasured). **C8, the `PinKeypad.h` split, goes with C7 to S10b**: its original-only names are read by the draws that stay in Users.cpp, so splitting the header now would leave a mixed unit including an original header, for no unit made pure (section 4.2). **Done in S10c C8 (`c5a26bcc`, 2026-10-28), section 2.18.**

**How each step was proved** (`tools_s10a/verify_step.sh`, two modes; numbers in `stages/s10a_c2` to `s10a_c11e`, outside the repository):

- **The nine images.** In the TFT6 mode the three LCD images (`pico_w_alpha`, `pico_w_air`, `pico_wire`) are held by sha256 to C0, and the six TFT products and the Ângulo by `elf_funcoes.py` against the step before, which must exit 0 ("the products change by link order only"), plus `tft6_check.py` with nothing allowed and the three sections `elf_funcoes.py` does not read (`.ota`, `.partition`, `.boot2`) compared byte for byte; the sha mode (S9's) holds all nine to the step before. Each split reproduced its probe, and the `.bin` sizes of the probes were C0's, except `pico2_w_release` +8 B for C10 and C11e alone (link-order padding, 998,992 -> 999,000; that image has 569 KB to its OTA ceiling); at C11e it is 998,992 B again.
- **The source.** `move_check.py --unit-move`: each moved definition has the same normalised text at both ends, and the source unit is its old text minus the moved definitions and include lines; its controls (one token changed in a moved function, a definition copied and left behind) are refused. `split_table.py` listed, for each definition, the file-scope statics it names and any use of `__FILE__`, `__LINE__`, `__func__`, a `static` local, a label or a lambda, and `inline_audit.py` predicted which cuts would stop an inlining (no LTO: `platformio.ini`); both are outside the repository.
- **The frames and the gates.** The six host screen groups of the Ângulo are 840 of 840 frames equal at every step. `check_ui_boundary.py` (0 findings) and its test (302 checks), `check_authz.py` and its test, `check_feature_sprawl.py` (362 sites at the tip, 363 at C0), `check_flash_budget.py` for the images and `--table`; allowances 24 and `UA_DESVIO( );` sites 7 (Dashboard 4, Alarm 1, Users 2) as at C0; the ten images built under `-Werror`.
- **C12.** `VERIFY_MODE=sha` against C11e, `ELF_ALLOWED` empty, on the clean tree: the nine images equal C11e's sha256 (`stages/s10a_c11e/sha256.txt`); the Ângulo builds to 833,464 B (sha256 `90b33f72...`, C11e's) and `elf_funcoes.py` finds its 4,271 functions, 498 read-only data objects, 24 switch tables and 2,519 flash texts equal, exit 0; the six host screen groups give 840 of 840 frames equal; the flash budgets of the ten images and `--table` hold. C12 changes comments, documents, the table and tool docstrings, so no image could change, and none did. The table and its check: section 3.3. `tools_s10a/ci_gates_local.py` over the committed tree: exit 0; its last line reads "52 of 52 steps passed; 1 skipped (own env, CI only)" (`stages/s10a_c12/ci_gates.txt`). The sanitizers' pass is the CI's alone.

**The bench** (2026-10-21, from about 13:25, the Wire + TFT board; the main session's record is `stages/s10a_bancada/resultado.txt`). The images were C0 (`331266dc`), C3 (`e979a02f`) and the tip (`7c18d00b`, C11e's), each in its original and Ângulo builds, made into UF2 files from the verified `firmware.bin` and decoded back to be equal to it. A guard ran around every screen: `/api/config` and `/api/alarms` before and after (live keys dropped) must be equal, and the binary log must hold no record of a save or an alarm in between; it was proved first (two snapshots in a row read equal; a copy with one value changed and a record appended is refused).

- **O1, the original's limits editor** (C0 against C3, one walk each, 39 steps, 87 captures; the account created and deleted by the walk): 37 steps equal, 0 different, 0 missing; the keypad's step (dealt at random) and the 5 s ramp's are not compared by the tool, and the ramp's, compared by hand, is pixel-identical (GELADEIRA, MIN 0.9 C, MAX 8.3 C after the ramp). Passes. **L1 on the original** (the tip against C0, the same walk): 37 equal, 0 different, 0 missing, the ramp's capture pixel-identical; the original has no `/api/ui`, so its paint times are not measured.
- **A1, the Ângulo's limits editor** (C0 against C3, `s8b_angulo.py --so limites`): on GELADEIRA (DS18B20, one channel) both builds give 8 ok and the same failure, which is a premise of the check and not a defect (the saved limits are MIN 0.9 C and MAX 1.0 C, so the first MIN step meets MAX and pushes it: the interlock working); on teste2 (DHT22) both give 9 ok, 0 failures (one tap and its undo; a held arrow repeating as taps at 0.15, 0.38 and 0.72 s; the 5 s ramp, 17 repeats = 77 unit steps; the pager; MIN against MAX; Cancelar). All 18 captures are pixel-identical between the builds. The plan's "after each step the frame shows MIN < MAX without another touch" holds on both: C0's builder repaired the copy on every frame, C3's frames are the same, repaired by the pass on the request the action raises. Passes.
- **A2 and L1 on the Ângulo** (`pintura_s10a.py`, which takes only `roteiro_ui.py`'s helpers and sends every touch through an allowlist by screen that refuses Salvar, the maintenance window, the PIN policy, the clock, the alignment, the calibration, the sensitivity, the MIN bar and any press over 150 ms; proved on a stub board, 10 of 10 cases). Each image was flashed and measured from a fresh boot with the same sequence (5 rounds of the dashboard and the 24 h chart, the first settings page, the alarms list, the sensor menu and the limits editor of teste2, 10 steps of its MAX, Cancelar); `GET /api/ui` `ms`, the last frame's paint, median / max:

| | C0 | C3 | tip |
|---|---:|---:|---:|
| dashboard | 30 / 30 | 30 / 30 | 30 / 30 |
| chart, 24 h | 58 / 59 | 58 / 59 | 58 / 59 |
| first settings page | 28 / 28 | 26 / 26 | 26 / 26 |
| alarms list | 29 / 29 | 30 / 30 | 30 / 30 |
| limits editor | 28 / 28 | 29 / 29 | 29 / 29 |
| a limits step (x10) | 24 / 25 | 24 / 25 | 24 / 26 |
| `heap_lb` after, B | 29,481 | 29,434 | 29,489 |
| `heap_f` after, B | 55,008 | 54,920 | 55,024 |

  Every difference is within 2 ms and tens of bytes, and the guard held in all three: A2 (C3 against C0) and L1 (the tip against C0) show no change that the measurement can tell from run to run.
- **The maintenance incident (14:04 to 14:23), the main session's error.** A2 was first started with the Ângulo's full route, `tools/tft_ui/roteiro_ui.py`, on the C0 Ângulo. Its maintenance steps press the maintenance window's Iniciar and Encerrar, which the plan forbids, and its docstring lists what it never presses without naming them. The guard caught it: the binary log gained `APP_UI_MAINT_ON` (454) and `APP_UI_MAINT_OFF` (455), twice each; `/api/alarms` was equal before and after (the window ended closed); `/api/config` changed only in `a_pending`, 0 -> 4, the alarm telemetry queue, because alarm telemetry is enabled to the maintainer's server (his phone, away), so four maintenance records waited for delivery. No alarm was set off (no record 470); the route's 192 steps passed; bench work stopped, with the board on the C0 Ângulo and the queue untouched. At the maintainer's request a receiver was started on the PC (`telbench/tele_sink.py`, HTTPS, port 8443) and the board's `t_srv` alone was moved to it by `POST /api/commit_all` (applied live, no reboot; the guard: config changed only in `t_srv` and `a_pending`): the queue drained at once (`a_pending` 4 -> 0, `TEL_ALARM_ACK` in the log) and the four records (maint_on and maint_off twice, user teste_ui, seq 1 to 4) reached the PC in one payload, so they will not reach the phone. **What it changed:** the device's log keeps the four maintenance records (454 and 455); the four records are on the PC and not on the phone; and A2 and L1 ran through the allowlist of `pintura_s10a.py` instead of the full route. **The end of the session** (14:48 to 14:50): the tip's Ângulo was left on the board (`stages/s10a_c11e`, `.bin` sha256 `90b33f72...`); `t_srv` was put back to the maintainer's server (applied live, nothing else changed, `a_pending` 0); the receiver was stopped (port 8443 closed; it took 36 payloads, 1,902 records). Against the day's first snapshot `/api/config` and `/api/alarms` are equal, and the binary log gained 400 records (the flashes' boots, logins, the test accounts' saves) and keeps the incident's four.

**The reviews.** The tools of C0 (the TFT6 verifier): COM RESSALVAS; its allowed list was unanchored and `.ota`, `.partition` and `.boot2` were unread by `elf_funcoes.py`; both fixed, and the controls of `verify_tft6_controls.sh` now check the sha256 of their input ELFs afterwards, since one of them had doctored a text letter of a scratch ELF through a symlink and restored it (the ELF's `objcopy` output equals its `.bin`, and `elf_funcoes.py` against the same text exits 0). C2, on its own: COM RESSALVAS; its important finding, the Ângulo's `handleTouch( )` growth (+71 by `elf_funcoes.py`, +46 by `objdump`), is explained (`stages/s10a_c2/angulo_growth_explained.txt`): by instructions alone the Ângulo grows +50 against `pico_wiretft`'s +48, the difference a literal pool that moved (one nop, one `b.n`); and a host run over all 256 type bytes showed that no sensor type has zero limits, so the Ângulo's fallback of 2 and the original's zero return are dead code (section 4.4). The tools' second review (parts A and B): COM RESSALVAS, fixed by the main session: `alloc_check.py`'s old verdict "allocation only" accepted a register passing another value and two stacked arguments swapped, and is now "masked-equal" with an explicit "not a proof" and exit 3 (four doctored ELFs give 3, 3, 0 and 1; the 0 is a literal word, masked, which `elf_funcoes.py` gates); `model_limitstep.py`'s fourth column ran a constant string that a commented copy satisfied, and now blanks comments and strings, wants exactly one live `actLimitStep(` equal to C2's text and pins the original's live call (three cheats refused); `inline_audit.py`'s "partly inlined" for C5 was merged calls (cross-jumping), and its legend says "inlined, merged or compiled out"; `pinkb_readers.py` misses a namespace alias, a token-pasting macro and ADL, none of which the tree has, and a miss would fail the build of C8, not pass. The review of the splits (C5 to C11 and the instrument, 2026-10-21): COM RESSALVAS, no blocker; its findings were comments, the instrument's limits and three commit messages. The messages were rewritten before C12, each new commit with its old one's tree, author and date: C5, C6 and C9 carried the builder's "(PASS: 4 section)" where the count of sections read belongs; C10 repeated P3's +8 B, but built on C9 its `pico2_w_release` keeps C9's 998,984 B (the probe's +8 B came on C0's base); C11e said the six TFT products exit 0 where five did, the sixth, `pico2_w_release`, exiting 1 with its RP2350 header word `__binary_info_header_end` alone changed (0x000f3d10 to 0x000f3d18, the image 8 B longer: the C11 rule). C12 answers the rest: the five citations of `DisplayManager_Dashboard.cpp` lines in `DisplayManager_UiA_Painel.cpp`, false since C5, and the `DisplayManager.cpp` line citations of that file (seven lines) and of `DisplayManager_UiA_Ajustes.cpp` (one), already wrong at `331266dc`, now name `drawSlotPanel( )`, `drawBottomButtons( )`, `render( )`, `slotAlarmBg( )` and `uiPass( )`; `Settings.cpp:934` there names `_clockAtBoot`; the comment above `showClockEntry( )` in `DisplayManager_SettingsCore.cpp` ("this file only draws it", "the rule this file follows") was true of Settings.cpp only and says so; every citation of a scratch plan in `src/` (`S9B_PLANO.md`, `S9_PLANO.md`, `S10A_PLANO.md`; none of them in the repository) cites this document's section 2.13 or 2.14; `tools/elf_funcoes.py`'s list of what "equal" does not cover gains three limits: a string of one character and two entries of an unnamed pointer table swapped, old and new tool alike, and the zero padding between objects, which the old tool's 64-byte window read by accident (five such bytes changed: the old tool exits 1 on each, the new one 0); its count of the texts with a literal target inside, "193 of the 3699" with no definition (the review counted 195), is measured again and defined: 192 of the 3713 texts, cut at the symbol edges, have one strictly inside, and 195 of 3699 before that cut (`pico_w_release`, C0 and C11e alike); and `tools/ui_boundary.json`'s reasons for `DisplayManager_Dashboard.cpp` and `DisplayManager_Panel.cpp` no longer credit the pixel primitives' guards to the unit they left.

**Where `S10A_PLANO.md` was wrong or incomplete** (the plan is not in the repository):

1. The native suites hold 745 cases, not 742 (the sync with main added 3); `passoLimite|separarLimites` stood on 4 lines of `src/ui/angulo`, not 5; the actions were 79 declared and 78 defined.
2. D4's rule ("a unit keeps its name and the class of the larger share of its lines") disagrees with the plan's own section 1. `split_table.py` measured Users.cpp's core at 381 lines against 956 that move, `_Alarm`'s at 37 against 108 and `_i18n`'s at 31 against 85, so D4 would have moved the core out of those three. Section 1 keeps the domain's name on the core (the PIN logic, the alarm state, `tr( )`) and S10a followed it: a deviation from D4's default, recorded here.
3. C11b was not a D5 case: `_activeLang` and `_activeLangLoaded` are static members of `DisplayManager` (`DisplayManager_LangParser.cpp:55-56`), so `drawSettingsLang( )` moved. The real refusals were Calibration's two `show*` functions and `drawSettingsLicense( )`; and the plan did not foresee that `[toggles.tft_graph]`'s `off_exclude` names Graph and Calendar, so their core shares join three lists, or that the `TR_OTA_WHY_*` `static_assert` (Ota.cpp:44-45) is part of C11d.
4. The chain move (C4) is not byte-neutral: D2's premise, that the Ângulo's `handleTouch( )` keeps its bytes too, does not hold (above).
5. R1's fallbacks did not help: Ff and Fe fail the same way, so the order "B1 first, then the chain" is necessary and not sufficient.
6. The instrument read layout as code in three ways, which the plan took for code changes: a text longer than the 64-byte window (a message of 76 bytes was "no text" and the 64 bytes behind it stood for it), a window that ran into the neighbour a split had moved (a table of five pointers, 20 bytes, was read for 64: three functions reported for a layout, none for code), and the RP2350's inline `tbb` tables, which `objdump` groups by alignment (`.word` against `.short`) for the same bytes. The flash-text scan had the same fault (four cases: two `CSWTCH` tables with no NUL, `TYPE_LABELS`, a channel label's tail with a pointer byte).
7. The TFT6 verifier had holes (the unanchored allowed list; the three sections `elf_funcoes.py` does not read), and a plan that asks for byte-identical LCD images and link-order-only TFT images needs both closed.
8. `pico_w_test_https` has 12 B of real flash left, not the 12 KB that PlatformIO's line reads (section 4.4); the plan's stop conditions did not name it, and c7 would have crossed it.

### 2.15 The Ângulo stops linking the original (S10b-1)

**S10b-1 (2026-10-21, branch `feat/ui-drivers-s10b`, base `50828bdf`, the sync with main `25e169df`) did this.** It took what S10a deferred to S10b and the host can prove: the mode chain of `handleTouch( )`, the theme catalogue, the study's vestiges that the Ângulo still carried (`UA_DESVIO`, the `_uaPintando` guards, `TRADUZIDO`) and the original's units that the Ângulo's link no longer reaches. It split no D5 unit: that is S10b-2 (section 4.4). Fourteen commits follow the sync: C0b, C2, C3, C4a, C4b, C5a, C5b, C6, C7 (the documentation), C6b, C6c and C6d (the link gate, after three reviews), C7b (the bench and its record) and C6e (the gate's command line, after the final review); C0 and C1, the tools and the probes, are outside the repository. The commits are named by step and not by hash: on 2026-10-22 the main session rewrote messages over the same trees (item 9 below), which changed the hash of C3 and of every commit after it, so the stage records name hashes the branch no longer holds. The plan's eight decisions ran on their defaults: the chain is a fragment (D1, measured byte-neutral by P1); the catalogue is defined at link time (D2); `tools/ui_boundary.json` ends with one entry, `AppManager_Events.cpp`'s history fix, which only main ends (D3); no D5 unit was split (D4); no step changed the nine images, so https's last LOAD end stayed at 0x100FEFDC (D5); the sync came first (D6); a `.thm` upload is refused at its first byte where the driver has no custom themes (D7); and the one core name that the nm grep still finds, `redrawTopPanel( )`, is named in the acceptance and allowed by name in the gate, the pattern never widened to hide it (D8).

- **The sync (S10b-0) and the maintainer's decision.** Main `25e169df` (#284, the language pack's log records) went into the branch as `50828bdf`, on origin, with no conflict in `src/`; the six conflicts were measured numbers (`flash_budget.json`, `PLANO_STABLE.md` section 3.1, the three READMEs, `tools/README.md`), each measured again on the merged tree from scratch, and the merge was reviewed COM RESSALVAS (`stages/sync4_c1/REVISAO.txt`). `pico_w_test_https` has 36 B of real flash after it where it had 12 B (the last LOAD ends at 0x100FEFDC). The review found two holes in `verify_step.sh`, an empty `TFT6_ENVS` that did not skip the six product blocks and the Ângulo's V-ELF list cut at 40 of 207 names with an allowed list taken from its own output; both are closed in C0 below. **On 2026-10-21 the maintainer decided that the Ângulo ships first on the Pico 2 W only**, and that a smaller or optimized version for the Pico W comes later. S10b's steps do not change, since they hold for every image. What changes after it: S12 no longer asks whether the Ângulo fits the Pico W's Wi-Fi image for the release, `ui=angulo` on the Pico W is to become a configurator refusal in `net_refusal`'s mould until the smaller version exists (not done: S12's), and the release product is the Pico 2 W with the Ângulo. Until then the Ângulo had been built for the RP2040 only. Its first build for the RP2350, a probe (`build_custom.py` on `pico2_w_release` with `ui=angulo`, `stages/p2w_angulo_probe`), was +42,336 B of `.bin` and +7,036 B of RAM over `pico2_w_release`, with 527,199 B left to the slot's ceiling; it also showed that no configurator build with `ui=angulo` links, because the ui-ram gate has no budget for `build_custom.py`'s `_custom` environment (section 4.4).
- **C0b, `pico2_w_angulo`.** A real environment for the product, so that every later step is built and sized on it too: `pico2_w_release`'s switches with `ui = "angulo"` and `publish = false`, its own flash and static-RAM budgets and its row in `PLANO_STABLE.md` section 3.1, an eleventh entry of the CI's firmware matrix (not a required check; CLAUDE.md, the READMEs and the files that enumerate the images say eleven) and `tools/third_party.toml` naming it beside `pico_wiretft_angulo`. **It does not trust the bench root** (`ota_trust_bench = false`). The first version copied `true` from the Wire's bench Ângulo, but this image is the product to be: `pico2_w_release` does not trust the root, no Pico 2 W bench needs it, and a build that wants it passes the flag as the parent's does (`AGENTS.md`). Turning it off took 64 B from the flash and the `.bin` (1,041,340 -> 1,041,276 B used, `.bin` 1,041,360 -> 1,041,296 B), both built from scratch on one tree with the flag the only difference; that is not the 156 B that `ota::kTrustBench`'s size suggests, and the image stands 16 B above the probe once the probe is corrected for #284, which nothing explains. The image: `.bin` 1,041,296 B (sha256 `67413903...`), RAM 132,564 B, 527,231 B to the slot's ceiling, the Ângulo's static RAM 9,313 B. The ten other images stayed byte for byte C0's (the ELFs too), `profiles.ini` gained one block and nothing else, and the gates passed 52 of 52 after three tests that assumed one Pico 2 W image were taught the second (`configurator_cases.json` needed the case that lights the cable's refusal, `test_configurator_page.py` and `test_rp2350_layout.py` read the RP2350 products from the manifest). Its G-BO is 593 B, against the Wire Ângulo's 617 B: the Wi-Fi image does not link more of the original than the Wire's.
- **C0, the tools and their review** (`tools_s10b/`, outside the repository). `reach.py <elf> <regex>` lists the referrers of a symbol (`bl`, `blx` and `b.w` targets, literal words, `.rodata`, `.data` and `.init_array` words) and follows them back to the first function the table does not class as original; `units_probe.sh` adds units to `[ui.original].units` in a scratch copy and records the link; `move_check.py --block` proves a fragment holds the old lines word for word and the file is the old file minus them plus one include; `greps_s10b.sh` prints the acceptance table; `verify_step.sh` got the two fixes above. C0 equalled the sync's record (the nine sha256, the Ângulo at 833,432 B, 840 frames; gates 52 of 52). **The review (COM RESSALVAS):** `reach.py`'s UNREFERENCED could not be trusted for data objects, because odd literal words that are not Thumb function starts were dropped, `.data` was skipped, and `nm`'s T/t letter for every symbol of an executable section made `.data` objects read as functions; an independent scan found a referrer for 83 of its 139 unreferenced verdicts, `_licOpening` and two of the original's cache objects among them. Fixed, with controls that fail on the old tool: 56 unreferenced on the whole image, none contradicted by the independent scan (39 controls). Three minor findings, fixed the same way: `move_check.py --block` took a trailing-backslash splice and refuses one now (38 controls); `verify_step.sh` exited 0 on a flagged skip and read a partial `TFT6_ENVS` as a silent switch, and exits 3 now with "THE CHECKS THAT RAN PASSED, BUT A SKIP WAS FLAGGED" (26 and 10 controls); the tools left `tools/__pycache__` in the worktree they read (bytecode is off in every tool and runner). One limit is now written where it applies: `units_probe.sh`'s "nothing changed" is V-ELF exit 0, function-equal and not byte-equal (the Render probe's `.bin` differs from C0's in 576 B over 248 symbols, Settings' in 2,045 B). C0's greps, the base of every later one: G-NM 2 (`redrawTopPanel( )`, core, and `availableThemes`); G-BO 617 B (Themes.cpp 324, `buildDashLayout( )` 276, the calibration's caches 17); `UA_DESVIO( );` at 7 sites, 2 definitions and 19 lines naming the macro; `_uaPintando` 9; `TRADUZIDO` 4; the identity allowances by unit AppManager_Events 4, Panel 3, Themes 2, Touch 1; the chain's mode tests 22; allowances 24; sprawl 362.
- **The probes (C1, `stages/s10b_c1/probes_summary.txt`).** **P1, the fragment, is byte-neutral:** the nine sha256 and sizes equal C0's, `elf_funcoes.py` exits 0 on all nine, https's last LOAD end is unchanged and `move_check.py --block` exits 0; the control, two adjacent `else if` branches of the fragment swapped, changes the six TFT products' sha256, each in `handleTouch( )` alone, and leaves the three LCD images equal. That resolves S10a's D6 with no product change: option (3), which S10a listed as unmeasured, holds where the by-value and the const-reference forms did not (section 2.14), so P2 was not run and the chain needs no bench check B0. In the Ângulo the fragment is one statement: the image loses 12,552 B, `handleTouchPanelV24( )` (3,084 B) and what only the chain reached leave, `uiTouch( )` arrives and `handleTouch( )` goes from 4,642 to 949 instructions; cppcheck is clean. Two gates read the fragment alone: the feature-sprawl watermark moves sites between files, and the net boundary flags a class include on `UiTouchChain.h`; the second got an allowance with its reason (the fragment is included inside `handleTouch( )`, uses the names `Touch.cpp` includes and cannot include a header itself) in the net-boundary list, in place of an `#include` inside the fragment. **P3, the catalogue:** the nine equal C0's; the Ângulo links with no undefined reference (820,760 B); nothing linked reads `currentTheme`, so `loadTheme( )` is empty there; `getThemeId( )` and `getThemeIndexByName( )` are in neither ELF; only `[env:pico_wiretft_angulo]` changes in `profiles.ini`. `bytes_original.py` counted `Temas.cpp`'s four functions as original rows by name (32 B of G-BO's 325 B), which C4a fixed. **P5, the vestiges:** the nine equal C0's; in the Ângulo only the constructor (-2 instructions), `uaPintar( )` (-3) and the offset of `_uaTudo` (0x2183 -> 0x2182) in five functions change; allowances 24 -> 4. **P4, the units,** ran on C5b (C6 below).
- **C2, the update screen.** The Ângulo's touch had one case left that handed the point back to the original's chain: `MODE_OTA_UPDATE` answered `TRADUZIDO` (`uaToquePendente( )` takes every mode no group has, so it was the only one), and `handleTouch( )`'s branch for that mode ran `actOtaDismiss( )`. The case now calls the action itself and answers a new value, `TRATADO_SEM_QUADRO` (taken, the frame unchanged: `TRATADO` would mark the frame dirty on every touch sample while Core 1 runs the update screen, and the pass would build and hash the screen's view model on each one); `uaToque( )` answers true on every path and calls `uaSujo( )` only for `TRATADO`; `TRADUZIDO` leaves the enum (4 lines -> 0). After it the chain is dead code in the Ângulo. Proved: the nine equal C0b's; the Wire Ângulo `.bin` 833,432 -> 833,472 B (+40) and `pico2_w_angulo` 1,041,296 -> 1,041,328 B (+32, with the RP2350 header word `__binary_info_header_end` +0x20), only `uaToque( )` (90 -> 84 instructions) and `uaToqueGraficos( )` (369 -> 390) changing, decoded, and their budgets and `PLANO_STABLE.md` rows rising by those bytes in the commit; V-PIX 840 of 840. `glue_check.py` gained a rule and nine mutants, all refused (the case without the call, the call after the return, the case answering `TRATADO` or `NAO_E_MEU`, the call made conditional, `TRADUZIDO` back, the old answer, `uaSujo( )` for every answer, `uaToquePendente( )` answering `NAO_E_MEU`), and the rule fails C0b with six problems, as it must. The dismissal is not exercised on the bench: it is the same action, held by the glue check.
- **C3, the chain.** `Touch.cpp`'s lines 400-1225 (the Ângulo's hook and the original's mode chain) moved word for word into `src/ui/original/UiTouchChain.h`, which `handleTouch( )` includes through the fifth selection header `src/ui/UiDriverTouch.h`. The Ângulo's fragment, `src/ui/angulo/UiTouchChain.h`, is the statement `uiTouch( x, y );`, and `uaToque( int16_t &, int16_t & )` became `void uiTouch( int16_t x, int16_t y )`, declared in `angulo/Members.h`. `SELECTION_HEADERS` gained the file and `test_ui_boundary.py` two cases (307 of 307), `Touch.cpp`'s two allowances went (24 -> 22), the sprawl watermark moved three sites between files and stays 362, and the net boundary lists the fragment with its reason (103 of 103). **The original, proved by its source:** the nine sha256, sizes and environments equal C0b's, and `move_check.py --block` exits 0 (its control, two adjacent branches swapped, exits 1 at old line 1176). **The Ângulo:** the Wire `.bin` 833,472 -> 820,880 B (-12,592) and `pico2_w_angulo` 1,041,328 -> 1,029,896 B (-11,432), budgets and rows down in the commit. What left with the chain is `handleTouchPanelV24( )` (3,084 B on the Wire, 2,884 B on Wi-Fi) and its lambda, `userEditRowCount( )`, `PinKb::keyAt( )`, `PwdKb::popupHit( )`, `sensorNthChannel( )`, libgcc's `__gnu_thumb1_case_si` on the Wire, the data `PwdKb::DIGITS`, `SYMBOLS` and `GROUPS` (11, 29 and 32 B), `CLOCK_COL_W` and `CLOCK_COL_X` (10 B each) and a 5-byte switch table, so `.rodata` is 128 B smaller (97 B of sized objects and 31 B unnamed). `handleTouch( )` is recompiled: 4,651 -> 949 instructions on the Wire (3,272 -> 573 on Wi-Fi), 757 of its 880 instructions without pools equal and the rest the early gates compiled again without the chain's locals (the frame goes from 260 to 116 B). The instruction level shows the shape and not the equality of those gates; that rests on `move_check.py` (the source), V-PIX 840 of 840 and bench B1. **`elf_funcoes.py` cannot carry this step alone:** the layout moves 12.5 KB, and it marks 203 (Wire) and 28 (Wi-Fi) functions changed, nearly all by literal words alone. The new `reloc_only.py` masks branch targets, pc-relative offsets and literal words, wants every branch and call to name the same symbol and every moved literal word to resolve to the same symbol and offset, the same string or, for an unnamed table, words that do, and classifies them: with the whole-blob comparison of the review below, 201 relocation-only and none unproven on the Wire (203 with the two real ones, `handleTouch( )` and the padding of `_fini`), 26 and 1 on Wi-Fi (28 with `handleTouch( )`; the one is `__binary_info_header_end`, the image-end word, down by the 11,432 B). The two functions the first version could not prove are string pointers that moved to the same strings: `WebManager::begin( )`'s table, now at 4 mod 8 behind a pad, and `desenharGrafico( )`.
- **C4a, the catalogue (D2).** `struct ThemePalette` and the six catalogue functions (`getThemeCount`, `getThemeId`, `getThemeIndexByName`, `getThemePalette`, `loadTheme`, `scanCustomThemes`) keep names, signatures and call sites; their declarations moved word for word from `src/Themes.h` to the new core header `src/ThemeCatalog.h`, which `Themes.h` includes. The original defines them in `Themes.cpp`, now the first entry of `[ui.original].units`, so the Ângulo images no longer compile it; the Ângulo defines them in `src/ui/angulo/Temas.cpp` over its three rows (`claro`, `escuro`, `auto`: `Themes.cpp`'s rows word for word, with an `RGB565` and a `THM_STATE_DARK` of its own), where `scanCustomThemes( )` and `loadTheme( )` do nothing, since nothing the Ângulo links reads `currentTheme`. `Themes.cpp` lost its two `#if SIMUT_UI_ANGULO` and with them two allowances (22 -> 20; sprawl 362 -> 360), and `profiles.ini` changed in the two Ângulo environments only. `bytes_original.py` attributes a row by its unit as well as its name: a unit that `[ui.original].units` lists is not compiled into a driver image, so its rows count only where the image's symbol table has that unit's `STT_FILE` symbol (it exists only for a unit with a local definition, so the list may hold only such units, and every run names the units it leaves out); `--selftest` builds two tiny ELFs that define one name in two units. The old tool read 325 B (Wire) and 297 B (Wi-Fi) of original rows on the new ELFs, the new one 293 B and 269 B. Proved: the nine equal C3's; the Wire `.bin` 820,880 -> 820,760 B (-120) and Wi-Fi 1,029,896 -> 1,029,768 B (-128); G-NM 2 -> 1 (`availableThemes` gone) and G-BO 617 -> 293 B and 593 -> 269 B; of the 15 functions `elf_funcoes.py` marks changed on the Wire, `reloc_only.py` finds 5 real (`scanCustomThemes( )` 26 -> 1 instructions, `getThemePalette( )` 29 -> 11, `loadTheme( )` 9 -> 1, `getThemeCount( )` 5 -> 2 and `_fini`), 8 relocation-only and 2 unproven, decoded (`SensorManager::pushSample( )` and `flash_init_boot2_copyout( )`: pool words that moved with the RAM objects that left); `availableThemes` (A) and `kTemas` (B), 168 B each, hold the same three rows field by field.
- **C4b, the `.thm` refusal (D7).** The Ângulo scans no `/themes` and uses no `.thm`, so `WebManager::handleUploadData( )` (`WebManager_Files.cpp`) refuses one at `UPLOAD_FILE_START`, under `#if !SIMUT_UI_CUSTOM_THEMES`, the capability the boundary gate names for this. It follows the refusals around it, a `LOG_CODE` warning, `_uploadRejected = true` and `return`, so nothing is written for the file and `handleUploadComplete( )` answers its 400. The reason ("Theme files are not used on this panel. Choose Light, Dark or Automatic under Settings > Themes.", `TRL` hash `7bb0d94e`, in both packs) is the log record's; **the answer on the wire stays the generic `{"error":"Invalid upload"}`**, because the response cannot be sent from inside the upload callback and none of the existing refusals carries a text (section 4.4). The sprawl watermark goes 360 -> 361 (the one `#if`) and no route is added (`check_authz.py` passes). Proved: the nine unchanged, with the text in none of them and in both Ângulo images (`strings -a firmware.elf | grep -c`: 0 on each of the nine, 1 on each Ângulo); the Wire `.bin` +176 B and Wi-Fi +200 B, budgets up in the commit; only `handleUploadData( )` really changes (743 -> 775 instructions on the Wire, 644 -> 675 on Wi-Fi); V-PIX 840 of 840; the language-pack gate fails, in a copy of the tree without the pack lines, for both packs. The suites have no case for it, since it is one check on the file name inside a route: bench B4 is its test. The refusal was removed in S10c A'4 on 2026-10-22, at the maintainer's word (section 2.16).
- **C5a, the macro.** The Ângulo images link none of the seven draws that began with `UA_DESVIO( );` (`drawInterfaceFixed`, `drawTopBar`, `drawSlotPanel`, `drawBottomButtons`, `drawAlarmAction`, `drawAlarmSensorMenu`, `drawMaintEntry`), so the macro had nothing left to do. The seven sites, both definitions in `UiDriver.h` and ten allowances left (Alarm 1, Dashboard 4, Users 2, `UiDriver.h` 3: 20 -> 10), and the 19 lines that named the macro now say what `nm -C` shows (19 -> 0). The precondition (R3), on C4b's ELFs: `nm -C` and `reach.py` find none of the seven in either image (0 of 5,039 and 5,266 symbols; the control matches 2). Proved: the nine equal C4b's; on both Ângulo images `elf_funcoes.py` exits 0 (4,264 and 4,322 functions equal, none changed). **The `.bin` files differ at equal size**: `ld` laid out the same 61 (Wire) and 66 (Wi-Fi) veneers in another order, and the `bl` operands that call them follow; `veneer_proof.py` (new) classifies every differing run of the flash image (341 and 171 runs, 0 other). Why `ld` reordered them is not isolated; the probable cause, not proven by a relink, is that the objects C5a changed gained sections (`Dashboard.cpp.o` 30 -> 78), BFD numbers input sections globally and the stub names embed that number.
- **C5b, the guards.** `fastFillRect( )`, `fastClearScreen( )` and `blitCanvas( )` began with `if (!_uaPintando) return;` under `#if SIMUT_UI_ANGULO`, so that in the Ângulo only `uaPintar( )` pushed pixels. None of the three is in either Ângulo image (nor `blitWindowDma`, `fillWindowDma`, `dmaTftOpen` or `dmaTftClose`: `nm -C` and `reach.py` find 0), so the three guards, the member (`angulo/Members.h`) and its two writes in `uaPintar( )` left, with six allowances (Panel.cpp identity 3 and driver-hook 3: 10 -> 4) and three `#if` sites (sprawl 361 -> 358); `DisplayManager_Panel.cpp` no longer tests `SIMUT_UI_ANGULO`. Proved: the nine equal C5a's and https's last LOAD end is still 0x100FEFDC; the Ângulo is 8 B smaller (Wire) and 16 B (Wi-Fi), budgets down; seven functions change on each image, all decoded: the constructor (the halfword store of `_uaPintando = false` and `_uaTudo = true` together is the byte store of `_uaTudo`), `uaPintar( )`, four functions that read `_uaTudo` at its new offset (pool word 0x2183 -> 0x2182 on the Wire, immediate #407 -> #406 on Wi-Fi: equal line for line after the replacement in 8 of 8 cases and unequal with a wrong one), `_fini` and the image-end word.
- **C6, the units that link out (P4) and `tools/check_ui_link.py`.** P4 (`stages/s10b_p4`) probed each original unit on both Ângulo images from C5b. **Seven link out of both:** `DisplayManager_Calendar.cpp`, `_Fonts.cpp`, `_Graph.cpp`, `_Ota.cpp`, `_Render.cpp`, `_i18nUi.cpp` and `UiWidgets.cpp`, which join `Themes.cpp` in `[ui.original].units`; the Ângulo builds stop compiling them, so a dependence on one becomes a link error instead of dead code that `--gc-sections` hides, and `profiles.ini` changes only in the two Ângulo environments (seven `-<unit>` lines each). **The others fail, each with its keeper, and wait for S10b-2:** Settings, on the Wi-Fi image alone (`AppManager::core0Yield( )` -> `showApConfirm( )`, whose caller is under `#if SIMUT_NET_WIFI`; the Wire image links without it); Dashboard (`buildDashLayout( )`, kept by `uaMontarPainel( )` and `uaToquePainel( )`); Calibration (`core0Yield( )` -> `showTouchCalibration( )` and `showSettingsDisplayOffset( )`, and `WebManager::handleResetTouchCal( )` -> `showTouchCalibration( )`); Auth (`fastRandom( )` by `scramblePinKeys( )`, and the screen stream's `readRect`, `readRectDmaStart`, `readRectDmaFinish`, `convert3to565` and `readRow`); Touch (`loopCore1( )` -> `handleTouch( )`); Users (14 keepers on the Wire, 15 on Wi-Fi, 29 symbols); Alarm (11 keepers, 9 symbols); and `DisplayManager.cpp`, the class itself (69 and 78 references to 36 and 38 symbols). `DisplayManager_Panel.cpp` has no original row and was not probed. In all 15 failing builds `ld` named only functions the baseline links (the plan's R2: it does not report references from sections it collects, so the Render probe links although `Users.cpp.o` names Render's `truncateText( )`), a unit's removal never changed the nine (R7) and no static constructor was lost. The seven together link on both images with the `.bin` unchanged (820,928 and 1,029,952 B), V-ELF exit 0 (4,264 and 4,322 functions equal), no symbol lost, the constructors (17 and 16) and the `init_array` entries (19 and 18) kept; the 344 and 347 bytes that differ are veneer order and their `bl` operands (`veneer_proof.py`, other 0, and its two doctored controls fail). **So C6 lowers no budget**: `--gc-sections` had dropped them already, and the gain is that a dependence is a link error now. `tools/check_ui_link.py` holds it in CI (`--selftest` in the `gates` job, and the gate on each `_angulo` ELF after "Check flash budget" in the `firmware` job; neither has run on GitHub yet). C6's version read name patterns only and C6b replaced it (below): `nm -S -C` of an Ângulo ELF must show no symbol on a row of class `original` of `UI_DRIVERS_funcoes.tsv` beyond the allowances named in the script, none of C6's name patterns (`DisplayManager::draw*`, `render`, `redraw*`, `simutFont`, `availableThemes`, `fastFillRect`, `fastClearScreen`, `blitCanvas`) and no draw, render or blit name outside `ua::` and the Adafruit GFX classes; `redrawTopPanel( )` is allowed by its exact name (D8), an allowance that is not in the image or that nothing flags is a finding, and an ELF without `ua::Pintor` is refused (exit 2). `ci_gates_local.py` passed 53 of 53 at C6 (the new `gates` step was the 53rd). **Superseded on 2026-10-28 (S10c, stage D):** the keepers of Settings, Dashboard, Calibration, Users, Alarm and `DisplayManager.cpp`'s licence draw named here are resolved; the figures above are P4's (section 2.18).

**The reviews.** The tools (C0): COM RESSALVAS, fixed (above). C2 and C3, together: COM RESSALVAS; the nine rebuilt from scratch equal the record, `move_check.py` refuses fifteen doctored fragments, `reloc_only.py` says REAL for each doctored byte and three decodes were redone. *Important:* `glue_check.py`'s new rule did not guard the path to the update case, so deleting `r = uaToqueGraficos( ... )` from the dispatcher, or making it `&& false`, was not refused, and `uaToquePendente( )` answering `TRATADO` would have stopped the dismissal silently; the rule requires the dispatcher's exact statements now (the stale-frame return, the touch's target, then Painel, Base, Menus, Ajustes, Identidade, Gráficos and Pendente, each under `r == NAO_E_MEU`, then the `TRATADO` mark): 12 new mutants refused where the old rule missed 12 of 12, the rule holds on C2 and C3 and still fails C0b. *Minor:* `reloc_only.py` compared only the first 32 bytes of an unnamed blob and compares it whole now (to the next symbol or literal target; fewer than 8 zero bytes of padding count as layout): a byte planted at offset 36 of `parseCliCommand( )`'s 180-byte table reads RELOC-ONLY in the old tool and UNPROVEN in the new, and on C3's pair the claim holds. C4a and C4b: COM RESSALVAS; the three products rebuilt from scratch equal the record, `Temas.cpp`'s nine row lines and its macros equal `Themes.cpp`'s and `Themes.h`'s, `check_authz.py` (62 routes) and `check_lang_packs.py` (173 strings) pass. *Important, and a defect of main too:* **the web page reports a refused upload as a success** (F5, section 4.4). *Minor:* the refusal's text names "Settings > Themes" where the Ângulo's menu item is the Portuguese literal `Tema`, and `/api/themes` lists `Claro`, `Escuro` and `Automático` in every language; the refusal is case-sensitive like the original's scan (`X.THM` passes); `a.thm\0x` passes `isSafeUploadFilename( )` and could put a `.thm` in LittleFS (it needs `PERM_FILE_UPLOAD`, nothing on the Ângulo reads it, and it was not run through the real parser); a multiple selection resets `_uploadRejected` per file; and `Temas.cpp`'s own `RGB565` and `THM_STATE_DARK` equal `Themes.h`'s today with nothing tying them (S11). C5a and C5b: COM RESSALVAS; R3 holds by raw `nm` and by `reach.py` (none of the seven draws and three primitives in either image at C4b, C5a and C5b), C5b's decodes were redone (the constructor by simulated stores, the pool word and the `strb.w` immediates, the vtable's 22 words to the same symbols) and the three products rebuilt from scratch equal the record. *Important:* **`veneer_proof.py` did not bite**: three of four doctored copies passed, because its block spanned the RAM veneers, the RAM bodies were compared vacuously and the `bl` sites were never decoded. C5a's claim stood by the reviewer's own check (0 target-name mismatches over 23,532 and 24,624 branch sites, every differing byte explained), and the tool was fixed with the reviewer's doctored copies as controls: the block from the flash veneers only (44 Wire, 45 Wi-Fi), the RAM veneers (17 and 21) a class of their own read through the load map, every direct branch (48,785 and 41,334) decoded from the `.bin` and compared by mnemonic and target name; C5a's pair passes (341 and 171 runs, 0 other) and the six built-in controls and the six doctored copies fail, each for its own reason. *Minor:* the comment of `test_ui_boundary.py` and the allowance description of `tools/README.md` still named `UA_DESVIO`, which C7 corrects. The review of C6 is answered in C6b's paragraph, the batch review in C6c's, and the review of C6c in C7b's (below).

**The bench** (2026-10-22, 00:54 to 02:22; B1 to B3 run, B4 waits for the maintainer). The main session's, on the Wire + TFT board (plain HTTP on 8080): the base's Ângulo (`50828bdf`, `.bin` 833,432 B, sha256 `44e1220f...`) and the tip's (the tree of C6b; C6c, C6d and this commit change no image: 820,928 B, `495af9cc...`), each a from-scratch build turned into a UF2 and decoded back equal, each flashed and then walked from its fresh boot with the same allowlisted tools: `pintura_s10a.py`, `toque_bancada.py`, `s8a_angulo.py` (alignment, clock and sounds), `s8b_angulo.py` (maintenance and limits on sensor 0, limits on sensor 1) and `pendentes_angulo.py` (groups 1 and 4 to 9); `tools/tft_ui/roteiro_ui.py` was not run (section 2.14). The guard held around each side and the whole session (from the first snapshot, 00:54, to the last, 02:22, `/api/config` and `/api/alarms` equal and 331 new records in the binary log, none of a save or an alarm). The tip's image was the one running: `heap_t` read 145,764 against the base's 144,904, +860 B, where the two ELFs' `end` differ by 864 B (0x2001ca20 to 0x2001c6c0, the static RAM C6's units took with them). **B1, the walk:** every tool gave the same result on both images: touch 13 ok; settings 25 ok; maintenance and limits on sensor 0 17 ok and 1 FAIL (its saved limits sit 0.1 C apart, so the first MIN step meets MAX: the interlock, a premise of the check, as in S10a); limits on sensor 1, 9 ok; the pending groups 100 ok, 0 FAIL and 6 notes with the same ids (among them the first long press after a slot tap that does not toggle the pinned card, open since 2026-10-21). Two of the six differ in content between the images, and both are live content: `g5_seta_de_pacote_em_torno_de_um_envio` saw 3 distinct status-strip hashes over about 30 s on the base and 1 on the tip, and `g5_aviso_web_antes_e_depois` read four different ones, two on each image (right after the web login and 7 s later). Of the 113 captures of each side, 73 are pixel-identical. The other 40 differ only in live data (the clock, the readings, the chart's plot, the detail's "now", the calendar's today, which gained its history between the two walks, and Status's uptime, free memory, file system and telemetry queue), in the PIN keypad's random deal, and in sensor 1's limits editor, where the 5 s ramp gave 17 repeats on both but summed 72 unit steps on the base and 77 on the tip (77 on both of S10a's images), so every later frame of that editor shows MIN 0.5 C apart. Both sums are among those the source's schedule allows for 17 repeats (63 to 81, by where the 2 s step falls between two repeats). Run again on each image (A against A, the base flashed back for it), the tip summed 77 twice more and the base 77 and then 72: the sum moves from run to run on one image, by timing, not between the images. **B2, paint and heap** (medians in ms, base to tip, five rounds): dashboard 29 to 29, chart 45 to 47, 24 h chart 59 to 61, first settings page 27 to 28, alarms list 30 to 29, sensor menu 25 to 26, limits editor 29 to 29, a limits step 24 to 24; every difference within 2 ms, as in S10a's table. `heap_f` after the sequence 55,008 to 55,876 (+868 B, about the 864 B of static RAM, the few bytes apart being the allocator's). `heap_lb` 29,096 to 49,868: the same free memory, in one block of 49.9 KB instead of 29.1 KB, and steady through each side's whole walk (29,067 and 49,811 at the end, 32 minutes later on both: 31.5 and 31.6 minutes by the boards' uptime). It is where the heap's blocks fell once the static RAM moved by 864 B, not memory freed; it was not analysed further (section 4.4). **B3, read-only:** `GET /api/themes` gives the same three rows on both images (`Claro`, `Escuro`, `Automático`); the CLI's theme list is on none of the five images checked (`show themes` is compiled only under `SIMUT_CLI_FULL`, `CommandParser.cpp:200-201`; a grep of the five `.bin` of `pico_w_release`, `pico2_w_release`, `pico_wiretft` and the two Ângulo images finds "Available Themes" 0 times and "Unknown command" twice in each, the control). **B4** (a `.thm` upload, which writes LittleFS if the refusal fails) waits for the maintainer's word; the refusal is proved on the host (C4b). The update screen's dismissal (C2) never goes to the bench. `pico2_w_angulo` has no board at all yet. The record: `stages/s10b_bancada/` (`resultado.txt`, the captures, `cmp_*.txt`).

**Where `S10B_PLANO.md` was wrong or incomplete** (the plan is not in the repository):

1. C0's unit list, the table's units whose rows are all original, was partly wrong: Auth is all core, Render, Dashboard, Settings and Calibration are not all original, and `Themes.cpp` was missing. Measured: Render, i18nUi, Graph, Calendar, Ota, Fonts and UiWidgets link out of both Ângulo images (Settings of the Wire image alone); Auth (6 references), Dashboard, Calibration, Touch, Users, Alarm and `DisplayManager.cpp` do not (C6).
2. Settings and the Wi-Fi image. Settings links out of the Wire Ângulo only because `showApConfirm( )`'s caller is under `#if SIMUT_NET_WIFI`. The product is the Pico 2 W with the Ângulo, which no environment built, and with Wi-Fi it links the original's Settings share again. The plan gained C0b, and the Ângulo's Wi-Fi screens (the AP confirmation is drawn by the original) became a product gap to plan, not a link detail (section 4.4). **Superseded on 2026-10-28 (S10c, stage D):** S10c D2 (`9fae2fa7`) moved `showApConfirm( )` to `DisplayManager_SettingsCore.cpp` and put `DisplayManager_Settings.cpp` in `[ui.original].units`, so the Wi-Fi Ângulo no longer links Settings' original share, and S10c D7 (`8dd53c51`) gave the confirmation its own frame in the Ângulo (section 2.18).
3. G-NM cannot reach 0. `redrawTopPanel( )` stays, a core function that the Ângulo's `uaToquePainel( )` keeps; D8 names it in the acceptance, and `check_ui_link.py` allows it by exact name.
4. The guards were dead code in the Ângulo. The plan has C5a and C5b remove the vestiges "once `reach.py` shows them unlinked", and they were unlinked already: none of the seven draws and none of the three pixel primitives is in either Ângulo image at C0 (the primitives are not in its ELF at all). C5a changes no function and C5b only the member's offset; the gain is in the source and the gates (allowances 20 -> 4, sprawl 361 -> 358).
5. C6 lowers no budget. The plan's table has the `.bin` "lower" and "may move by padding"; `--gc-sections` had dropped the seven units' functions already, the sizes are equal and only veneer order moves 344 and 347 bytes. The gain is that a dependence becomes a link error.
6. `elf_funcoes.py` cannot be the proof of a step that moves 12.5 KB of layout (C3) or reorders veneers (C5a, C6): `reloc_only.py` and `veneer_proof.py` were not in the plan, and both needed a review before they bit.
7. The fragment passed the gates only after two answers (R9 named the risk): the net boundary got an allowance with a reason, and the fifth selection header is not in `check_feature_sprawl.py`'s `SKIP_FILES`, so its one `#if SIMUT_UI_ANGULO` counts as a site moved from `Touch.cpp`.
8. `UiDriver.h` said that `UiDriverHooks.h` is included "from `DisplayManager.cpp` and `DisplayManager_Ota.cpp` only" (the final review of S10a, F1). `grep` finds ten units that include it; the comment says what the header is for now, and `UiOriginal.h` had the same count of two.
9. Three statements in earlier commit messages are wrong or overstated, and are corrected here and in C7's message (the main session rewrote C3's and C5a's on 2026-10-22 over the same trees, as S10a's were, and in a second pass the two later messages that cited a hash the first pass retired, C5b's and C7's; C4b's record is a snapshot and keeps its hash): **C3's** says an unnamed table is compared "to words that do" and that `desenharGrafico( )` reads `kTouchFinding`'s pointers from an address 3 mod 4 which the rule's window cut into misaligned words. The old rule compared the first 32 bytes of an unnamed blob; compared whole, every blob is equal and the claim holds, the wording overstated. And `desenharGrafico( )`'s literal points at a 1-byte constant (0xc8) just before `kTouchFinding`, which the old window ran on from into `kTouchFinding`'s pointers (`stages/s10b_c3/fix_reloc_whole/CORRECTION.txt`). **C5a's** split of the 341 and 171 differing runs (72 and 70 in the veneer block, 269 and 101 `bl` operands) is the old `veneer_proof.py`'s; with the fixed tool the totals 341 and 171 hold. **C4b's** record names the build's hash before its tree was recreated (`47f90483`); the build of the commit is byte-equal.

**C7, the documentation.** No function and no byte of an image changes. The documents are this section, section 2.2's two recorded deviations, section 3.3's table and section 3.6's rerun, section 4.4's open points and the stale names (`uaToque( )`, `UA_DESVIO`, `_uaPintando`, `TRADUZIDO`, and a promise or two of earlier stages that S10b-1 kept or changed). The comments are `UiDriver.h` (five selection headers, and the count of the units that include `UiDriverHooks.h`), `UiOriginal.h` (the same count), the header of `DisplayManager_Touch.cpp`, the comment of `test_ui_boundary.py` on `UA_DESVIO`, a note of `check_feature_sprawl.py` on the fifth header, and `tools/README.md`'s description of the boundary gate (4 allowances in one file now; the last stage does not empty the file); the table is `UI_DRIVERS_funcoes.tsv` (section 3.3). **The counts of `tools/README.md` and `CLAUDE.md` are measured, not adjusted:** the table of `tools/README.md` has 191 rows (`grep -c '^| `'`: 52 called by CI, 17 by the build, 64 referenced elsewhere, 58 standalone) and its title and headers said so at C7 (193 rows since C6b gave main's two scripts theirs, 66 referenced elsewhere). They were off before S10b: the headers said 15 and 62 for the build and the referenced groups, which have 17 and 64 rows; the title said 187 where the headers added up to 186 and the rows to 190; and `CLAUDE.md` said 185 (on main it says 146, and main's `tools/README.md` 171, which is right). C6 moved the title and the CI header by one each, rightly, and left the rest. Two scripts of main, `cli_gates.py` and `test_cli_gates.py` (2026-10-13), had no row; C6b gave them one. **Proved,** on the working tree before the commit (`stages/s10b_c7`, `run_c7.sh`; both Ângulo images built from scratch): **the nine sha256 equal C6's** (`VERIFY_MODE=sha`); **both Ângulo `.bin` files are byte for byte C6's** (820,928 B, sha256 `495af9cc...`, and 1,029,952 B, `1d6ef6ab...`), and V-ELF exits 0 on both against C6's ELFs with an empty list (4,264 and 4,322 functions equal, none changed); V-PIX 840 of 840; the eleven budgets, `--table` and `test_flash_budget.py` pass; `ci_gates_local.py` passes 53 of 53 (the sanitizers' pass is the CI's alone); `check_ui_boundary.py` reads 0 findings, `tsv_check.py` 372 rows and 0 findings. `verify_step.sh` flags the tree dirty, by design, before a commit.

**C6b, the link gate from the function table.** The review of C6 (COM RESSALVAS, `rev_s10b_c6`) found the gate's patterns too narrow: they needed the `DisplayManager::` prefix and matched only a bare `render`, and nine draw functions planted under other names in the real symbol table of the C6 Wire Ângulo (`ui::drawThermometerLarge`, a static `drawDropMini`, `renderAlarmRow`, `blitTitleBar`, `uiButton`, a `drawKnob [clone .constprop.0]`, and `DisplayManager::draw`, `Draw3dBar` and `draw_bar`) all passed with exit 0; of the 188 symbols that `pico_wiretft` has and the C6 Wire Ângulo lacks, the patterns matched 88 and missed 100, 56 of them draws defined in units the Ângulo still compiles (Dashboard, Users, Settings), where no link error covers them. `tools/check_ui_link.py` now takes its truth from the function table. It reads `nm -S -C` of the ELF and attributes each symbol to a row of `UI_DRIVERS_funcoes.tsv` with `bytes_original.py`'s own code (`row_of( )` is new there, the one reading of a font row by prefix; that tool's output is byte-identical on six ELFs before and after), and a symbol on a row of class `original` is a finding unless `ALLOWED` names it with its reason: `buildDashLayout( )` and the calibration's seven `g_*` caches (S10b-2's keepers, P4's table) and the four theme-catalogue names that `Temas.cpp` defines (valid only while `Themes.cpp` has no file symbol in the image). A unit that `[ui.original].units` lists and the image does not link counts by name too, which `bytes_original.py`'s unit rule does not do (it leaves those rows out, so a planted `uiButton` would pass; the mutant that restores that rule fails the gate). `redrawTopPanel( )` is class core in the table, not original, and stays allowed by the pattern that still matches it. C6's patterns stay as the second check, with one more: a name with a component that begins with draw, render or blit in any case, outside `ua::` and the Adafruit GFX classes, which five core symbols match (`renderGraphOptimized( )` and its local constant, `renderSystemInfo( )`, the two of `WebManager::RenderGuard`) and are allowed by name; 18 allowances in all at C6b (22 since C6c, below), each used on both images, and one that is not in the image or that nothing flags is a finding. The review's four minor findings: `--nm` with a missing tool exits 2 (it was a traceback, exit 1); the docstring says seven units, not eight; the `[ui.original]` comment of `features.toml` says that `handleResetTouchCal( )` calls only `showTouchCalibration( )` (`core0Yield( )` calls both; the C6 paragraph above said both of the two and is corrected); `tools/README.md` has rows for main's `cli_gates.py` and `test_cli_gates.py` (193 scripts, and CLAUDE.md says 193). **Proved** (`stages/s10b_c6b`): the nine planted names each exit 1 on both Ângulo ELFs (the C6 gate: 0); C0's Wire and C0b's Wi-Fi ELFs exit 1 with 8 findings each, `availableThemes` among them, now through the table; C7's two Ângulo ELFs exit 0 with the 18 allowances used; the product ELFs and a missing file exit 2; `--selftest` runs 48 cases (13 before), among them the table path, the unit condition and the stale rule, and 20 of 20 one-line mutations of the gate and of the row reader fail it or the suite; the nine products and both Ângulo `.bin` files are byte for byte C7's, built from scratch on the working tree; `gen_features.py --check` and `ci_gates_local.py` (53 of 53) pass. **Superseded on 2026-10-28 (S10c, stage D):** the allowances of `buildDashLayout( )` and of the calibration's seven `g_*` caches are gone. S10c D1 (`8bf2db72`) took `buildDashLayout( )`'s (22 to 21 allowances used), S10c D3a (`aea1f5f8`) the seven caches' and `showTouchCalibration( )`'s (21 to 13) and S10c D3b (`c67a1cb4`) `showSettingsDisplayOffset( )`'s; `tools/check_ui_link.py` reads 0 findings with 12 of 12 allowances used on both Ângulo images at `3d433d6c` (section 2.18).

**C6c, the link gate's blind spots.** The batch review of S10b-1 (COM RESSALVAS, 2026-10-22) found three. (1) Of the 188 symbols that `pico_wiretft` links and the Wire Ângulo does not, the C6b gate flagged 140 and passed 48 when they were appended to the real Wire Ângulo `nm` text: 18 had no row (code and tables that headers define: `panelDHT22`, `minMax*`, `SENSOR_PANEL_DISPATCH`, `PinKb::keyAt`, `PwdKb::*`, `CLOCK_COL_*`, `themeTint`, `fmtFloat1/2`, `menuLabelNoNumber`, `sensorNthChannel`), 2 sat on a mixed row (`handleTouchPanelV24( )` and its lambda), 11 on core rows (`dmaTft*`, `fillWindowDma`, `channelLabel`...) and 17 were compiler or library code. (2) Exit 1 and the product-image refusal had no negative control in CI: the mutants "`check( )` returns 0", "`main( )` returns 0" and "no refusal" passed `--selftest`. (3) A function added to Dashboard, Users or Settings with no row and a name no pattern matches passes, and nothing in CI forces a row. **The change touches tools and this table only; no image is built differently.** **15 rows for the headers' names**, each header as its own unit: `DisplayManager.h` (`CLOCK_COL_X`, `CLOCK_COL_W`), `PasswordKeyboard.h` (the three `PwdKb` tables and `popupHit`), `Themes.h` (`themeTint`) and `sensors/SensorPanelDispatch.h` (the five adapters and `SENSOR_PANEL_DISPATCH`) as original, 10 rows; `PinKeypad.h` (`PinKb::keyAt`: the Ângulo includes that header for the keypad's model, so it cannot be the original's), `DisplayManager_FmtFloat.h` (`fmtFloat1`, `fmtFloat2`), `display/SettingsMenu.h` (`menuLabelNoNumber`) and `sensors/SensorHelpers.h` (`sensorNthChannel`) as core, 5 rows. The table has 387 rows: 210 core, 135 original and 42 mixed. `row_of( )` needed no change: it reads the symbol key alone, and the unit matters only to the unit rule, which acts on the `.cpp` units of the manifest (a case of `--selftest` holds that no listed unit is a header). `tools/bytes_original.py` on the C6b images changes only by these rows: the original rows of `pico_wiretft` rise by 4,298 B and those of `pico_w_release` by 4,310 B (each the sum of the 13 names in `nm`, checked apart), the core rows by 484 B on both, and the two Ângulo images only in the count of original rows with no symbol (65 to 78); the boundary gate, which derives the original's headers from this table, needed one change: a row whose unit is itself a header made that header the original's, and `sensors/SensorPanelDispatch.h` is one that section 2.5 leaves out (mixed units include it, and `test_ui_boundary.py` pins it), so `tools/check_ui_boundary.py` now takes only the units that are source files in that derivation (the derived set is the same but for the four spellings of `src/ui/original/UiTouchChain`, which the path already covers), with two cases more in `test_ui_boundary.py` (309 checks, 307 before; the control of the pair fails without the change); it has 0 findings. **The mixed rule:** a symbol on a row of class mixed is a finding unless `ALLOWED` names it. Both tip Ângulo images link four, `handleTouch( )`, `loopCore1( )`, `showTouchCalibration( )` and `showSettingsDisplayOffset( )` (the table has 8 mixed function rows besides its 34 per-mode views), and each has a named allowance with its reason: 22 allowances now, each used on both images. `handleTouchPanelV24( )` and its lambda are findings. **The core rows and the library get no rule:** core is shared code by the table's definition, and library code is linked by whoever calls it. After C6c, of the 188, 155 are findings and the 33 that pass are 16 on core rows and 17 compiler or library names. **`--selftest` runs 71 cases (48 at C6b)**, among them the gate's own main function on a fake `nm` and `readelf` (exits 0, 1 and 2, and 2 over 1), the header rows, the mixed rule and the 48 above; the 15 mutants of the review and 12 of C6c's own each fail it, and none is equivalent. **Limits, stated in the docstring and in section 4.4:** a function added to Dashboard.cpp, Users.cpp or Settings.cpp (or a header) with no row and a name no pattern matches passes, because nothing in CI forces a row (`tsv_check.py`, which holds the table to the code, is scratch); core rows and library code get no rule; and the header rows cover the names the review found, not every definition of those headers. **Proved** (`stages/s10b_c6c`, outside the repository): the 48 rerun; the two tip Ângulo ELFs exit 0 with the 22 allowances used, C0's and C0b's exit 1 (16 findings each, 8 at C6b; the 8 added are header names and the mixed rule's), the product ELFs and a missing file exit 2; the nine products and both Ângulo `.bin` files are byte for byte C6b's. **Superseded on 2026-10-28 (S10c, stage D):** `PinKb::keyAt( )` is still in `PinKeypad.h` after S10c C8 (`c5a26bcc`), with `popupKeyAt( )`, the `popupRow*` helpers and the `FOOT_*` names, because `handleTouchPanelV24( )` reads them and its row in the function table is mixed; the 22 allowances of this paragraph are 12 since S10c D3b (section 2.18).

**C6d, the review of C6c answered.** The review (COM RESSALVAS, 2026-10-22) found four things. (1) Two of its nine mutants survived the 71 cases of `--selftest`, both on the exit paths: `main( )` returning the last ELF's code (every multi-ELF case put the worse image last) and `check( )` leaving the stale allowances out of the exit code (the stale cases stopped at `scan( )`). `--selftest` runs 79 cases now, each new one through `main( )`: the ELFs in the orders (finding, clean) 1, (refusal, clean) 2, (refusal, finding) 2 and (clean, finding, clean) 1; an allowed name missing from the fake `nm` 1; a table in which `handleTouch( )` is core 1 (the other kind of stale allowance); no ELF 2; `--original-units`. The review's 9 mutants, the batch review's 15, C6c's 12 and 12 of C6d's own on the exit and option paths (48 in all) each fail it; the 71 cases let 2 of the review's 9 and 5 of the 12 through. (2) `PinKb::keyAt`, `fmtFloat1` and `fmtFloat2` are original rows now (387 rows: 207 core, 138 original, 42 mixed), each note with its callers by image: in the six images that link them, `keyAt` is called by `handleTouchPanelV24( )` alone, `fmtFloat1` by `drawGraphScreen( )` and `drawGraphDetailScreen( )`, `fmtFloat2` by `drawGraphDetailScreen( )` alone; neither Ângulo image links them. `keyAt` has a native test (`test_validators`), so the class rests on the callers. `bytes_original.py` moves 394 B, the sum of the three in `nm`, from core to original on `pico_w_release` (original 61,689 to 62,083 B) and `pico_wiretft` (60,997 to 61,391 B), and none on the Ângulo images (only their count of original rows with no symbol, 78 to 81). Of the 188 symbols of C6c's count, 158 are findings now (155) and the 30 that pass are 13 on core rows and 17 compiler or library names. `sensorNthChannel` and `menuLabelNoNumber` stay core (a native test each). (3) `_CORE1` said every image links `loopCore1( )`: of the eleven images ten do and `pico_w_air` does not (`nm`); both Ângulo images link it, which is what the allowance needs. (4) C6c's message said the control of its two new `test_ui_boundary.py` cases fails without the change: the first new case and an existing one fail, and the control passes either way. **Proved** (`stages/s10b_c6d`, outside the repository): the two Ângulo ELFs exit 0 with the 22 allowances used, C0's and C0b's exit 1 with 17 findings (16, and `keyAt`), the products and a missing file exit 2; `check_ui_boundary.py` 0 findings, `test_ui_boundary.py` 309 of 309, `tsv_check.py` 387 rows and 0 findings. No image was rebuilt: no build step reads the tools, the table or this document (the nine `extra_scripts` and every tool their names reach name none of them).

**C7b, the bench and its record.** No function and no byte of an image changes. The documents: this section's bench paragraph (B1 to B3 as they ran on 2026-10-22), item 9 (the rewrite done), the count of commits and the two labels of the batch review's finding 4 ("C7 is the ninth" where ten commits followed the sync then, and fourteen do now; "(this commit)" on C7's and C6c's paragraphs; C7's "191 rows", 193 since C6b), section 3's table after C6c and C6d, and section 4.4 (the commit messages closed; B4, the injected touches and `heap_lb` added). The messages were rewritten three times on 2026-10-22, each time over the same trees, authors and dates: C3 to C6b first (C3's and C5a's corrections); C5b to C6b again (two hashes the first pass had retired); and C6c's own after its review (what the control of its two new tests shows). **Proved** on the working tree before the commit: only this file changes, and no build step reads it (`check_ui_boundary.py` reads section 2.5, untouched here, and runs in CI's gates only); `check_ui_boundary.py` 0 findings, `tools/test_ui_boundary.py` and `ci_gates_local.py` pass.

**C6e, the gate's command line under test.** The final review (COM RESSALVAS, 2026-10-22) found three exit-path mutants of `tools/check_ui_link.py` that passed the 79 cases, because every main-path case called `main( )` in this process with a readelf that printed nothing: `sys.exit(main(...))` as `main(...)` (a planted draw exits 0 on the command line, and CI's step would pass an image with findings), `scan(..., files)` as `scan(..., set())` (an image that links `Themes.cpp`, with its allowance, exits 0) and `rc.count(1) == 1` (two images with findings exit 0; CI passes one ELF, so this one cannot reach it). `--selftest` runs 85 cases now. Four run the file as a process, as CI does, on a fake `nm` and `readelf`: a clean image exits 0, a planted original draw 1, a `readelf` with a `FILE` line for `Themes.cpp` over an `nm` that holds the four catalogue names 1 (those four findings), a product image 2. Two call `main( )` for the third mutant (two images with findings exit 1, two refused 2). `--selftest` also exits from inside `main( )` now: the first mutant drops the exit status of the `--selftest` run itself, and the new cases alone printed their FAIL lines under an exit 0. **Proved** (`stages/s10b_c6e`, outside the repository): the review's 12 mutants and C6d's 48 each fail `--selftest`, and the three that passed the 79 cases pass the 79-case file (the control); the exits on the real ELFs are C6d's (the two Ângulo images 0, C0's and C0b's 1, the products and a missing file 2); `check_ui_boundary.py` 0 findings, `tools/test_ui_boundary.py` 309 of 309, and no build step reads the changed files (the nine `extra_scripts` and every tool their names reach name none of them). The review's three minor findings are in the bench paragraph above.

### 2.16 The Ângulo's theme: a mode and an accent (S10c, A')

**S10c A' (2026-10-22, branch `feat/ui-drivers-s10c`, base `c0d835f8`, the tip of S10b-1) did this.** In the original a theme is a palette of 24 colours, simut_def or a `.thm` file. In the Ângulo it is a mode (light, dark or automatic) and one accent colour from a palette of six, and everything else follows the mode. S10c's first Stage A mapped the same `.thm` files into both UIs; the maintainer voided it on 2026-10-22 (first bullet) and the planner replaced it with Stage A'. Nine commits changed the repository, in this order on the branch: A'1 (the palette in the tokens), A'4 (a `.thm` is stored, not refused), A'1b (the review of A'1 answered), A'2 (the mode and the accent on the glass), A'3 (the Tema screen and the catalogue of 18), A'2b (the tables in flash, the glue linked into its test), A'3b (the preview's scope, the lower-case names, the pressed-state gap), A'2c (the glue run on the host, the Ângulo's host tests in CI) and A'2d (the review of A'3b and A'2c answered, in the tests). A'5, the bench on the Pico 2 W, changed no file; this section is A'6. After it the branch took Stage C1's first two commits (C1a and C1a-b, the configurator: section 4.4) and A'2e, the answer to the review of A'2d and A'6, in the tests. The plans (`S10C_PLANO.md` and `S10C_A_PLANO.md`) are not in the repository, and the findings they numbered F4, F5, F8 and F9 keep those labels here. The commits are named by step and not by hash, for the reason section 2.15 gives: the branch may still be amended before it is integrated.

- **The maintainer's words of 2026-10-22, and the defaults that ran.** He changed Stage A's design: "O tema na interface Ângulo eu quero que funcione diferente. O tema vai definir apenas a cor de destaque. Todo o resto da interface vai seguir o modo escuro ou claro. No menu de tema, o usuário vai escolher uma cor em uma palheta e essa cor deve ter contraste adequado tanto para o tema claro, quanto escuro." So in the Ângulo the theme defines only the accent, everything else follows the light or dark mode, and each palette colour must hold its contrast in both modes. Asked what an uploaded `.thm` does there, he answered "Só serve à original": the Ângulo accepts and stores the file, lists none and applies none, and the file takes effect only when the board runs the original. He asked for a print of the dashboard to decide the colours, was sent a capture of the panel and a host preview of the six accents in both modes, and answered "Essas 6 cores já são suficientes por enquanto. Pode implementar." (A'-D1). The plan's other decisions ran on their defaults, which he did not object to: **A'-D2**, the storage, `themeIndex = modo + 3 x acento`; **A'-D3**, the web lists the 18 pairs; **A'-D4**, one Tema screen with a live preview and one primary button, "Aplicar tema"; **A'-D5**, the text on the accent is the mode's stock `acentoTinta`, shared by every entry; **A'-D6**, no `acentoSuave` role.
- **A'1, the palette in the tokens.** `tools/tft_ui/tokens_tft.json` gets `acentos`, a list of six entries. Each has an `id` (a lower-case slug) and, but for the first, a `claro` and an `escuro` hex: the accent for that mode. **Entry 0, `verde-azulado`, is today's accent and carries no hex:** it is the guide's three accent roles, so `TEMA_CLARO`, `TEMA_ESCURO` and both `_DESAB` tables stay word for word in the generated header (which grew by 450 lines in A'1 and lost none) and the stock look is the default. The JSON holds no text, only ids: the names are panel texts, for A'3 and for Stage B. One colour cannot serve both modes (the planner's hand computation on the panel's colours: accent text at 4.5:1 needs a luminance of at most 0.168 against the light `fundo` and at least 0.230 against the dark `superficie`, and the two ranges do not overlap), so each entry is one hue with a light value and a dark value, as the guide's own accent already brightens in the dark theme. The six, with the contrast of the accent on `fundo` in each mode, as `python3 tools/tft_ui/gen_tokens.py --report` prints them (run on 2026-10-22 on the tree of A'2c; that is where every ratio in this section comes from, and the plan's hand-computed ratios equal the generator's to 2 decimals in all 48 cells of the golden table). The hex of entry 0 is the guide's, `guia` in the same JSON, and not an `acentos` value:

| # | id | light acento | on fundo | dark acento | on fundo |
|---|---|---|---|---|---|
| 0 | `verde-azulado` (stock) | `#1f6355` | 6.83:1 | `#5fb39a` | 7.33:1 |
| 1 | `petroleo` | `#1a6478` | 6.15:1 | `#5ab0c8` | 7.70:1 |
| 2 | `azul` | `#1f5299` | 7.27:1 | `#6a9ee8` | 6.87:1 |
| 3 | `indigo` | `#4a4aa0` | 7.03:1 | `#9a9cf0` | 7.66:1 |
| 4 | `violeta` | `#7a3f9a` | 6.60:1 | `#c08ae0` | 7.25:1 |
| 5 | `magenta` | `#9a3a78` | 6.00:1 | `#e08ac4` | 7.83:1 |

- **What an entry is held to, and what is derived (A'1).** Every entry, in both modes, must pass these floors (`ALVOS_ACENTO` in `gen_tokens.py`): `acento` at least 4.5:1 on `fundo` and on `superficie` (it is used as text) and 3:1 on `superficie2`; `acentoTinta` at least 4.5:1 on `acento` and on `acentoForte`; `acentoForte` at least 3:1 on `superficie`; and the mode's `acentoTinta` must beat the other mode's as the text on that accent. The tightest is magenta in light mode, `acento` on `fundo` at 6.00:1 against its floor of 4.5:1 (1.33 times it). Two structural rules follow. **The 17 roles of each entry and mode are distinct in RGB565** (F5 of the plan): `ua::desab( )` finds a role by colour equality, so two roles of one colour would take each other's disabled twin. **The hue rule,** on the colours the panel shows (HSV hue): only the accent calls attention (ANGULO.md section 2, rule 1), so it must not read as a status colour. It is at least 30 deg from the mode's `perigo` and from its `alerta`, at least 15 deg from its `positivo`, and the entries of one mode are at least 20 deg from one another. The stock teal is 19.7 deg (light) and 21.1 deg (dark) from `positivo`; the closest pairs are petroleo and azul at 20.8 deg in light mode and azul and indigo at 21.9 deg in dark mode; the nearest an accent comes to `perigo` or `alerta` is magenta, 46.6 deg from `perigo` in light mode. A distance is compared after `round( d, 6 )` (A'1b): a hue exactly on a limit passes and is not refused by float noise, and the six shipped hues are at least 0.76 deg from every limit. **The rest is derived, not stored:** `acentoForte` is 25 % of the mode's `tinta` mixed over the accent (`blend565( tinta, acento, 0.25 )`), which moves it away from the background in both modes; `acentoTinta` is the mode's stock value (`#f1faf6` in light mode, `#0e211b` in dark mode), shared by every entry, since pure white would equal `superficie` in light mode; and the disabled twin of every table is `blend565( role, fundo, 0.45 )`, as before. **The write mode runs every check first and refuses to write** when one fails (exit 1, the header untouched), as `design/build-tokens.mjs` does; a colour that misses a floor is moved in lightness, and the floor stays.
- **A'1's proof.** `tools/tft_ui/test_acentos.py` (standard library only; 50 cases at A'1, 54 since A'1b, 54 on 2026-10-22) reads the header back and judges it with colour arithmetic written in the test and not imported from the generator, so that the two cannot agree by sharing a mistake: the golden table of contrasts to 2 decimals (48 figures: `acento` on `superficie`, `fundo` and `superficie2`, and the text on it, for six entries and two modes), every floor, the 17 distinct roles, the hue rule, both derivations (two values worked by hand), entry 0 equal to the guide, the JSON's shape and the hue limits at their boundary in exact arithmetic. Each check has a control, the same check on a copy with one violation, which must report it; and the generator, run on a spec with one violation (a light `acento` at 4.02:1, a dark `acento` equal to `tinta2`, a hue of 10 deg, `acentoForte` equal to `acento`, two accents on one hue, a hex on entry 0, a text on an entry, a repeated id), must exit 1, name the pair and leave the header it would write unchanged (sha256). The test was written first and failed before the generator knew `acentos` (4 of the 10 cases it reached). Of 12 mutants of the generator, 11 were killed; the survivor, the text-colour rule off, is implied by the floors for any accent that passes them. Nothing read the tables yet, so no image moved: the nine product images and both Ângulo `.bin` equal the base, `elf_funcoes.py` finds 0 changes, and V-PIX holds its 840 frames.
- **A'4, a `.thm` is stored, not refused.** C4b's refusal (section 2.15, above) was removed on 2026-10-22 at the maintainer's word: `WebManager::handleUploadData( )` no longer refuses a `.thm` at `UPLOAD_FILE_START`, and the Ângulo stores the file like any other, lists no theme from it and applies none. The rescan after an upload (`scanCustomThemes( )` and the record "Custom themes rescanned") and the rescan after a delete stay, under `#if SIMUT_UI_CUSTOM_THEMES` (the capability, 0 on the Ângulo, `UiCaps.h`), so the Ângulo neither rescans nor logs a rescan, and the original compiles what it compiled. `@TRL` entry `7bb0d94e`, C4b's reason, is out of both packs (`grep -c 7bb0d94e data/lang/*.lng` prints 0 for each). The comments of `UiCaps.h` and `Temas.cpp` say what holds now. **The nine images are byte-equal to the base,** and the control that makes the proof bite, the delete site's guard written `#if 0` so that the original loses its rescan, makes all nine differ. `__LINE__` does not reach an image from this file (the edit moves every line of it after line 148 and the nine, `pico_w_asserts` among them, stayed equal), so no line was held in place. The Ângulo images change in code in `handleUploadData( )` and `handleDelete( )` only (the rest is layout, proved name by name), and lose three read-only strings (".thm", "Custom themes rescanned" and the refusal's text, 126 B with their NULs): the Wire Ângulo `.bin` 820,928 -> 820,568 B (-360) and the Pico 2 W's 1,029,952 -> 1,029,576 B (-376), budgets lowered in the commit.
- **The sprawl +1, a recorded deviation.** A'4 raises `tools/feature_sprawl.json`'s mark for `SIMUT_UI_CUSTOM_THEMES` in `WebManager_Files.cpp` from 1 to 2 (the gate's total 358 -> 359; `check_feature_sprawl.py` prints 359 on the tree of A'2c): C4b's one `#if !` became two `#if`, one in each function that rescans. The plan's stop list (S10C_PLANO.md section 12) names "the sprawl goes up". The review of A'4 tried the alternative that the plan kept as a fallback for another purpose, folding the guard into the existing `if` as `SIMUT_UI_CUSTOM_THEMES &&`: it gives the same images (the nine and the Wire Ângulo `.bin` byte-identical to the `#if` form's, with 0 references to `scanCustomThemes( )` in either), but `check_feature_sprawl.py` counts only `#if` lines, so the fold would hide the macro's two sites from the gate (1 -> 0 on paper, the decision unchanged). The two honest `#if` sites stay, and the mark goes 1 -> 2 in the same commit, as the gate asks. The main session decided it: the stop is read as unplanned growth, and this one was measured against its alternative.
- **A'2, the mode and the accent on the glass, and the encoding (F4).** `cfg.themeIndex` is an `int8_t` and now holds both choices: **`themeIndex = modo + 3 x acento`**, the mode 0 light, 1 dark, 2 automatic, and the accent the entry of the palette (0 to 5). So 0, 1 and 2 keep their meaning, the three modes with the stock accent, and the byte of a board that ran the S10b Ângulo reads as it did. **The decode** is `ua::escolhaDoIndice( int8_t )`: `t` in [0, 18) is mode `t % 3` and accent `t / 3`, and every other `int8_t`, negative or 18 and above, is light with the stock accent. `ua::indiceDaEscolha( modo, acento )` is its inverse and gives 0 for a pair outside the palette. `test_temas.py` runs the decode over all 256 values and kills the mutant that wraps with `t % N` instead of checking the range. **The number 3 and `ACENTOS_N` live in these two functions:** `test_temas.py` refuses `ACENTOS_N` anywhere else outside a `static_assert`, and a bare 3 or 6 in the Tema screen, its touch and `uaTema( )`. The comment above them says who else reads the generated count, since A'2c (it had called the two the only readers): "The two encoding functions: the only code that knows how many modes there are, and the only hand-written code that names the generated count of the palette (ACENTOS_N) outside a static_assert. Other files tie a size to it in static_assert statements (Temas.cpp: the catalogue; MenusConfig.cpp: the Tema screen). tema( ) and desab( ) read the palette's size too, through kAcentos above: the extent of the generated index array, so the count has one source and no second copy." So the other readers of `entrega::ACENTOS_N` are `Temas.cpp` (`static_assert( NUM_TEMAS == 3 * ACENTOS_N )`) and `MenusConfig.cpp` (two: the screen names every entry of the palette and only those, and the swatches fit one row), and `tema( )` and `desab( )` read the size through `kAcentos`. `test_temas.py` holds the comment to the sources: a file that reads `ACENTOS_N` and is not named, or a name that reads nothing, is reported. On the glass, Core 1 still reads only the themeIndex byte and const tables: `ua::tema( escuro, acento )` hands out one of twelve tables (six entries, two modes), `ua::Tema` gained `entrada`, its palette entry, and `ua::desab( )` takes the 45 % twin of that same entry for all 17 roles. **The frame hash ignored the accent (G5 of the plan):** it covered the mode and not the accent, so a web or CLI change of the accent alone repainted nothing on a screen whose view model was unchanged. `ua::IdentidadeDoQuadro` is what a frame is besides its view model (the screen, the mode, the accent and the alignment); the glue hashes it, and `repintaTudo( )` asks for a full repaint when the mode, the accent or the alignment changed. The update screen, which Core 0 paints, has never hashed the alignment and passes 0, 0. On the host, `host_telas.py --acentos` draws the dashboard, a menu and a chart in the six accents and both modes (36 frames, 67,056 accent pixels, each frame holding its entry's accent and none of another's), and its sheet went to the maintainer. Flash: the Wire Ângulo +872 B and the Pico 2 W +968 B; static RAM +290 B on both (9,313 -> 9,603 B, twelve resident tables of 36 B where four stood), which A'2b took back.
- **Reading across the UIs (F4), tested on a board (C5, 2026-10-30).** The original reads the same byte as a built-in palette plus the custom ones: `getThemePalette( )` in `Themes.cpp` gives `availableThemes[index]` for the built-ins (one, simut_def, in every product), then custom theme `index - 1` when that `.thm` is loaded (at most 8 are), and simut_def for any other index. So on the original 0 is simut_def and 1 + k is custom theme k, where on the Ângulo the byte is a mode and an accent. A board that changes image is expected to keep the byte (the plan's reading of the code: the UI is not part of the OTA variant, so an OTA from one UI to the other is accepted in both directions) and to read it in the other meaning. What the Ângulo writes as 3 to 8 reads on the original as custom themes 2 to 7 where those files are loaded, and 9 to 17 as simut_def; what it writes as 1 and 2 (dark and automatic) already read as custom themes 0 and 1. What the original writes (0 to 8) reads on the Ângulo as a valid pair, and back on the Ângulo the same pair returns. **C5 crossed the two images on 2026-10-30** (the gate of v2.13.0, the Pico 2 W, 15 steps; the records are in `scratchpad/bancada_20261030_rel2130/`: `SESSAO1.txt`, `c5_results.jsonl`, one row per run of the 15 steps, and the captures `c5_glass_*.bmp`, 17 taken and 16 judged by the session, each judged one with its `.png`; the first step's first run, stopped by a defect of the bench's tool, left the 17th). The OTA kept the byte in both directions and the 527 configuration values at every step. Number 2 (the original's custom theme 1, `contraste`) drew on the Ângulo as automatic mode with the stock accent; number 7, saved while the Ângulo ran (dark, accent 2), drew on the original as simut_def, past the built-in theme and the two loaded custom ones, as this paragraph reads it; number 2 saved again drew as `contraste`, and the original built from the same commit (`pico2_w_original`) read the byte the same way. The *Upgrading* text of v2.13.0 in `CHANGELOG.md` says what a field device shows after the switch.
- **A'3, the Tema screen and the catalogue of 18.** The old screen was three rows and a note that became false ("A cor de destaque muda com o tema."). The new one puts the mode and the accent on one screen, with a live preview and one primary button; its bands are on the 4 px grid. The title has Back. **Three segments** of 96 x 44 px follow, each an icon over its label ("Claro", "Escuro", "Automático"); then the mode in words ("Modo claro", "Modo escuro", "Modo automático: escuro das 19 às 7"); **six swatches**, 44 px targets each holding a 36 px disc in its entry's accent for the mode now on the glass; the accent in words ("Cor de destaque: azul"); and **"Aplicar tema"**. A selected control never rests on colour alone: a segment is the accent fill with `acentoTinta` content plus the words line, and a swatch has the check mark and a 2 px ring of `tinta` with a 2 px gap, plus the words line. Pressed, a filled control turns `acentoForte` and an unselected segment `superficie2`; the kit draws that, and nothing on the device feeds it (section 4.4). **"Aplicar tema" is disabled, drawn as its 45 % twin and listed as the inert target, while the choice on show equals the saved one,** so a tap on it does nothing and posts nothing. **The preview** is one `int8` of Core 1 state (`_uaPrevia`): a tap on a segment or a swatch sets it, Back and any other mode drop it, and nothing is saved until Aplicar. It is honoured only while the frame being painted is `MODE_SETTINGS_THEMES`: `uaTema( UiMode )` takes the mode of the frame and not `_uiMode`, so the update screen, which Core 0 paints while `_uiMode` may still say Tema, is painted with the saved theme (A'3b holds this with 1,296 + 1,024 cases, below). The touch is one rule per kind of target (a segment previews its mode, a swatch its accent, the enabled button posts `actApplyTheme( indiceDaEscolha( m, k ) )` once, Back drops the preview) and no numeric literal bounds it; `UiEvent::id` is an `int`, so it holds 17. **The names:** the colour is in lower case after a comma or a colon ("Claro, azul", "Cor de destaque: azul"; ANGULO.md section 7, set in A'3b), and the stock accent's three rows keep "Claro", "Escuro" and "Automático", since they carry no colour. **The catalogue:** `Temas.cpp` lists the 18 choices, index `t` being mode `t % 3` and accent `t / 3`, with ids of at most 15 B (`escuro-petroleo` is 15) and names of at most 23 B of UTF-8 (`Automático, petróleo` is 22), which are the sizes of a `.thm`'s code and name, and `static_assert( NUM_TEMAS == 3 * ACENTOS_N )`; `test_temas.py` holds the sizes, each row's index and the literals. **`/api/themes` lists the 18** as `{"id":<index>,"name":<displayName>}`, the web page posts the id as `theme` to `/api/save_sys`, and the commit accepts it because it is below `getThemeCount( )`. **`WebUI.h`, `WebManager_Api.cpp` and `WebManager_Commit.cpp` do not change** (their diff against the S10b tip is empty), and each choice is a whole pair, so the web has nothing to keep. "Automático" fits its segment, 86 px in the 88 px of inner width, in 9 pt regular; `test_temas.py` measures every fixed label (the words lines 84, 97 and 282 px in 304, the widest accent line 244 px in 304, "Aplicar tema" 107 px in 280). Flash: the Wire Ângulo +2,256 B and the Pico 2 W +2,272 B.
- **A'2b, where the tables live, and the glue linked into its test.** A'2 had built the palette's twelve tables as `const` objects with a constructor, so `kPaleta` sat in `.bss` (0x1B0 B) and a static initialiser filled it at every boot. The review of A'2 asked for `constexpr` tables from the generator. `gen_tokens.py` now emits every table of the header `constexpr`, the kit's `deEntrega( )` and `Paleta` are `constexpr`, and the compiler builds `kPaleta`, which sits in flash beside the generated tables. **Static RAM falls from 9,603 to 9,171 B on both Ângulo images** (-432 B, measured by `check_ui_ram.py`, the gate's own lines in both builds; `ui_ram_budget.json` lowered in the same change), and flash used falls by 364 B on the Wire Ângulo and 396 B on the Pico 2 W; the flash budgets and the OTA table of `PLANO_STABLE.md` section 3.1 followed. **Core 1 now reads the palette from flash** where it read RAM: A'2b did not measure what those reads cost in a paint, and A'5 did, within a recorded limit (the bench, below). The rendered pixels do not change: V-PIX holds 948 of 948 frames against A'3's record, and the accent sheet is byte for byte A'2's. The second part: the review had found that no run exercised the glue (`DisplayManager_UiA.cpp` is part of `DisplayManager`, with two displays, a touch panel and two cores, and cannot link on the host), so A'2b linked its text. `test_temas.py` cuts `fnv( )`, `uaTema( )` and the two runs of statements that hash the view model and the frame's identity out of the file, verbatim, and `test_temas_glue_main.cpp` compiles them against a stand-in for the few members they touch. Of 31 mutants of the glue, the review's three and 24 more passed A'3's test; all 27 fail now, 3 were refused already and 1 stays live and equivalent (`e.modo >= MODO_ESCURO`). `test_temas.py` went to 50 cases. The kit's `initarray_check.py` proves that the new image's constructors are the old image's without the one slot that left.
- **A'3b, the preview's scope, the lower-case names and the pressed-state gap.** The review of A'3 found the preview's scoping unpinned. By reading, no frame paints the preview outside the Tema screen, but three mutants passed every check: M1, `uaTema( )` ignoring the frame's mode; M2, `uiPaintOta( )` taking the theme of `_uiMode`; M3, the `_uaPrevia = -1` reset deleted. Ten mutants and variants were live against A'3's tree; against A'2b's, M1 and M2 were already killed (by the glue link and the exact-text pins) and M3 and six variants passed. **All ten are killed now, by what `uaTema( )` returns:** the link runs `uaTema( )` for every frame mode against every screen on show (89,388 frames), so that a `uaTema( )` which reads the screen on show instead of the frame's mode is caught by what it gives and not by the compiler; the update screen's piece, which now starts at the statement that gives it its theme, runs 1,296 cases (every saved choice against every preview on every screen on show, the hash being the saved theme's); and the one statement of `uaQuadro( )` that resets the preview is cut out and run over 1,024 cases (4 screens x 256 values), a glue with none or two being refused at the cut. Exact-text pins complement it: the preview is named three times in the glue, its reset comes before the line that returns while no look is due, `uaPintar( )` is called for `_uiMode` and then for `MODE_OTA_UPDATE`, and in the menus glue the preview is named only inside the Tema screen's two cases. `test_temas.py` went from 50 to 65 cases. **Microcopy:** ANGULO.md section 7 asks for sentence case, so a colour name after a colon or a comma is in lower case. `TEMA_CORES` is `verde-azulado`, `petróleo`, `azul`, `índigo`, `violeta`, `magenta`; `Temas.cpp` names the 15 rows that carry a colour "Claro, azul", "Escuro, petróleo" and so on; and a case of `test_temas.py` holds it, with three controls. Those 15 names and "verde-azulado" are the 16 flash texts that differ from A'2b's image, and no function changes (V-P2W: 0 functions). V-PIX against A'2b's record: 831 frames equal and 117 different, the 117 being exactly the list made before any frame was drawn, the Tema screen's 39 samples x 3 images. **Pressed states:** no screen of the device shows one; section 4.4 has the gap.
- **A'2c, the glue run on the host, and the Ângulo's host tests in CI.** Three reviews in a row (A'2, A'3, A'2b) found mutants of `DisplayManager_UiA.cpp` that passed `test_temas.py`, each outside what the test had cut out or read as text, and no workflow ran the Ângulo's host tests, which need Adafruit GFX from `.pio`. A'2c stopped chasing lines (option (a) of its brief, picked by measuring). **`test_temas.py` cuts six whole functions of the glue and the file's state out verbatim, without their comments, by brace matching** (`uaTema`, `uaRelogio`, `uaPintar`, `uaEstado`, `uaQuadro` and `uiPaintOta`: a reformatted signature is still found, a comment that names a function is not taken for it, and none or two definitions are refused at the cut), and `test_temas_glue_main.cpp` compiles them against a stand-in of 169 lines for what they call (the panel's push, into an array that is the glass; the clock; the saved configuration) and the kit as it is, and runs them. It runs every saved index (the 18, out-of-range values and no configuration) by every clock line (none, malformed, each hour, provisional) by three screens on show, through `uaQuadro( )`; the clock with the state mutex busy; the preview on and off the Tema screen, with and without a pass that returns at the 250 ms gate; `uiPaintOta( )` for every saved index against every preview and screen; and the repaint decisions (nothing for an unchanged frame, all 12 strips for the accent alone, the mode alone or the alignment alone, 2 for the view model alone). The glass is compared pixel by pixel with an image computed from the generated tables, and `uaEstado( )` reports the mode on the glass and the hash, which a model written out again in the test must equal: **17,829 checks,** and one wrong pixel in every pushed strip must fail them (the instrument). A static rule complements the run: the glue picks a palette in `uaTema( )` alone, so a paint site that reads `ua::tema( )` or `temaClaro( )` itself is refused. The cost: 65 cases in 3.7 s became 91 in 9.5 s (16 cores, g++ 13.3.0). Of 26 mutants of the glue, the 11 that passed A'3b's test fail now (N1 and N3 to N5 of the review of A'2b, and 7 of A'2c's own on the paint path: the strips cleared to the surface colour, the frame recorded for the screen on show, the strip hashes not kept, the repaint flag not lowered, the margin not blackened, the alignment band not blackened, the clock read from the wrong buffer when the mutex is busy) and the other 15 still fail; the harmless changes of form (a reformatted signature, CRLF, a comment, a static) pass. The exact-text checks of A'2b and A'3b stay as a second layer. The comment on the encoding functions was corrected in this step (A'2, above). The CI part is in the next bullet.
- **A'2d, the review of A'3b and A'2c answered: the paint protocol, the clock's bounds, the counters and the Tema touch.** The review ran 29 mutants of the two glue files through `test_temas.py`, and 15 of them passed it at A'2c's tree. All 29 fail it now, and A'2c's 32 keep their verdicts (four harmless changes of form still pass, one stays equivalent). Tests only: no file under `src/` changes, so no image does. **The paint:** the stand-in's screen also draws bars across the 4 px margin, so a margin blackened before the screen shows on the glass; and the stand-in's push is deferred as the DMA is (the two rules of `DisplayManager_UiA_Empurrar.cpp`: a push starts by waiting for the one in flight, and only a wait ends it), so a strip overwritten before its wait reaches the glass wrong and one never waited for never reaches it. After every paint no push is in flight, and a repaint that skips a strip between two pushed ones is run. **The clock:** `uaRelogio( )` on 13 lines at the bounds (the hours 00, 23, 24, 29 and -1, a tail of 5 and of 4 characters after either separator), with the state mutex free and busy, and the automatic mode's choice at the same bounds through `uaQuadro( )`. **The state and the counters:** `uaEstado( )` is read while the screen on show differs from the one on the glass, and `_bootPaintSeq` and `_lastHeartbeat` are asserted over 24 passes on both cores, and the update screen's `_otaDrawnSeq` and `_otaPctDrawn` in its own cases. **The Tema touch, in the repository:** `escolhaNaTela( )` and the Tema case of `uaToqueMenus( )` are cut out of `DisplayManager_UiA_Menus.cpp` the same way and run by `tft_ui/host/test_temas_toque_main.cpp` (65,792 pairs of saved choice and preview; 185,040 taps over every saved index, 24 previews, 15 targets and the touch gate granting and refusing), and the kit's text rule for the case is ported into the test with its 13 mutants as controls, so CI now holds what only the kit held. **The cut refuses the preprocessor:** a piece that holds a conditional, or that a conditional the test does not name encloses, is refused with the function and the line; the list is empty for all ten pieces today. `test_temas.py` went from 91 to 160 cases, the glue program from 17,829 to 28,318 checks, and the run from 9.5 s to 18.1 s (three alternating runs of each, 16 cores, under another job's load). **Left alone:** the Tema screen's builder, the `MODE_SETTINGS_THEMES` case of `uaMontarMenus( )`, is not run, and two mutants of it pass (the saved choice taken from the shown one, the accent forced to the stock); section 4.4 records it.
- **A'2e, the review of A'2d and A'6 answered** (2026-10-22, the main session, on the review's own prototypes). Tests only. **The margin fill is a bus user:** the library's `fillMarginsBlack( )` is synchronous and must find the bus free (the header of `DisplayManager_UiA_Empurrar.cpp`: it runs before the frame's first burst), so the stand-in counts a fill while a push is in flight as a violation; moving the fill into the first burst or to just before the last wait now fails (the review's O1a and O1b, which passed 160 of 160). **Compose and push overlap:** the stand-in counts the waits the glue asks for itself while a push is in flight (the wait inside `uaEmpurrar( )` is the hardware's rule and is not counted), and the direct paints must make exactly 1 (every strip pushed: the last wait), 2 (strips 4 and 6 with 5 skipped: before strips 6 and 8 reuse a half still read) and 0 (none); a glue that waits after every push or before every strip paints the same pixels and now fails (O2, O5). **The routing to the Tema case:** the text rule now also holds the first statement of `uaToqueMenus( )`, the switch of the screens it owns ending in `return UaToque::NAO_E_MEU`, with `MODE_SETTINGS_THEMES` among them, followed by `if ( !a ) return UaToque::TRATADO;` (X6 and X7, which passed every run, since the run starts inside the case). **The Tema screen's builder runs:** its `MODE_SETTINGS_THEMES` case in `uaMontarMenus( )` is cut out as a third piece of the menus glue and run over every saved index and none by 24 previews (6,168 cases): the shown pair, the saved index and no pressed state; A'2d's two live mutants (B1, the saved choice taken from the shown one; B2, the accent forced to the stock) fail it. The eight mutants are controls the test runs itself. `test_temas.py` went from 160 to 168 cases, in 19.2 s (one run, 16 cores).
- **The proofs that run, and where.** In CI's `gates` job (`.github/workflows/build.yml`): `gen_tokens.py --check --report` (the header equals the JSON and every pair is at or over its target); `test_acentos.py` (since A'1); and, since A'2c, after the job fetches Adafruit GFX at the version `platformio.ini` pins (`pio pkg install -e pico_wiretft_angulo --library <the pin, read from the file> --no-save`: the library alone, 5.2 s cold on the author's machine), `test_temas.py`, `test_cantos.py` and `test_alvos.py` (22 s added at A'2c on the author's machine, and `test_temas.py` alone from 9.5 s to 18.1 s at A'2d; a 4-vCPU runner was not measured). `test_temas.py` ran 168 cases, 0 failed, on 2026-10-22 (91 at A'2c, 160 at A'2d): the kit's host program (800 checks that day; the A'2 commit counted 786), the glue link (28,318 checks since A'2d, 17,829 at A'2c), the Tema touch's run and its text rule (A'2d), the screen's builder and the routing (A'2e), the mutants of the kit and of the glue that must each fail it, the static rules, and the Tema screen's and the catalogue's checks. In CI's form (the `run:` texts read from the workflow, under `bash -e`, from a clean export of the tree), with no fetch the test step exits 2, with no pin in `platformio.ini` the fetch step exits 1 and fetches nothing, a planted failing assertion in the glue program fails the step (exit 1, 91 cases, 1 failed, on A'2c's tree on 2026-10-22) and removing it passes. **The steps have not run on GitHub's runner.** Outside CI, because nothing in the repository holds what they compare: **V-PIX,** `host_telas.py` over the six groups (5, 62, 82, 56, 66 and 45 samples on 2026-10-22, 316 in all, every group exit 0), whose 948 frames (3 images per sample) must equal the previous step's record, except the samples a step names before drawing (A'3: 9 images replaced and 108 new; A'3b: exactly the 117 listed), and which is not in CI since no frame record is committed; `host_telas.py --acentos`; the kit's `glue_check.py --tema` (since A'2d its rule also runs inside `test_temas.py`, in CI; the Tema touch's five statements and 13 mutants, all refused at A'3: applying on a swatch tap, posting k, posting m, keeping `i < 3`, Back applying or keeping the preview, the inert button acting, an ungated or double Aplicar, a segment or a swatch previewing the wrong half, the preview ignored); and the nine sha256, the proof at every step that the nine product images equal the base's. The kit (`tools_s10c/`) is outside the repository.

**The reviews.** Each step was reviewed on its own before the next one began, by a read-only session that builds, plants mutants and re-runs the proofs. The first five reviews all returned COM RESSALVAS, and each was answered (A'4's with a recorded deviation); the records are `rev_s10c_*`, outside the repository. **A'1:** three minor findings and one note, answered by A'1b. (1) The write mode's refusal was pinned only for floors and distinct roles: mutants that skipped the hue lines or the between-entries section survived the 50 cases, though the code did refuse (planted: exit 1, the header's sha256 unchanged). (2) Hue limits were decided by float noise at the boundary: 108 RGB565 accents lie exactly on a limit and 4 were refused by float error (the light `#8c5500` printed "FAIL 30.0 deg (>= 30)"). (3) CLAUDE.md said 193 scripts where `tools/README.md` counts 194. (4) Note: per mode only one floor can fail alone. A'1b made controls 2, 3 and 7 assert the write refusal (exit 1, the failure named, the sha256 unchanged), added `round( d, 6 )` and an exact boundary case (written first, and red: `#8c5500` and `#945d08` were refused by float error, and 53 of 54 cases passed; 54 of 54 after), and corrected CLAUDE.md to 194; 8 of 9 mutants of the generator are killed and the survivor is equivalent. The review verified the exact arithmetic of every derivation and of the 48 golden cells, 24 floor plants, 30 role collisions and 17 mutants (10 killed, 7 live: 2 equivalent, 2 at the boundary, 3 in the write mode), and rebuilt both Ângulo images from scratch equal. **A'2 (with A'1b):** (1) important, no run exercised the glue, which no host harness links: `ua::tema( escuro, 0 )`, `e.modo != MODO_ESCURO` and `fnv( &id, 2, h )` passed 14 of 14; (2) minor, `kAcentos` is a third reader of the palette size; (3) minor, `test_temas.py` and `--acentos` are in no workflow; (4) the tables were `const ... PROGMEM` and not `constexpr`, so `kPaleta` sat in `.bss`. Answered by A'2b, after A'3. **A'4:** no code defect. `pico_w_release`, `pico2_w_release` and `pico_wiretft`, built from scratch, equal the base (and the fold run's other six); the `#if 0` control differs in 9 of 9; exactly three read-only strings leave both Ângulo ELFs; `data_ptr_proof.py` proves 6 of 6 pointer tables, and its control and a new mutant fail it; `reloc_only.py` on fresh builds finds, on the Wire, 2 real (the two handlers), 197 relocation-only and `lastErr`, and on the Pico 2 W 2 real, 21 relocation-only and the image-end word (-376 B). It kept the sprawl +1 as a recorded deviation (above) and noted one limit: an ELF list taken from the step's own build is circular, and `reloc_only.py` per name is what protects, which the review re-ran. **A'3:** (1) important, the preview's scoping is unpinned (M1 to M3); (2) important, the kit's retirement of its S8a rule was inert (`s8b_glue_check.py` executes the S8a script, whose whole-case equality of `MODE_SETTINGS_THEMES` failed the default run), which the main session fixed in the kit: the default run on A'3's tree went from exit 1 to "S8a and S8b pass (24 + 28 + 5 mutants refused)" and exit 0; (3) minor, the 13 mutants are text edits and behaviour outside the case escapes; (4) minor, colour names in lower case; (5) minor, the pressed states never run on the glass. The review confirmed that the bands match the plan to the pixel, that `UiEvent::id` is an `int`, and that the 18 ids and names agree with `escolhaDoIndice` (the longest 15 B and 22 B). Answered by A'3b, except (2), which the main session fixed in the kit. **A'2b:** (1) important, the glue's test was blind outside its four cuts: N1 (the accent never reaches the glass at the paint site, `ua::Pintor p( faixa, ua::tema( tema.escuro, 0 ), faixa )`), N2 (the preview reset deleted, A'3's M3), N3 (`fillScreen( ua::temaClaro( ).fundo )`) and N4 (`s_escuroNaTela = false`) passed 50 of 50; (2) important, CI never ran `test_temas.py`, which needs GFX from `.pio`, nor `test_cantos.py`, `test_alvos.py` or `host_telas.py`; (3) minor, the comment on the encoding functions called them the only readers of the count. The review also estimated the XIP cost of a frame (about 12 `tema.fundo` reads, one pass of draw code, a two-load `desab( )`; the live theme and its twin about 70 B) and asked A'5 for `s_ms` and `s_msMax` for A'3 against A'2b (dashboard, chart, Tema; 200 or more paints; Core 0 under TLS load), XIP counters if cheap, and a fallback of copying the 70 B to Core 1's stack per frame. Answered by A'2c, except what only the bench can answer (the bench, below). **The review of A'3b and A'2c:** COM RESSALVAS, the sixth, on the tests only. (1) important: 10 of its 20 mutants of the paint, hash and clock path passed `test_temas.py` (the margin blackened before the screen, either DMA wait dropped, the hour's bound at 30, the tail's at 4, `e.modoAtual` from the glass, and four counters Core 0 waits on); (2) important: outside the six cut functions, three mutants of the Tema touch passed (a mode tap resetting the accent, an accent tap resetting the mode, a preview of 0 ignored), and the kit's text rule, which CI does not run, refused the first two only (run by the main session); (3) minor: the cut ignored the preprocessor (`#if 0` around a function, an `#ifdef` inside `uaTema( )`). It verified CI's form of the gates step, the 18 rows of the catalogue, the 117 changed frames, 1,270 regenerated frames equal to A'2c's record, and the pressed-state gap. Answered by A'2d, above. **The review of A'2d and A'6:** COM RESSALVAS, the seventh. (1) important: the push model held two of the three rules of `DisplayManager_UiA_Empurrar.cpp`; the margin fill moved into a burst passed (O1a, O1b). (2) minor: the overlap of compose and push was not asserted (O2, O5). (3) minor: the routing to the Tema case was outside the cut (X6, X7). (4) to (6) minor: three statements of this section (the count of cases on A'6's own tree, a bench sentence that read the maximum column as the medians, the counters' passes lumped). It confirmed 60 and more of this section's numbers against the sources (the palette table against the JSON and the generator, the bench table against the A'5 record, the README counts), found the reading across the UIs marked untested, and prototyped the margin fill's check and the builder's cut. Answered by A'2e, above.

**The bench** (2026-10-22, 12:07 to 12:35; the record is `resultado.txt` of `stages/s10c_a5/`, outside the repository). The main session's, on the Pico 2 W `6A73...` (USB 3-3.1, Wi-Fi 192.168.3.33:8080, plain HTTP) with the PicoHand on the Pico `E660...` (USB 3-3.2): the bench of that day, with a test password common to all builds. The images were turned into factory UF2s (the partition table plus slot A, decoded back to the `.bin` before each flash), flashed by a 1200 bps touch and a copy to the RP2350 drive, and each was proved by an effect only it has: `/api/themes` lists **3 choices on the base and 18 on the tip.** The base is the S10b tip's `pico2_w_angulo` (`.bin` `1d6ef6ab...`) and the tip is A'2c's (`.bin` `16ed16c3...`, byte for byte A'3b's). The board's config listed five sensors and two did not answer on this protoboard (slot 1, a DS18B20, in error; slot 4, a BMP280); a tap on a card of the sensor in error opened `MODE_ALARM_ACTION`, and the maintainer chose "a", to remove the two. It was done by the web page's own action (free the slot) through `/api/commit_all`: `/api/config` then differed in exactly the five keys of those slots, and the board ran on three sensors. **Paint A/B** (`pintura_s10a.py`, 40 rounds each from a fresh boot on that three-sensor config, 0 FAIL on both; medians/max in ms):

| screen | base (S10b tip) | tip (A'2c) |
|---|---|---|
| dashboard | 42/43 | 42/42 |
| chart | 44/45 | 44/45 |
| 24 h chart | 45/46 | 44/46 |
| first settings page | 42/42 | 41/41 |
| alarms list | 42/42 | 42/42 |
| sensor menu | 38/38 | 37/37 |
| limits editor | 39/39 | 39/39 |
| a limits step | 20/29 | 19/30 |

Reading the palette from flash on Core 1 (A'2b) costs nothing measurable at this resolution: every median is within 1 ms (one maximum, a limits step's, is 1 ms against the tip: 29 against 30). `heap_f` after the sequence is 296,740 B on the base and 296,868 B on the tip (+128 B, and +168 B across the reads; the tip's static RAM is 142 B smaller, +290 B at A'2 and -432 B at A'2b). **Its recorded deviation:** the review of A'2b asked for A'3 against A'2b, 200 or more paints, Core 0 under TLS load and XIP counters. This is the integrated base against the tip, 40 rounds per screen, on plain HTTP (the bench has no TLS pair), with the Tema screen not among the eight (`pintura_s10a.py` has no Tema step) and no XIP counters. It bounds the cost at the 1 ms resolution for those screens without TLS load, and it does not measure the worst case the review named (section 4.4). A first base run on the old five-sensor config (dashboard 41, chart 43, 24 h chart 44) is kept in `base_pintura40.log` and not compared. **The Tema flow** (`tema_bancada.py --so tema,reinicio,web,thm`, a bench tool written without board access and proved first against a stub: 70 stub checks pass and about 25 controls each fail exactly their check) ran **60 ok, 0 FAIL and 2 info.** On the panel, Tema showed 3 segments and 6 swatches in the header's accents (light, and dark in the preview), with "Aplicar tema" inert while the preview equals the saved choice. Escuro with Índigo, applied, gave `themeIndex` 10 (`indiceDaEscolha( 1, 3 )`, read from the tree) and the panel returned to the dashboard, where the selected slot button's pixels at (152, 156, 240) equal the header's `0x9CFE` (indigo in dark mode), expanded as `/api/screenshot` does. After a reboot by the PicoHand (PONG, then RESET) the web was back in 25 s with the uptime reset, with index 10 and the same pixels. On the web, `theme=6` ("Claro, azul") gave a new frame with no touch, pixels (24, 80, 152) = `0x1A93`, and `theme=12` ("Claro, violeta", the accent alone, the mode staying light) gave another with no touch, pixels (120, 60, 152) = `0x79F3`: the G5 fix, on the device. A `.thm` (`contraste.thm`) uploaded with 200, was listed (513 B) and downloaded `cmp`-equal; `/api/themes` still listed 18 with no file name; the log held the upload's `SEC_FILE_UPLOAD` and no `CFG_THEME_APPLIED` (580, the rescan's code); the delete answered 200, the file was gone, and the log held `SEC_FILE_DELETE` and no rescan record. Restored: theme 0, the teal pixel (24, 96, 80) = `0x1B0A`, the test account deleted, config and alarms equal to the start. **The guard** (the snapshots g0 to g7 of the record): g0 (12:07) to g2 equal, g3 to g4 the intended sensor change only, g5 to g6 to g7 equal over the three flashes, and the tool's own start and end equal. The board ends on the tip's image (A'2c's), theme 0, with S10 (AMBIENTE) selected.

**Where `S10C_A_PLANO.md` was wrong or incomplete** (the plan is not in the repository):

1. It priced the palette in flash only (about 0.7 kB of tables and about 1.3 kB of catalogue, "sizes unverified") and said nothing of static RAM. A'2 built the tables as objects with a constructor and cost 290 B of RAM on both Ângulo images (9,313 -> 9,603 B), which the review of A'2 found and A'2b put right (9,171 B). The flash is larger than the estimate: `used` went from 808,896 to 811,300 B on the Wire Ângulo (+2,404 B) and from 1,029,932 to 1,032,400 B on the Pico 2 W (+2,468 B), with A'4's saving of 360 and 376 B inside the sums.
2. Its tests for A'2 (a host decode, `desab( )` over 17 roles x 6 entries x 2 modes, V-PIX, a contact sheet) did not run `DisplayManager_UiA.cpp`, where the frame hash lives. Three reviews found mutants of it that passed; A'2b linked pieces of its text and A'2c ran six whole functions of it. The Ângulo's host tests had no place in CI because the `gates` job had no Adafruit GFX (it fetches it since A'2c).
3. It named `tools/ci_gates_local.py` among A'1's files. The repository has no such file: the runner is the proof kit's, outside the repository, and it reads `build.yml`, so `test_acentos.py` joined CI by a step of the `gates` job.
4. "Automático" in the segment: the plan measured 86 px at 9 pt and left open whether the segment uses that font. The kit's segmented control is bold, which makes it 97 px against the 88 px of inner width; A'3 draws the labels in 9 pt regular, so A'-D4's fallback (two sub-screens) was not needed.
5. The view model: the plan's `VmTemas` was { modo, acento, salvo }, 3 B. A'3 added `premidoTipo` and `premidoIndice`, because a pressed state has to reach the draw function to be shown at all, and the glue leaves them at `TT_NADA` (section 4.4). The preview's member also needed a place: after `_uaVisto` it grew `DisplayManager` by 8 B (AppManager's constructor word 8584 -> 8592, UNPROVEN by `reloc_only.py`, and a RAM cost no gate counts), so it sits in the padding after the two flags.
6. The colour names: the plan wrote "Escuro, azul" for the catalogue and "Cor de destaque: Azul" for the screen. A'3 shipped them capitalised ("Claro, Petróleo"), and ANGULO.md section 7 wants lower case after a comma or a colon; A'3b set it.
7. The `__LINE__` fallback of A'4 (fold the guard into the existing `if` as `SIMUT_UI_CUSTOM_THEMES &&` if line numbers reach an image) was not needed, since `__LINE__` does not reach an image from this file, and the fold would have hidden the macro from `check_feature_sprawl.py` (the sprawl +1, above).

**A'6, the documentation.** No function and no byte of an image changes. The documents are this section, the sentence added to C4b's paragraph in section 2.15, section 4.4's block for S10c A' and the two S10b-1 lines it closes (with two other statements of that block marked as superseded), the `_doc` string of `tools/tft_ui/tokens_tft.json` (the header does not change: `gen_tokens.py --check` still matches), the eleventh environment's sentence in `CLAUDE.md` (it said nothing runs `pico2_w_angulo` on a board; the Pico 2 W on the bench has since 2026-10-22), and `tools/README.md`: the rows of `gen_tokens.py`, `test_acentos.py`, `test_temas.py` and `host_telas.py` brought to the tree of A'2c, and `test_temas.py`, `test_cantos.py` and `test_alvos.py` moved from "Referenced elsewhere" to "Called by CI", since A'2c made the `gates` job run them (the group header said 66 for 67 rows since A'2). The manual is not changed: `docs/MANUAL.md` ("Themes") and `docs/MANUAL.pt-BR.md` ("Temas") describe the shipping images, which draw the original, and the Ângulo is not approved for products. **Proved,** on the working tree before the commit. Nothing was built: no file under `src/` changes (`git diff` of `src/` against A'2c is empty), so no image can. `git diff --stat` against A'2c names the four files and no other. `python3 tools/tft_ui/gen_tokens.py --check` exits 0 ("the header matches the JSON") with the longer `_doc`, which the generator never reads. The figures this section and `tools/README.md` quote were read again from the tools on 2026-10-22: `gen_tokens.py --report` exits 0 with every pair at or over its target; `test_acentos.py` 54 cases and `test_temas.py` 160 cases (91 on A'2c's tree, where A'6 was written), 0 failed in both; `host_telas.py` exits 0 on the six groups, and its `--acentos` draws 36 frames with 67,056 accent pixels. `python3 tools/check_angulo.py` exits 0 (92 documents without emoji). The proof kit's `ci_gates_local.py`, which reads `build.yml` and runs CI's gate steps on the tree, passes 58 of 58 steps and skips 1 (the sanitizer pass, which only CI runs), the count A'2c recorded. **The one gate that reads `docs/README.md` and `tools/README.md` is `check_angulo.py`:** it takes the Living documents from the first and scans both, `CLAUDE.md` and this document for emoji; `check_readme_numbers.py` reads the three product READMEs and not these two.

### 2.17 The Ângulo speaks the device's language (S10c, B)

**S10c B (2026-10-22 and 2026-10-23, branch `feat/ui-drivers-s10c`, after Stage A' and Stage C1's first commits) did this.** Until B3, every text the Ângulo drew was a pt-BR literal in `src/ui/angulo/`, so the panel spoke Portuguese whatever language the device ran. Now every text it draws is written once, in English, in the code, as `UA_TXT( "<English>" )`. The active language pack (`/lang/language_<code>.lng`) gives the text that stands for that English. A device in English, which has no pack, and a pack that lacks the text both draw the English. Eighteen commits changed the repository, in this order on the branch:

- B1, the gate, in report mode;
- B2a, the lookup, on the host; B2a-b, a looked-up text is never a format; B2b, the load at boot; B2b-b, three comments;
- B3, the Painel and Base screens; B3-b, one back action for the kit, a host recorder that cuts on a character, and every group in three languages; B3-c, the pager's count, and what a screen places by a translated text;
- B4a, the Menus screens; B4b-0, `LANG_FILE_MAX`; B4a-b, the accepted cuts by language, and floors for the cross-language checks; B4b-0-b, the gate's reader of `TV_CAP`; B4b, the Ajustes screens;
- B5a, the Gráficos screens, the decimal separator, and the names of the months and weekdays; B5a-b, the ellipsis exemption and the separator's guard; B5b, the Identidade screens; B5b-b, the review of B5b answered;
- B6, which makes the gate block in CI and writes this section.

B7, the bench in the three languages, ran on 2026-10-24 from 20:35 to 21:23, on the Pico 2 W with `pico2_w_angulo` of 871a455e: pt-BR and es-ES complete with 0 FAIL, and English 32 ok and the one expected FAIL (an English boot listed only English, which #294 fixed on main and sync 6, merge 3ce6bd30, brought here). Its record, `stages/s10c_b7/resultado.txt`, and the plan (`S10C_PLANO.md`, section 6 and note 61) are not in the repository; this line said B7 had not run until 2026-10-30. The commits are named by step and not by hash, for the reason section 2.15 gives.

- **The maintainer's words of 2026-10-22, and what "the same way" kept.** "Quero que Ângulo tenha traduções da mesma forma que a interface original." The original translates its panel from the pack's `@DICT`, a positional list that the products load whole into the heap. Its bound is `LANG_DICT_MAX`, 6,144 B; on 2026-10-23 the list is 3,292 B in pt-BR and 3,393 B in es-ES, and stage B did not change it. The original reads a pack only at boot, and a text the pack lacks shows the English. The Ângulo keeps all three: the same packs, the same moment, and the same fallback. It adds a section of its own. A language chosen on the Idioma screen is saved at once and takes effect at the next restart; since B4a the screen says so. `TRL( )` is not this mechanism: nothing on the device reads `@TRL`, and `DisplayManager::trlLookup( )` has answered "absent" since 2026-08-29.
- **B2a, the section and the lookup.** `@ANGULO` holds one line per text: the FNV-1a 32 hash of the English's UTF-8 bytes in 8 hex digits, a space, and the text. The section comes before `@WEBDICT`, which must stay the file's suffix. The texts would not fit under `@DICT`'s bound, and in `@DICT` they would cost every product that heap. The products' scanner (`LangPackScanner`, `src/LangPackIndex.h`) takes an unknown directive as the end of the body before it and reads on. It knows `@ANGULO` only in the Ângulo images, under the capability `SIMUT_UI_PACK_TEXTS` (B2b), so the products' heap does not grow. The hash is computed when a text is asked for, not stored at each call site. Measured on 2026-10-22 with the project's flags, 100 call sites are 4,146 B with the hash at run time and 4,750 B with a constant per site on the Cortex-M0+ (4,142 and 4,738 B on the Cortex-M33).
- **The table.** `ua::txt( )` (`src/ui/angulo/Textos.cpp`) answers from one table: an 8 B header, the hashes in ascending order (4 B each), the offsets into the text area (2 B each), and the texts, each with its NUL. A binary search finds the hash. No table, or no entry, gives the English.
- **B2b, the load at boot.** `AppManager::setup( )` (`AppManager_Boot.cpp`) calls `DisplayManager::uiLangLoaded( )` (`DisplayManager_UiA_Textos.cpp`) once, right after the pack loads. The call:
  1. reads the section's byte range, which the device's own scanner found, into one buffer of exactly that length;
  2. builds the table with one allocation;
  3. frees the buffer;
  4. publishes the table by one release store.

  Core 1 sees either no table or the whole one. A section over `UA_TEXTOS_MAX` (16,384 B) is refused before anything is allocated. A refusal is `I18N_PACK_FAILED` at `LOG_WARN`, with the context 100 plus the result (101 to 107), and every text then shows its English. The original's hook returns 0. Core 1 starts painting before the pack loads, so the first boot frames are in English, as the original's are. B3 made `uiPass( )` repaint every strip when the language changes, as the original's boot render does.
- **The sizes**, measured on 2026-10-23 on the tree of B5b-b. The table's size is the loader's own arithmetic over the section: 8 B, plus 6 B per entry, plus the texts with their NULs. The host's loader reports the same figure, 8,230 B for pt-BR. The heap figures are what the board should show; B7 measures them there.

| | pt-BR | es-ES | English |
|---|---:|---:|---:|
| texts in `@ANGULO` | 373 | 373 | no pack |
| their UTF-8 | 5,611 B | 5,860 B | — |
| the section (of `UA_TEXTOS_MAX`, 16,384 B) | 9,341 B (57.0 %) | 9,590 B (58.5 %) | — |
| the table, kept in the heap | 8,230 B | 8,479 B | 0 |
| the heap at boot, buffer and table at once | 17,571 B | 18,069 B | 0 |
| the pack file (of `LANG_FILE_MAX`, 65,536 B) | 51,387 B | 53,371 B | — |
| `@DICT` (of `LANG_DICT_MAX`, 6,144 B), unchanged | 3,292 B | 3,393 B | — |

- **The flash.** Over the stage, flash used grew by 3,416 B on the Wire's Ângulo image (`pico_wiretft_angulo`, 811,300 to 814,716 B) and by 3,208 B on the Pico 2 W's (`pico2_w_angulo`, 1,032,400 to 1,035,608 B). Of that, the loader and its hook are 944 and 904 B (B2b). The steps from B3 to B5b-b, which converted the 454 call sites, add 2,472 and 2,304 B, with B4b-0's -40 and +8 B among them. Static RAM stayed at 115,912 and 131,572 B from B2b to B5b-b. Each step's figures are its own measurement, recorded in `tools/flash_budget.json`; the steps that moved no byte are left out. Wire / Pico 2 W, in bytes:

| step | B2b | B3 | B3-b | B3-c | B4a | B4b-0 | B4b | B5a | B5a-b | B5b | B5b-b |
|---|---|---|---|---|---|---|---|---|---|---|---|
| flash used | +944 / +904 | +728 / +672 | -48 / -56 | +16 / +16 | +648 / +608 | -40 / +8 | +480 / +456 | +328 / +384 | +8 / +8 | +336 / +192 | +16 / +16 |

- **The rules every conversion followed (B3 to B5b-b), and what holds each one.**
  1. **English, Portuguese and Spanish.** The English follows ANGULO.md section 7. The pt-BR is today's approved text, byte for byte. V-PIX held the pt-BR frames equal at every step: 948 of 948 at the end. Two deliberate changes were listed before their steps: B4a's Idioma note, and B4b's PIN-policy keypad, whose touch zone starts 4 px lower because the English "Keypad"'s descenders crossed it. The es-ES is new, in the Spanish of Spain. Where a text did not fit, es-ES was shortened, and those wordings wait for a native review (section 4.4).
  2. **A looked-up text is never a format** (B2a-b). A pack is a file that anyone with `PERM_FILE_UPLOAD` can put on the device, and the loader keeps only hashes, so it cannot compare a translation's conversions with the English's. The words go in as arguments of a literal format: `snprintf( b, n, "%d %s", v, UA_TXT( "min" ) )`. The gate refuses `UA_TXT( )` as the format of a printf-family call. From the end of `Textos.h`, `-Wformat-nonliteral` and `-Wformat-security` are errors (B2b).
  3. **No `UA_TXT( )` in an initialiser.** A table built at start-up would hold the English for good, because the pack is not loaded yet. So the tables became switches or functions at draw time: `kMensagens` (B3), `kMotivos` and the months and weekdays (B5a), and the identity bar's `kVoltar` and `kCancelar` (B5b).
  4. **One source for a label that a layout measures and a draw draws.** If the two read different texts, a translation moves a box and leaves its target, or its ink, behind. The single sources are:
     - the kit's `acaoVoltar( )` (B3-b);
     - `contagemDoPaginador( )` (B3-c);
     - `CaixasRodape`, which keeps the labels its boxes were measured from (B4b);
     - `separadorDecimal( )` (B5a);
     - the identity bar's actions, the footer's labels, and one function per badge (B5b).
  5. **One English, one translation.** Where a language needs two forms of one English, the English must differ:
     - English weekday initials repeat ("S" is both Sunday and Saturday), so the keys are "Su" to "Sa" (B5a);
     - the account's permission row and the sensor menu's maintenance row are "Maintenance mode", so that es-ES can say "Mantenimiento" there and keep "Mantenim." where the room is short (B5b-b, B6-b).
  6. **The decimal separator follows the language** (B5a, B5a-b). It is the pack's text for "Decimal separator". Only "," or "." is accepted, and anything else gives ".". pt-BR and es-ES write ",", and English writes ".". The original writes "." in every language (`fmtFloat1`). The placeholder of a missing value follows the separator too: "--,-" in pt-BR, "--.-" in English (B5a for the charts, B5b for the Painel). Dates stay DD/MM and times 24-hour in every language, as in the original.
  7. **Every copy is bounded, and numbers stay in literal formats.**
- **The gate, `tools/check_ui_texts.py`** (B1; blocking since B6). Any string literal under `src/ui/angulo/` that holds a letter and is not the whole argument of `UA_TXT( )` is a finding, unless `tools/ui_texts_allow.json` names it with a reason. The gate reads what the compiler reads. Comments are removed by `check_net_boundary.py`'s lexer, escapes are decoded, adjacent literals are joined, and raw strings are read as written. Other findings are an allowance that matches nothing, a text allowed twice, an English that is not UTF-8, and a looked-up text used as a format. Its `--selftest` runs 92 cases in temporary trees, then 10 controls, each a part of the gate turned off; every control must fail a case. On a copy of the real tree, a planted literal adds exactly one finding. The work left, by step, as the gate of the tip counts it on each step's tree:

| step | texts outside `UA_TXT( )` | files | in `UA_TXT( )` | allowed (groups) |
|---|---:|---:|---:|---:|
| B1 | 523 (408 distinct) | 33 of 80 | 0 | 213 (12) |
| B2b-b | 523 | 33 of 83 | 0 | 216 (14) |
| B3, Painel and Base | 451 | 26 | 69 | 228 (14) |
| B3-b | 448 | 25 | 69 | 229 (14) |
| B3-c | 447 | 25 | 69 | 229 (14) |
| B4a, Menus | 316 | 18 | 181 | 262 (15) |
| B4b, Ajustes | 191 | 12 | 290 | 267 (15) |
| B5a, Gráficos | 103 | 5 | 377 | 274 (15) |
| B5b, Identidade | 0 | 0 of 83 | 454 | 307 (16) |

  The allowed literals grew by 94 over the stage, in these groups:
  - 48 are formats left with no word, because their words went into `UA_TXT( )`;
  - 22 are the keys' faces: the letters a PIN keypad's group or a text keyboard's layer types;
  - 18 are the theme catalogue's names, which the web lists;
  - 6 are host-harness diagnostics, a `static_assert`'s message, the `#pragma` options and a file mode.

  Two groups were decided one by one, because their texts are drawn or served. **A key's face** (B5b) is data that a pack must not relabel. **A name the web lists** (B4a) is the 18 names that `GET /api/themes` serves as they are, in pt-BR, in every language. The panel's Tema screen draws words of its own for them. Whether the web should show them in the device's language is the maintainer's to decide (section 4.4). Decided on 2026-10-24 and done on 2026-10-28: the device sends English names and a stable code, and the page translates them (section 4.4, the web's theme names); the group now holds the 18 English names.
- **The pack gate, `tools/check_lang_packs.py`** (every firmware build; B2a onward). It requires one entry per `UA_TXT( )` literal in both packs. It refuses an orphan, a hash shared by two literals, a section after `@WEBDICT`, a section over `UA_TEXTOS_MAX`, and every malformed line. Since B4b-0 it also holds `LANG_FILE_MAX` at or under the web text viewer's cap (`TV_CAP` in `WebUI.h`): a pack the device accepts stays one the web can show. `LANG_FILE_MAX` went from 49,152 B to 65,536 B in that step, because es-ES had reached 97 % of the old ceiling with three groups of screens still to translate. `tools/test_lang_gate.py` holds the gate's cases: 52 on 2026-10-23.
- **The host checks, `tools/tft_ui/test_idiomas.py`** (B3 onward; every group since B3-b). Every group of screens is drawn in English, pt-BR and es-ES from the shipped packs. Each language must show no missing glyph, no text past its card or its button, and no fixed text cut, apart from the cuts `cortes_aceitos.txt` accepts for that language. The cross-language checks hold what a screen places by a translated text's width, each with a floor so that a step cannot quietly draw fewer:
  - the back button's target, in 168 samples;
  - the pager's count beside its legend;
  - the footers' boxes against their labels' ink, 86 buttons;
  - the identity bar's "Cancel", 43 samples;
  - the room a badge leaves a cut name, 9 rows;
  - the decimal separator, over 262 numbers per language;
  - the calendar's month and weekday names;
  - the update screens' paragraphs, which must end 12 px or more above the foot;
  - the placeholder of a missing value, 12 per language.

  Planted packs must fail exactly the samples whose frames they change: an "ő", a label of 74 characters, a text of 102 characters wider than its card, and a text whose "í" the host recorder would cut in two. On 2026-10-23 the suite ran 121 checks, 0 failed, in 19.6 s (4 min of CPU).
- **The proofs that run, and where.** CI's `gates` job runs:
  - `test_textos.py`, the lookup and the loader under ASan and UBSan with their mutants (105 checks);
  - `test_lang_gate.py`;
  - since B6, `check_ui_texts.py` with its selftest and its first control run as a process;
  - after the Adafruit GFX fetch, `test_idiomas.py`.

  `check_lang_packs.py` runs in every firmware build. Each step also ran, outside the repository:
  - V-PIX: the pt-BR frames against the previous step's;
  - V-SHA9: the nine product images rebuilt from scratch and byte-identical, apart from B4b-0's re-stamp of the web pages and B5b-b, which built none;
  - V-P2W: every changed function of the two Ângulo images named, by `tools/elf_funcoes.py` and the kit's `reloc_only.py`.

  B4b-0's proof was redone by decomposition, after its review showed that the reader of the first difference had proved only the first difference.
- **The reviews.** A read-only session reviewed each step before the next one built on it. It rebuilt the images, re-ran the proofs and planted mutants. Nine reviews covered stage B, and all nine returned COM RESSALVAS with nothing blocking. Eight were answered by a step named after them: B2a-b, B2b-b, B3-b, B3-c, B4a-b, B4b-0-b, B5a-b and B5b-b. B4b's needed no commit of its own: its first point closes with B6, which makes blocking the gate that sees it; the others went to the proof kit, the native review, the bench and B5a's brief. The records are `rev_s10c_*`, outside the repository.

**Where `S10C_PLANO.md` was wrong or incomplete** (section 6; the plan is not in the repository):

1. Its inventory was a grep upper bound: 774 lines in 65 files. B1's gate counted 523 texts in 33 files. The grep's 819 lines on that tree held 358 that were only a comment or an `#include`, and it missed 36 findings on lines with runs shorter than three letters.
2. It planned two groups of screens per step. B4 and B5 were each split in two (B4a and B4b, B5a and B5b), one group per step.
3. It did not foresee the file ceiling. es-ES reached 47,697 B, 97 % of `LANG_FILE_MAX` (49,152 B), after B4a. B4b-0 raised the ceiling to 64 KiB, the web viewer's cap, and the gate holds the two in that order.
4. It gave the decimal separator as `ua::n1( )` alone. `kb( )` (`telas/MenusStatus.cpp`) wrote the comma too (B5a), and so did the placeholders of a missing value: `"--%s-"` in the charts (B5a) and the Painel's `dec( )` (B5b).
5. It named V-PIX "840/840" and "`ci_gates_local.py` (54/54)". The groups' samples grew to 948 pt-BR frames. The runner reads `build.yml`, which had 61 steps at B5b and has 62 after B6. B6 also found that the runner ran each multi-line step without errexit, while CI runs `bash -e` (the `gates` job's log prints `shell: /usr/bin/bash -e {0}`). A step whose first line failed and whose last passed was reported ok. Five steps that the runner runs have more than one line. The runner is fixed: B5b-b's run was the first with `-e`, and B6's the first full one.
6. It did not say when the hook runs against Core 1. The call is right after the pack loads, and Core 1 starts painting before it, so the first boot frames are in English, as the original's are (B3).

**B6, the gate blocks.** The `gates` job of `.github/workflows/build.yml` runs `check_ui_texts.py --selftest`, then the first control as a process: `--selftest --no-strip` must exit non-zero with failed cases. Then it runs the gate on the tree. Besides this section, B6 changes these places:
- the gate's docstring, which said it was not in CI;
- its row in `tools/README.md`, moved from "Referenced elsewhere" to "Called by CI" (and the page's count of scripts, 197 since B3 for 198 rows);
- the comment of the `test_idiomas.py` step, which named only the checks of B3-c;
- three comments of `src/ui/angulo/Textos.h`, one of which said the load runs "before the first paint", and one of `src/DisplayManager_LangParser.cpp`, which counted two groups of screens left where there were three;
- section 4.4's block for S10c A', where F9's items (c) and (d) are closed and (b) is handed to the maintainer;
- a new block in section 4.4 for S10c B.

**Proved**, on the working tree before the commit (`stages/s10c_b6/`):
- `check_ui_texts.py` on the tree exits 0, with 0 findings.
- On a copy of the real tree, a planted panel literal gives exit 1 and exactly one finding, at its file and line. An allowance that matches nothing gives exit 1 and one finding. The same literal inside `UA_TXT( )` gives exit 0.
- `--selftest` passes 102 cases and controls, 0 failed, in 4.1 s. `--selftest --no-strip` exits 1: 92 cases, 52 failed.
- The step itself, read from `build.yml` and run with `bash -e` as CI runs it, exits 0 on the tree. It exits 1 on three copies: one with a planted literal (the gate's finding), one whose `--no-strip` control was made to return 0 (the step's own message), and one whose control raises (no RESULT line).
- The proof kit's runner, which reads `build.yml` and runs CI's gate steps on the tree, now with `bash -e`, passes 62 of 62 steps, the new one among them, and skips 1: the sanitizer pass, which only CI runs.
- `check_angulo.py` exits 0 (92 documents without emoji), and so does `check_readme_numbers.py`. The README's four groups hold 60, 17, 63 and 58 rows, 198 in all.
- The eleven images were built from scratch in an export of the tree, with the comments of `src/` in. The nine are byte-identical to B5b's, and the two Ângulo images to B5b-b's.

**B6-b, the review of B6 answered** (2026-10-23). The review returned COM RESSALVAS. One point would have failed in CI: `test_textos.py` read the products' scanner of before B2b with `git show e2adcc27:src/LangPackIndex.h`, and CI checks out at depth 1, where that object is absent. In a depth-1 clone the suite gave 105 checks and 1 failed; in the full worktree, where the proof kit's runner ran, 0 failed. A hash can also vanish when the branch is amended before it is integrated. The scanner of e2adcc27 is now a committed copy, `tools/tft_ui/host/LangPackIndex_antes_B2b.h`, held by its sha256 (a planted byte fails the case). The proof kit's runner gained a mode that runs in a depth-1 clone with the working tree laid over it. The other points:
- the sensor menu's maintenance row moved to "Maintenance mode", so es-ES says "Mantenimiento" there. The status page keeps "Maintenance": with "Maintenance mode" its English cut a sensor's name;
- the README's count of the pack gate's cases said 48 for 52;
- the inventory row of B3-b;
- CLAUDE.md's bullet now says the gate runs in CI and not in a local build;
- the step now prints a message when the RESULT line is missing.

**Proved** (`stages/s10c_b6b/`):
- In a depth-1 clone of B6's tree, `test_textos.py` gave 105 checks with 1 failed, the history read. With this commit's two files laid over the same clone, it gives 105 with 0 failed. With one byte appended to the copy, the case fails on the sha256.
- The proof kit's runner, in a depth-1 clone of B6's tree with `--quick`, passed 49 of 50 steps; the one FAIL was the text lookup. With `--raso`, on this tree, it passes 62 of 62 steps and skips 1, the sanitizer pass, which only CI runs.
- The sensor menu's row is drawn as "Manutenção" in pt-BR (x 48 to 141, unchanged), "Maintenance mode" in English (48 to 191) and "Mantenimiento" in es-ES (48 to 158). The badge and the status page are unchanged. `test_idiomas.py` gives 121 checks, 0 failed; V-PIX holds 948 pt-BR frames equal, 0 different; en and es exit 0 in every group.
- The step, read from `build.yml` and run with `bash -e`, exits 0 on the tree. On a copy whose `--no-strip` prints "0 failed" and exits 1, it exits 1 with its message.
- The nine images from scratch are byte-identical to B5b's. The two Ângulo images keep their sizes (814,716 and 1,035,608 B used). V-P2W against B6's images finds one changed function in each: `desenharSensorAlarme( )`, whose literal's word now points at "Maintenance mode".
**What B7 is for.** The bench, on the Pico 2 W, is where these are proved:
- the two cores' order (the release store against Core 1's acquire load), which the host cannot show;
- the heap: against English, the table should cost 8,230 B in pt-BR and 8,479 B in es-ES, within the allocator's slack;
- the boot frames, repainted in the pack's language;
- every screen on the glass in the three languages, and the paint time in each.

### 2.18 The original's code leaves the units the Ângulo links (S10c, D)

**S10c stage D (2026-10-23 to 2026-10-28, branch `feat/ui-drivers`, tip `3d433d6c`) did this.** It took the original's code out of the units the Ângulo still linked, so that a dependence of the Ângulo on the original is a link error and not dead code that `--gc-sections` hides. Six moves: Dashboard (S10c D1), the alarm screen (S10c D5), Settings (S10c D2), Calibration (S10c D3), the licence (S10c D4) and Users (S10c D6); then the `PinKeypad.h` split (S10c C8) and a frame in the Ângulo for the setup-network confirmation (S10c D7 and D7b). Every figure below comes from the step's commit message (`git log -1 --format=%B <hash>`), from a record in `stages/` (outside the repository, as in sections 2.14 to 2.17) or from a command run for this section, and the text says which when it is not the first.

**The result at `3d433d6c` (measured on 2026-10-28).** `[ui.original].units` lists fourteen units (`tools/features.toml`; a case of `check_ui_link.py --selftest` reads the count): the eight of S10b-1 (section 2.15) and `DisplayManager_Dashboard.cpp`, `DisplayManager_AlarmUi.cpp`, `DisplayManager_Settings.cpp`, `DisplayManager_Calibration.cpp`, `DisplayManager_LicenseUi.cpp` and `DisplayManager_UsersUi.cpp`. On the two Ângulo ELFs of S10c D7b's from-scratch build (`stages/s10c_d7b/imgs_final/`), `check_ui_link.py` reads 0 findings with 12 of 12 allowances used (`stages/s10c_d7b/out_check_ui_link.txt`), and `bytes_original.py`, run for this section on the same two ELFs, reads 0 B of flash and 0 B of RAM in original rows (293 B on the Wire image and 269 B on Wi-Fi before S10c D1, section 3.6) and 3,844 B and 3,374 B in mixed rows (4,184 B and 3,640 B in section 3.6). The sprawl watermark is 365 (363 at S10c D1, 364 from S10c D2). The sha256 of the eleven images of this tree are in `stages/s10c_d7b/sha256_d7b.txt`.

**The steps, in the order of the branch.**

- **S10c D1** (`8bf2db72`, with `e5262ef6`): `buildDashLayout( )` moves word for word from `DisplayManager_Dashboard.cpp` to `DisplayManager_Panel.cpp`, and Dashboard.cpp joins `[ui.original].units`. The nine products equal the S10c D0 probe's sha256 (the three LCD3 images keep the base's); one branch of `drawBottomButtons( )` changed in each TFT product, equivalent because `buildDashLayout( )` returns 0 to 5 (read from the source and run on the host: 24,772,609 calls, ASan and UBSan clean). On the Ângulo, V-ELF exits 0, V-PIX holds 948 of 948 frames, `check_ui_link.py` goes from 22 to 21 allowances, and the original's bytes go from 293 B to 17 B (Wire) and from 269 B to 17 B (Wi-Fi). `e5262ef6` corrects `DisplayManager_Panel.cpp`'s brief and section 4.4; the eleven images built from scratch in an export of its tree are byte-identical to `8bf2db72`'s.
- **S10c D5** (`87875859`): the alarm screen's three draw functions, `drawAlarmAction( )`, `slotAlarmBg( )` and `redrawAlarmFlash( )`, move to `DisplayManager_AlarmUi.cpp`, a unit of the original in `[ui.original].units` and in both LCD exclusion lists. The three LCD3 images keep the base's sha256. In the products `redrawAlarmFlash( )` reloads `_lastRenderedState.topSlotIdx` after a call (+8 B on pico_w_release and pico_w_test_https, +24 B on pico2_w_release), equivalent because only Core 1 writes it, in the same loop iteration. The Ângulo never linked the three: its images keep their sizes (826,752 and 1,035,632 B), the bytes that differ are veneers the linker reordered (`veneer_proof.py`: OTHER 0, in `stages/s10c_d5/veneer_angulo_pico_wiretft_angulo.txt` and `veneer_angulo_pico2_w_angulo.txt`), and `check_ui_link.py` reads 21 of 21 allowances before and after.
- **S10c D2** (`9fae2fa7`): `showApConfirm( )` moves word for word to `DisplayManager_SettingsCore.cpp`, in its own `#if SIMUT_NET_WIFI`, and `DisplayManager_Settings.cpp` joins `[ui.original].units`. Every `.bin` keeps its size (0 B on all eleven); the LCD3 images and pico_wiretft are sha256-equal and the other five TFT images differ in bytes by link order only (`elf_funcoes.py` exit 0, `reloc_only.py` 0 REAL and 0 UNPROVEN, `tft6_sections.py` exit 0). Both Ângulo images link with the unit out (V-ELF 0, no symbol lost or gained) and `check_ui_link.py` reads 21 of 21, none naming `showApConfirm( )`. The sprawl rose by 1 (363 -> 364), the maintainer's +1.
- **S10c D3** (`aea1f5f8`, `c67a1cb4`, `79ac927a`): S10c D3a makes `showTouchCalibration( )` raise `UiChange::TOUCH_CAL_ENTER`, and the original resets its seven `g_*` draw caches in `resetTouchCalCaches( )`; S10c D3b does the same for `showSettingsDisplayOffset( )` and the `_lastOffsetDraw*` sentinels through `UiChange::DISPLAY_OFFSET_ENTER`, inline; S10c D3c moves both functions to `DisplayManager_CalibrationCore.cpp`, and Calibration.cpp joins `[ui.original].units`. Proof: a host model extracts the functions by name from the text of S10c D2 and from the new text and runs 1,000,000 seeded dirty states through both (S10c D3b: 2,000,000 runs and 4,000,000 snapshots at `mutex_enter` and `mutex_exit` compared; negative controls killed: 9 at S10c D3a (`S10C_PLANO.md`: "9 of 9 mutants killed"), 14 at S10c D3c). `check_ui_link.py` goes from 21 to 13 allowances (S10c D3a) and to 12 (S10c D3b). At S10c D3a the `.bin` grows by 8 B (pico_w_release, pico_w_test_https, pico2_w_release) or 16 B (pico_w_test, pico_w_asserts, pico_wiretft), the Ângulo shrinks by 72 B (Wire) and 80 B (Wi-Fi), and the original's bytes in the Ângulo go from 17 B of flash and 2 B of RAM to 0. At S10c D3c the LCD3 images are sha256-equal, the six other products and both Ângulo images keep their sizes and differ in bytes, and `layout_diff.py` accounts for every changed byte (the moved functions, six slid symbols, five re-targeted `bl`).
- **S10c D4** (`a26d1199`, `6c216cc6`, `1f2ea33a`): S10c D4b-1 gives the licence opening's buffer `_licOpening` external linkage through `display/LicenseOpening.h`; S10c D4b-2 moves `drawSettingsLicense( )` word for word to `DisplayManager_LicenseUi.cpp` (`[ui.original].units` and both LCD exclusion lists); S10c D4-b corrects two lines of `DisplayManager.h` ("file-static"). At S10c D4b-1 eight of the eleven sha256 change at equal sizes (the LCD3 images are byte-identical): `word_diff.py` explains every differing byte as an address of the same variable in its own ELF (pico_w_release 100 bytes in 97 words, pico2_w_angulo 188 in 139, none unexplained in all eight), and the review's objdump found 0 differing instruction lines. At S10c D4b-2 `elf_funcoes.py` reads pico2_w_release as 4,083 functions equal and none changed, and the five RP2040 TFT images with one function each, `LangPackScanner::identify() const`, which `reloc_only.py` reads UNPROVEN and `decoded_diff.py` decodes identical; `check_ui_link.py` reads 12 of 12. S10c D4-b: the eleven from scratch are sha256-equal to S10c D4b-2's, 11 of 11.
- **S10c D6a** (`5f04277d`): twenty definitions of `DisplayManager_Users.cpp` move word for word to `DisplayManager_UsersUi.cpp`: the PIN keypad's draws, the alarm-sensor menu, the maintenance entry, the user list, editor and delete confirmation, the panel message, the PIN policy screen, `handleTouchPanelV24( )` (the original's touch hand-off for these screens) and three file-static helpers. Users.cpp keeps the PIN state and its fail ladder, the session and the `show*( )` entry points, which both UIs run. The new unit joins `[ui.original].units` and both LCD exclusion lists (it is TFT-only, so it has no `#if`); the function table moves 28 rows (18 original, 1 core, 9 mixed) and `check_ui_link.py` counts fourteen units. **The products change by code, and that is accepted** (S10c D6-0, route (a)): with the callers of `PinKb::orderedFace( )`, `clampPinPolicy( )` and `maxLenFor( )` in two units, GCC inlines these helpers into some of their callers, and eight functions changed in each TFT image and three (`pinPolicy( )`, `scramblePinKeys( )`, `pinAppendChar( )`) in each Ângulo image. Against the base the `.bin` grows by 168 B on pico_w_release; 160 B on pico_w_test, pico_w_test_https, pico_w_asserts and pico_wiretft; 112 B on pico2_w_release; 64 B on pico_wiretft_angulo; 56 B on pico2_w_angulo; the three LCD3 images keep the base's sha256; the headroom of section 3.1 of `PLANO_STABLE.md` goes from 5,195 to 5,027 B on pico_w_release, and the last LOAD of pico_w_test_https ends 4,124 B below the region's end (4,284 B before). That the behaviour is unchanged is shown by the decoded argument and the bench below, not by this commit.
- **S10c D6b** (`fef95de0`): the comments that still named `DisplayManager_Users.cpp` name `DisplayManager_UsersUi.cpp` where S10c D6a moved the code, and three includes of Users.cpp that it no longer uses (`DisplayManager_PinActions.h`, `DisplayManager_Actions.h`, `ui/UiDriverHooks.h`) become comments. Comments and includes only: with the comments stripped, the C++ of S10c D6a and of this commit differ by exactly three `#include` lines (`code_equal.py`), 14 files changed with 30 insertions and 30 deletions, every file keeps its line count, and the eleven images from scratch are sha256-equal to S10c D6a's.
- **S10c C8** (`c5a26bcc`): thirteen names that only the original's paint, its hit test and the native suite read leave `src/PinKeypad.h` for the new `src/ui/original/PinKeypadLayout.h`, token for token, inside the same namespace `PinKb` (`PinKeypad.h` goes from 407 to 373 lines). `pinkb_readers.py` on `fef95de0` read the 76 names of `PinKb`: 62 stay, 12 are original-only and move (and `slotX`, which only a test or a tool reads, goes with them because it calls `keyX`), 2 are read only by a test or a tool, 0 are unread. The eleven images from scratch are sha256-equal to S10c D6b's, built twice; the negative control (one token of `dotRadius` edited) gives a pico_w_release of 1,035,108 B and another sha256 (1,035,116 B before); `test_validators` 344 of 344, `check_ui_boundary.py` 0 findings, `test_ui_boundary.py` 311 of 311.
- **S10c D7** (`8dd53c51`): `MODE_CONFIRM_AP` ("start the setup access point?") gets its own frame in the Ângulo, its third `VmDialogo` user after the mute and account-delete confirmations: Confirm sends the existing `EVT_START_AP` (`actStartAp( )`, behind `acceptTouch( 0xC3 )`) and Cancel goes back to Settings (`showSettingsMain( )`, behind `0xC2`), the original's zones; Core 0 still checks `PERM_NET_CONFIG` before `startApMode( )`. The frame and the touch are two members of `DisplayManager` defined in one `#if SIMUT_NET_WIFI` block of `DisplayManager_UiA.cpp`, with stubs in its `#else`, because the wired Ângulo cannot reach the mode. Tests first: `tools/tft_ui/test_confirma_ap.py` ran red on `c5a26bcc` (24 checks, 17 failed) and after the change gives 82 checks, 0 failed, with 14 of 14 mutants of the glue killed; V-PIX holds the 948 frames of the six groups equal. The nine products are sha256-equal to S10c C8's, pico_wiretft_angulo is function-equal (4,284 functions equal, 0 changed), and pico2_w_angulo grows from 1,035,616 B to 1,036,016 B (+400). The sprawl rose by 1 (364 -> 365), recorded in `tools/features.toml` beside S10c D2's.
- **S10c D6c** (`77d0406e`): four more includes of Users.cpp (`DisplayManager_Fonts.h`, `UiWidgets.h`, `display/CountInText.h`, `sensors/SensorChannelTable.h`) become a four-line comment, and three comments that S10c D6a left describing code in the wrong place are corrected. Users.cpp keeps its 573 lines (`tsv_check_d6a.py`, the copy of `tsv_check.py` that looks for `handleTouchPanelV24( )` in `DisplayManager_UsersUi.cpp`: the same 388 rows and 22 findings before and after, all in `DisplayManager_LangParser.cpp`); the eleven images from scratch are sha256-equal to S10c D7's; the negative control (an `asm` nop in `clearEnteredPin( )`) changes pico_w_release.
- **S10c D7b** (`3d433d6c`): the Confirm label of the setup-network dialog becomes "Start setup" (the first pick, "Open setup network", measured 174 px against the 144 px button and was refused by `host_telas.py` and `test_confirma_ap.py`; "Start setup" is 93 px), and `src/ui/angulo/telas/Base.h` records how the Ângulo reads "irreversible" (deviation 4 below). The nine products and pico_wiretft_angulo are sha256-equal to S10c D6c's; pico2_w_angulo differs (sha256 `ba766149...`) at the same `.bin` size, 1,036,016 B. `ci_gates_local.py --raso`: 63 of 64 steps passed and 1 was skipped, and the one failure is the fuzz step, because clang++ lost `libclang-cpp.so.18.1` in the 2026-10-24 upgrade of the machine.

**S10c D6: the decoded argument.** Because S10c D6a changes the compiled code, "the move changes nothing" had to be shown for each recompiled function, not assumed (`stages/s10c_d6/argumento/RESUMO.txt`, third update of 2026-10-28; its tools, models and the audit's scratch are in `tools/`, `models/` and `rev/` beside it, outside the repository). **The claim:** the eight functions that the move recompiled (`pinPolicy( )`, `pinAppendChar( )`, `blitPinCards( )`, `drawPinDotsInto( )`, `scramblePinKeys( )`, `drawPinPadInto( )`, `drawUserEdit( )` and `handleTouchPanelV24( )`) compute the same in the base (the images of `1f2ea33a`) and in the probe of S10c D6-0 that S10c D6a is sha256-equal to; in the five RP2040 TFT images (pico_w_release, pico_w_test, pico_w_test_https, pico_w_asserts, pico_wiretft) and in pico2_w_release, and in the two Ângulo images for the three of them that the Ângulo links (`pinPolicy( )` and `pinAppendChar( )` in both and `scramblePinKeys( )` in `pico_wiretft_angulo`: `RESUMO.second.txt`, lines 6, 7 and 10; `scramblePinKeys( )` in `pico2_w_angulo`: the line `pico2_w_angulo (NEW)` of the third update, `RESUMO.txt`, which gives all three for that image). **The verdicts:** no function came out not equivalent, and no instruction was found that computes something different. `pinPolicy( )` (exhaustive over 2^24 inputs) and `pinAppendChar( )` (exhaustive) are equivalent, and so are `blitPinCards( )`, `drawPinDotsInto( )`, `scramblePinKeys( )` and `drawPinPadInto( )`; `drawUserEdit( )` is equivalent under A2 (RP2040: 52 of 53 hunks, the 53rd dead under A2; Pico 2 W: 8 of 8); `handleTouchPanelV24( )` is equivalent under A2 and A6 (RP2040: 40 of 42 hunks, the other two alignment `nop`s; Pico 2 W: 39 of 40, the 40th an alignment `nop` right after a `b.n` that no verified path reaches, named and not argued away), with its policy inputs declared under A9 on the Pico 2 W. The policy slice of `handleTouchPanelV24( )` ran in full: on the RP2040 images row 2 ran every (min, kb, alpha) triple, 16,777,216 runs (2026-10-28, 08:41 to 09:07), and the base, the step and the source never differed, so rows 0, 1 and 2 are exhaustive there; on the Pico 2 W rows 0 and 2 ran 16,777,216 runs and row 1 131,088, and the base and the step differed in none.

**The tools and their controls.** A Thumb and Thumb-2 interpreter (`tmu.h`) runs both images' decoded text, and `verify_rom` and `defuse.py`'s `verify_bin` compare every raw halfword of every 16- and 32-bit instruction and every pool word with the `.bin`. `defuse.py` is a translation validator that walks base and step in lockstep and refuses what it does not model (`ror` and `rrx` operands, a load into `pc`, writeback on `sp`, a register post-index, a condition its flag setter does not define); the rest is exhaustive or sampled runs of host models. The controls: per-form mutants of the Pico 2 W text, 221 in all, 211 refused, and the other 10 each inspected and named equivalent or dead (for example `asr #31` -> `asr #32`, which gives identical values, or a writeback whose base register is dead afterwards); class mutants of the four larger functions (17 of 17, 26 of 26, 23 of 23 and 18 of 19 refused, the one not refused swapping the operands of an equality, which is equivalent); and a unit test of the interpreter (`forms_unit`: 26 baseline cases, 0 failed, 41 mutants, and every one changed the outcome).

**The audit.** An adversarial revisor audited it on 2026-10-28 (verdict COM RESSALVAS, nothing not equivalent). 39 of 39 hand mutants outside the regions were refused (19 on the RP2040 images, 20 on the Pico 2 W); sweeps of 1,518 mutants (an immediate changed by 1 or 4, a condition inverted) and of 2,520 (mnemonic and source register) left only escapes that are explained (equivalent, inside a modelled region, dead under A2, or an artefact of a tool); 22 of 24 wrong hints were refused (the two accepted belong to A13); removing A2 or A6 makes the validator refuse, so both are needed; `verify_bin` refused 7 of 7 planted bytes. The audit found two gaps in what `defuse.py` checks and closed them with its own scans, now named assumptions: A12, the frame balance, which `defuse.py` does not model (`balance.py` over the images, functions and base and step, 108 lines: the prologue equals the epilogue and the pop list the push list everywhere, and the two lines flagged, the touch function on the Pico 2 W, are explained by arithmetic), and A13, the entries of the summarised regions (`regtrack.py` over the 12 regions, base and step: the entries are exactly the declared inputs plus r8 and r9 of A11).

**The assumptions, what is not audited and what is unproven.** A1 to A13 are each named in their record. A1: the key index is at least 0 at the inline face (a mechanical scan of every write to the key register and the guard's bound of 254, by induction from 0). A2: the user editor's row count is 3, 4 or 5 (the decoded Pico 2 W `userEditRowCount` over all 65,536 inputs returns 3 for 65,280, 4 for 1 and 5 for 255). A3: `pinKeypadMode( )` returns a zero-extended byte (decoded). A6: `maxLenFor( )` and `pinKeypadMode( )` preserve memory (decoded structure and 200,000 random runs, none returned above 255 or wrote outside its own frame). A11: r8 and r9 at the step's face entry (decoded). A12 and A13 are above. **Not audited:** A3, A4, A5, A7, A8 and A10, which the passes only read. **Unproven:** `tbh` runs in no `tmu.h` model or unit vector (in `defuse.py` only in the touch run and one mutant); the shifter carry-out of wide flag-setting logical forms is not modelled (no such form occurs in these functions as far as the decoded text shows, but no mechanical scan of `tmu.h` for it was run); the Q flag of `smlabb` is not modelled (nothing reads it). **The instrument was wrong before the images were:** a soundness defect in `defuse.py`'s `arrive( )` (a loop counter's constant 0 was lost when its class was split into a fresh symbol), the shift type that `tmu.h` dropped (every shifted-register operand was taken as `lsl`, so `rsb r3,r3,r6,asr #1` would have been computed wrongly), and a first A6 harness without `clampPinPolicy( )`, which made the base and the step differ in every run because the base calls it out of line and the step inlines it. Each was fixed and the verdicts re-run (the RP2040 regression: 21 of 21 image-function pairs give the same errors, hunks and verdicts with the changed tool).

**The bench of 2026-10-28** (11:15 to 12:15, the Pico 2 W of the rig, four flashes, one of them for main's PR #295; `stages/s10c_d6_bancada/run_20261028/resultado.txt`). **S10c D6's A/B**, on pico2_w_release: the base (the image of `1f2ea33a`) was flashed at 11:36:35 and walked, 68 steps done and 5 skipped, 152 captures; the image of `fef95de0`, byte-equal to S10c D6a's, S10c C8's, S10c D6c's and S10c D7b's, was flashed at 11:57:07 and walked, 68 done and 5 skipped. `ab_comparar` reads **67 equal, 0 differ, 0 missing**, 1 not comparable by design (`id_cfg_conta`: the cards are dealt at random) and 43 skipped on both sides. The rig's keypad was `cards` (policy [4, 8, 3, 0]), so the deal and `blitPinCards( )` ran on both sides, and the faces of the PIN-choosing pad read '1' to '9', '', '0', '' on both. No RP2040 image was flashed: the decoded argument covers them. **S10c D7**, on pico2_w_angulo of the tree `dc3405c3` (the commit was amended after the bench, and `3d433d6c` has that tree): flashed at 12:09:56, `ap_cancelar_bancada.py` ran from 12:10 to 12:14 with 73 checks ok and 1 FAIL, the tool's own (it expected `/api/perms` to name the Ângulo, and both Pico 2 W images report `releasetwo`; `/api/ui`, which only the Ângulo serves, answered). In pt-BR and in es-ES the dialog on the glass differed from the host frames in 0 of 76,800 pixels; Cancel went back to Settings; the board answered from the same address with a growing uptime; and the log of the whole run has no `APP_AP_MODE_TRIGGERED`, `SYS_AP_START`, `APP_READY_AP` or `NET_CONNECT_TIMEOUT` record. **Confirm was not pressed** (it starts the setup AP and takes the board off the LAN), and the English label was not on the glass: the bench ran the two languages whose label S10c D7b left as it was. The es-ES texts of the dialog are drafts that await the native review (S10c D7).

**Recorded deviations.** (1) The sprawl watermark rose twice, each time at the maintainer's word: +1 at S10c D2 (363 -> 364, `SIMUT_NET_WIFI` in `showApConfirm( )`'s own frame) and +1 at S10c D7 (364 -> 365, the first `#if SIMUT_NET_WIFI` of `DisplayManager_UiA.cpp`), both recorded in `tools/features.toml`. (2) S10c D6a changes the products' compiled code (GCC inlines `PinKb::orderedFace( )`, `clampPinPolicy( )` and `maxLenFor( )`), accepted on route (a) of S10c D6-0 and proved by the decoded argument and the bench above, not by equal sha256. (3) S10c C8 kept `keyAt( )`, `popupKeyAt( )`, `popupRows`, `popupRowKeys`, `popupRowY`, `popupRowX0` and the `FOOT_*` names in `PinKeypad.h` (fourteen of the 26 names the plan expected to move), because `handleTouchPanelV24( )` reads them and its row in the function table is mixed. (4) The danger colour: ANGULO.md section 4.1 says the colour only changes when the action is irreversible ("a cor só muda quando ela é irreversível"). The Ângulo reads "irreversible" as "cannot be undone from the panel", the maintainer's decision of 2026-10-28 (`src/ui/angulo/telas/Base.h`, the note above `VmDialogo`): the mute-all and setup-network confirmations set `destrutivo`, and so does the account deletion (three dialogs on that date, by grep).

**Left for later.** (1) The hit-test names of `PinKeypad.h` (deviation 3): moving them means reclassifying `handleTouchPanelV24( )`, a code change for S11. **S11 A3 tried it on 2026-10-30 and did not keep it:** six images change (section 4.7), so it is S12's. (2) The Ângulo's `uiChanged( )` runs inside `_stateMutex` since S10c D3b, safe today (`uaSujo( )` only sets a flag), with the rule that it must not take the mutex written in comments only: a check is worth adding if that function grows (the review of S10c D3). (3) The function-table checker, fixed on 2026-10-28. `tsv_check.py` (scratch, `tools_s10c/`) looked for `handleTouchPanelV24( )` in `DisplayManager_Users.cpp` and read 388 rows and 30 findings at `3d433d6c`, 8 of them spurious (the per-mode rows of that function in `DisplayManager_UsersUi.cpp`, since S10c D6a; it read 22 at `1f2ea33a`). The main session promoted the two additions of `tsv_check_d6a.py` (`stages/s10c_d6/scripts/`) into it (the old file is `tsv_check.py.antes_d6a`): it reads 388 rows and 22 findings at `1f2ea33a`, `3d433d6c` and `5f2cdafd`, all in `DisplayManager_LangParser.cpp` and stale since B4b-0 (the same 22 as in the messages of S10c D6a, D6b, C8 and D6c), and a first line of `drawUserEdit( )` moved by one gives exactly one more. Those 22 rows waited for the sync with main; S10c sync 6 brought the table to the code on 2026-10-28 (`ccc46e86`): the 22, the 8 one-line offsets of main's hunks (`DisplayManager_i18n.cpp` 5, `DisplayManager_i18nUi.cpp`, `DisplayManager.h`, `display/SettingsMenu.h`), the six definitions that #294 gave `DisplayManager_LangParser.cpp` (`loadLangImpl( )`, `firstLangName( )`, `listLangFile( )`, `isLangListed( )`, `getListedLangName( )`, `getListedLangCode( )`; `loadLangFile( )` stays, as a one-line wrapper) and ten per-mode rows of `ui/original/UiTouchChain.h` that #294's hunk in the Idioma branch moved by a line and that `tsv_check.py` passed (it holds a per-mode row's start to a window of six lines and does not read its end; `permode_check.py` in `stages/tsv_sync6/` does): 394 rows and 0 findings. Its `--selftest` raises `StopIteration` on these trees, the old file too, and is not fixed. (4) The es-ES texts of the setup-network dialog await the native review.

## 3. The audit

### 3.1 (a) Members written inside draw bodies

**Scope.** The 56 function definitions named `draw*`, `redraw*`, `render` or
`drawOtaScreen` in `DisplayManager.h` (one inline, `redrawTopPanel`) and in the
TFT units, on `192e1ad`. 31 of them write at least one `DisplayManager` member;
the other 25 write none. A member here is a data member of the class, or a
field of a member struct (`_topPanel.showMinMax`).

**How it was produced** (two methods, so one cannot hide the other's error):

1. A text scanner, `python3 $B/tools/ui_writes.py <worktree>` (`--all` for every
   function, `--callees` for the helpers). It blanks comments and literals,
   reads the member list from `DisplayManager.h`, and reports an assignment,
   `++`/`--`, a first argument of `memcpy`, `snprintf`, `safeCopy` and the like,
   or a taken address. It follows a `DashPanel&` parameter as `_topPanel` or
   `_bottomPanel`, and it lists function-local and file-scope statics.
2. The compiler as a control, `python3 $B/tools/ui_constcheck.py <copy> <env> <worktree>`.
   In a scratch **copy** of the tree (`pio run -e <env> -t compiledb` makes
   `compile_commands.json` there), every non-static data member of
   `DisplayManager` is made `const` (a pointer member: the pointer, not the
   pointee), and each display unit is compiled with `-fsyntax-only`. Every
   write, increment, atomic store, or pass to a mutable pointer or reference is
   then an error at the line that does it, and gcc goes on, so one run lists
   them all (1.3 s for 16 units). Run for `pico_wiretft` and for `pico_w_release`
   (the second adds `drawApConfirm`, which only the Wi-Fi builds compile).
   Control: the six members the plan named all appear in `render`, at the
   lines below, and three of `render`'s write sites are not in the plan's lines
   (`_lastRenderedState` at 1814 and 1915, and the `_forceFullRedraw` set at 1901).

The two agree on every draw body. The compiler cannot see through an alias that
is not a member, which is why the scanner is kept: `drawSlotPanel` writes
through `DashPanel& panel`. The scanner cannot resolve scopes; the compiler
does. No draw body writes through `_sysConfigPtr` (grep and the aliases `cfg`,
`rec`, `u` in the draw bodies, 2026-10-14).

**The plan's starting list, checked.** The plan's line numbers belong to the
study tree; on `192e1ad` they are:

| member | plan | `192e1ad` | what the code says |
|---|---|---|---|
| `_lastRenderedState` | 1947, 2044 | `DisplayManager.cpp:1814, 1894, 1915, 1991` | **four** sites, not two: the AP-progress frame (1814) and the full-redraw branch (1915) also store it |
| `_bootPaintSeq` | 1868, 1948 | `:1815, 1895` | as planned (`++` after the AP frame and after the boot frame) |
| `showMinMax` | 2007, 2011 | `:1954` (`_topPanel`), `:1958` (`_bottomPanel`) | a field of `DashPanel`, not a member of the class; the reset after 30 s without a touch |
| `_prevAlarmSlotMask` | 2041 | `:1988` | as planned; no other function reads it |
| `_forceFullRedraw` | 1967 | `:1901` (set), `:1914` (cleared) | **two** sites: `render` itself raises it when the boot frame hands over to the dashboard |
| `_langChanged` | 1844 | `:1791` | as planned; set by `setLanguage` at `:280` |

**Everything the draw bodies write**, grouped by what depends on it. Lines are
`192e1ad`. "Elsewhere" is the other users of the member, from the same scanner
(`python3 $B/tools/ui_xref.py <worktree> <names>`, every function of the units).

A. State that other code reads, which a new driver's paint must keep or replace:

| writer (lines) | member, lines | elsewhere | why it matters |
|---|---|---|---|
| `render` `DisplayManager.cpp:1772-1992` | `_bootPaintSeq` 1815, 1895 | `bootPaintSeq( )`: `AppManager_Boot.cpp:543, 546` | Core 0 opens the AP-by-touch window on the first boot frame |
| | `_lastRenderedState` 1814, 1894, 1915, 1991 | `loopCore1` (writes 1289, 1438, 1442; reads 1489, 1514, 1522), `restoreNormalDashboard` (15 reads), `redrawAlarmFlash` | the alarm rotation and flash, and the restore, read `selectedSlotIdx` and `isBooting` from it |
| | `_langChanged` 1791 (cleared) | set by `setLanguage` `:280` | forces a full boot redraw after a pack loads |
| | `_forceFullRedraw` 1901 (set), 1914 (cleared) | set by the constructor, `setBootStatus*` (4), `forceDashboard`, `forceGraphView`, `handleTouch` (5) | |
| | `_topPanel.showMinMax` 1954, `_bottomPanel.showMinMax` 1958 | toggled or cleared by `handleTouch` (`Touch.cpp:49, 435, 505, 524, 542, 575, 576`), read by `drawSlotPanel` 801 | the 30 s return to the normal view is a product timer, and it lives in this body |
| | `_prevAlarmSlotMask` 1988 | none | diff state of the alarm-button redraw |
| `drawTopBar` `Dashboard.cpp:457-748` | `_webNotifyStartMs` 551, `_webNotifyUser[0]` 552 | set by `setWebNotification` `DisplayManager.cpp:2004`; read at `:1925` | the banner's expiry is decided here |
| | `_pktArrowState` 652, `_pktArrowFlashOn` 656, `_pktArrowFlashTime` 657 | set by `setTelemetrySendStatus` (`Settings.cpp:754-766`); `_pktArrowState` read by `render` `:1927` | the telemetry arrow's flash (state 3 returns to 1 after its end time) is advanced in this body |
| `drawSlotPanel` `Dashboard.cpp:751-1038` | `panel.lastMinMax` 803, 869 (`panel` is `_topPanel` or `_bottomPanel`) | none | diff state, but written through a parameter |
| `drawLoadingScreen` `Graph.cpp:33-50` | `_loadingDrawn` 49 (set) | cleared by the constructor `DisplayManager.cpp:153` and by `requestLoadingScreen` `Graph.cpp:71`; read by `isLoadingDrawn( )`, waited on up to 500 ms at `AppManager_Events.cpp:170, 233` | answers the plan's open item: this is the one place that sets it |
| `drawOtaScreen` `Ota.cpp:150-226` | `_otaDrawnSeq` 225 (atomic release store), `_otaRedrawAll` 224, `_otaPctDrawn` 187, 223 | `showOta` waits on `_otaDrawnSeq` (`DisplayManager.cpp:808-822`); `otaTick` sets `_otaRedrawAll` (`Ota.cpp:139`) | Core 0 may paint this screen while Core 1 is parked |
| `redrawTopPanel` `DisplayManager.h:949-990` | `_sharedState.topSlotIdx` 953, `.selectedSlotIdx` 967 (and `EVT_SLOT_SELECT` pushed at 968-969), `.topSlotTemp/Hum/Pres/Type/Valid` 977-981, `.topSlotName` 982 | the whole of `_sharedState` is the Core 0 to Core 1 channel | core state under the mutex, written by a function that also draws |
| `drawAlarmEdit` `Settings.cpp:222-334` | `_tempAlarmConfig.chMax[]` 241, 243, `.chMin[]` 244 | `handleTouch` (15 uses); since 2026-10-31 Core 1 copies it at Salvar (`postSavedAlarmLimits( )`) and Core 0 diffs the copy on `EVT_SAVE_ALARMS` (`editedAlarmRecord( )`, which handed out the live copy, is gone) | the draw normalises the working copy (min below max, clamped to `saneMax`): configuration data written by a draw function |
| `drawAlarmEdit` | `_editFieldFocus` 249, 250 | `handleTouch` (13) | clamp |
| `drawSettingsLicense` `DisplayManager.cpp:2026-2104` | `_licenseTotalPages` 2045, 2046; `_licensePage` 2047, 2048 | `handleTouch` (2 and 8 uses) | touch reads the page count the draw computed |
| `drawSystemStatus` `Settings.cpp:1334-1526` | `_statusLastDraw` 1521 | `loopCore1` reads it for the 1 s redraw | |

B. Page clamps: the draw function bounds a page index with the original's
four rows per page, `(total + 3) / 4`. `handleTouch` also reads each of them.
A driver whose pages hold another number of rows must clamp its own.

| writer | member, lines |
|---|---|
| `drawSettingsThemes` `Settings.cpp:33-76` | `_themePage` 39, 40 |
| `drawSettingsAlarms` `Settings.cpp:92-131` | `_alarmPage` 96, 97 |
| `drawSettingsMain` `Settings.cpp:346-433` | `_mainMenuPage` 357, 358 |
| `drawUserList` `Users.cpp:928-979` (reached through `drawSettingsUsers` and `drawAuthUser`) | `_usersPage` 933, 934 |
| `drawSettingsLang` `i18n.cpp:178-262` | `_langPage` 197, 198 |

C. Repaint bookkeeping nobody else reads. The screen's entry point (`show*`,
core) sets these to their initial values; its painter (original) consumes them.
A driver that paints from the whole state does not need them, and they stay in
the class so its layout does not move.

| writer | member, lines |
|---|---|
| 25 painters of full screens | `_forceSettingsRedraw` cleared: `drawSettingsLicense` 2103; `drawAlarmAction` `Alarm.cpp:71`; `drawSettingsDisplayOffset` `Calibration.cpp:186`; `drawTouchSensitivity` 308; `drawTouchCalibration` 500, 508, 559, 609; `drawSettingsThemes` `Settings.cpp:75`; `drawSettingsAlarms` 130; `drawAlarmEdit` 332; `drawSettingsMain` 431; `drawSettingsPassword` 598, 736; `drawApConfirm` 799 (Wi-Fi builds); `drawClockEntry` 922; `drawMuteConfirm` 983; `drawSettingsSounds` 1164; `drawMelodySelect` 1235; `drawSystemStatus` 1336; `drawPinScreen` `Users.cpp:595`; `drawAlarmSensorMenu` 738; `drawMaintEntry` 786, 829; `drawUserList` 963; `drawUserEdit` 1068; `drawUserConfirmDel` 1073; `drawPanelMessage` 1116; `drawPinPolicy` 1295; `drawSettingsLang` `i18n.cpp:259` |
| `drawSettingsThemes` | `_lastThemePage`, `_lastPreviewThemeIdx` 75 |
| `drawSettingsAlarms` | `_lastAlarmPage`, `_lastAlarmSelection` 130 |
| `drawAlarmEdit` | `_lastEditPage` 333 |
| `drawSettingsMain` | `_lastMainMenuPage` 431 |
| `drawUserList` | `_lastUsersPage`, `_lastUsersSel` 963, 978 |
| `drawUserEdit` | `_lastUserEditPage`, `_lastUserEditSel` 1068 |
| `drawPinPolicy` | `_lastPolicySel` 1296 |
| `drawSettingsLang` | `_lastLangPage` 260, `_lastPreviewLangIdx` 261 |
| `drawSettingsDisplayOffset` | `_lastOffsetDrawX` 187, 206; `_lastOffsetDrawY` 207 |
| `drawClockEntry` | `_clockValuesDirty` 916, 923 (set by `handleTouch`) |
| `drawPinScreen` | `_pinCardsDirty` 633 (set by `handleTouchPanelV24`, 2 sites) |

D. Not members, so not in the class, but state a draw body writes and that
nothing outside its file reads: file-scope `g_lastSensStability`,
`g_lastSensThreshold`, `g_lastSensDone`, `g_sensFirstDraw`, `g_lastCalPointIdx`,
`g_lastCalHoldReady`, `g_lastCalStep` (`Calibration.cpp:22-28`; the two `show*`
entry points reset them at `:49-55`); function-local statics `s_lastMenuSel`
(`Settings.cpp:397, 432`), `lastSoundPage` (1049, 1165), `s_lastSoundSel` (1086,
1166) and `lastSec` (`Users.cpp:602, 605`); and the scratch arrays of
`drawGraphScreen`, `drawGraphDetailScreen`, `drawPeakMarker` and
`drawSystemStatus`.

E. Written for a draw function by a callee, which the body-level census does not
show: `redrawAlarmFlash` (`Alarm.cpp:157-185`), `restoreNormalDashboard`
(`Dashboard.cpp:170-180`), `redrawTopPanel` and `render` hand `_topPanel` or
`_bottomPanel` to `drawSlotPanel`, which writes `panel.lastMinMax`; `render`
calls `drawTopBar`, which is group A; `buildDashLayout` (`Dashboard.cpp:1040-1081`,
called by `drawBottomButtons` and by `handleTouch`) clamps `_currentPage` at 1068.

**Writes that are not in the plan's duties.** The plan's section 3.4 lists
seven duties. This census adds four a driver that skips `render` and `drawTopBar`
would lose: the 30 s return of both cards from min/max to the normal view
(`render` 1950-1962); the web-notification banner's expiry (`drawTopBar` 551-552);
the telemetry arrow's flash-to-fixed transition (`drawTopBar` 648-660); and the
`drawAlarmEdit` normalisation of the limits' working copy. The study's glue reads
`_pktArrowState` and `_webNotifyStartMs` (`DisplayManager_UiA.cpp:180-190`) and
never writes them, and `UA_DESVIO` returns at the top of `drawTopBar`
(`Dashboard.cpp:471` of the study), so by reading, state 3 is never advanced to
1 in the study. That was not run on hardware. Section 2.4 carries them as duties
8 to 11.

### 3.2 (b) Callers outside the display units

Who, outside `src/DisplayManager*.cpp`, calls a `draw*` function or uses a name
declared in `src/Themes.h`, `src/UiWidgets.h` or `src/DisplayManager_Fonts.h`.
Whoever is on this list keeps compiling in an image that does not link the
original driver, so this is the work list for stage S10b. Taken on `192e1ad`
(2026-10-14) with:

```
# the names the three headers declare, found with grep -w in src/ and test/, minus the display units
grep -rnwE '<names>' src test --include=*.cpp --include=*.h \
  | grep -vE '^src/(DisplayManager[A-Za-z_0-9]*\.(cpp|h)|Themes\.(cpp|h)|UiWidgets\.(cpp|h)):'
grep -rnE '#\s*include\s*[<"][^>"]*(Themes|UiWidgets|DisplayManager_Fonts)\.h[>"]' src test
grep -rnE '\b(re)?draw[A-Z_]?\w*\s*\(' src test --include=*.cpp --include=*.h   # then minus the units
```

`<names>` are the 70 identifiers of the three headers: `RGB565`, `themeTint`,
`ThemePalette`, `currentTheme`, `availableThemes`, `loadTheme`, `getThemeCount`,
`getThemeId`, `getThemeIndexByName`, `getThemePalette`, `scanCustomThemes`, the
28 `C_*` colour macros, `UiBtnStyle`, `UiArrowDir` and their enumerators,
`uiBlend565`, `uiTitleBar`, `uiButton`, `uiNavArrow`, `uiFooterMenu`,
`uiCloseX`, `uiPageDots`, `uiScrollbar`, `uiUnit`, `uiUnitWidth`, `uiMenuIcon`,
and `simutFont9pt`, `simutFont12pt`, `simutFont24pt`.

**`draw*` functions.** No file outside the display units calls a
`DisplayManager::draw*` member. Everything outside reaches the panel through
`show*`, `set*`, `is*`, `get*`, `request*` and `inject*`. The `draw*(` lines
outside the units are 43: 3 are phase names in comments of `FlashIrqProbe.h`
(`:122-126`), and 40 are in the sensor headers, which define and call free
helpers (`drawThermometerLarge`, `drawDropLarge`, `drawMinMaxTempRow`,
`drawMinMaxGraphBtn`, ...) in `src/sensors/SensorDrawing.h` and call GFX
primitives. Those headers are drawing code of the original that lives outside
the units (section 3.3 lists it).

**`Themes.h`.** Seventeen lines in six files call its functions, plus four
`themeTint` calls in sensor headers. Every one is a catalogue or a
persistence call; none reads a colour:

| caller | lines | calls | what it is |
|---|---|---|---|
| `AppManager_Boot.cpp` | 996, 997 | `scanCustomThemes( )`, `loadTheme(cfg.themeIndex)` | boot, after the file system mounts |
| `AppManager_Events.cpp` | 363 | `loadTheme(cfg.themeIndex)` | `EVT_APPLY_THEME` from the panel (`:359-364`), then `saveConfiguration( )` |
| `AppManager_Commands.cpp` | 215, 216, 223, 232, 234, 237, 239 | `getThemeCount`, `getThemeId`, `getThemePalette(i)->displayName`, `getThemeIndexByName`, `loadTheme` | the CLI: `show themes` (`CMD_SHOW_THEMES`, `CommandParser.cpp:201`) and the `theme` command; full-CLI images only |
| `WebManager_Api.cpp` | 410, 413 | `getThemeCount`, `getThemePalette(i)->displayName` | `GET /api/themes` (`handleApiThemes`, registered at `WebManager_History.cpp:2405`) |
| `WebManager_Commit.cpp` | 133, 140 | `getThemeCount`, `loadTheme(t)` | the `theme` field of `commit_all`: range check, then apply |
| `WebManager_Files.cpp` | 148, 519, 520 | `scanCustomThemes( )` twice, `getThemeCount( )` | a `.thm` file deleted or uploaded: hot reload of the custom themes |
| `sensors/DHT22Driver.h` | 102, 200 | `themeTint(humidity)` | the humidity drop's glint |
| `sensors/BME280Driver.h` | 443, 553 | `themeTint(humidity)` | the same |

Not callers, but they see the whole header: `DisplayManager.h:39` includes
`Themes.h`, so every file that includes `DisplayManager.h` receives the `C_*`
macros and `currentTheme`; `sensors/SensorDrawing.h:24-27` includes it and
defines a fallback `RGB565`; `WebManager_Auth.cpp:14` includes it and uses
nothing from it. No `C_*` macro, `currentTheme` or `availableThemes` is used
outside the display units, `Themes.cpp` and the sensor headers.

**`UiWidgets.h`.** No caller outside the units. It is included by ten of
them (`DisplayManager.cpp`, `_Dashboard`, `_Auth`, `_Graph`, `_Users`,
`_i18n`, `_Calibration`, `_Ota`, `_Settings`, `_Calendar`) and by
`UiWidgets.cpp`.

**`DisplayManager_Fonts.h`.** No caller outside the units. It is included by
twelve units (`DisplayManager.cpp`, `_Dashboard`, `_Auth`, `_Graph`, `_Users`,
`_i18n`, `_Calibration`, `_Ota`, `_Alarm`, `_Settings`, `_Calendar`, `_Touch`),
by `UiWidgets.cpp`, and by `DisplayManager_Fonts.cpp`, the only file that
includes the three font tables (`FreeSansBold9pt8b_latin1.h`,
`FreeSansBold12pt8b_latin1.h`, `FreeSansBold24pt7b_subset.h`). The sensor
headers do not include it: they receive the font as a parameter.

**Other original-side headers that something outside the units consumes**
(checked the same way, because the plan counts them with the original):

| header | consumed outside the units by | note for the split |
|---|---|---|
| `sensors/SensorDrawing.h` | `BME280Driver.h`, `DHT22Driver.h`, `DS18B20Driver.h`, `SensorPanelDispatch.h` | it holds drawing (`drawThermometerLarge`, ...) and hit-testing geometry the touch handler uses: `MINMAX_BTN_W` (`:191`) and `minMaxBtnX( )` (`:195`) are read at `DisplayManager_Touch.cpp:432-433` |
| `PinKeypad.h` (`PinKb::`) | `SystemDefs_Validate.h`, `StorageManager.*`, `WebManager_Api.cpp`, `WebManager_History.cpp` (`GET /api/keypad`), `AppManager_CmdHandlers.cpp`, `AppManager_Panel.cpp` | PIN policy and matching (core) share a header with the keypad's geometry (original), which `GET /api/keypad` also publishes, on the original panel's images only since S12's F4 (2026-10-31; the Ângulo's answer carries none of it); the split was done in S10c C8 (`c5a26bcc`, 2026-10-28): the layout that only the original reads is in `src/ui/original/PinKeypadLayout.h`, and `keyAt( )`, `popupKeyAt( )`, the `popupRow*` helpers and the `FOOT_*` names stay here (section 2.18) |
| `PasswordKeyboard.h` | none | used by the units only |
| `display/SettingsMenu.h`, `display/ClockEntry.h`, `OtaScreen.h` | the native suites (`test_validators`, `test_lan`) | pure logic with tests; they stay with the core |
| `TftWithOffset.h` | `display/ILI9341_320x240.h` | panel layer |

What this means for the Ângulo image: the six files above must build with the
two theme hooks of the contract (`uiThemeCount`, `uiThemeId`, `uiThemeName`) or
stop calling these functions, and the `.thm` upload path needs the refusal the
plan describes; the sensor headers go with the original or take the colour
as a parameter, since `themeTint` reads the palette.

### 3.3 (c) Every function, classified

The classification is `docs/analysis/UI_DRIVERS_funcoes.tsv`, one row per line,
with the columns `unit`, `line`, `end`, `symbol`, `class`, `original_lines`,
`core_lines` and `note`. It is also the input of the byte count in section 3.6.
This section summarises it as it stands on `192e1ad` (2026-10-14); the three
classes are defined in section 1, and the two other display drivers,
`DisplayManager_Alpha.cpp` and `DisplayManager_None.cpp`, are left out unclassified.

**What a row is.** 371 rows: 290 function definitions (the 15 units of the TFT
build, plus `redrawTopPanel`, the one `redraw*` function that `DisplayManager.h`
defines in the class body), 34 per-mode rows that are views into the two touch
handlers (symbol `DisplayManager::handleTouch [MODE_X]`; the note carries
`draws=N events=M`), and 47 rows for data the units define (statics, tables,
constants, the three GFX font tables, the theme tables), because the byte count
needs them. A mixed row says, as line ranges, which lines are original and
which are core. Of the 290 function rows, 183 are core, 98 original and 9 mixed;
the 47 data rows are 20 core and 27 original.

| unit | core | original | mixed | rows |
|---|---:|---:|---:|---:|
| `DisplayManager.h` | 0 | 0 | 1 | 1 |
| `DisplayManager.cpp` | 75 | 3 | 2 | 80 |
| `DisplayManager_Alarm.cpp` | 9 | 3 | 0 | 12 |
| `DisplayManager_Auth.cpp` | 9 | 0 | 0 | 9 |
| `DisplayManager_Calendar.cpp` | 3 | 1 | 0 | 4 |
| `DisplayManager_Calibration.cpp` | 9 | 13 | 2 | 24 |
| `DisplayManager_Dashboard.cpp` | 8 | 19 | 1 | 28 |
| `DisplayManager_Fonts.cpp` | 0 | 6 | 0 | 6 |
| `DisplayManager_Graph.cpp` | 2 | 9 | 0 | 11 |
| `DisplayManager_LangParser.cpp` | 22 | 0 | 0 | 22 |
| `DisplayManager_Ota.cpp` | 3 | 8 | 0 | 11 |
| `DisplayManager_Settings.cpp` | 23 | 19 | 1 | 43 |
| `DisplayManager_Touch.cpp` | 3 | 0 | 27 | 30 |
| `DisplayManager_Users.cpp` | 32 | 18 | 9 | 59 |
| `DisplayManager_i18n.cpp` | 5 | 1 | 0 | 6 |
| `Themes.cpp` | 0 | 13 | 0 | 13 |
| `UiWidgets.cpp` | 0 | 12 | 0 | 12 |
| **total** | **203** | **125** | **43** | **371** |

**After S10a (2026-10-21, tip `7c18d00b` and C12).** The table was refreshed against the tree. It has 372 rows: the 371 of `192e1ad` and `DisplayManager::licenseOpening`, the accessor S4 added for the Ângulo's licence screen. 52 rows are in another unit than at `192e1ad`: 51 moved with the eight splits (section 2.14), and `redrawTopPanel( )` left the class body for `DisplayManager_Actions.h` in S4 and is core now, since its two draws are one call of the driver's `uiTopPanel( )`. By class the rows are 205 core, 125 original and 42 mixed (203, 125 and 43 at `192e1ad`).

| unit | core | original | mixed | rows |
|---|---:|---:|---:|---:|
| `DisplayManager_Actions.h` | 1 | 0 | 0 | 1 |
| `DisplayManager.cpp` | 76 | 1 | 1 | 78 |
| `DisplayManager_Render.cpp` | 0 | 2 | 1 | 3 |
| `DisplayManager_Dashboard.cpp` | 0 | 19 | 1 | 20 |
| `DisplayManager_Panel.cpp` | 8 | 0 | 0 | 8 |
| `DisplayManager_Touch.cpp` | 3 | 0 | 27 | 30 |
| `DisplayManager_Settings.cpp` | 1 | 19 | 1 | 21 |
| `DisplayManager_SettingsCore.cpp` | 22 | 0 | 0 | 22 |
| `DisplayManager_Users.cpp` | 32 | 18 | 9 | 59 |
| `DisplayManager_Graph.cpp` | 0 | 9 | 0 | 9 |
| `DisplayManager_GraphCore.cpp` | 2 | 0 | 0 | 2 |
| `DisplayManager_Calibration.cpp` | 0 | 13 | 2 | 15 |
| `DisplayManager_CalibrationCore.cpp` | 9 | 0 | 0 | 9 |
| `DisplayManager_Calendar.cpp` | 0 | 1 | 0 | 1 |
| `DisplayManager_CalendarCore.cpp` | 3 | 0 | 0 | 3 |
| `DisplayManager_Alarm.cpp` | 9 | 3 | 0 | 12 |
| `DisplayManager_Ota.cpp` | 0 | 8 | 0 | 8 |
| `DisplayManager_OtaCore.cpp` | 3 | 0 | 0 | 3 |
| `DisplayManager_Auth.cpp` | 9 | 0 | 0 | 9 |
| `DisplayManager_i18n.cpp` | 5 | 0 | 0 | 5 |
| `DisplayManager_i18nUi.cpp` | 0 | 1 | 0 | 1 |
| `DisplayManager_LangParser.cpp` | 22 | 0 | 0 | 22 |
| `DisplayManager_Fonts.cpp` | 0 | 6 | 0 | 6 |
| `Themes.cpp` | 0 | 13 | 0 | 13 |
| `UiWidgets.cpp` | 0 | 12 | 0 | 12 |
| **total** | **205** | **125** | **42** | **372** |

- **One class now.** Core only: `DisplayManager_Panel.cpp`, `_SettingsCore.cpp`, `_CalibrationCore.cpp`, `_GraphCore.cpp`, `_OtaCore.cpp`, `_CalendarCore.cpp`, `_i18n.cpp`, `_Auth.cpp`, `_LangParser.cpp` and `DisplayManager_Actions.h`. Original only: `DisplayManager_i18nUi.cpp`, `_Graph.cpp`, `_Calendar.cpp`, `_Ota.cpp`, `_Fonts.cpp`, `Themes.cpp` and `UiWidgets.cpp`.
- **Still more than one, and why.** `DisplayManager.cpp` is core with `drawSettingsLicense( )` (original; D5, S10b's licence hook) and `loopCore1( )` (mixed). `DisplayManager_Render.cpp` is original, and `render( )` is mixed (the 30 s return of both cards from min/max is core). `DisplayManager_Dashboard.cpp` is original with `drawTopBar( )` (mixed: the banner's expiry and the telemetry arrow's flash end). `DisplayManager_Settings.cpp` is original with `showApConfirm( )` (core; D5) and `drawAlarmEdit( )` (mixed). `DisplayManager_Calibration.cpp` is original with the two mixed `show*` functions (D5). `DisplayManager_Alarm.cpp` (9 core, 3 original) and `DisplayManager_Users.cpp` (32, 18 and 9 mixed) are whole by D5. `DisplayManager_Touch.cpp` is 3 core rows and 27 mixed: `handleTouch( )` and its 26 views, which wait for D6. The units with both core and original rows are four (`DisplayManager.cpp`, `_Settings.cpp`, `_Users.cpp`, `_Alarm.cpp`), each a D5 case. **Resolved on 2026-10-28 (S10c, stage D):** the licence draw is in `DisplayManager_LicenseUi.cpp` (S10c D4b-2, `6c216cc6`), `showApConfirm( )` in `DisplayManager_SettingsCore.cpp` (S10c D2, `9fae2fa7`), Calibration's two `show*` functions in `DisplayManager_CalibrationCore.cpp` with no write to the original's caches (S10c D3, `aea1f5f8`, `c67a1cb4`, `79ac927a`), the alarm draws in `DisplayManager_AlarmUi.cpp` (S10c D5, `87875859`) and the Users draws in `DisplayManager_UsersUi.cpp` (S10c D6a, `5f04277d`); the function table `UI_DRIVERS_funcoes.tsv` is the live record of each function's unit and class (section 2.18).
- **How it was refreshed and checked.** One script, `tools_s10a/tsv_check.py` (scratch, like the S0 readers), reads each row against the tree: a function row against the S0 scanner's definition (first line and closing brace), a data row against the file-scope declaration (a font row against its `#include`), a per-mode row against its function (inside it, in order, its first lines naming its mode), and a mixed row's ranges against its function; and it lists any definition of a unit with no row. It was proved first. On `192e1ad`'s sources with S0's table it finds 1 row of 371 (`CHANNEL_LABEL` ends at 141 in the table and at 139 in the code: the table counted the `static_assert` after it); on the S10a tip with S0's table (`6702b7e5`'s, which `331266dc` and C11e still held), 307 (rerun 2026-10-21, `stages/s10a_c12b/tsv_check_tip_before_refresh.txt`; C12's first text said 322, which no record held and the rerun does not give); with the refreshed table, 0. `--selftest` passes the clean table and refuses each of seven doctored copies (a line, an end, a moved row left in its old unit, a data row, a per-mode row outside its function, a range outside its function, a definition with no row) with exactly one finding. A mixed row's ranges (`original_lines`, `core_lines`) shifted with the function when its text is unchanged (7 rows: `showTouchCalibration`, `showSettingsDisplayOffset` and five per-mode rows). The other 35 (six of the eight mixed functions, the ninth of S0's list, `redrawTopPanel( )`, being core since S4, and 29 per-mode rows) changed in S4 to S10a and keep `192e1ad`'s numbers, which the note says in its first words ("[ranges and counts are 192e1ad's: the body changed since]"); their `draws=` and `events=` counts are `192e1ad`'s too. `check_ui_boundary.py` reads the table's units and classes: 0 findings.

**After S10b-1 (2026-10-21, the tree of C6, refreshed in C7).** The table has the same 372 rows and the same classes (205 core, 125 original and 42 mixed): S10b-1 moved no row between classes. Two things moved, and 83 rows with them. **63 rows changed lines:** the seven `UA_DESVIO( );` sites left `DisplayManager_Dashboard.cpp`, `DisplayManager_Users.cpp` and `DisplayManager_Alarm.cpp` and the three `_uaPintando` guards left `DisplayManager_Panel.cpp`, by a line or two each (10, 30, 5 and 3 rows); #284's three lines moved 11 rows of `DisplayManager_LangParser.cpp`; and 4 rows are `DisplayManager_Touch.cpp`'s: `handleTouch( )` now ends at line 401 (it ended at 1226) and the three `accept*` gates sit at 403-467. **20 rows changed unit:** the chain's per-mode rows, from `[MODE_DASHBOARD]` (the press) to `[MODE_OTA_UPDATE]`, now belong to `ui/original/UiTouchChain.h`, the fragment of section 2.15, with its lines (9-826); all are mixed. `DisplayManager_Touch.cpp` keeps `handleTouch( )` (mixed), the three gates (core) and the six views before the chain (the finger-up on the dashboard, the calibration's release, the sensitivity sampling, the calibration's press, the boot skip and the web-busy gate): the 26 views of `handleTouch( )` are 6 there and 20 in the fragment. The `handleTouch( )` row keeps its `192e1ad` ranges (its note says so) and its note says where the chain is. The six catalogue rows of `Themes.cpp` say that the Ângulo defines the same names in `src/ui/angulo/Temas.cpp`. **The Ângulo's own code has no rows, then or now:** `Temas.cpp`, `uiTouch( )` and the glue are in `src/ui/angulo/`, which this table never classified (it classifies the original's tree, and `bytes_original.py` attributes by its units), so neither arrived as a row.

| unit | core | original | mixed | rows |
|---|---:|---:|---:|---:|
| `DisplayManager_Actions.h` | 1 | 0 | 0 | 1 |
| `DisplayManager.cpp` | 76 | 1 | 1 | 78 |
| `DisplayManager_Render.cpp` | 0 | 2 | 1 | 3 |
| `DisplayManager_Dashboard.cpp` | 0 | 19 | 1 | 20 |
| `DisplayManager_Panel.cpp` | 8 | 0 | 0 | 8 |
| `DisplayManager_Touch.cpp` | 3 | 0 | 7 | 10 |
| `ui/original/UiTouchChain.h` | 0 | 0 | 20 | 20 |
| `DisplayManager_Settings.cpp` | 1 | 19 | 1 | 21 |
| `DisplayManager_SettingsCore.cpp` | 22 | 0 | 0 | 22 |
| `DisplayManager_Users.cpp` | 32 | 18 | 9 | 59 |
| `DisplayManager_Graph.cpp` | 0 | 9 | 0 | 9 |
| `DisplayManager_GraphCore.cpp` | 2 | 0 | 0 | 2 |
| `DisplayManager_Calibration.cpp` | 0 | 13 | 2 | 15 |
| `DisplayManager_CalibrationCore.cpp` | 9 | 0 | 0 | 9 |
| `DisplayManager_Calendar.cpp` | 0 | 1 | 0 | 1 |
| `DisplayManager_CalendarCore.cpp` | 3 | 0 | 0 | 3 |
| `DisplayManager_Alarm.cpp` | 9 | 3 | 0 | 12 |
| `DisplayManager_Ota.cpp` | 0 | 8 | 0 | 8 |
| `DisplayManager_OtaCore.cpp` | 3 | 0 | 0 | 3 |
| `DisplayManager_Auth.cpp` | 9 | 0 | 0 | 9 |
| `DisplayManager_i18n.cpp` | 5 | 0 | 0 | 5 |
| `DisplayManager_i18nUi.cpp` | 0 | 1 | 0 | 1 |
| `DisplayManager_LangParser.cpp` | 22 | 0 | 0 | 22 |
| `DisplayManager_Fonts.cpp` | 0 | 6 | 0 | 6 |
| `Themes.cpp` | 0 | 13 | 0 | 13 |
| `UiWidgets.cpp` | 0 | 12 | 0 | 12 |
| **total** | **205** | **125** | **42** | **372** |

- **Still more than one, and why** (after S10b-1). As after S10a, except for `DisplayManager_Touch.cpp`, which is 3 core rows and the mixed `handleTouch( )` with 6 views; the chain's 20 views are the fragment's. The units with both core and original rows are the four of S10a (`DisplayManager.cpp`, `_Settings.cpp`, `_Users.cpp`, `_Alarm.cpp`), each a D5 case that S10b-2 resolves (section 4.4).

**After C6c and C6d (2026-10-22).** The table has 387 rows: 207 core, 138 original and 42 mixed. C6c added 15 rows for names that headers define, each header its own unit, so that the link gate attributes them (section 2.15), and C6d moved three of them from core to original by their callers (13 original, 2 core); no row of C7's table changed or moved (a comparison of the two files: 0 rows removed or changed, the 372 in their order).

| unit | core | original | mixed | rows |
|---|---:|---:|---:|---:|
| `DisplayManager.h` | 0 | 1 | 0 | 1 |
| `PasswordKeyboard.h` | 0 | 2 | 0 | 2 |
| `PinKeypad.h` | 0 | 1 | 0 | 1 |
| `Themes.h` | 0 | 1 | 0 | 1 |
| `DisplayManager_FmtFloat.h` | 0 | 2 | 0 | 2 |
| `display/SettingsMenu.h` | 1 | 0 | 0 | 1 |
| `sensors/SensorHelpers.h` | 1 | 0 | 0 | 1 |
| `sensors/SensorPanelDispatch.h` | 0 | 6 | 0 | 6 |
| **added by C6c and C6d** | **2** | **13** | **0** | **15** |
| **total** | **207** | **138** | **42** | **387** |

The scratch checker (`tsv_check.py`) reads the 387 rows with 0 findings once it scans those headers; its version from before C6c reports 14 findings, "unit not scanned", on the same table.
- **How it was refreshed and checked.** `tools_s10b/tsv_check.py`, S10a's script taught the fragment (scratch, like the S0 readers). On the sources of C6 with S10a's table it reads 83 findings (the 63 and the 20 above); with the refreshed table, 0. S10a's script on the new table reports exactly the 20 fragment rows ("the parent `handleTouch` is not in `ui/original/UiTouchChain.h`"), because it knows one parent file per view. `--selftest` passes the clean table and refuses each of nine doctored copies (S10a's seven and two new ones, a fragment row outside the fragment and a fragment row back in `DisplayManager_Touch.cpp`) with exactly one finding; the check also wants `DisplayManager_Touch.cpp` to include `ui/UiDriverTouch.h` inside `handleTouch( )` when any row has the fragment's unit. The refresh took `edc95d6d`'s `src/` as the base, shifted a mixed row's ranges only when its text was unchanged and its note did not call them `192e1ad`'s (7 rows shifted, 33 kept theirs) and added no row. `check_ui_boundary.py` reads the table's units: 0 findings.

**How it was produced and checked (S0).** The definitions came from
`python3 $B/tools/ui_funcs.py <worktree>`, a scanner that blanks comments and
literals and follows the braces. It was cross-checked against the symbol table of
the `pico_wiretft` image: each `DisplayManager::` symbol has a definition except
two inline accessors of the header (`pushUiEvent`, `getTopSlotIdx`); 40
definitions have no symbol in that image (inlined, or compiled out of that
build, as `drawApConfirm` is). Four readers, one per group of units, read the
function bodies and wrote the rows. Then every definition the scanner found was
matched to a row by unit, name and line: all 289 in the units have one, and so
does `redrawTopPanel`; the 35 other definitions in `DisplayManager.h` are
one-line accessors and have none. The match checks coverage and not judgement. A
class is a reading of the code, which is why every mixed row carries its line
ranges: anyone can read the split again.

**The nine mixed functions, whole** (S0's list, on `192e1ad`; `redrawTopPanel` is core since S4, so eight after S10a). The
per-mode rows below are views into two of them.

| function | original part | core part |
|---|---|---|
| `loopCore1` `DisplayManager.cpp:1241-1745` | the first paint, the `_themeChanged` block (it draws and sets `MODE_DASHBOARD`), the dispatch from mode to `draw*` and `render`, the repaint cadences, the 3 s revert of the graph header | setup, park, capture, heartbeat, touch read, the calls to `handleTouch` and `otaTick`, the alarm navigation, rotation and flash timers, lockout expiry, the sensitivity-to-calibration tick, pacing |
| `render` `:1772-1992` | the dashboard and boot-terminal paint with its cache | the 30 s return of both cards from min/max (`:1949-1962`) |
| `drawTopBar` `Dashboard.cpp:457-748` | the top bar painter | the web-notification banner's expiry (540-553) and the telemetry arrow's flash end and phase (650-658) |
| `drawAlarmEdit` `Settings.cpp:222-334` | the limit editor's painter | 234-246 repair MIN >= MAX in the working copy `_tempAlarmConfig`; 249-250 clamp `_editFieldFocus` |
| `handleTouch` `Touch.cpp:24-1683` | the per-mode hit-test geometry, the 19 immediate draws, the `PwdKb` key geometry (about 200 of 1,270 code lines) | the gates, finger-up handling, calibration and sensitivity sampling, hold tracking, press ownership, and every `pushUiEvent` |
| `handleTouchPanelV24` `Users.cpp:1304-1686` | the hit-test lines | the gates, state edits and events; it draws nothing |
| `showTouchCalibration` `Calibration.cpp:31-62` | 49-55 reset the seven `g_*` draw caches of that file | the mode entry: resets the working state, sets `_uiMode`, raises the repaint flags |
| `showSettingsDisplayOffset` `Calibration.cpp:106-118` | 113-114 set the `_lastOffsetDraw*` sentinels | the mode entry, as above |
| `redrawTopPanel` `DisplayManager.h:949-990` | 986-989, the two draws | 950-985, the sync of `_sharedState` under the mutex and the possible `EVT_SLOT_SELECT` |

**Superseded on 2026-10-28 (S10c, stage D):** the two rows `showTouchCalibration` and `showSettingsDisplayOffset` above are the 2026-10-14 tree's. Since S10c D3a and D3b (`aea1f5f8`, `c67a1cb4`) the seven `g_*` stores run in `resetTouchCalCaches( )` behind `UiChange::TOUCH_CAL_ENTER`, and the `_lastOffsetDraw*` sentinels are set inline behind `UiChange::DISPLAY_OFFSET_ENTER`; both functions have lived in `DisplayManager_CalibrationCore.cpp` since S10c D3c (`79ac927a`) (section 2.18).

**The per-mode branches of the two touch handlers**, the work list of stages S5
to S9. The counts come from the notes of the TSV, which come from
`python3 $B/tools/ui_touch.py <worktree>`.

| handler | branch | lines | immediate draws | `pushUiEvent` | what it is |
|---|---|---|---:|---:|---|
| `handleTouch` | `DASHBOARD` | 38-51 | 1 | 0 | finger up: a short tap on the top panel flips its min/max and repaints it at once |
| `handleTouch` | `SETTINGS_TOUCH_CAL` | 61-140 | 0 | 1 | finger up: averages the 8 held points, validates, computes the window, pushes `EVT_APPLY_TOUCH_CAL` |
| `handleTouch` | `SETTINGS_TOUCH_SENS` | 159-278 | 0 | 1 | sampling of `p.z` into a stability threshold, `EVT_SAVE_TOUCH_CAL`; one layout test (CANCEL) |
| `handleTouch` | `SETTINGS_TOUCH_CAL` | 297-351 | 0 | 0 | press: hold sampling of the 8 points; one layout test (the Done/Retry area, y >= 185) |
| `handleTouch` | `isBooting` | 357-362 | 0 | 0 | not a mode: the SKIP rectangle sets `_skipPressed` while the boot screen is up |
| `handleTouch` | `DASHBOARD` | 389-396 | 0 | 0 | web-busy gate: the touch is consumed while a web client holds the device |
| `handleTouch` | `DASHBOARD` | 398-581 | 4 | 4 | press, three zones (top panel, slot panel, button bar): gestures, alarm routing, paging, slot select, graph open |
| `handleTouch` | `GRAPH_VIEW` | 583-650 | 6 | 5 | close, header tap (3 s name), five bottom buttons, central tap; pan, zoom and calendar events |
| `handleTouch` | `GRAPH_DETAIL` | 651-729 | 6 | 5 | a second copy of the graph view's close, header and bottom-bar code, plus the page cycle |
| `handleTouch` | `CALENDAR` | 731-800 | 2 | 6 | close, month arrows, the 7x6 day grid, the bottom bar |
| `handleTouch` | `SETTINGS_THEMES` | 802-829 | 0 | 1 | row and button zones; preview index, paging by 4, `EVT_APPLY_THEME` |
| `handleTouch` | `SETTINGS_ALARMS` | 830-863 | 0 | 0 | row and button zones; selection, paging by 4, jump to the sensor menu |
| `handleTouch` | `SETTINGS_ALARM_EDIT` | 864-979 | 0 | 1 | bar rows (`ALARM_EDIT_*`), arrow ends, footer; limit stepping with hold ramp, MIN/MAX interlock, `EVT_SAVE_ALARMS` |
| `handleTouch` | `handleTouchPanelV24` | 980-983 | 0 | 0 | not a mode: hands the v24 modes to `handleTouchPanelV24` |
| `handleTouch` | `SETTINGS_MAIN` | 984-1019 | 0 | 1 | row and footer zones; selection, paging by 4, `EVT_MENU_SELECT` |
| `handleTouch` | `SETTINGS_DISPLAY_OFFSET` | 1020-1091 | 0 | 1 | direction pad, reset, footer; +-4 preview clamp, live `setDisplayOffset`, `EVT_APPLY_DISPLAY_OFFSET` |
| `handleTouch` | `SETTINGS_LANG` | 1092-1129 | 0 | 1 | row and footer zones; preview index, paging, `EVT_APPLY_LANG` |
| `handleTouch` | `SETTINGS_PASSWORD` | 1132-1271 | 0 | 1 | keyboard geometry from `PasswordKeyboard.h`; phase machine, length and mismatch checks, `EVT_SAVE_PASSWORD` |
| `handleTouch` | `SETTINGS_SOUNDS` | 1274-1486 | 0 | 1 | row zones, footer, left/right half of the volume rows; melody preview, mute-all confirmation, `EVT_SAVE_SOUNDS` |
| `handleTouch` | `SETTINGS_STATUS` | 1489-1506 | 0 | 0 | footer zones; page cycling over `STATUS_PAGES` |
| `handleTouch` | `SETTINGS_LICENSE` | 1508-1540 | 0 | 0 | upper and lower half of the text, footer; page counter against `_licenseTotalPages` |
| `handleTouch` | `ALARM_ACTION` | 1543-1574 | 0 | 2 | three button bands (y 60-105, 115-160, 170-215); `EVT_ALARM_SILENCE`, `EVT_ALARM_DEACTIVATE` |
| `handleTouch` | `CONFIRM_MUTE_ALL` | 1575-1611 | 0 | 0 | two rectangles (y 190-230); the manual accept gate and the sound-settings changes |
| `handleTouch` | `CONFIRM_AP` | 1613-1630 | 0 | 1 | the same two rectangles; `EVT_START_AP` (Wi-Fi builds only) |
| `handleTouch` | `SET_CLOCK` | 1632-1673 | 0 | 1 | footer rectangles, +1/-1 bands, column test; clock entry stepping, `EVT_SET_CLOCK` |
| `handleTouch` | `OTA_UPDATE` | 1674-1682 | 0 | 0 | a tap dismisses an ending phase; no hit test |
| `handleTouchPanelV24` | `AUTH` | 1307-1407 | 0 | 0 | keypad cards, footer and `PinKb` hit tests around lockout checks; `pinSubmit` pushes the PIN events |
| `handleTouchPanelV24` | `PANEL_MESSAGE` | 1409-1412 | 0 | 0 | one tap at y >= 185 returns through `leavePanelMessage` |
| `handleTouchPanelV24` | `SETTINGS_ALARM_SENSOR` | 1414-1456 | 0 | 1 | three row bands, four footer zones; `EVT_ALARM_BLOCK`, mode entries |
| `handleTouchPanelV24` | `SETTINGS_MAINT` | 1458-1498 | 0 | 1 | footer zones and two `ALARM_EDIT_*` bars; hour and minute stepping, `EVT_MAINT_SET` |
| `handleTouchPanelV24` | `AUTH_USER+SETTINGS_USERS` | 1500-1552 | 0 | 0 | four row bands, four footer zones; selection, paging by 4, PIN entry or the user editor |
| `handleTouchPanelV24` | `SETTINGS_PIN_POLICY` | 1554-1616 | 0 | 1 | three 44 px bands, four footer zones; cycling the working policy, `EVT_PIN_POLICY` |
| `handleTouchPanelV24` | `SETTINGS_USER_EDIT` | 1619-1671 | 0 | 1 | four row bands, four footer zones; permission toggles, PIN entry, delete confirmation, `EVT_USER_PERMS` |
| `handleTouchPanelV24` | `SETTINGS_USER_CONFIRM_DEL` | 1673-1683 | 0 | 1 | y >= 195 split at x = 160: cancel, or delete with `EVT_USER_DEL` |
| **`handleTouch`, 26 rows** | | | **19** | **33** | |
| **`handleTouchPanelV24`, 8 rows** | | | **0** | **5** | |

`handleTouch` has 26 rows: the 19 branches of its mode chain, and 7 parts before
or beside the chain (finger up on the dashboard, the calibration release, the
sensitivity sampling, the calibration press, the boot skip, the web-busy gate,
and the delegation to `handleTouchPanelV24`). Together they hold **19 immediate
draws and 33 `pushUiEvent` calls**. Only DASHBOARD, GRAPH_VIEW, GRAPH_DETAIL and
CALENDAR draw immediately. `handleTouchPanelV24` has 8 rows, **no immediate
draw**, and 5 `pushUiEvent` calls (the AUTH branch reaches 2 more through
`pinSubmit`): everything it changes on the glass goes through `_repaintSettings`
and `_forceSettingsRedraw`, and the loop paints it. All 42 `pushUiEvent` call
sites in the units: 33 and 5 in the two handlers, 2 in `pinSubmit`, 1 in
`redrawTopPanel` and 1 in `loopCore1` (the alarm rotation).

### 3.4 (d) The study's glue

What the study (`study/tft-angulo-ui-a` at `6116de0`, a branch off `686a1de`
that carries the Ângulo UI as a transitional layer over the original) had to
add to the product files and to its own glue, counted on 2026-10-14. The
numbers say how much of that shape the driver split removes.

Produced by, from the worktree (nothing is checked out):

```
B=study/tft-angulo-ui-a
git grep -c -F 'UA_DESVIO( );' $B -- src
git grep -c -E 'return[[:space:]]+UaToque::TRADUZIDO\b' $B -- src/ui_angulo
git grep -c -E 'return[[:space:]]+UaToque::TRATADO\b'   $B -- src/ui_angulo
git grep -c -E 'UaToque::(TRADUZIDO|TRATADO)' $B -- src/ui_angulo   # every mention
```

The study kept its glue in `src/ui_angulo/`, which is why the commands above and
the table below name that path: they read the study branch. Since S2a the same
files are in `src/ui/angulo/` of this branch (and the kit that was
`src/angulo/` is `src/ui/angulo/kit/`).

**`UA_DESVIO( );` in the product files.** The macro was `do { uaSujo( ); return; } while ( 0 )` (S10b's C5a removed it)
in the Ângulo build and an empty statement in the others
(`DisplayManager.h:50-54` of the study). Every site is the first statement of a
draw entry point, so the old drawing code stops there and the Ângulo renderer
paints instead. The count is 37 sites in 37 distinct functions:

| product file | sites | functions |
|---|---:|---|
| `DisplayManager.cpp` | 1 | `drawSettingsLicense` |
| `DisplayManager_Alarm.cpp` | 1 | `drawAlarmAction` |
| `DisplayManager_Calendar.cpp` | 1 | `drawCalendarScreen` |
| `DisplayManager_Calibration.cpp` | 3 | `drawSettingsDisplayOffset`, `drawTouchSensitivity`, `drawTouchCalibration` |
| `DisplayManager_Dashboard.cpp` | 4 | `drawInterfaceFixed`, `drawTopBar`, `drawSlotPanel`, `drawBottomButtons` |
| `DisplayManager_Graph.cpp` | 6 | `drawLoadingScreen`, `drawGraphBusyHint`, `drawPeriodButtons`, `drawGraphHeaderBar`, `drawGraphScreen`, `drawGraphDetailScreen` |
| `DisplayManager_Settings.cpp` | 12 | `drawSettingsThemes`, `drawSettingsAlarms`, `drawAlarmStatusOnly`, `drawAlarmEdit`, `drawSettingsMain`, `drawSettingsPassword`, `drawApConfirm`, `drawClockEntry`, `drawMuteConfirm`, `drawSettingsSounds`, `drawMelodySelect`, `drawSystemStatus` |
| `DisplayManager_Users.cpp` | 8 | `drawPinScreen`, `drawAlarmSensorMenu`, `drawMaintEntry`, `drawUserList`, `drawUserEdit`, `drawUserConfirmDel`, `drawPanelMessage`, `drawPinPolicy` |
| `DisplayManager_i18n.cpp` | 1 | `drawSettingsLang` |
| **total** | **37** | 9 files |

The other hooks the study left in product files are not `UA_DESVIO` sites, and
are the list S2a imported and S4 to S10b-1 removed, all but `AppManager_Events.cpp`'s fix (section 4.5): `DisplayManager.h:41-55` and
`1562-1601`; `DisplayManager.cpp:103`, `1435`, `1739`, `1831`;
`DisplayManager_Dashboard.cpp:318`, `346`, `363` (the `_uaPintando` guards on
`fastFillRect`, `fastClearScreen` and `blitCanvas`); `DisplayManager_Touch.cpp:409-415`
(`if (uaToque(x, y)) return;`, plus the study's own fix at `:175`);
`DisplayManager_Ota.cpp:151`; `Themes.cpp:46` and `453`; `WebManager_Core.cpp:196`;
and `AppManager_Events.cpp:147` (the second fix). These are the study tree's
line numbers.

**`UaToque` results in the glue files.** A group's touch handler returns
`NAO_E_MEU` (the mode is not its own), `TRATADO` (handled in the glue; the old
per-mode branch must not run) or `TRADUZIDO` (the point was moved to where the
old layout has the same element, and the old branch then runs). `TRADUZIDO` is
the dependence on the old coordinates; `TRATADO` is business logic written a
second time in the glue, which the core actions of section 2.3 replace. **S10b (C2):** `TRADUZIDO` left the enum, and `UaToque` has a third value, `TRATADO_SEM_QUADRO` (taken, the frame unchanged: the update screen's tap, which calls `actOtaDismiss( )` itself).

| glue file (in the study, `src/ui_angulo/`) | `return UaToque::TRADUZIDO` | `return UaToque::TRATADO` | every mention |
|---|---:|---:|---:|
| `DisplayManager_UiA.cpp` | 3 | 4 | 9 |
| `DisplayManager_UiA_Ajustes.cpp` | 11 | 68 | 79 |
| `DisplayManager_UiA_Graficos.cpp` | 1 | 7 | 8 |
| `DisplayManager_UiA_Identidade.cpp` | 1 | 10 | 12 |
| `DisplayManager_UiA_Menus.cpp` | 0 | 2 | 2 |
| `DisplayManager_UiA_Painel.cpp` | 2 | 4 | 6 |
| **total** | **18** | **95** | **116** |

The plan counted 116 "return sites" by file (Ajustes 79, Identidade 12, UiA 9,
Graficos 8, Painel 6, Menus 2). Those are the mention lines of the last column.
Strictly, `return UaToque::X` statements are 113 (18 + 95); the other three
mentions are the dispatcher's two comparisons at `DisplayManager_UiA.cpp:381-382`
and one ternary at `DisplayManager_UiA_Identidade.cpp:275` that yields
`TRADUZIDO` or `TRATADO`. The plan's figure is right as a count of lines; it is
not 116 return statements. Every `NAO_E_MEU` return (one per group at its mode
test, `UiA.cpp:484` and five more) is outside both columns.

### 3.5 (e) Immediate draws in touch handling

A touch branch that paints at once, instead of raising a repaint request for
the loop, is a place where a driver that is not the original must be told. The
census was taken on `192e1ad` (2026-10-14) with the grep the plan named, and
checked with a scanner that blanks comments first and then follows the call
graph from the two handlers:

```
grep -nE '\b(draw|redraw|blit)\w*\(' src/DisplayManager_Touch.cpp src/DisplayManager_Users.cpp   # the audit's tree; since S10c D6a these draws are in src/ui/original/DisplayManager_UsersUi.cpp (since S11 A1)
python3 $B/tools/ui_touch.py <worktree>     # per-mode table; $B = the S0 scratch directory
```

The grep alone over-reports: in `DisplayManager_Users.cpp` it matches every
`drawRoundRect` and `blitCanvas` inside the `draw*` functions, and one comment
at `:1586`. Read by handler body, the result is:

- **19 immediate draws, all in `handleTouch`**, in five of its branches (table
  below). `handleTouchPanelV24` has none: its branches only raise requests
  (`_repaintSettings`, `_forceSettingsRedraw`, `_pinCardsDirty`).
- **Nothing else the branches call paints.** Forty-four functions are reachable
  from the two handlers without crossing a `draw*`, `redraw*`, `blit*` or fast
  primitive (the `act*` list of section 2.3 starts from them); none of them
  contains a draw call. `redrawTopPanel` (`DisplayManager.h:949-990`, inline) is
  the one callee that paints: it is two of the 19, and each call draws two
  things, the top card and the bottom button strip.
- **Two direct panel calls that are not draws:** `_driver.tft->setDisplayOffset( )`
  at `DisplayManager_Touch.cpp:1070` and `:1085` (MODE_SETTINGS_DISPLAY_OFFSET)
  move the controller's alignment live. They belong to the panel layer and stay
  with the core.

| mode (branch) | line | draw called | what it repaints, and why it is immediate |
|---|---|---|---|
| DASHBOARD, finger-up path | `Touch.cpp:50` | `redrawTopPanel( )` | top card and bottom buttons, after a short tap toggled the top card's min/max |
| DASHBOARD | `:509` | `redrawTopPanel( )` | the same, after the 3 s hold fired and pinned or released the top card |
| DASHBOARD | `:548` | `drawSlotPanel( bottom card )` | the bottom card, from a snapshot taken under the mutex, after its min/max toggled |
| DASHBOARD | `:566` | `drawBottomButtons( selected )` | the button strip, after the PAGE button advanced `_currentPage` |
| DASHBOARD | `:577` | `drawBottomButtons( tapped slot )` | the strip with the tapped slot highlighted, before `EVT_SLOT_SELECT` is pushed |
| GRAPH_VIEW | `:591` | `drawGraphHeaderBar( )` | the header band only: date and time become the sensor name for 3 s |
| GRAPH_VIEW | `:608` `:615` `:622` `:629` `:637` | `drawGraphBusyHint( )` | one accent line at the top edge, as "working" feedback before pan, zoom and calendar events |
| GRAPH_DETAIL | `:659` | `drawGraphHeaderBar( )` | as in GRAPH_VIEW |
| GRAPH_DETAIL | `:674` `:681` `:688` `:695` `:703` | `drawGraphBusyHint( )` | as in GRAPH_VIEW |
| CALENDAR | `:771` `:789` | `drawGraphBusyHint( )` | feedback before a day is read from `/history`, and before "Today" |

By mode: DASHBOARD 5 (4 in the pressed path and 1 on release), GRAPH_VIEW 6,
GRAPH_DETAIL 6, CALENDAR 2, total 19. By function: `drawGraphBusyHint` 12,
`drawBottomButtons` 2, `drawGraphHeaderBar` 2, `redrawTopPanel` 2,
`drawSlotPanel` 1.

**The `UiChange` kinds, sketched.** `uiChanged( kind, arg )` is called at the
exact point where the original drew; for the original, a constant `kind` folds
the `switch` into the old call, so the bytes do not move. Five kinds come from
the immediate draws above. The second group is not drawn at once today: it is
the flag a branch raises and the Core 1 loop consumes. It is listed because
a driver that paints from the whole state needs only "something changed", and
because the counts size the work: the number is how many sites set the flag to
true in the TFT units on this tree (grep of `_x = true`, 2026-10-14).
**Superseded on 2026-10-28 (S10c, stage D):** the sketch below is the 2026-10-14 one; the `UiChange` enum in `src/ui/UiDriver.h` is the record of the kinds, and it has eight at `3d433d6c` (`ALARM_FLASH`, `ALARM_RESTORE`, `BOTTOM_PANEL`, `BOTTOM_BUTTONS`, `GRAPH_HEADER`, `GRAPH_BUSY`, and the two that S10c D3 added, `TOUCH_CAL_ENTER` and `DISPLAY_OFFSET_ENTER`), counted from the source (section 2.18).

```cpp
// Said where the original painted at once. The original maps each kind to the
// call it replaces; the Ângulo driver maps every kind to "repaint".
enum class UiChange : uint8_t {
    // immediate draws (19 sites, section 3.5)
    TOP_PANEL,        // redrawTopPanel( )        top card + bottom buttons       2 sites
    BOTTOM_PANEL,     // drawSlotPanel( bottom )  bottom card                     1
    BOTTOM_BUTTONS,   // drawBottomButtons( arg ) arg = slot to highlight         2
    GRAPH_HEADER,     // drawGraphHeaderBar( )    header band of graph and detail 2
    GRAPH_BUSY,       // drawGraphBusyHint( )     accent line, "working"          12
    // deferred requests (flags the loop reads; they can stay flags for the original)
    SCREEN_NEW,       // _forceSettingsRedraw     a new screen or page            51 sites
    SCREEN_PART,      // _repaintSettings         selection or one band           114
    ALARM_WORD,       // _alarmStatusDirty        the ON/OFF word of a row        1
    CLOCK_VALUES,     // _clockValuesDirty        one band of the clock entry     1
    PIN_CARDS,        // _pinCardsDirty           the dealt keypad                2
    CACHES,           // a show* entry point resets the original's draw caches   11 entry points
    GRAPH,            // _repaintGraph            graph and detail screens        7
    CALENDAR,         // _repaintCalendar         the calendar                    2
    LOADING,          // _repaintLoading          the graph's loading screen      1
    DASH_DATA,        // _isDirty                 dashboard values                20
    DASH_FULL,        // _forceFullRedraw         the whole dashboard             12
};
```

Sixteen kinds, inside the plan's estimate of 15 to 25. `CACHES` is the one the
contract of section 2.4 adds: eleven `show*` entry points (`showSettingsThemes`,
`showSettingsAlarms`, `showAlarmEdit`, `showSettingsMain`, `showSettingsUsers`,
`showAuthUser`, `showUserEdit`, `showPinPolicy`, `showSettingsLang`,
`showSettingsDisplayOffset`, `showTouchCalibration`) write the original's draw
caches: the `_last*Page`, `_last*Sel` and `_lastOffsetDraw*` sentinels of section
3.1 group C, and the seven `g_*` caches of `DisplayManager_Calibration.cpp` (since S10c D3, 2026-10-28, the writes of
`showTouchCalibration( )` and `showSettingsDisplayOffset( )` go through `UiChange::TOUCH_CAL_ENTER` and
`UiChange::DISPLAY_OFFSET_ENTER`: section 2.18).
Sites by function kind
for the two big flags: `_repaintSettings` is raised 56 times in `handleTouch`,
23 in `handleTouchPanelV24`, 24 in the `show*` and `enterPinFor` entry points,
4 in `loopCore1` and 7 elsewhere; `_forceSettingsRedraw` 17, 4, 24, 3 and 3.
Only the first five kinds need a call at the touch site; the rest can stay as
the original wrote them until a stage decides otherwise.

### 3.6 (f) Bytes of the original driver

Measured on 2026-10-14 in the baseline images of `192e1ad`: `pico_wiretft` (the
TFT Wire, no radio) and `pico_w_release` (the Wi-Fi release). Both were built
locally, without the web override 2n, so their `.bin` files are 328 B under the
sizes recorded in `tools/flash_budget.json` (1,034,172 against 1,034,500 for
the release); no display file is affected by that override.

```
NM=~/.platformio/packages/toolchain-rp2040-earlephilhower/bin/arm-none-eabi-nm   # -S -C is what the script runs
python3 tools/bytes_original.py --src <worktree> \
    --top 25 --unmapped $BASE/pico_wiretft/firmware.elf $BASE/pico_w_release/firmware.elf
```

`tools/bytes_original.py` reads this repository's `docs/analysis/UI_DRIVERS_funcoes.tsv`
by default. `--src` needs `ui_funcs.py` (section 5) beside the script: copy it from
`$B/tools/`, or leave `--src` out and read the sums by class. With the S0 copy of
the script and with the versioned one, the output of the command above (122
lines) is the same byte for byte (`cmp`, 2026-10-14).

The script lists every symbol with `nm -S -C`, attaches each one to a row of the
TSV by its demangled name without the parameter list (clones, function-local
statics and lambdas belong to their function; the three font rows match by
prefix, so `FreeSansBold9pt8b` takes its `Bitmaps` and `Glyphs`), and adds up
the sizes by class. **Original rows are counted whole. Mixed rows are counted
apart and are never added to the original.** `--src` also estimates, for each
mixed function, its original share from the TSV's line ranges (code lines only,
comments and blanks left out); that figure is an estimate from line counts, not
a measurement, and it is printed on its own line.

| | `pico_wiretft` | `pico_w_release` |
|---|---:|---:|
| **original rows, whole (flash)** | **56,699 B** | **57,379 B** |
| text | 44,799 | 45,479 |
| rodata and data | 11,900 | 11,900 |
| of which the three GFX font tables (`FreeSansBold9/12/24`) | 11,696 | 11,696 |
| of which `Themes.cpp` (functions and the one built-in palette) | 1,744 | 1,744 |
| of which `UiWidgets.cpp` | 2,716 | 2,716 |
| of which drawing in the `DisplayManager*.cpp` units | 40,543 | 41,223 |
| RAM (`.bss`) of the original rows | 3,460 | 3,460 |
| mixed rows, whole functions, **apart** | 25,324 | 25,592 |
| their original share, estimated from code lines | about 9,900 | about 9,950 |
| core rows, for the balance | 19,656 | 19,728 |
| original-side code that lives in headers, outside table (c) | 5,428 | 5,944 |

The 3,460 B of RAM are the scratch arrays of the graph screens (2,234), the
settings screens (364) and `Themes.cpp` (860, of which the custom-theme array is
800). The built-in palette table is one theme, 56 B: the other ten themes are
`.thm` files on the file system, not in the image. The header-defined line is
the sensor panel painters and their helpers (`panelBMP280`, `panelDHT22`,
`panelDS18B20`, `minMax*`, `drawTempValue1D`, `drawMinMaxGraphBtn`, the
thermometer, drop and barometer glyphs), `themeTint` and the `PwdKb` tables;
the plan counts them with the original, and they are not rows of the TSV. In
the Wi-Fi release the string literals held by original functions are another
1,314 B at object level (summed from the `.rodata.<function>.str1.*` sections of
the compiled units, before the linker merges equal strings); the pool has no
symbols, so `nm` cannot show it.

**The mixed functions counted, with their sizes in `pico_wiretft`:**
`handleTouch` 10,900 (11,108 in the release), `loopCore1` 5,604,
`handleTouchPanelV24` 3,152 (a body of 3,040 and a clone of 112), `render`
2,552, `drawTopBar` 1,384, `drawAlarmEdit` 1,124, `redrawTopPanel` 268,
`showTouchCalibration` 208, `showSettingsDisplayOffset` 132: 25,324 B. Their
original share by code lines is 16% of `handleTouch`, 51% of `loopCore1`, 94% of
`render` and 92% of `drawTopBar`, which is where the roughly 9,900 B come from.

**What this number cannot see.** Code the compiler inlined into another
function is in the caller's size, so a small helper can hide inside a core
function; the 38 original rows with no symbol in `pico_wiretft` (37 in the
release) are constants, inlined helpers and the Wi-Fi-only screens. The merged
string pool has no symbols. The `Adafruit_GFX` 5x7 `font` table (1,280 B) is the
library's and is used by the boot terminal and the License screen, so it is not
counted.

**Rerun after S10a (2026-10-21).** The same script on the images of `7c18d00b` (`stages/s10a_c11e`, outside the repository; the nine of C12 equal them byte for byte, section 2.14), with the refreshed table and without `--src`, because the line ranges of 35 mixed rows are `192e1ad`'s (section 3.3): `python3 tools/bytes_original.py --top 25 --unmapped <elf>` on each.

| | `pico_wiretft` | `pico_w_release` | at `192e1ad` |
|---|---:|---:|---|
| original rows, whole (flash) | 56,699 B | 57,379 B | the same |
| text / rodata and data | 44,799 / 11,900 | 45,479 / 11,900 | the same |
| RAM (`.bss`) of the original rows | 3,460 B | 3,460 B | the same |
| of which drawing in the `DisplayManager*.cpp` units | 40,543 B | 41,223 B | the same |
| mixed rows, whole functions, apart | 25,168 B | 25,416 B | 25,324 / 25,592 |
| core rows, for the balance | 19,892 B | 19,964 B | 19,656 / 19,728 |

The original's whole rows sum to the same byte in both images: S4 to S10a did not change their size. The mixed functions weigh 156 B (`pico_wiretft`) and 176 B (`pico_w_release`) less, and the core rows 236 B more in both: the hooks and actions of S4 to S9 took the draws out of the mixed functions. In `pico_wiretft` the two units the splits made for the original weigh 756 B (`DisplayManager_Render.cpp`) and 632 B (`DisplayManager_i18nUi.cpp`). The original rows with no symbol in the image are 38 and 37, as in S0, and the `DisplayManager::` symbols no row accounts for are the same two inline accessors of the header (`pushUiEvent`, 80 B, and `getTopSlotIdx`, 28 B): the refreshed table leaves no other symbol of the class unmapped.

**Rerun after S10b-1 (2026-10-21).** The same script on the images of C6 (`stages/s10b_c6`, outside the repository; the nine of C7 equal them byte for byte, section 2.15), with the refreshed table and without `--src`: `python3 tools/bytes_original.py --top 25 --unmapped <elf>` on each of four images, the two Ângulo images beside the two products. Since C4a the script attributes a row by its unit as well as its name, and names the units of `[ui.original].units` that have no file symbol in an image and are not counted (eight in both Ângulo images: Calendar, Fonts, Graph, Ota, Render, i18nUi, `Themes.cpp` and `UiWidgets.cpp`).

| | `pico_wiretft` | `pico_w_release` | `pico_wiretft_angulo` | `pico2_w_angulo` |
|---|---:|---:|---:|---:|
| original rows, whole (flash) | 56,699 B | 57,379 B | **293 B** | **269 B** |
| text / rodata and data | 44,799 / 11,900 | 45,479 / 11,900 | 293 / 0 | 269 / 0 |
| RAM (`.bss`) of the original rows | 3,460 B | 3,460 B | 2 B | 2 B |
| of which drawing in the `DisplayManager*.cpp` units | 40,543 B | 41,223 B | 293 B | 269 B |
| mixed rows, whole functions, apart | 25,168 B | 25,416 B | 4,184 B | 3,640 B |
| core rows, for the balance | 19,804 B | 19,876 B | 18,574 B | 15,576 B |
| original rows with no symbol in the image | 38 | 37 | 65 | 65 |

The two products are S10a's, row for row, in the original and the mixed rows; their core rows weigh 88 B less than at S10a (19,892 -> 19,804 B and 19,964 -> 19,876 B), which came with the sync, since no S10b step changes a product image. **The Ângulo images keep 293 B (Wire) and 269 B (Wi-Fi) of original rows,** against 1,381 B in the study's image (above) and the 617 B and 593 B of C0: `buildDashLayout( )` (276 B on the Wire, 252 B on Wi-Fi), which the Ângulo's `uaMontarPainel( )` and `uaToquePainel( )` keep, and the calibration's seven cache objects (17 B of flash and 2 B of RAM), which `showTouchCalibration( )` keeps; both are S10b-2's (section 4.4). The mixed functions the Ângulo images keep weigh 4,184 B and 3,640 B, against the 21,484 B of the study's Ângulo: `handleTouch( )` is the Ângulo's own gate now (949 instructions on the Wire) and the chain left with `handleTouchPanelV24( )`. **The gap moved, and one limit lifted.** The study's Ângulo was 45,856 B above its plain `pico_wiretft`; the product Ângulo is 34,256 B above (`.bin` 820,928 against 786,672 B), and `pico2_w_angulo`, a real Wi-Fi Ângulo build, is 31,008 B above `pico2_w_release` (`.bin` 1,029,952 against 998,944 B) with 538,575 B of OTA headroom. The Pico W's Wi-Fi release (5,195 B of headroom in the same record) has no Ângulo build, and a transposition of the Pico 2 W's 31 kB would not fit it: the maintainer's decision of 2026-10-21, a smaller version later, answers it (section 2.15). **Superseded on 2026-10-28 (S10c, stage D):** the Ângulo images keep no original row at `3d433d6c`. S10c D1 (`8bf2db72`) moved `buildDashLayout( )` out (293 B -> 17 B on the Wire and 269 B -> 17 B on Wi-Fi), S10c D3a (`aea1f5f8`) took the 17 B of flash and 2 B of RAM of the seven cache objects to 0, and `bytes_original.py` run on 2026-10-28 on the two Ângulo ELFs of `stages/s10c_d7b/imgs_final` reads 0 B of flash and 0 B of RAM in original rows and 3,844 B (Wire) and 3,374 B (Wi-Fi) in mixed rows (section 2.18).

**Does it support the plan's "about 31 kB"?** The plan says the Ângulo image fits
the Wi-Fi release only if the original's code is at least about 31 kB. Read as
a size, that holds with room to spare: the original rows are 57.4 kB in the
release, 1.85 times the figure, and 63.3 kB with the header-defined painters.
Read as the number of bytes the split can still take out of an Ângulo image, it
does not hold, because the study's Ângulo image already leaves almost all of
it out. Measured on the study's own images (`synth_20261014`, branch
`study/tft-angulo-ui-a`; the Ângulo ELF was built at 17:15 on 2026-10-14, two
minutes before `6116de0` was committed):

- The study's plain `pico_wiretft` has the same sha256 as this baseline
  (`4f5a868b...`, 785,408 B), so the two compare. The study's `pico_wiretft_ui_a`
  is 831,264 B, **+45,856 B**.
- Of the original rows, **1,381 B** are still in the Ângulo ELF (against 56,699).
  Each `UA_DESVIO( );` returns at the top of a draw entry point, so the compiler
  drops the unreachable rest of the body, and `--gc-sections` then drops what only
  those bodies used: 88 symbols totalling 32,368 B are gone (the font tables,
  `uiMenuIcon`, the sensor painters, `drawOtaScreen`, `drawPeriodButtons`), and 56
  common symbols shrank by a net 33,145 B (`drawGraphScreen` -3,212,
  `drawGraphDetailScreen` -3,164, `drawSettingsPassword` -1,896).
- What the Ângulo adds is large: **382 new symbols, 104,889 B** (text 67,075, data
  37,814): the `ua::` renderers 50,897, the `entrega::` font and icon data 33,033,
  the `DisplayManager::ua*` glue 17,420. Net of the two reductions the sized
  symbols grow by 39,376 B; the other 6,480 B of the `.bin` growth are the string
  pool, padding and tables without symbols.
- The Wi-Fi release has **5,643 B** of OTA headroom (`PLANO_STABLE.md` section 3.1:
  ceiling 1,040,384 B against a signed image of 1,034,741 B). The study's growth
  exceeds it by 33,733 B counted by symbols and by 40,213 B counted by `.bin`.
  That gap, 34 to 40 kB, is the nearest thing in the data to the plan's "about 31
  kB"; it is what has to be found, not something the original's code can still
  give, because that code is already out.
- What the split can still remove from an Ângulo image is the residual 1,381 B
  and the original share of the mixed functions, about 9,900 B by line count in
  the baseline (the Ângulo ELF keeps 21,484 B of mixed functions in whole): about
  11 kB. The gap would remain at about 22 to 29 kB.

So the measurement supports that the original is big, and does not support that
removing it closes the gap, because the study's shape already removed it. This
contradicts the plan's section 2, which rejects the study's shape because it "links
all the old drawing code": by the ELF it links 1.4 kB of it, and what it still
carries is the touch translation and the mixed functions. Whether the Ângulo
fits the Wi-Fi release depends on the Ângulo's own 105 kB, not on the
original; S12 should plan for the refusal path. Two limits on this: the
Ângulo ELF is the TFT Wire product and not a Wi-Fi release (there is no Ângulo
build of `pico_w_release` among the artefacts, so the Wi-Fi figures are a
transposition), and the Ângulo is still changing, so its own size is a moving
number.

## 4. Where the code contradicts the plan, and what else the audit found

### 4.1 The plan against the code

Each item says what the code says. `PLANO.md` is the planner's text of 2026-10-14
in the S0 scratch directory; it is not in the repository.

1. **The study's shape does not link the old draw code.** The plan's section 2 rejects
   it because it "links all the old drawing code, +22.4 kB on the Pico W
   release against 7.3 kB of OTA headroom". The ELF of the study's Ângulo image
   keeps 1,381 B of the original's 56,699 B (section 3.6): `UA_DESVIO( );` makes
   the rest of each draw body unreachable and `--gc-sections` drops what only
   those bodies used. What the shape links is the touch translation and the
   mixed functions. Its growth on `pico_wiretft` is +45,856 B, not +22.4 kB (another
   product, and the plan names no build for its figure, so the two do not compare), and the Wi-Fi
   release's headroom is 5,643 B (`PLANO_STABLE.md` section 3.1), not 7.3 kB. The
   plan's "about 31 kB" is closer to the gap the Ângulo has to close, 34 to 40 kB,
   than to a saving the split can still make, about 11 kB.
2. **The starting list for table (a)** has `_lastRenderedState` at two sites and
   `_forceFullRedraw` at one; `render` writes the first at four (`:1814, 1894,
   1915, 1991`) and the second at two (`:1901, 1914`). `showMinMax` is a field of
   `DashPanel`. The plan's line numbers are the study tree's: `render` sits 53
   lines lower there and `Touch.cpp` 19 lines lower than on `192e1ad`.
3. **The plan's seven duties** (its section 3.4) are not all of them: section 2.4 adds
   five (items 8 to 12).
4. **`uiBegin( )`.** The plan gives its body as "mark everything dirty". On `192e1ad`
   the first-init branch holds the first paint (`DisplayManager.cpp:1287-1289`,
   original) and the resume branch the dirty mark (`:1294-1297`, core). The hook
   has to hold the three painting lines.
5. **The 116 `UaToque` "return sites"** are 116 mention lines. Return statements are
   113 (18 `TRADUZIDO`, 95 `TRATADO`); the rest are the dispatcher's two
   comparisons and one ternary (section 3.4).
6. **The groups of S6 and S9 cut `handleTouchPanelV24` in two.** The plan puts
   PASSWORD with `handleTouchPanelV24` for S9; PASSWORD is a branch of `handleTouch`
   (`Touch.cpp:1132-1271`). And S6 lists PANEL_MESSAGE and USER_CONFIRM_DEL
   among the base and menu modes, but both are branches of `handleTouchPanelV24`
   (`Users.cpp:1409-1412`, `:1673-1683`), the function S9 takes. That function
   has eight branches: AUTH, PANEL_MESSAGE, ALARM_SENSOR, MAINT, AUTH_USER with
   USERS, PIN_POLICY, USER_EDIT and USER_CONFIRM_DEL. S6 took the two it lists
   (section 2.9), S8b took MAINT and PIN_POLICY (section 2.12), and S9 took the other four, AUTH, ALARM_SENSOR,
   AUTH_USER with USERS and USER_EDIT, with PASSWORD from `handleTouch( )` (section 2.13).
7. **The mixed units are no longer unverified** (section 3.3). `DisplayManager_Auth.cpp`
   holds no authentication: its nine rows are all core (section 4.2).
8. **`redrawTopPanel` is defined in `DisplayManager.h`**, is mixed, and is called from
   two places in `handleTouch`; the plan lists unit files only.

### 4.2 What the plan does not mention

- **Themes.cpp is called from six files outside the display**, on 17 lines
  (section 3.2): `AppManager_Boot.cpp`, `AppManager_Events.cpp`,
  `AppManager_Commands.cpp`, `WebManager_Api.cpp`, `WebManager_Commit.cpp` and
  `WebManager_Files.cpp`. They need the catalogue (`getThemeCount`, `getThemeId`,
  `getThemePalette( i )->displayName` for the web API), a lookup by name
  (`getThemeIndexByName`, for the CLI), a rescan (`scanCustomThemes`, at boot and
  after a `.thm` is uploaded or deleted) and the apply (`loadTheme`). The plan's
  three hooks cover the first need only.
- **Geometry is written twice, once for the painter and once for the touch**, in
  places where nothing ties the two. The header's promise that hit zones are
  derived from the drawn geometry holds for `ALARM_EDIT_*`, `CLOCK_*`,
  `GRAPH_PBTN_*` and `CARD_*`, and not for these:
  - the row bands `y < 80`, `y < 118`, `y < 156` as literals, in 7 places of 6
    branches of `handleTouch` (`Touch.cpp:805, 833, 919, 987, 1098, 1283-1284,
    1379-1380`) and 3 of `handleTouchPanelV24` (`Users.cpp:1432, 1504, 1639`),
    where the painters place the rows from `ALARM_EDIT_Y0` (40) and
    `ALARM_EDIT_STEP` (38), at 40, 78, 116 and 154: the literals are those starts
    plus 2;
  - "4 rows per page" in the core lines of the touch (`Touch.cpp:806, 809, 816, 820,
    834, 845, 853, 857, 988, 991, 998, 1003, 1099, 1103, 1111, 1117, 1279, 1285`),
    besides the clamps of section 3.1 group B;
  - the nine items of the sound screen: `TOTAL_SOUND_ITEMS = 9` at `Touch.cpp:1374`
    against `TOTAL_ITEMS = 9` at `Settings.cpp:1055`, and the row-to-event and
    row-to-setting `case` numbers in the painter's order (`:1292-1355, 1398-1430`,
    the five-case preview switch three times at `:1292, 1307, 1321`);
  - the alarm-action bands y 60-105, 115-160 and 170-215 (`Touch.cpp:1544, 1552,
    1560`) against the three buttons drawn at 60, 115 and 170 (`Alarm.cpp`,
    `drawAlarmAction`);
  - the confirm rectangles x 20-150 and 170-300 at y 190-230 (`Touch.cpp:1577-1585`,
    `:1615-1620`) against the buttons drawn at 20 and 170, y 190, 130 by 40
    (`Settings.cpp:840-841, 955-956, 1025-1034`);
  - the calibration window, a 20 px inset with spans of 280 and 200 (`Touch.cpp:112-123`),
    which mirrored `CAL_SCR_X/Y` = {20, 300}, {20, 220} (`DisplayManager.h:1434-1435`)
    where the crosshairs are drawn; since S3 (2026-10-15) both come from `CAL_INSET`
    and the panel's size, so this one is tied;
  - GRAPH_VIEW and GRAPH_DETAIL carry the same close, header-tap and bottom-bar code,
    55 lines apiece, identical but for three comments (`Touch.cpp:585-642` and
    `:653-708`, compared after whitespace normalisation).
- **The PIN keypad's hit-test geometry is in `PinKeypad.h`**, beside the policy and
  the matching, and that header is included by `SystemDefs_Validate.h`,
  `SystemDefs_Records.h`, `StorageManager.*`, `WebManager_Api.cpp`,
  `WebManager_History.cpp`, `AppManager_CmdHandlers.cpp` and `AppManager_Panel.cpp`.
  S9 did not split it (decision D8, 2026-10-21). After S9 the Ângulo's touch reads none
  of the geometry, but the original UI is not its only reader: `GET /api/keypad`
  (`WebManager::handleApiKeypad( )`, `WebManager_History.cpp`) publishes the layout, the
  grid of `pinGrid( )` and the popup's `POP_KEY_W`, `POP_KEY_H`, `POP_GAP`, `POP_ONE_Y`,
  `POP_ROW0_Y` and `POP_ROW1_Y`, so that the bench can tap the cards (on the original panel's images only since S12's F4, 2026-10-31: the Ângulo's answer has no grid and no popup, and names `/api/ui`); `WebManager_Api.cpp`
  and the CLI read the policy only (`maxLenFor( )`, `ALPHA_ALNUM`). A split moves geometry
  that a product route reads, and the link order of five product units with it. S10a did not take it (2026-10-21): the names that only the original reads (D3's expected list: `keyAt`, `keyX`, `keyY`, `slotX`, `slotW`, the `popup*` functions and `POP_CARD_*`, `DOTS_*` and `FOOT_*`) are read by the draws that stay in `DisplayManager_Users.cpp`, so splitting the header now would leave a mixed unit including an original header, for no unit made pure. The split was done in S10c C8 (`c5a26bcc`, 2026-10-28), after S10c D6a moved the Users draws: `pinkb_readers.py` on `fef95de0` read the 76 names of `PinKb`, 62 of which stay, 12 original-only and 2 read only by a test or a tool (`decoyCount` and `slotX`). The 12 moved to `src/ui/original/PinKeypadLayout.h` token for token, and `slotX` with them because it calls `keyX`; `PinKeypad.h` keeps the policy and the published geometry. **Recorded deviation:** fourteen of the 26 names the plan expected to move stay (`keyAt`, `popupKeyAt`, `popupRows`, `popupRowKeys`, `popupRowY`, `popupRowX0`, `FOOT_Y`, `FOOT_H`, `FOOT_BACK_X`, `FOOT_BACK_W`, `FOOT_EXIT_X`, `FOOT_EXIT_W`, `FOOT_OK_X` and `FOOT_OK_W`), because `handleTouchPanelV24( )` reads them and its row in the function table is mixed; moving them means reclassifying that function, a code change left for S11 (section 2.18); S11 A3 tried it on 2026-10-30 and did not keep it, six images changed (section 4.7): left for S12.
- **The panel size, 320 by 240, was written into core primitives** (S0, on `192e1ad`):
  `Dashboard.cpp:272, 296` (the DMA window checks), `:327-328` (the clamp in
  `fastFillRect`), `:345-346` (`fastClearScreen`), `:424, 445, 454` (the strip
  blits), `DisplayManager.cpp:1258` (`GFXcanvas16( 320, 45 )`),
  `Calibration.cpp:416-417` (`mapTouchPoint`) and the calibration targets. S3
  (2026-10-15) moved them to `DisplayDriver::W` and `H`, except the strip blits,
  which are the original's layout; section 2.6 has the list as it stands.
- **Text outside `tr( )`.** `drawCalendarScreen` draws Portuguese month names and weekday
  initials (`Calendar.cpp:45-48`); `drawTopBar` writes `"WEB '%s' - toque bloqueado"`
  (`Dashboard.cpp:487`); and five functions choose between two literals with
  `isPt` (`_currentLangIdx == LANG_PT`, `LANG_PT` being 1,
  `SystemDefs_Records.h:71`): `drawApConfirm` 5, `drawClockEntry` 2,
  `drawMuteConfirm` 6, `drawSettingsSounds` 5 and `drawMelodySelect` 2 uses. None
  of that text goes through the language pack. What an es-ES pack selects here
  was not checked.
- **Dead code.** `drawSlotPanel` ends both branches of its `if` with `goto
  _slot_bottom_fill` (`Dashboard.cpp:920, 932`), and the 97 lines up to the label
  at `:1030` have no label of their own and no way in. The compiler drops them,
  so they cost no bytes, but a reader must still check them. `fixCardCorners`
  (`Dashboard.cpp:55-69`) and `drawGraphIcon` (`Graph.cpp:372-377`) have no
  caller; only comments and the declarations mention them. Neither has a symbol
  in the images.
- **Strings and flash inside paint bodies, against duty 7.** The rule is written at
  `DisplayManager.cpp:1412-1414`, and the original breaks it in 10 bodies
  (section 2.4, item 7): 16 `String` objects in 9 bodies and one temporary in
  `drawSystemStatus`. It does not read flash: `StorageManager::getBoardSerialNumber( )`
  calls `pico_get_unique_board_id`, which the SDK fills once in a constructor
  before `main`, and `tr( )` answers from RAM. The count of heap allocations
  depends on `String` having no small-string buffer, read from the header and
  not measured.
- **`DisplayManager_Auth.cpp` has no authentication in it.** Its nine definitions are the
  GRAM read-back behind the screenshot and the mirror (`readPixel`, `readRect`,
  `readRectDmaStart`, `readRectDmaFinish`, `convert3to565`, `readRow`, the two DMA
  channel getters) and `fastRandom`, which the PIN shuffle uses. All nine are core
  rows.

### 4.3 The build environment: override 2n

The baseline images of this audit were built locally, against the shared
`~/.platformio`, which carries the framework override 2m and not 2n
(`webserver_fewer_writes.patch`, #189). The sizes recorded in
`tools/flash_budget.json` for `192e1ad` come from a PlatformIO core that carries
both (`PLATFORMIO_CORE_DIR`, a hardlinked copy patched by
`tools/arduino_pico_overrides/patch.sh`). The local `.bin` is below the record by
328 B on `pico_w_release`, `pico_w_test`, `pico_w_asserts`, `pico_wire` and
`pico_wiretft`, by 296 B on `pico2_w_release`, by 4,096 B on `pico_w_air` (its 4 KB
step) and equal on `pico_w_alpha`. Every comparison in this work is local against
local (the nine sha256 in `$BASE`; the study's plain `pico_wiretft` has the same
sha256 as the baseline's, so it was built the same way), so those hold. Two figures mix the sources and are off by at most 328 B in 40 kB:
the headroom of 5,643 B in section 3.6 is the CI record, and the local release
has 5,971 B. **A budget entry taken from a local build would be about 328 B under
what CI measures, and the gate would fail the Ângulo image in CI. That must be
settled before S2b writes a `flash_budget.json` entry**: either every
measurement of the branch is made against a core patched as CI's is, or the
numbers come from CI.

### 4.4 Open points

- Whether the Ângulo is meant for the Wi-Fi release at all. The measurement (section
  3.6) puts the gap at 34 to 40 kB and the split's remaining saving at about 11
  kB; S12's refusal path is the likely outcome unless the Ângulo's own 105 kB
  shrinks. **Answered by the maintainer on 2026-10-21:** the Ângulo ships first on the Pico 2 W only, and a smaller or optimized version for the Pico W comes later; section 2.15 has what that changes.
- The `String` use of item 7: leave it in the original (it keeps its bytes) or
  remove it in a separate fix.
- Whether the five readers of section 5 move into `tools/` too and which of the
  tables are re-run as a gate. Three scripts moved in S2b (2026-10-14); none of
  the tables is a gate yet. S0 itself changed no file under `src/`, `tools/` or
  any generated file.
- Nothing here was run on hardware: the bench was not touched. Every number
  is from a build, an ELF or the source.

**S8b, B1: closed in S10a (2026-10-21).** S8B_PLANO.md moved the editor's step (the three lambdas of `handleTouch( )` and the hold's ramp) into `actLimitStep( )` and dropped the Ângulo's `passoLimite( )`. S8b built three forms and none stayed inside the step's list: the lambdas moved word for word left one closure out of line in 7 of the 10 images; marked always_inline they moved `handleTouch( )` and brought `sensorLimitChannel( )` out of line; always_inline and flatten moved the Ângulo's `uaMontarAjustes( )`. The step stayed a copy until S10a took it in the form S9b had measured, Fb (section 2.14): `limitStepOn( )` holds the lambdas, `actLimitStep( )` the rest, `passoLimite( )` and `separarLimites( )` are gone, and the six TFT images changed in `handleTouch( )` alone, which the bench saw as nothing (O1, A1). What S8b left open: (a) the Ângulo's builder wrote the working copy that Core 0 reads after Salvar; since C3 it only reads, and the read itself is F3 below; the interlock can still leave MIN equal to MAX at a channel's sane limit, and the original's draw repairs the copy on a repaint request, as before; (b) the plan's zeros for `passoLimite` and `separarLimites` in the Ângulo hold now; (c) closed 2026-10-16 (`limitStepMag( )`, C9); (d) and (e): the glue check runs the step case and the builder's two checks since C3.

**S9: what it found and left open (2026-10-21).** None of these is S9's to change: each was so before S9, which moved the code around them word for word.

- **S8b's limits step** (above) is not S9's either (decision D7): a stage S9b takes B1, C4 and the two glue mutants that wait for them.
- **The keypad and `_stateMutex`.** `scramblePinKeys( )` and `pinAppendChar( )` say "Called with _stateMutex held" (the comments above them in `DisplayManager_Users.cpp`); the touch path calls them without it (`DisplayManager_Actions.h`, the keypad's actions), as `handleTouchPanelV24( )` did before. The keypad's actions stay off the entry while `_pinWaiting` says Core 0 is reading it (`pinInputBlocked( )`); whether Cancelar's `clearEnteredPin( )` in that window can race Core 0's read is not settled (unverified).
- **The text keyboard's buffers outlive it.** `showSettingsPassword( )` wipes `_kbBuffer` and `_kbConfirmBuf` when the keyboard opens, and the result screen wipes them for another try; leaving by X, or after a saved PIN, wipes neither, so the text stays in RAM until the keyboard opens again. Core 0's `newPwd[16]` (`AppManager_Events.cpp:486-488`) is not wiped after `panelSetUserPin( )`, which takes it as `const char *` and cannot. The legacy PIN's keyboard opens only from the CLI's `screen pwd`.
- **`_pinCardsDirty`** is set by the keypad's actions and read only by the original's paint (`drawPinScreen( )`); the Ângulo never reads it, and repaints from `_repaintSettings`.
- **The Ângulo's view-model builder clamps `_usersPage`** itself (`uaMontarIdentidade( )`, the account lists' case): duty 10's page clamp, whose hook (`uiPages( )`) does not exist yet.
- **`GET /api/keypad` on the Ângulo image (closed on 2026-10-31 by S12's F4, below).** It published the original's grid and popup geometry (section 4.2), not the keys on the glass; a client that taps by it would miss the Ângulo's keys (unverified on the bench).
- **The Ângulo's two guards (D3).** The sensor menu tests the slot before the permission, so an unprivileged tap on an invalid slot no longer beeps; the menu always has a valid slot, so no tap reaches that case. Definir PIN and Excluir stay off a new account because `alvosConta( )` omits them (`telas/IdentidadeContas.cpp:292`), where the original has no such rows (`userEditRowCount( )` gives 3).

**S10a: what it found and left open (2026-10-21).** None of these is S10a's to change, or each was left on purpose; section 2.14 has the numbers.

- **D6, the chain (closed in S10b, C1 and C3: option (3), the fragment, measured byte-neutral by P1; section 2.15).** `handleTouch( )`'s mode chain cannot become an always_inline `uiTouch( )` without changing `handleTouch( )` in all seven images (P2, P2r, P2f and P2e). S10a ran on D6's default, to defer it to S10b, where the chain becomes the hook with the Ângulo's own `uiTouch( )`; the other options (take it now, by const reference, with a bench check; or include the text as a fragment inside `handleTouch( )`, unmeasured) wait for the maintainer.
- **The D5 units** (S10b-1 did not resolve them: it measured each one's keepers, and the splits are S10b-2's, below), whole until S10b-2 resolves each with a hook or a `UiChange` in place of the write across the cut: `DisplayManager_Users.cpp` (`PinKb::orderedFace( )`), `DisplayManager_Alarm.cpp` (`isSlotAlarming( )`), `drawSettingsLicense( )` (`_licOpening`), Calibration's two `show*` functions (the `g_*` caches) and `showApConfirm( )`. With them waited C7 (the Users draws), C8 (`PinKeypad.h`, section 4.2) and C11a. **Closed on 2026-10-28 (S10c, stage D), one unit at a time:** `DisplayManager_Users.cpp` (`PinKb::orderedFace( )`) by S10c D6a (`5f04277d`), whose move is C7; `DisplayManager_Alarm.cpp` (`isSlotAlarming( )`) by S10c D5 (`87875859`), whose move of the three alarm draws to `DisplayManager_AlarmUi.cpp` is C11a of the S10a plan; `drawSettingsLicense( )` (`_licOpening`) by S10c D4b-1 and D4b-2 (`a26d1199`, `6c216cc6`); Calibration's two `show*` functions (the `g_*` caches) by S10c D3a to D3c (`aea1f5f8`, `c67a1cb4`, `79ac927a`); `showApConfirm( )` by S10c D2 (`9fae2fa7`); and C8 (`PinKeypad.h`) by S10c C8 (`c5a26bcc`). S4's C11a (`1bbc4614`, the packet arrow) is another item, done then (section 2.18).
- **`pico_w_test_https` has 12 B of real flash left** (measured 2026-10-21 with `readelf -l`): the last LOAD ends at 0x100FEFF4 and the FLASH region at 0x100FF000 (`memmap_default.ld`, length 1,044,480); at C0 the end was 0x100FEF9C, 100 B free, and C2's 88 B took most of it. PlatformIO's line ("used 1,032,436 bytes from 1,044,480") sums sections and reads as 12 KB free: the real headroom is the region's end minus the last LOAD's end. The image is `ota_exempt` and not a required check, but CI builds it, and the Users split added 160 B and it no longer linked. Every later step holds it within those 12 B: a split that is not byte-neutral cannot go in. **Since the sync of 2026-10-21 (S10b-0) it has 36 B** (#284 took 24 B from the image; the last LOAD ends at 0x100FEFDC), and no step of S10b-1 changed it. **Since the sync of 2026-10-23 (S10c stage D, merge `262962f5`) it has 4,300 B:** main's #288 serves its licence page from LittleFS, so the last LOAD ends at 0x100FDF34 (`readelf -l` on the merged image; main's own image ends there too). The image shrank by 4,264 B, not main's 4,256. Seven of the 8 B are the licence blob it dropped, 4,267 B on the branch and 4,260 B on main (`nm -S`, `LICENSE_PAGE_GZ`; the next symbol follows it with no padding in both); the eighth is not decoded. That is the room the S10b-2 splits waited for.
- **F3 (closed on 2026-10-31 by #307, merged into `main` as `aaccce35`; the save checked on both panels on the bench the same day, `docs/VERIFICATION.md`).** `AppManager::panelSaveAlarmLimits( )` read the live working copy by reference after Salvar, with no copy and no lock (S8b's finding (a); the writers of the copy run on Core 1). Salvar now posts a copy into a one-entry box (`src/display/AlarmLimitsSnap.h`, `DisplayManager::_limitsSaved`) and Core 0 takes it out under `_stateMutex`; `editedAlarmRecord( )` is gone. The limit that stays, and the flash cost of each image, are in the CHANGELOG's Unreleased section.
- **F4 (closed on 2026-10-31, S12).** The Ângulo's `GET /api/keypad` published the original's grid and popup geometry (decision D3's record), not the keys on the glass: a route question for S10b or S12. The maintainer chose option (c) on 2026-10-31: on the Ângulo the route sends `faces`, `up`, `kb` and `policy` and `"targets":"/api/ui"`, and no `grid` and no `pop`; on the original the answer is what it was, byte for byte. The switch is a UI hook, `uiKeypadTargets( )` (`nullptr` in `src/ui/UiDriverMembers.h`, `"/api/ui"` in `src/ui/angulo/Members.h`), a constant on each side, so each image carries only its own answer's format strings; the nine other images keep their sha256 and the two Ângulo images shrink (the CHANGELOG's Unreleased section has the table). The two bench tools that tap the original panel by the grid read the route through `tools/keypad_answer.py` and refuse the Ângulo's answer. Not checked on a board.
- **Dead code found by the review of C2.** A host run over all 256 type bytes shows that no sensor type has zero limits, so the Ângulo's fallback of 2 and the original's zero return in the step are never taken; they are kept as written.
- **Line citations that remain** outside C12's list: `DisplayManager_Touch.cpp:1684` in `src/ui/angulo/telas/GraficosOta.cpp` (the file ends before that line since S5 to S10a) and `Touch.cpp:117` in `src/AppManager_Commands.cpp`; they become names the day someone next touches them. A sweep of the Living documents for line citations whose line S10a changed (each cited line at `331266dc` against the tip, 2026-10-21) found seven: the two of `DisplayManager_Dashboard.cpp:243-247` and `:279-318` in `MODELO_DE_RECURSOS.md` (R-H16 and the display's resources row), true at `331266dc` and false since C5, and two stub lines in the notes of this table, now names; the other three, `DisplayManager.cpp:1106-1167`, `:1209` and `:1399-1630` in `MODELO_DE_RECURSOS.md`, were already false at `331266dc`, as are its `DisplayManager.cpp:200-228` citations and `DisplayManager_Touch.cpp:393-1640` (the lines drifted with S4 to S9 and with main). Recorded for main, not changed here: that document's R-D10 says the bridge `simutStateMutexHeldByCurrentCore( )` exists only in `DisplayManager.cpp`, which the Air excludes; since 2026-09-26 it is in `src/ConcurrencyAsserts.cpp` (that file's header says so), so the rule's reason no longer holds as written.

**S10b-1: what it found and left open (2026-10-21).** None of these is S10b-1's to change, or each was left on purpose; section 2.15 has the numbers.

- **S10b-2: the D5 units and their keepers** (P4, section 2.15). Settings (`showApConfirm( )`, on the Wi-Fi image), Dashboard (`buildDashLayout( )`), Calibration (the two `show*` functions, with a `UiChange` that resets the seven `g_*` caches), Auth (`fastRandom( )` and the screen stream's `readRect*`, `convert3to565` and `readRow`), Touch (`loopCore1( )` -> `handleTouch( )`), Users (+160 B when `PinKb::orderedFace( )` is inlined, which https cannot take), Alarm (+2 instructions in `redrawAlarmFlash( )`, which only a decoded diff can prove, since no alarm may be set off on the bench) and `DisplayManager.cpp` (`drawSettingsLicense( )` and `_licOpening`, the class's own references). Each is probed alone, the way S10a's P3 was, and then joins `[ui.original].units`; C8 (`PinKeypad.h`, section 4.2) depends on Users. `buildDashLayout( )` goes to `DisplayManager_Panel.cpp` after `inline_audit.py` (`DisplayManager_Dashboard.cpp` calls it), and `showApConfirm( )` to `DisplayManager_SettingsCore.cpp` with its own `#if SIMUT_NET_WIFI` (sprawl +1). Every one changes products, by link order or by code, and part of it waits for room on https (36 B). `DisplayManager_Panel.cpp` has no original row and was not probed. **Since 2026-10-23 (S10c stage D):** https has 4,300 B after the sync with main; the six moves were probed on d907fd58 (D0: the LCD3 kept their sha256 in all six; D2 moves by link order only; D1, D3, D5 and D6 change code, D4 no function); and **Dashboard is done** (S10c D1, `8bf2db72`): `buildDashLayout( )` lives in `DisplayManager_Panel.cpp`, Dashboard.cpp is in `[ui.original].units`, one branch of `drawBottomButtons( )` changed in the six TFT products, equivalent because the function returns 0 to 5. **The alarm screen's three draw functions are done** (S10c D5, `87875859`): they live in `DisplayManager_AlarmUi.cpp`, a unit of the original in `[ui.original].units` and in both LCD exclusion lists. In the products `redrawAlarmFlash( )` now reloads `_lastRenderedState.topSlotIdx` after a call (+8 B on pico_w_release and pico_w_test_https, +24 B on pico2_w_release), equivalent because only Core 1 writes it, in the same loop iteration. The Ângulo never linked the three, so its bytes of the original stay 17 B; its two images keep their sizes, and the bytes that differ are veneers the linker reordered (`veneer_proof.py`: OTHER 0). **Since 2026-10-28 stage D has done the rest (S10c, section 2.18):** Settings by S10c D2 (`9fae2fa7`: `showApConfirm( )` in `DisplayManager_SettingsCore.cpp`, `DisplayManager_Settings.cpp` in `[ui.original].units`, the sprawl +1), Calibration by S10c D3 (`aea1f5f8`, `c67a1cb4`, `79ac927a`: the two `UiChange` kinds that reset the caches, then both functions in `DisplayManager_CalibrationCore.cpp`), the licence by S10c D4 (`a26d1199`, `6c216cc6`, `1f2ea33a`: `_licOpening` with external linkage, then `drawSettingsLicense( )` in `DisplayManager_LicenseUi.cpp`) and Users by S10c D6a and D6b (`5f04277d`, `fef95de0`: the draws in `DisplayManager_UsersUi.cpp`, the products changed by code and proved by the decoded argument and the bench), then C8 (`c5a26bcc`). Auth and Touch were not among the six moves. `[ui.original].units` lists fourteen units at `3d433d6c`. What remains is the hit-test names that `PinKeypad.h` keeps, for S11, and Touch's own link: two of the 12 allowances that `tools/check_ui_link.py` still has (counted 2026-10-28) are `DisplayManager::handleTouch()` and `DisplayManager::loopCore1()` (`loopCore1( )` -> `handleTouch( )`).
- **F4 (closed on 2026-10-31 in S12, see S10a's entry above; it was open here):** the Ângulo's `GET /api/keypad` published the original's grid and popup geometry, not the keys on the glass (S10a's D3 record). The Ângulo's touch no longer links the original's keypad geometry, but the route did, and it was a route question for S10b-2 or S12. **On 2026-10-28:** S10b-2 ended with stage D (section 2.18) without taking it, and S12 took it.
- **F5 (closed: main's #299, 96a5af06, in v2.12.0, brought here by sync 8; the Files page now reads the status and says a refused upload was refused, `tools/test_webui_upload_result.py`). As it was recorded: a PR to main: the web page shows a refused upload as "Upload complete."** `doUpload( )` (`WebUI.h:6364-6367`) awaits `fetchSafe( )` (`:7973`), which throws only for a status of 500 or more (503 excepted), and never reads `resp.ok`; the device's 400 `{"error":"Invalid upload"}` therefore ends in the green toast (the page's own code run in node: a 400 gives the success toast, a 500 the error). Every existing 400 refusal does the same on all nine products, so `WebUI.h` is shared and the fix is main's, not this branch's. Until then bench B4's acceptance is the log record and the file listing unchanged, not the toast.
- **The `.thm` refusal's answer (closed in S10c A', 2026-10-22: the refusal is gone, section 2.16).** The wire carries the generic 400 and the reason goes to the log only; the Ângulo's microcopy rule wants the user told what happened and what to do, a follow-up for the page or the route. Its text also names "Settings > Themes" and "Light, Dark or Automatic", labels the Ângulo does not show (its menu item is the Portuguese literal `Tema`, and `/api/themes` lists `Claro`, `Escuro` and `Automático` in every language): align them when the Ângulo's texts go through the packs (the plan's open question 1). The manual's list of upload refusals (`cap26-api-rest.md`, line 1133) does not name the `.thm` one. Recorded, not changed: the refusal is case-sensitive like the original's scan (`X.THM` passes); `a.thm\0x` passes `isSafeUploadFilename( )` and could put a `.thm` in LittleFS (it needs `PERM_FILE_UPLOAD`, nothing on the Ângulo reads it, and it was not run through the real parser); a multiple selection resets `_uploadRejected` per file. Bench B4 has not run. **Closed:** A'4 removed the refusal at the maintainer's word ("Só serve à original"), so a `.thm` upload to the Ângulo is answered 200 like any file, the log holds the upload's `SEC_FILE_UPLOAD` and no rescan record (A'5 saw it on the Pico 2 W), and `7bb0d94e` is out of both packs. With no refusal, the points above (the labels in its text, its case-sensitivity, the `a.thm\0x` name, the manual's list of refusals) have nothing left to apply to on the Ângulo.
- **The one allowance left.** `tools/ui_boundary.json` ends with `AppManager_Events.cpp`'s four identity sites, the history fix of section 4.5, which only main ends (D3): the file does not end empty on this branch alone. The fifth selection header is not in `check_feature_sprawl.py`'s `SKIP_FILES`, so its one `#if SIMUT_UI_ANGULO` counts as a site (moved from `DisplayManager_Touch.cpp`, the watermark 362 at C3 and 358 now); listing it would lower the mark by one, and is S11's call. **Closed in S11 A5 (`04c5126d`, 2026-10-30):** `ui/UiDriverTouch.h` is in `SKIP_FILES`, its site is out of the count and the watermark is 368 (it was 369); main's is 354 (`origin/main` `43b275d8`, measured the same day), so the branch is 14 above it.
- **F-g.** `StorageManager.cpp:275` and `:308` describe `.thm` files in the `/themes/` folder on every image, the Ângulo's included, where nothing reads them.
- **The RAM gates (closed in S10c C1a, 2026-10-22).** `tools/check_ui_ram.py` bounded SRAM at the RP2040's `SRAM_END` (0x20042000) on every image, and `build_custom.py`'s `_custom` environment had no RAM budget in `ui_ram_budget.json`, so no configurator build of the Ângulo linked. C1a gave the gate an SRAM end per chip (0x20042000 on the RP2040, 0x20082000 on the RP2350, from the core's pico-sdk `addressmap.h`), the chip taken from the board's `build.mcu` or from a required `--chip`, and gave `_custom` the budget of the real Ângulo environment of the same hardware (`pico2_w_angulo` or `pico_wiretft_angulo`, named in the option `custom_ui_ram_budget_env` and read when the gate runs), held as a ceiling. A configurator build of `pico2_w_release` with `ui = angulo` now links, and its `.bin` is byte for byte `pico2_w_angulo`'s (sha256 `16ed16c3...`, the review rebuilt both from scratch). The configurator refuses the Ângulo on the Pico W with Wi-Fi (the maintainer's decision of 2026-10-21) and without the panel chart (it does not compile: C1a-b's rule), and since C1b the page offers it: a "Touch display look" choice beside the display, the Ângulo locked with the rule's reason on every Pico W profile, free on the Pico 2 W and the Wire TFT, the chart switch locked on while the Ângulo is chosen, `ui` carried by the link and by the build request when it differs from the product's own (the page's test runs the real `app.js` on a stand-in of the page: 29 visits and 25 planted breaks). What it left was closed by S10c C1d (2026-10-29), apart from the list in the next bullet: the look's row shows the price the cost matrix measured for choosing it (the key is "ui=angulo", the one the matrix uses for a choice, as for a network driver; with no entry the row shows none and the warnings say "No measurement"), a switch's lock reason and its "changes nothing" line reach a screen reader (they were `<p>` elements with no id, and the switch's `aria-describedby` named only its help), the page speaks Spanish (es-ES), and the Ângulo's help no longer shows this document's path or an old figure (the citation lives in the manifest's comment above the entry). `check_flash_probe.py` keeps the RP2040's end: on the RP2350 it can only fail falsely, never pass falsely, and C1a-b says so beside it. The real environments keep 9,171 B (9,313 B at the S10b tip; section 2.16).
- **What the configurator still lacks (2026-10-29).** (1) The Ângulo is offered as a study, and its help says so ("not approved for products yet"), because that is true until Stage C: the Pico 2 W release with the Ângulo by default needs the maintainer's decisions C-D1 to C-D4 and B-D2 and the font licences, and then the switch, the notices, the retention gate and the documents. (2) None of this reaches `main` before S11 (the move) and the merge: the configurator page is on `main`, but the choice of the look, the two rules that refuse the Ângulo, its price and the Spanish texts exist on `feat/ui-drivers` only. (3) The Spanish texts (the page's `STR.es`, the 28 manifest texts C1d gave an `.es` and the 68 that had one) are proposals that await the maintainer's native review, like the device's es-ES texts; the page says so on one line that shows in Spanish only. (4) The Ângulo's price is the one the cost matrix measured (`ui=angulo` in `tools/feature_costs.json`, measured on 2026-10-29 at `src` d1515696: +36,816 B of flash and +6,028 B of static RAM on the Pico 2 W; the page shows the date of the run in its note): a change that moves the Ângulo's size leaves the figure stale until the matrix is run again (`tools/measure_savings.py --matrix`). The full run of that day moved the page's "near the limit" margin, the largest miss of its sum against the check builds rounded up to a whole KB, from 8 KB (the run of 2026-10-13) to 5 KB, so a few verdicts between those two distances from a ceiling turn from "near the limit" to "fits". A thirteenth check, added afterwards and measured by `--checks`, carries the Ângulo (`ui=angulo` with mDNS and the buzzer off on the Pico 2 W): the sum missed the real build by 8 B, so it left the margin where it was. (5) Static English that the page never translated, for pt either: the `aria-label` of the language group and of the two navigations, the `noscript` text and the meta description of `index.html`.
- **The Pico 2 W product.** (1) **The Ângulo's Wi-Fi screens (closed in S10c, stage D, 2026-10-28).** This item said that the AP confirmation (`showApConfirm( )` and its draw, called from `AppManager::core0Yield( )` under `#if SIMUT_NET_WIFI`) was drawn by the original, so that the Wi-Fi Ângulo linked Settings' original share and painted that one screen in the original's look. It was not so even then, by the code (not seen on the board): `pico2_w_angulo` linked `showApConfirm( )` alone, `drawApConfirm( )` is called only from `UiOriginal.h`, and the mode fell to `uaMontarPendente( )`, "This screen (mode N) is not in this build yet" with a "Back to the panel" button, so the panel could not start the setup AP on the Pico 2 W Ângulo (note 60 of `S10C_PLANO.md`; the plan is not in the repository). Since S10c D2 (`9fae2fa7`) `showApConfirm( )` is core code in `DisplayManager_SettingsCore.cpp` and `DisplayManager_Settings.cpp` is in `[ui.original].units`; since S10c D7 (`8dd53c51`) the Ângulo draws the dialog itself. On 2026-10-28 the dialog on the glass of the Pico 2 W matched the host frames in 0 of 76,800 pixels in pt-BR and in es-ES, and Cancel went back to Settings; Confirm has not been pressed on a board (section 2.18). (2) `ui=angulo` on the Pico W is to become a configurator refusal in `net_refusal`'s mould until the smaller version exists (S12). (3) Nothing of `pico2_w_angulo` has run on a board: B1 to B4 are the Wire + TFT bench's, and no Pico 2 W bench has the Ângulo. **Superseded on 2026-10-22:** the Pico 2 W on the new bench has run `pico2_w_angulo`, the S10b tip's image and then A'2c's (section 2.16 has what ran). (4) The CI hooks of S10b-1 have not run on GitHub: the eleventh matrix entry and `check_ui_link.py` in the `gates` and `firmware` jobs.
- **The switch (S10c C3, 2026-10-30).** `pico2_w_release` has `ui = "angulo"` (the maintainer's decision C-D1 of 2026-10-30) and `pico2_w_angulo` became `pico2_w_original` (`ui = "original"`, not published, not a required check). What stays: the published asset names (`releasetwo`), the required check `firmware (pico2_w_release)`, and `SIMUT_ENV_NAME`, which is `releasetwo` for both Pico 2 W images because it comes from the chip, the display and the network and not from the environment (`src/simut_config.h`), so a device can move between the two over the air in both directions (C-D2; the retention gate on the board, C5, ran on 2026-10-30: section 2.16). Proof, host only, with the shim under the build lock: the eleven images built from the commit's tree, the new `pico2_w_release` `.bin` sha256-equal to the previous tip's `pico2_w_angulo` (1,036,864 B, `2aebc1ed...`), the new `pico2_w_original` equal to the previous tip's `pico2_w_release` (1,000,048 B, `808c8359...`) and the other nine equal, so the environment's name does not reach the image and no differing byte is left to decode; the generated environment blocks are equal two by two up to a blank line. The CI no longer picks the images that carry the Ângulo by the ending of their names: `tools/env_ui.py` prints an environment's `ui` from the manifest, the `firmware` job's link gate runs on `steps.ui.outputs.ui == 'angulo'`, and `tools/test_env_ui.py` holds the workflow and the list of `tools/ui_ram_budget.json` to the manifest with mutants (an Ângulo profile under another name is still gated, an original one named like an Ângulo one is not). The static-RAM gate (`tools/check_ui_ram.py`) already read the image and looked the budget up by `$PIOENV`; its entry is `pico2_w_release`'s now, 9,175 B. The configurator's base on the Pico 2 W is the Ângulo image and the original is `ui=original`. Its cost row was measured again on that base (2026-10-30, `tools/measure_savings.py --matrix`: the tool does not restrict by product, so the whole run, 83 builds, under the shim and the build lock): base 1,036,840 B of flash used, 1,036,864 B of `.bin` and 131,556 B of RAM (999,912, 999,936 and 125,528 on 2026-10-29), and the original is the flip `ui=original`, -36,816 B of flash and -6,028 B of RAM, the Ângulo's price of 2026-10-29 with its sign turned. `tft_graph` is a rule entry from that base (the Ângulo needs the chart), so the original look with the chart off has no measured price for the chart switch: the matrix measures flips from the product's base only (open). The other three products' rows moved too, because sync 8 changed `src` after the previous run: `pico_w_release`'s base +104 B and 16 of its flips, only the `bluetooth` flip of the Alpha and of the Air. The checks are 13 again: the one that set the Ângulo went (a `set` holds changes, and `ui=angulo` is no flip of the release now: the first run of this step ended in `print_checks( )` with a KeyError on it), and one sets the original (real 979,296 B, sum 979,272 B, +24 B). Left for later stages: the `[ui_panels]` help texts still say "study / not approved" (C6); `THIRD_PARTY_NOTICES.md`, `tools/third_party.toml` and `tools/gen_notices.py` named `pico2_w_angulo` until C4 (next item).
- **The notices per image (S10c C4, 2026-10-30; reviewed the same day).** `tools/third_party.toml` and `tools/gen_notices.py` follow the switch: `pico2_angulo` is gone, `pico2_original` (`pico2_w_original`) is the second bench image, and a bench image is named in each row that carries it (it no longer inherits the rows of the image it extends, which was wrong for the original panel's faces). Each row was checked against `nm -C --defined-only` of the eleven `firmware.elf` built from 34205d58: Bricolage Grotesque (6 faces, 18 symbols: `entrega::DISP9`, `DISP12`, `DISP18`, `DISP24`, `DISP24_MARCA` and `DISP32`, each with its glyph table, its bitmaps and its face), FreeMono Bold (2 faces, 6 symbols), FreeSans and FreeSans Bold (2 faces, 6 symbols) and the 42 `ICONE_*_bits` icon bitmaps are in `pico2_w_release` and `pico_wiretft_angulo` and in no other image; the original's `FreeSansBold9pt8b`, `12pt8b` and `24pt7b` (9 symbols) are in `pico_w_release`, `pico_w_test`, `pico_w_test_https`, `pico_w_asserts`, `pico_wiretft` and `pico2_w_original` and in neither Ângulo image, whose faces are the other FreeFont row; `Adafruit_GFX::` (32 symbols in the originals, 22 in the Ângulo images), ILI9341 (10) and XPT2046 (4) are in all eight touch images; `cyw43_`, `dhcp_server_`, LEAmDNS, BTstack and the ENC28J60 driver match their rows. The Ângulo's FreeFont row said FreeSans at "7 and 9 pt": the kit has `SANS9` and `SANSB9` only, and the row says 9 pt now. The three Ângulo rows are marked `ui = "angulo"`: they are out of the family lists and in a block of their own, `/* @IF ui_angulo */` of `WebUI.h`, lettered A to C (a number in a family list depends on the family), between the list and the closing bar, which `gen_notices.py` keeps outside the family blocks. `[ui.original].web_omit = "ui_angulo"` cuts it from every image with the original panel, the way the net classes and drivers cut theirs: `gen_features.py` reads the key on a `[ui.*]` and `compose( )` adds it after the network's (the answer to "can a UI unit carry `web_omit`": it can, since this change). The Ângulo's own screen names the three sets through four `UA_TXT( )` texts (a heading and one paragraph each) appended by `anexarFontesDoAngulo( )` in `telas/MenusLicenca.cpp`, with a line each in the `@ANGULO` section of both packs; `gen_notices.py --check` holds that hand-written text to the rows (the name, and "holder, licence."). `LICENCA_FONTES_MAX` went from 8 to 12 (16 B of static RAM, `tools/ui_ram_budget.json` 9,175 -> 9,191 B): the worst case is 5 + 1 + 4 = 10, the five strings of the English opening, `LICENSE_TEXT_EN`, which is one source (the MIT text and the list in one string), and this panel's four. **The review's fixes (2026-10-30).** (1) The first draft kept a `page_images` list on the FreeSans Bold row so that the originals' pages stayed byte for byte; it made the radio page say "the five images with the touch panel", which counted `pico2_w_release`, and the Ângulo's own block says that image keeps none of those faces. The key is gone and the generator refuses a key it does not know; the radio page names the four images (the Wire page already said "all images but pico_wire"). The seven radio originals' pages differ from C3's in that one line, on purpose: the whole `WebUI.h` cut per profile equals 34205d58's on all nine originals before the fix and differs in one line after it, the licence blob is 13 to 27 B longer (`WebUI_GZ::LICENSE_PAGE_GZ` the only symbol whose size changes; the other changed words are `handleLicense( )`'s length, `__binary_info_header_end` and one address in `WebManager::begin( )`), flash used +16 B on `pico2_w_original` and `pico2_w_release`, +32 B on `pico_w_release` and `pico_w_asserts` (headroom to the OTA ceiling 4,083 -> 4,051 B and 1,779 -> 1,747 B), +0 on `pico_w_alpha` and `pico_w_air`; `pico_w_test` and `pico_w_test_https` keep that page in LittleFS (`custom_fs_pages`) and their `.bin` is byte-equal; the Wire images and `pico_wiretft_angulo` keep their bytes. Nothing had read an image, so `gen_notices.py --check --elf <firmware.elf> [--env]`, a step of the `firmware` job after each build, holds the 20 rows that have a `symbols` probe (of 26) to the `nm` of the image (a row naming an image that lacks the component, or leaving out one that has it, exits 1: shown on the real ELFs, three controls), and `tools/test_gen_notices.py` reads each page label back against its row. (2) `MenusLicenca.h` states the three source counts and `static_assert`s their sum against the maximum (red with the maximum at 9, green at 10 and 12, in the harness build), the function pins its own table and the glue its `LICENSE_OPENING_EN`, and `anexarFontesDoAngulo( )` appends all four or none (it used to drop sources one by one, silently); the images are 32 and 48 B smaller than the first draft's. (3) A blank line between the three paragraphs was tried and left out: pt and en stay at 19 pages, es goes to 20. (4) V-PIX has a new reference, `stages/s10c_c4/vpix_ref/quadros` (951 frames, 6 contact sheets) with its judge `vpix_c4.py`: D7b's is not edited, and sync6_c0's judge cannot say EQUAL any more because of the 12 licence frames below; on a fresh run all 951 frames equal the new reference and its control bites. Proof, host only, with the shim under the build lock, the eleven images built from 34205d58 first and from the final tree after: `pico2_w_release` 1,036,864 -> 1,037,872 B (+1,008) and `pico_wiretft_angulo` 827,616 -> 828,648 B (+1,032), of which `WebUI_GZ::LICENSE_PAGE_GZ` +550 and +584 B, `anexarFontesDoAngulo( )` +116 and +100 B, `uaMontarMenus( )` +12 and +20 B, the four literals 328 B with their NULs and `s_indiceLic` +16 B of RAM (`flash_budget.json` 1,036,840 -> 1,037,848 and 815,580 -> 816,612 B used, with their section 3.1 rows). V-PIX against D7b's records: 939 of 951 frames equal; the 12 that differ are the four whole-text samples of the licence screen (`licenca_p1`, `licenca_meio`, `licenca_traco`, `licenca_ultima`, three files each): the text went from 17 to 19 pages (the pager reads "de 19"), the middle page is another page, and the last page holds the Ângulo's lines. Not verified on hardware.
- **Instruments.** `reach.py` has one known false keeper (`_customThemes` <- `SensorManager::pushSample( )`, through libstdc++'s sort-threshold word) and reads two 1-byte caches as unreferenced; why `ld` ordered the veneers differently at C5a is not isolated; `check_ui_link.py` is the only CI check on the Ângulo's link set (the scratch V-NM and `reach.py` are not CI), and it has limits its docstring lists: a function added to Dashboard.cpp, Users.cpp or Settings.cpp (or a header) with no row and a name no pattern matches passes, because nothing in CI forces a row (`tsv_check.py`, which holds the table to the code, is scratch); core rows and library code get no rule.
- **S11:** `Temas.cpp`'s `RGB565` and `THM_STATE_DARK` repeat `Themes.h`'s, with nothing to tie them; `DisplayManager_Touch.cpp` keeps the original's headers (`DisplayManager_Fonts.h`, `PasswordKeyboard.h`, `sensors/SensorDrawing.h`), which moving would reorder the header-defined functions of its object. **Status at S11 (2026-10-30):** those three stay, a recorded deviation (Q5 of the S11 plan, decided by the main session with the plan's default and told to the maintainer); the headers whose includers are all original units moved in A2 (`8b24b04e`, the three FreeSans tables; no other header qualifies); the `Temas.cpp` tie is not part of this series.
- **The commit messages (closed on 2026-10-22).** C3's and C5a's were rewritten over the same trees before the branch was integrated, and C5b's and C7's in a second pass (section 2.15, item 9); C4b's record, a snapshot, keeps the old hash.
- **B4 on the bench (closed in S10c A', 2026-10-22).** A `.thm` upload to the Ângulo waits for the maintainer's word (it writes LittleFS if the refusal fails); the refusal is proved on the host (C4b), not on a board. **Closed:** he answered ("Só serve à original"), the refusal is gone (A'4), and A'5 ran the upload on the Pico 2 W: `contraste.thm` uploaded with 200, listed (513 B), downloaded `cmp`-equal and deleted with 200, with `SEC_FILE_UPLOAD` and `SEC_FILE_DELETE` in the log and no rescan record (section 2.16).
- **Injected touches that are not taken** (the bench's `POST /api/touch`): on the original, "Nova conta" on the users list and the graph's detail (main `2f4bc3d3` and the branch alike, 2026-10-21); on the Ângulo, the first 3.5 s hold right after a slot tap does not toggle the pinned card, and a retry 2 s later does (the S10a tip on 2026-10-21; the base and the tip of S10b-1 on 2026-10-22). Not seen with a finger and not judged a defect; open.
- **`heap_lb` on the Wire Ângulo** went from 29.1 KB on the base to 49.9 KB on the tip with the same free memory (+0.9 KB), measured on the bench on 2026-10-22 (section 2.15): where the heap's blocks fell once C6's 864 B of static RAM left, not memory freed, and not analysed. A later change of the static RAM may move it back. The Pico 2 W's image, with Wi-Fi and TLS, has a layout of its own; its `heap_lb` was read at the gate of v2.13.0: 258,683 to 258,769 B in 18 reads of `/api/status` on the candidate (2026-10-30 22:26 to 2026-10-31 00:41; `heap_f` 288,020 to 288,116 B) and 259,042 B on the development image at 21:09 on 2026-10-30 (`scratchpad/bancada_20261030_rel2130/`: `g0/` and `partc_p2_out*/guard/`).

**S10c A': what it found and left open (2026-10-22).** None of these is this stage's to change, or each was left on purpose; section 2.16 has the numbers.

- **The pressed state is drawn on no screen of the device** (ANGULO.md rule 8, which asks for every state of a control to be drawn before the component counts as ready). The kit draws it: `TeclaEstado::PRESSIONADA` fills the keys of the PIN keypad (`telas/IdentidadePin.cpp`) and of the text keyboard (`telas/IdentidadeTexto.cpp`), and since A'3 the Tema screen's segments, swatches and button read `premidoTipo` and `premidoIndice` (`telas/MenusConfig.cpp`). The host draws it, so its sheets show it. **The glue never feeds it:** the keypads' builders set `vm.pressionada = -1` (`DisplayManager_UiA_Identidade.cpp`, "the old keypad shows no pressed key") and the Tema screen's builder sets `premidoTipo = TT_NADA` (`DisplayManager_UiA_Menus.cpp`). No other view model has a pressed field, and the touch-down reaches no view model at all: the panel acts when the touch lands (`acceptTouch( )` accepts once per press, whatever the zone, and `acceptHoldTouch( )` repeats a held press for the volume steppers) and keeps no "finger is down on target X" state for Core 1 to read. The original shows no press either. So the host sheets misrepresent this one state, on every screen with controls. The review of A'3 asked whether any other screen shows it on the device, and A'3b answered by grep over `src/` and `tools/` at A'2b's tree: none does. Closing it is a step of its own and a decision for the maintainer: the core's touch path, which both UIs share, would keep the target kind and index under the finger from touch-down to release (the hit test the Ângulo's chain already does), the three builders would read it instead of `TT_NADA` and -1, and a repaint would be asked on touch-down and on release, with a bench check for the latency of a press on Core 1. The record is `PRESSED_STATES_GAP.txt` of A'3b, outside the repository.
- **The Tema screen's builder is not run on the host** (A'2d; **closed in A'2e**, the same day: its case is cut and run over 6,168 cases, and the two mutants below fail it). The `MODE_SETTINGS_THEMES` case of `uaMontarMenus( )` (`DisplayManager_UiA_Menus.cpp`) fills the view model that marks the saved choice, disables Aplicar and names the accent; two mutants of it pass `test_temas.py` (the saved choice taken from the shown one, which would leave Aplicar always disabled; the accent forced to the stock). The bench saw Aplicar inert while the preview equalled the saved choice and the applied choice on the glass (A'5, section 2.16), so the image of that day behaved; no host test holds it. Closing it is one more cut in A'2d's way.
- **F8, recorded and not fixed.** The rescan after an upload and after a delete (`WebManager_Files.cpp`) runs outside `RenderGuard` while Core 1 may be reading the custom palettes, and after a rescan nothing re-applies the theme, so a deleted theme stays on the glass until the next apply or boot. In the Ângulo neither runs any more: A'4 put both under `#if SIMUT_UI_CUSTOM_THEMES`, and the Ângulo has no custom palette to read (A'5 saw no rescan record after an upload or a delete). It stays the original's, and a fix to it moves the nine product images.
- **F4's rule, as the encoding now holds it, and what C5 observed (2026-10-30).** `themeIndex = modo + 3 x acento`; 0, 1 and 2 keep their meaning; an `int8_t` outside [0, 18) decodes as light with the stock accent; the number 3 and `ACENTOS_N` live in `ua::escolhaDoIndice( )` and `ua::indiceDaEscolha( )`, and the `static_assert` statements of `Temas.cpp` and `MenusConfig.cpp` tie the catalogue and the screen to the count (section 2.16). **Closed on 2026-10-30 (C5):** the original reads the same byte as a built-in or custom theme (0 is simut_def, 1 to 8 is custom theme `t - 1` when that file is loaded, anything else is simut_def), so after an image switch the Ângulo's pairs read as custom themes or simut_def, and what the original wrote reads as pairs. The two images crossed on the Pico 2 W in C5 (the gate of v2.13.0; records in `scratchpad/bancada_20261030_rel2130/`): the OTA kept the byte in both directions, the original drew 7 as simut_def and 2 as `contraste`, and the Ângulo drew 2 as automatic mode with the stock accent and 7 as dark with accent 2. The *Upgrading* text of v2.13.0 says what a field device shows.
- **The paint cost under TLS load is unmeasured.** A'2b moved the palette from RAM to flash, which Core 1 reads through the XIP cache. A'5 ran the A/B the bench allowed (section 2.16): 40 rounds per screen, the S10b tip against A'2c's, eight screens on plain HTTP, no Tema step and no XIP counters, every median within 1 ms. That bounds the cost at the 1 ms resolution for those screens without TLS load. It is not the worst case that the review of A'2b named: A'3 against A'2b, 200 or more paints, Core 0 under TLS load, the Tema screen among them, XIP counters around `uaPintar( )`. A bench item for when a board has a TLS pair; if it costs, the review's fallback is to copy the live theme and its twin (about 70 B) to Core 1's stack per frame.
- **F9's four minor findings** (the S10c plan's), each against the tree of A'2c. (a) The Tema screen's touch accepted only rows `i < 3`: **gone** in A'3 (one rule per kind of target; `test_temas.py` refuses a bare 3 or 6 in the touch). (b) `/api/themes` names a theme with no name "Tema %d", in Portuguese: **still there** (`WebManager_Api.cpp:422`); **closed on 2026-10-28**: main's #298 made it "Theme %d" (`src/ThemeApiRow.h`), and sync 7 brought it here. The Ângulo's 18 entries all carry names, so only the original reaches it, and the names of the 18 stay const pt-BR (A'3); it waited for Stage B, which decides the language of the catalogue's names. **Stage B left both as they are** (2026-10-23): the 18 names are the text gate's allowance group "a name the web lists", and whether the catalogue follows the device's language is the maintainer's to decide (the S10c B block below). (c) `ua::n1( )` writes the pt-BR decimal comma in every language: **still there** (`Angulo.cpp`, the format `"%ld,%ld"`); it waited for B5. **Closed in B5a** (section 2.17): the separator is the pack's text for "Decimal separator", and English writes ".". (d) The Idioma screen says "Esta build mostra o painel em português.": **still there** (`telas/MenusConfig.cpp:199`, moved from :134 by A'3); it waited for B4, where the note becomes "takes effect at the next restart". **Closed in B4a**: the note is "Takes effect at the next restart.", in the device's language.
- **F-g (S10b-1's line above) still holds.** `StorageManager.cpp:275` and `:308` still describe `.thm` files in the `/themes/` folder (the filesystem's note and the folder's note), on every image, the Ângulo's included, and nothing the Ângulo links reads them. A'4 makes the Ângulo's case plainer and not different: it stores an uploaded `.thm` at the maintainer's word and uses none, so the notes are the original's. Recorded, not changed: the strings are core and every image has them.
- **The CI steps of A'2c have not run on GitHub.** The `gates` job fetches Adafruit GFX and runs `test_temas.py`, `test_cantos.py` and `test_alvos.py` since A'2c; the steps were run in CI's form from a clean export, and the first pull request shows the runner's time and whether the fetch holds there.
- **The manual's theme section** (`docs/MANUAL.md`, "Themes"; `docs/MANUAL.pt-BR.md`, "Temas") describes the shipping images, which draw the original: up to eight custom `.thm` themes of 24 roles, written with the editor in `tools/theme-editor/`. The Ângulo is not approved for products, so the manual is not changed here. It changes when a shipping image draws the Ângulo (Stage C, C6).

**S10c B: what it found and left open (2026-10-23).** None of these is this stage's to change, or each was left on purpose; section 2.17 has the numbers.

- **The Spanish waits for a native review.** es-ES was written for this stage, in the Spanish of Spain, and nobody who speaks it natively has read it. Where a text did not fit, it was shortened:
  - "Maintenance" is "Mantenim." on the alarm list's badge and the maintenance screen's title, where "Mantenimiento" cut the sensor's name (a planted pack, 2026-10-23: 4 samples of the badge and 22 lines of the title fail), and on the status page, which shares the key because its English must stay short for the sensor names beside it, though "Mantenimiento" fits there in es-ES. The sensor menu's row and the account's permission row say "Mantenimiento" (the key "Maintenance mode", B5b-b and B6-b). The pack's web dictionary abbreviates it too ("Mant. (panel)", a permission's label);
  - "Start maintenance" is "Iniciar mantenim.", beside "Cancelar" in the footer;
  - "Touch click" is "Clic al tocar";
  - "Touch sensitivity" is "Sensibilidad del toque", where the original's pack says "Sensibilidad Táctil", whose accent crossed the safe area by 1 px;
  - "Add account" is "Añadir" on the account list's bar;
  - "The PINs do not match. Start over." became "Los PIN no coinciden. Pruebe otra vez." ("try again", a shift of sense the review of B5b noted).

  In es-ES, 5 of the calendar's 12 month titles (Mar, May, Ago, Sep and Nov) fall to the smaller title font in some years, because "Volver" leaves the title 109 px. The update screen's title "Actualización de firmware" falls to it as well.
- **One pt-BR text is English.** The board's chart is titled "Board Temp" in pt-BR, as it was before the stage. It stays byte for byte; es-ES says "Temp. de la placa". Translating it changes pt-BR frames, and that change is to be listed when it is made.
- **The web's theme names follow the device's language (closed on 2026-10-28).** The maintainer decided on 2026-10-24 that the device sends English and the page translates. Main's #298 (c9028947) sends each theme's stable code with an English name and the page translates it by `thm_<code>` from the active pack's `@WEBDICT`; sync 7 (02dba88c, d1515696) brought it here and gave the Ângulo's 18 entries English names ("Light" ... "Automatic, magenta"), with their keys in both packs composed from each pack's `@ANGULO` words (the es-ES values are a proposal until the maintainer's native review). The panel's Tema screen still draws its own words. The console's theme list, the CFG_THEME_APPLIED record and the CLI's success message read the same names (`AppManager_Commands.cpp` ~:216-239), so on the Ângulo images they show English now (pt-BR before). The names stay the gate's allowance group "a name the web lists", in English. F9 (b)'s "Tema %d" is "Theme %d".
- **One English, one translation.** The lookup keys by the English, so a later screen that reuses a generic English in another sense gets the first sense's word, with no warning. Today, for example:
  - "Low" is the sound's "Grave";
  - "Drop", "Rise", "Decline", "Alarm", "Click" and "Pulse" are also sounds (the review of B4b).

  The rule for a new text: look for its English in the packs' `@ANGULO` first. Where the sense differs, the English must differ, as "Maintenance mode" does (B5b-b).
- **The badge case sees 10 to 12 px, not 1.** Measured by B5b-b: the room a badge leaves a cut name, held in 9 rows of which 2 carry a cut name, fails when the measured badge is 10 px wider than drawn (the user list, English) or 12 px (the account picker, pt-BR and es-ES), and when it is 2 px narrower in one of the two. That is what it is for: a badge measured from another word is wider ("Locked" and "Bloqueada" differ by 29 px). A host record of the limit each cut is given would see 1 px.
- **What only the bench can show (B7).** These need a board:
  - the release store against Core 1's acquire load;
  - the heap against the sizes of section 2.17;
  - the boot frames repainted in the pack's language;
  - B4b's PIN-policy keypad, whose touch zone starts 4 px lower;
  - the frames in the three languages.

  Nothing in stage B had run on a board when this list was written. B7 ran on 2026-10-24 (section 2.17): its walks drew the frames in the three languages (pt-BR and es-ES with 0 FAIL, English 32 ok and the one expected FAIL) and recorded the heap (`heap_f` medians 288,620 B in pt-BR, 288,212 B in es-ES and 300,108 B in English, its record `stages/s10c_b7/resultado.txt`). The other three items are not in that record.
- **The stack frames grew, and Core 1's stack was not measured.** The draw functions run on Core 1, whose stack is 8,192 B (`s_core1Stack`, `src/DisplayManager.cpp:200`). B4b's frames grew the most: `desenharAlinhar( )` went from 288 to 392 B on the Wire (+104 B), and the rest by up to 64 B. B5b's grew by 0 to 24 B. The figures are read from the code, 4 B per pushed register plus the prologue's `sub sp`. No step measured the stack under load.
- **The gate is lexical.** What it cannot see is in its docstring (KNOWN LIMITS):
  - a text a program builds at run time;
  - `#if 0` code, which it scans as code;
  - a text that a header outside `src/ui/angulo/` defines and the Ângulo only passes along;
  - a looked-up text that reaches a printf through a variable.

  The last is `-Wformat-nonliteral`'s, which is an error from the end of `Textos.h` (B2b).

### 4.5 Product defects fixed for the Ângulo's bench image

Two defects of Core 0's history navigation (`AppManager_Events.cpp`) are fixed for the Ângulo's image only (fix/ui-angulo-historico, 2026-10-15), by pure functions in `src/display/GraphAnchor.h` called under `#if SIMUT_UI_ANGULO`. **F1:** `_graphAnchorEnd`, the window's end that a calendar day or an arrow sets, was never cleared, so every later `EVT_OPEN_GRAPH` from the dashboard, and the calendar's Today, showed that old window as the present (on the bench, 62094 s behind the clock, image 9c3e0a25, 2026-10-14); `graphAnchorOpen( )` drops the anchor, and its base, when the chart is asked for from any screen that is not `MODE_GRAPH_VIEW`, `MODE_GRAPH_DETAIL` or `MODE_GRAPH_LOADING`, the rule that the study branch's 6116de0 had inline. **F3:** one period back and one forward, a few seconds apart, ended at the first tap's clock and was judged against a later one, so the window read as the past (`fim`, ▶ still enabled), and a second ▶ reached the present with the anchor frozen at that moment. `graphAnchorStep( )` keeps beside the anchor the end of the live window at the moment the first arrow left it (`_graphLiveBase`, a static of `core0Yield( )`, 0 when the anchor did not come from the live window): a window the taps bring back there is the live window again, whatever the wait and whatever period was chosen in between (three 24 h steps back and then 1 h steps: 71 forwards stay in the past and the 72nd is live), and so is one that ends at or after the clock. A calendar day (`graphAnchorDay( )`) has no base and keeps its alignment, so the calendar's today, one step back on 24 h at 10:00, is yesterday and not the present. The first version of the fix called a window the present when it ended less than half a step before the clock; the review of 4122282 showed with a host probe that this turned yesterday into the present before noon, and the base replaced the bound. The products have the same two defects and keep their lines, so their images stay byte for byte as they are; the fix for them is the same functions without the `#if`, a change to `main` that is not made here. The four `#if` are one `identity` allowance of `AppManager_Events.cpp` in `tools/ui_boundary.json` (transitional: `ends in S12` is only the format the gate requires, and taking the fix into `main` ends it), and `check_feature_sprawl` goes 358 to 362.

Proof: `test_validators` 302 -> 316 cases; all 32 killable mutants of the header fail the cases named for them and the two equivalent ones fail none (`stages/hist_fix2/native_m01_s01.txt` to `native_m34_e02.txt`, with the clean runs before and after); a host model whose oracle is the old flush lines compiled from a6026d5, with the study's condition against the real `enum UiMode`, finds 0 differences in 468,498 comparisons and states where the new rule differs from the old lines on purpose: the window stays in the past, equal (194,203 points); it ends at or after the clock, where the old code froze the anchor at the clock and the header says live (27,802); the taps are back at the base, where the old code kept the past and the header says live (4,489) (`stages/hist_fix2/model_graphanchor.txt`). `GRAPH_SCREEN_*` in the header repeat three values of `UiMode`, because the host suite cannot include `SystemDefs_Records.h`; `AppManager_Events.cpp` pins them with a `static_assert` in every image. The Ângulo image is 811,792 B used, `.bin` 823,824 B (+80 B on a6026d5). Not run on a board; the bench should check, on the Ângulo image: after a calendar day, the dashboard's chart button and the calendar's Today open at the present (the window ends within 2 min of the clock); a period chip and the way back from the detail keep the chosen day; one ◀ then one ▶, after any wait (10 min on the 1 h period), shows the present, with no `fim` and ▶ disabled; the calendar's today on 24 h at 10:00, one ◀, is yesterday with ▶ enabled, and ▶ again is today's window, the present; one ◀ on 24 h, the chip to 1 h, then ▶: the past until the 24th ▶, which is the present; a calendar day days back, ▶ day by day: the past until the window reaches the clock.

### 4.6 Touch: what the Ângulo answers like the original, and what it does differently on purpose

On 2026-10-15 the maintainer looked at the Ângulo on the bench panel and asked that every touch point answer and that the history navigation work like the original's where the look allows (fix/ui-angulo-navegacao, on top of the touch slop and the chart's anchor). **Answered like the original now:** a touch on a control drawn disabled does nothing and reaches no neighbour (an inert target, type 0, which the glue reads as a dead zone: no gate, no touch sound); a tap anywhere in the chart's band 0,32 320x164 opens the detail (the original's central zone, 0,40 320x155, "Tap on central zone"); a tap on the detail's cards turns the page in the original's order (temperature, humidity if the sensor has it now, pressure if it has it, then back to the chart); the calendar's month arrows take one tap each. **Still different, on purpose:** "Voltar" is in the title bar (a target 32 px tall, 43 px with the slop) instead of the X in the top right corner (35x40); the detail's "Voltar" goes to the chart, not to the panel; the detail has no period footer (the original's footer buttons left the detail anyway, because `showGraphPlot( )` sets MODE_GRAPH_VIEW when the new window lands); the calendar has no footer month buttons, and its "Hoje" is at the right end of the footer; no header tap shows the sensor's name, because the title always shows it; the chart has five period chips and no zoom-step buttons. **What proves it:** `tools/tft_ui/test_alvos.py` runs 31 cases of the hit test with 10 mutants and 16 cases of the control recorder with 11 (it draws each kit control function enabled and disabled and asserts the records; a recorder that skipped the disabled ones passed every sample before, and now fails 10 cases), every mutant bitten (2026-10-15). `host_telas.py` records every control the kit and the screens draw and runs the firmware's own `ua::alvoEm( )` over every pixel of each: an enabled control must be answered by a live target on all of them, a disabled one on none, so a disabled control listed live, a live target laid over it, or an inert one laid over an enabled control fails: 1,802 controls checked in the six groups, none dimmed on purpose or excluded at the call. Its mutations are kept with the stage's records (stages/nav_fix2): the chart's disabled next arrow listed live fails exactly the 13 chart samples at the present; a live target laid over the detail's dimmed arrows, the 3 samples where one is dimmed; an inert target over the enabled "7 d" chip, the 14 chart samples. It also fails a sample whose target list drops a target or fills MAX_ALVOS (`addAlvo( )` dropped silently; the largest list is 38 of 48, the text keyboard). **The detail's cycle** is the original's rule for every page number: the first page of the sensor's list above the one on show, else the chart (the glue's comment lists the four sensor shapes). **The title** is an inert target on every screen with a title bar (`areaDoTitulo( )`): on 67c6521, 50 of the 261 samples with a title answered a live target on some pixel of its ink (20 on the chart and the detail), and none do now. The check sees the kit's controls and the few drawn by hand that are annotated (`UA_CONTROLE`), no more. **Bench:** the run of 67c6521 on 2026-10-15 at 17:15 passed the history anchor (F3), the greyed arrow and the absent month arrow, the slop between chips, the central band, the cards of a one-page sensor (cards, then the chart), the calendar's arrows and the glass edge. **Still pending**, for the image after this change: touch the title's text on the chart (grafico_24h), the detail (detalhe_umidade), the calendar (calendario_setembro) and the settings menu (menu_admin_p1, the strip under "Configurações"): nothing opens, turns or is selected, and nothing sounds; touch the end of a long title beside "Voltar" (sensor_nome_longo) and beside "Nova conta" (usuarios_3_contas): the action does not fire; on a sensor with humidity and pressure, tap the cards through temperature, humidity, pressure and back to the chart.

### 4.7 S11: the original's files move into `src/ui/original/` (2026-10-30)

**What moved.** A1 (`1e87d8bf`) moved the original panel's 14 units from `src/` to `src/ui/original/`, beside `UiOriginal.h`, `UiTouchChain.h` and `PinKeypadLayout.h`: `Themes.cpp`, `UiWidgets.cpp` and the twelve `DisplayManager_*` units of `[ui.original].units` (Calendar, Fonts, Graph, Ota, Render, i18nUi, Dashboard, AlarmUi, Settings, Calibration, LicenseUi, UsersUi). All 14 renames are R100. Their headers stay in `src/`: `Themes.h`, `UiWidgets.h` and `DisplayManager_Fonts.h` have core includers (9, 9 and 12 `.cpp` files) and `DisplayManager_AlarmUi.h` does not exist. A2 (`8b24b04e`) moved the three FreeSans font tables (`FreeSansBold9pt8b_latin1.h`, `FreeSansBold12pt8b_latin1.h`, `FreeSansBold24pt7b_subset.h`), the only 3 of 196 headers whose every includer is under `src/ui/original/` (R100, no include line edited); the eleven images came out sha256-equal to A1's and every `firmware.elf` byte-equal. A5 (`04c5126d`) put `ui/UiDriverTouch.h` in `SKIP_FILES` of `check_feature_sprawl.py`: the watermark is 368 (it was 369), 14 above main's 354.

**The proof of A1.** The two Ângulo images (`pico2_w_release`, `pico_wiretft_angulo`) are sha256-equal to the baseline `caafa549`. The nine others change bytes by link order alone (the 14 objects now come last of `src/`) and each is decoded: 0 REAL, 0 UNPROVEN, 0 OTHER (`elf_funcoes.py` with every changed function listed, `reloc_only.py`, `tft6_sections.py`). `reloc_only.py` was taught four rules, each with negative controls that plant the same change in the real images and must still see it: (1) a word one past the end of the same sized symbol in both images; (2) the zero padding in the window of a symbol of size 0 (`_fini`); (3) the image-length word of `__binary_info_header_end`; (4) a data object compared word by word (`FS_DIR_NOTES` on five images). The sha256 pairs, `caafa549` > A1: `pico_w_release` 81efd9ece9d8 > e642206eb0ec; `pico_w_test` 979a5c032e27 > 5cb4ff09acee; `pico_w_test_https` 9c48565b3f2e > cbc4fb713a22; `pico_w_asserts` 2bf13d990e4a > 2f1a40c2bdad (1,038,396 B > 1,038,388 B); `pico_w_alpha` e56744fd9005 > 5d0ef27f81fb; `pico_w_air` b28943d0949e > 2b59807c5dbe; `pico_wire` 48e6fecd5238 > d81702c36f15; `pico_wiretft` 64487d04ed73 > e227ec4c9991 (787,776 B > 787,768 B); `pico2_w_original` 6980720e2666 > aab8f1d5c073 (1,000,064 B > 1,000,056 B); `pico2_w_release` deabc77b3ed5 = deabc77b3ed5; `pico_wiretft_angulo` 48e0082cd4e4 = 48e0082cd4e4. V-PIX of the Ângulo against `vpix_ref`: 960 of 960 frames and 6 of 6 sheets equal. No bench ran on the moved images: the maintainer accepted the decoded proof (Q3).

**A3 was tried and not kept.** The 14 hit-test names `PinKeypad.h` keeps (`keyAt`, `popupKeyAt`, `popupRows`, `popupRowKeys`, `popupRowY`, `popupRowX0` and the eight `FOOT_*` constants) were moved word for word into `PinKeypadLayout.h`, and the nine `handleTouchPanelV24( )` rows of the function table reclassified `original`. `pinkb_readers.py` read the 14 as ORIGINAL-ONLY and `GET /api/keypad` reads none of them; the gates were green (`check_ui_link.py --selftest` 87 cases once the case that names the row's class was updated, both Ângulo ELFs 0 findings and 12 of 12 allowances, `test_ui_boundary.py` 316 of 316). Six of the eleven images changed sha256: `pico_w_release` and `pico2_w_original` keep their size, `pico_w_test`, `pico_w_test_https`, `pico_w_asserts` and `pico_wiretft` grow 8 B; `pico2_w_release`, `pico_wiretft_angulo`, `pico_w_alpha`, `pico_w_air` and `pico_wire` stay equal. In each of the six the symbol order differs only by `PinKb::keyAt` and the lambda inside `DisplayManager::handleTouchPanelV24( )` trading places in the object (`nm -n` of the ELFs, A2's tip against A3's, 5,347 symbols in `pico_w_release`); the bytes were not decoded further, since Q6 asks for equal sha256. Per Q6 the step was reverted and the names stay in `PinKeypad.h`: S12's, with a decoded proof as in A1 if the maintainer accepts one.

**What stayed, and one wall that moved.** `DisplayManager_Touch.cpp` keeps three original headers (section 4.4, the S11 item). And `check_ui_boundary.py`'s wall "no identity macro outside `src/ui/`" no longer covers the 14 units, because they now sit inside `src/ui/`: a planted `#ifdef SIMUT_UI_ANGULO` in `Render.cpp` gave 2 findings with the unit in `src/` (`identity` and `no-ifdef`) and gives 1 in `src/ui/original/` (`no-ifdef`, which looks at every file), and that one still catches it. It is by design.

## 5. Tools

All take the worktree (or an ELF) as an argument and write nothing in it. The
first five are in `$B/tools/` only. The last two rows are in `tools/` of the
repository since S2b (2026-10-14), with a row each in `tools/README.md`.

| script | what it does | used in |
|---|---|---|
| `ui_funcs.py` | scans the C++ of the TFT units for function definitions (comments, literals and preprocessor lines blanked, braces followed): file, line, end, name, arity | 3.1, 3.3 |
| `ui_writes.py` | for each function, the members it writes (assignment, `++`, libc first argument, taken address), a `DashPanel&` parameter followed as `_topPanel` or `_bottomPanel`, statics listed | 3.1 |
| `ui_constcheck.py` | the compiler as a control: makes every data member of `DisplayManager` `const` in a scratch copy and lists each write as a compile error | 3.1 |
| `ui_xref.py` | which functions mention a name | 3.1 |
| `ui_touch.py` | the branches of `handleTouch` and `handleTouchPanelV24` with their immediate draws, `pushUiEvent` calls, events and repaint flags | 3.3, 3.5 |
| `tools/bytes_original.py` | sums `nm -S -C` sizes by the class of the TSV row | 3.6 |
| `tools/build9.sh`, `tools/elf_funcoes.py` | the plan's kit: build the nine images (`<out>/sha256.txt`, compared with `diff`), compare two ELFs function by function. `tools/test_elf_funcoes.py` proves the second on small ARM programs: 37 cases and 26 mutants of the tool, 2026-10-15 | later stages |
