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
the nine baseline images. The scripts stay outside the repository for now; S0
adds only this document and its TSV.

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
| `src/ui/original/`, `src/ui/angulo/` | the original's inline hook headers (its units move there only in S11); the Ângulo's glue, screens and kit (fonts, icons, tokens) |

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

All are private `DisplayManager` members, declared without `#if`. The right
column is where the original's body comes from on `192e1ad`.

| hook | the core calls it | original: body from |
|---|---|---|
| `uiBegin( )` | once, when Core 1 starts or restarts, before the loop | `DisplayManager.cpp:1287-1289`: clear the glass, draw the fixed chrome, reset the render cache |
| `uiThemeChanged( )` | `_themeChanged` was raised | `:1400-1449`. The `_uiMode = MODE_DASHBOARD` at `:1439` is a mode change and stays in the core **(S0)** |
| `uiPass( snap )` | once per loop pass, after the touch read and the OTA tick | the dispatch `:1451-1555` and `:1559-1700`, calling the core ticks where the old code had logic (section 2.4, duty 9) |
| `uiPaintOta( )` | the update screen must be on the glass, from either core | `drawOtaScreen`, `DisplayManager_Ota.cpp:150-226` |
| `uiTouch( x, y )` | a touch passed the core's gates | the mode chain of `handleTouch`, `Touch.cpp:398-1682`, and `handleTouchPanelV24`, `Users.cpp:1304-1686` |
| `uiTouchUp( )` | the finger left the glass | `Touch.cpp:38-51` (the dashboard's short tap) |
| `uiChanged( kind, arg )` | a core action changed what is on screen | a `switch` over the `UiChange` kinds of section 3.5; for the original a constant `kind` folds it into the old call |
| `uiPages( list, items )` **(S0)** | the core must bound a page index | the arithmetic now spread over six draw bodies, `(items + 3) / 4`, the License text's wrap count, `buildDashLayout`'s button strip |
| `uiThemeCount( )`, `uiThemeId( i )`, `uiThemeName( i )` | the catalogue is read | `availableThemes` and the custom `.thm` list, `Themes.cpp:433-487` |
| `WebManager::registerUiDriverRoutes( )` | the web server registers routes | empty inline; the Ângulo adds `GET /api/ui` |

`uiChanged` lets an action call it exactly where the original drew at once, for
example `drawBottomButtons` at `Touch.cpp:566` and `:577`, or `redrawTopPanel` at
`:50` and `:509`. The theme hooks do not cover every caller outside the
display units **(S0)**: section 3.2 lists `getThemeIndexByName` (the CLI),
`scanCustomThemes` (boot, and after a `.thm` is uploaded or deleted),
`loadTheme` and `getThemePalette( i )->displayName` (the web API), so the
catalogue hooks need to grow to an index-by-name lookup, a rescan and an apply.

### 2.3 Core actions

The business parts of the per-mode branches of `handleTouch` and
`handleTouchPanelV24` become public `act*( )` members, which both drivers call.
The per-mode table of section 3.3 is the work list: 34 branch rows, 38
`pushUiEvent` calls and 19 immediate draws in them. The plan estimates 70 to 90
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
  the 3 s graph header name, `Themes.cpp`, `DisplayManager_Fonts.*`, `UiWidgets.*`,
  `PasswordKeyboard.h` and `sensors/SensorDrawing.h`.
- **The mixed units** are settled by section 3.3: 98 original function rows, 183
  core and 9 mixed, per unit. S10a cuts along those rows.

### 2.6 Room for a 3.5-inch panel

The panel size is meant to come from the panel layer: `Ili9341_320x240` carries
`width` and `height` (`src/display/ILI9341_320x240.h:21-22`). Today the size is
written into core primitives as a literal: `fillWindowDma` and `blitWindowDma`
(`Dashboard.cpp:272, 296`), the clamp in `fastFillRect` (`:327-328`),
`fastClearScreen` (`:345-346`), the strip blits (`:424, 445, 454`), the
`GFXcanvas16( 320, 45 )` at `DisplayManager.cpp:1258`, `mapTouchPoint`
(`Calibration.cpp:416-417`, the mapping to 320 and 240), the calibration targets
`CAL_SCR_X/Y` (`DisplayManager.h:1434-1435`) and `Touch.cpp:112-123`. The original's
layouts stay 320x240. Visual changes (contrast, font sizes) are made in the
Ângulo's own files only: `src/ui/angulo/kit/angulo_tokens.h` for colours
(the source of truth is the `simut-rx` tokens), `Angulo.cpp` for the themes,
`Angulo.h` for spacing and the font roles, `kit/fontes/*.h`, `AnguloMeta.h`,
and `telas/*.cpp`, checked by `tools/tft_ui/host_*.py`. `AGENTS.md` section 7
("the TFT panel is not Ângulo") changes when the Ângulo ships.

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
| `drawAlarmEdit` `Settings.cpp:222-334` | `_tempAlarmConfig.chMax[]` 241, 243, `.chMin[]` 244 | `editedAlarmRecord( )`: Core 0 diffs it on `EVT_SAVE_ALARMS`; `handleTouch` (15 uses) | the draw normalises the working copy (min below max, clamped to `saneMax`): configuration data written by a draw function |
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
| `PinKeypad.h` (`PinKb::`) | `SystemDefs_Validate.h`, `StorageManager.*`, `WebManager_Api.cpp` (`GET /api/keypad`), `WebManager_History.cpp`, `AppManager_CmdHandlers.cpp`, `AppManager_Panel.cpp` | PIN policy and matching (core) share a header with the keypad's geometry (original); S9 splits it |
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

**How it was produced and checked.** The definitions came from
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

**The nine mixed functions, whole.** The per-mode rows below are views into two
of them.

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

**`UA_DESVIO( );` in the product files.** The macro is `do { uaSujo( ); return; } while ( 0 )`
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
are the list S2a imports and S4 and S10b remove: `DisplayManager.h:41-55` and
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
second time in the glue, which the core actions of section 2.3 replace.

| glue file (`src/ui_angulo/`) | `return UaToque::TRADUZIDO` | `return UaToque::TRATADO` | every mention |
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
grep -nE '\b(draw|redraw|blit)\w*\(' src/DisplayManager_Touch.cpp src/DisplayManager_Users.cpp
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
3.1 group C, and the seven `g_*` caches of `DisplayManager_Calibration.cpp`.
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
python3 $B/tools/bytes_original.py --tsv docs/analysis/UI_DRIVERS_funcoes.tsv --src <worktree> \
    --top 25 --unmapped $BASE/pico_wiretft/firmware.elf $BASE/pico_w_release/firmware.elf
```

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
   USERS, PIN_POLICY, USER_EDIT and USER_CONFIRM_DEL.
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
    which mirrors `CAL_SCR_X/Y` = {20, 300}, {20, 220} (`DisplayManager.h:1434-1435`)
    where the crosshairs are drawn;
  - GRAPH_VIEW and GRAPH_DETAIL carry the same close, header-tap and bottom-bar code,
    55 lines apiece, identical but for three comments (`Touch.cpp:585-642` and
    `:653-708`, compared after whitespace normalisation).
- **The PIN keypad's hit-test geometry is in `PinKeypad.h`**, beside the policy and
  the matching, and that header is included by `SystemDefs_Validate.h`,
  `SystemDefs_Records.h`, `StorageManager.*`, `WebManager_Api.cpp` (`GET /api/keypad`),
  `WebManager_History.cpp`, `AppManager_CmdHandlers.cpp` (the CLI's `show display
  keypad`) and `AppManager_Panel.cpp`. The geometry cannot move into a driver
  with the rest of the original; S9 has to split the header.
- **The panel size, 320 by 240, is written into core primitives**: `Dashboard.cpp:272,
  296` (the DMA window checks), `:327-328` (the clamp in `fastFillRect`), `:345-346`
  (`fastClearScreen`), `:424, 445, 454` (the strip blits), `DisplayManager.cpp:1258`
  (`GFXcanvas16( 320, 45 )`), `Calibration.cpp:416-417` (`mapTouchPoint`) and the
  calibration targets (section 2.6).
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
  shrinks. This needs the maintainer.
- The `String` use of item 7: leave it in the original (it keeps its bytes) or
  remove it in a separate fix.
- Whether the scripts of section 5 move into `tools/` and which of the tables are
  re-run as a gate. Nothing in S0 changes `src/`, `tools/` or any generated
  file.
- Nothing here was run on hardware: the bench was not touched. Every number
  is from a build, an ELF or the source.

## 5. Tools

All are in `$B/tools/`, take the worktree (or an ELF) as an argument, and write
nothing in it.

| script | what it does | used in |
|---|---|---|
| `ui_funcs.py` | scans the C++ of the TFT units for function definitions (comments, literals and preprocessor lines blanked, braces followed): file, line, end, name, arity | 3.1, 3.3 |
| `ui_writes.py` | for each function, the members it writes (assignment, `++`, libc first argument, taken address), a `DashPanel&` parameter followed as `_topPanel` or `_bottomPanel`, statics listed | 3.1 |
| `ui_constcheck.py` | the compiler as a control: makes every data member of `DisplayManager` `const` in a scratch copy and lists each write as a compile error | 3.1 |
| `ui_xref.py` | which functions mention a name | 3.1 |
| `ui_touch.py` | the branches of `handleTouch` and `handleTouchPanelV24` with their immediate draws, `pushUiEvent` calls, events and repaint flags | 3.3, 3.5 |
| `bytes_original.py` | sums `nm -S -C` sizes by the class of the TSV row | 3.6 |
| `build9.sh`, `elf_funcoes.py` | the plan's kit: build the nine images, compare two ELFs function by function | later stages |
