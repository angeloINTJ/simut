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
| `uiBegin( )` | once, when Core 1 starts or restarts, before the loop | `DisplayManager.cpp:1287-1289`: clear the glass, draw the fixed chrome, reset the render cache **S4 (C3):** the first two lines are the body, in `ui/original/UiOriginal.h`; the cache reset stays in the core after the call. The Ângulo's marks its frame whole |
| `uiThemeChanged( )` | `_themeChanged` was raised | `:1400-1449`. The `_uiMode = MODE_DASHBOARD` at `:1439` is a mode change **(S0)**; **S4 (C4) kept it inside the hook**, in the branch that draws, so the block moved as it was (duty 12). The Ângulo's is its old `uaTemaAplicado`  |
| `uiPass( snap )` | once per loop pass, after the touch read and the OTA tick | the dispatch `:1451-1555` and `:1559-1700`, calling the core ticks where the old code had logic (section 2.4, duty 9) **S4 (C8):** the dispatch from the dashboard branch to `SET_CLOCK` (149 lines) is the body, calling the ticks where the old code had logic. The Ângulo's runs the ticks of the mode, takes the six repaint requests and calls `uaQuadro( )` (section 2.7) |
| `uiPaintOta( )` | the update screen must be on the glass, from either core | `drawOtaScreen`, `DisplayManager_Ota.cpp:150-226` **S4 (C2):** the body is the one call `drawOtaScreen( )`, which stays out of line. The Ângulo's is its old `uaOta` |
| `uiTouch( x, y )` | a touch passed the core's gates | the mode chain of `handleTouch`, `Touch.cpp:398-1682`, and `handleTouchPanelV24`, `Users.cpp:1304-1686` |
| `uiTouchUp( )` | the finger left the glass | `Touch.cpp:38-51` (the dashboard's short tap) |
| `uiChanged( kind, arg )` | a core action changed what is on screen | a `switch` over the `UiChange` kinds of section 3.5; for the original a constant `kind` folds it into the old call **S4 (C6):** begun with `ALARM_FLASH` and `ALARM_RESTORE`; the switch folds, and the Ângulo marks its frame. The other kinds come with S5 to S10a |
| `uiTopPanel( snap )` | the top card is to be shown (`redrawTopPanel( )`, after it synced the slot and mirrored the data) | **S5 (C2):** the two calls `redrawTopPanel( )` ended with, `drawSlotPanel` of the top card and `drawBottomButtons`; the Ângulo's is `uaSujo( )` |
| `uiBootSkipHit( x, y )` | a touch while the device boots with the skip button showing | **S5 (C7):** `Touch.cpp:364-365`, `y > 190 && x > 80 && x < 240`; the Ângulo asks the boot frame whether the point is on its `BOOT_PULAR` target, and only when the frame on the glass is the boot's (`s_modo` is the dashboard's) |
| `uiPages( list, items )` **(S0)** | the core must bound a page index | the arithmetic now spread over six draw bodies, `(items + 3) / 4`, the License text's wrap count, `buildDashLayout`'s button strip |
| `uiThemeCount( )`, `uiThemeId( i )`, `uiThemeName( i )` | the catalogue is read | `availableThemes` and the custom `.thm` list, `Themes.cpp:433-487` |
| `licenseOpening( ) const` | the Ângulo's licence screen needs the opening in the pack's language, which Core 0 composes into a file-static buffer | **S4 (C9):** a member defined next to that buffer, replacing the free function `uaAberturaLicenca` the Ângulo declared weak; no product calls it and the linker drops it |
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
catalogue hooks need to grow to an index-by-name lookup, a rescan and an apply.

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
  it includes (`FreeSansBold9pt8b_latin1.h`, `FreeSansBold12pt8b_latin1.h`,
  `FreeSansBold24pt7b_subset.h`), `UiWidgets.*`,
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
`WebManager`. The gate names exactly those four and lets only them include the Ângulo's headers (`SELECTION_HEADERS`).
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
(`UA_DESVIO`); the paint itself was under `C1P_RENDER` before S4 and still is. In an Ângulo image `LOOP_TAIL`, `UI_GRAPH`
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
(`uaToque( )`, `DisplayManager_UiA.cpp`), so the alarm screen's three buttons (C4), the lower card and the row (C5) and the
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
added 16 B (816,280 B, .bin 828,312 B) for the frame check in `uiBootSkipHit`. Until S10b every action is compiled twice
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
  `actStartAp( )`.

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
extract, so each driver keeps its navigation, and the original's moves into `uiTouch` in S10a. No hook and no `UiChange` came with
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
  that names no symbol, runs past the constant into a different neighbour. The move is the layout's. `UA_DESVIO( )` expands to
  `do { uaSujo( ); return; } while ( 0 )` in the Ângulo image and to nothing elsewhere (`src/ui/UiDriver.h`): no string and no
  `__LINE__`, so the macro puts no data in the unit. What its `return` did was make the rest of `drawSettingsLicense( )` unreachable.
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
`EVT_MENU_SELECT` (`AppManager_Events.cpp`). A stale dialog cannot delete an account: `uaToque( )` returns before any group when the
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
   (section 4.1, item 6). S6 touched only those two branches; the function's other six are S9's.
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
touch. They stay on the bench list below until the dialogs' touch moves into `uiTouch` (S10a), where one test can cover both drivers.

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
- **The update screen (C5), and a deviation from the plan.** The plan ends at `TRADUZIDO` = 0 on the glue; the code ends at 1. It asked for the case to stay `TRADUZIDO` only if a `TRATADO` answer built a frame on every touch sample while an update runs. `stages/s7_c5/ota_r3.txt` (read from the source, not measured on a board) found that it does not in RECEIVING: Core 1 is parked for the whole upload and runs no `handleTouch( )` and no `uiPass( )`. In READY and in the two endings (REFUSED 12 s, CUT 8 s) Core 1 runs the screen, and there `TRATADO` would make each touched pass build the view model and hash it, for a tap the phase test ignores. The decision rests on those phases, not on RECEIVING; the case stays `TRADUZIDO` and the old chain runs `actOtaDismiss( )`. S10a retires the chain and flips the case, two lines; the bench item for the flip is a finger held on the READY screen for 10 s, with `show metrics` iteration maxima compared before and after.
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
6. The header-name state belongs to the original alone; `actGraphShowName( )` may move into the original's `uiTouch` in S10a.
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
- **The pure part** (`src/display/SettingsSteps.h`, C8): `volumeStep( v, down )`, `offsetStep( v, dir )` with `DISPLAY_OFFSET_LIMIT` (it replaces the Ângulo's `OFFSET_LIMITE` and the citation that sat on it) and `langSlotCount( packLoaded, langCount )`. The plan's `langSlotCount( packLoaded )` takes the count as an argument: the first version included `SystemDefs_Records.h` for `LANG_COUNT`, and `test_main.cpp` would not compile with it (it declares `dallasCrc8( )` and `HIST_NAN_SENTINEL` itself). The three were screened before the edit was kept: with them in place `pico_w_release`, `pico_wiretft`, `pico2_w_release` and `pico_w_asserts` build to the sha256 of C5 (`stages/s8a_c8/screen_*.log`), and the verifier then held all nine; the Ângulo's functions are equal to C7's as well.

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

The study kept its glue in `src/ui_angulo/`, which is why the commands above and
the table below name that path: they read the study branch. Since S2a the same
files are in `src/ui/angulo/` of this branch (and the kit that was
`src/angulo/` is `src/ui/angulo/kit/`).

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
   (section 2.9) and left the other six to S9.
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
  `SystemDefs_Records.h`, `StorageManager.*`, `WebManager_Api.cpp` (`GET /api/keypad`),
  `WebManager_History.cpp`, `AppManager_CmdHandlers.cpp` (the CLI's `show display
  keypad`) and `AppManager_Panel.cpp`. The geometry cannot move into a driver
  with the rest of the original; S9 has to split the header.
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
  shrinks. This needs the maintainer.
- The `String` use of item 7: leave it in the original (it keeps its bytes) or
  remove it in a separate fix.
- Whether the five readers of section 5 move into `tools/` too and which of the
  tables are re-run as a gate. Three scripts moved in S2b (2026-10-14); none of
  the tables is a gate yet. S0 itself changed no file under `src/`, `tools/` or
  any generated file.
- Nothing here was run on hardware: the bench was not touched. Every number
  is from a build, an ELF or the source.

### 4.5 Product defects fixed for the Ângulo's bench image

Two defects of Core 0's history navigation (`AppManager_Events.cpp`) are fixed for the Ângulo's image only (fix/ui-angulo-historico, 2026-10-15), by pure functions in `src/display/GraphAnchor.h` called under `#if SIMUT_UI_ANGULO`. **F1:** `_graphAnchorEnd`, the window's end that a calendar day or an arrow sets, was never cleared, so every later `EVT_OPEN_GRAPH` from the dashboard, and the calendar's Today, showed that old window as the present (on the bench, 62094 s behind the clock, image 9c3e0a25, 2026-10-14); `graphAnchorOpen( )` drops the anchor, and its base, when the chart is asked for from any screen that is not `MODE_GRAPH_VIEW`, `MODE_GRAPH_DETAIL` or `MODE_GRAPH_LOADING`, the rule that the study branch's 6116de0 had inline. **F3:** one period back and one forward, a few seconds apart, ended at the first tap's clock and was judged against a later one, so the window read as the past (`fim`, ▶ still enabled), and a second ▶ reached the present with the anchor frozen at that moment. `graphAnchorStep( )` keeps beside the anchor the end of the live window at the moment the first arrow left it (`_graphLiveBase`, a static of `core0Yield( )`, 0 when the anchor did not come from the live window): a window the taps bring back there is the live window again, whatever the wait and whatever period was chosen in between (three 24 h steps back and then 1 h steps: 71 forwards stay in the past and the 72nd is live), and so is one that ends at or after the clock. A calendar day (`graphAnchorDay( )`) has no base and keeps its alignment, so the calendar's today, one step back on 24 h at 10:00, is yesterday and not the present. The first version of the fix called a window the present when it ended less than half a step before the clock; the review of 4122282 showed with a host probe that this turned yesterday into the present before noon, and the base replaced the bound. The products have the same two defects and keep their lines, so their images stay byte for byte as they are; the fix for them is the same functions without the `#if`, a change to `main` that is not made here. The four `#if` are one `identity` allowance of `AppManager_Events.cpp` in `tools/ui_boundary.json` (transitional: `ends in S12` is only the format the gate requires, and taking the fix into `main` ends it), and `check_feature_sprawl` goes 358 to 362.

Proof: `test_validators` 302 -> 316 cases; all 32 killable mutants of the header fail the cases named for them and the two equivalent ones fail none (`stages/hist_fix2/native_m01_s01.txt` to `native_m34_e02.txt`, with the clean runs before and after); a host model whose oracle is the old flush lines compiled from a6026d5, with the study's condition against the real `enum UiMode`, finds 0 differences in 468,498 comparisons and states where the new rule differs from the old lines on purpose: the window stays in the past, equal (194,203 points); it ends at or after the clock, where the old code froze the anchor at the clock and the header says live (27,802); the taps are back at the base, where the old code kept the past and the header says live (4,489) (`stages/hist_fix2/model_graphanchor.txt`). `GRAPH_SCREEN_*` in the header repeat three values of `UiMode`, because the host suite cannot include `SystemDefs_Records.h`; `AppManager_Events.cpp` pins them with a `static_assert` in every image. The Ângulo image is 811,792 B used, `.bin` 823,824 B (+80 B on a6026d5). Not run on a board; the bench should check, on the Ângulo image: after a calendar day, the dashboard's chart button and the calendar's Today open at the present (the window ends within 2 min of the clock); a period chip and the way back from the detail keep the chosen day; one ◀ then one ▶, after any wait (10 min on the 1 h period), shows the present, with no `fim` and ▶ disabled; the calendar's today on 24 h at 10:00, one ◀, is yesterday with ▶ enabled, and ▶ again is today's window, the present; one ◀ on 24 h, the chip to 1 h, then ▶: the past until the 24th ▶, which is the present; a calendar day days back, ▶ day by day: the past until the window reaches the clock.

### 4.6 Touch: what the Ângulo answers like the original, and what it does differently on purpose

On 2026-10-15 the maintainer looked at the Ângulo on the bench panel and asked that every touch point answer and that the history navigation work like the original's where the look allows (fix/ui-angulo-navegacao, on top of the touch slop and the chart's anchor). **Answered like the original now:** a touch on a control drawn disabled does nothing and reaches no neighbour (an inert target, type 0, which the glue reads as a dead zone: no gate, no touch sound); a tap anywhere in the chart's band 0,32 320x164 opens the detail (the original's central zone, 0,40 320x155, "Tap on central zone"); a tap on the detail's cards turns the page in the original's order (temperature, humidity if the sensor has it now, pressure if it has it, then back to the chart); the calendar's month arrows take one tap each. **Still different, on purpose:** "Voltar" is in the title bar (a target 32 px tall, 43 px with the slop) instead of the X in the top right corner (35x40); the detail's "Voltar" goes to the chart, not to the panel; the detail has no period footer (the original's footer buttons left the detail anyway, because `showGraphPlot( )` sets MODE_GRAPH_VIEW when the new window lands); the calendar has no footer month buttons, and its "Hoje" is at the right end of the footer; no header tap shows the sensor's name, because the title always shows it; the chart has five period chips and no zoom-step buttons. **What proves it:** `tools/tft_ui/test_alvos.py` runs 31 cases of the hit test with 10 mutants and 16 cases of the control recorder with 11 (it draws each kit control function enabled and disabled and asserts the records; a recorder that skipped the disabled ones passed every sample before, and now fails 10 cases), every mutant bitten (2026-10-15). `host_telas.py` records every control the kit and the screens draw and runs the firmware's own `ua::alvoEm( )` over every pixel of each: an enabled control must be answered by a live target on all of them, a disabled one on none, so a disabled control listed live, a live target laid over it, or an inert one laid over an enabled control fails: 1,802 controls checked in the six groups, none dimmed on purpose or excluded at the call. Its mutations are kept with the stage's records (stages/nav_fix2): the chart's disabled next arrow listed live fails exactly the 13 chart samples at the present; a live target laid over the detail's dimmed arrows, the 3 samples where one is dimmed; an inert target over the enabled "7 d" chip, the 14 chart samples. It also fails a sample whose target list drops a target or fills MAX_ALVOS (`addAlvo( )` dropped silently; the largest list is 38 of 48, the text keyboard). **The detail's cycle** is the original's rule for every page number: the first page of the sensor's list above the one on show, else the chart (the glue's comment lists the four sensor shapes). **The title** is an inert target on every screen with a title bar (`areaDoTitulo( )`): on 67c6521, 50 of the 261 samples with a title answered a live target on some pixel of its ink (20 on the chart and the detail), and none do now. The check sees the kit's controls and the few drawn by hand that are annotated (`UA_CONTROLE`), no more. **Bench:** the run of 67c6521 on 2026-10-15 at 17:15 passed the history anchor (F3), the greyed arrow and the absent month arrow, the slop between chips, the central band, the cards of a one-page sensor (cards, then the chart), the calendar's arrows and the glass edge. **Still pending**, for the image after this change: touch the title's text on the chart (grafico_24h), the detail (detalhe_umidade), the calendar (calendario_setembro) and the settings menu (menu_admin_p1, the strip under "Configurações"): nothing opens, turns or is selected, and nothing sounds; touch the end of a long title beside "Voltar" (sensor_nome_longo) and beside "Nova conta" (usuarios_3_contas): the action does not fire; on a sensor with humidity and pressure, tap the cards through temperature, humidity, pressure and back to the chart.

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
