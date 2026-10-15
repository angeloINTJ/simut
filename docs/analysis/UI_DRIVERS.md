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

Status of the sections, updated in the commit that fills each one:

| section | state |
|---|---|
| 2. The contract | pending |
| 3.1 (a) Members written inside draw bodies | done |
| 3.2 (b) Callers outside the display units | done |
| 3.3 (c) Every function, classified | pending |
| 3.4 (d) The study's glue | done |
| 3.5 (e) Immediate draws in touch handling | done |
| 3.6 (f) Bytes of the original driver | pending |
| 4. Where the code contradicts the plan | pending |

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

Pending.

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

Pending.

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
    GRAPH,            // _repaintGraph            graph and detail screens        7
    CALENDAR,         // _repaintCalendar         the calendar                    2
    LOADING,          // _repaintLoading          the graph's loading screen      1
    DASH_DATA,        // _isDirty                 dashboard values                20
    DASH_FULL,        // _forceFullRedraw         the whole dashboard             12
};
```

Fifteen kinds, inside the plan's estimate of 15 to 25. Sites by function kind
for the two big flags: `_repaintSettings` is raised 56 times in `handleTouch`,
23 in `handleTouchPanelV24`, 24 in the `show*` and `enterPinFor` entry points,
4 in `loopCore1` and 7 elsewhere; `_forceSettingsRedraw` 17, 4, 24, 3 and 3.
Only the first five kinds need a call at the touch site; the rest can stay as
the original wrote them until a stage decides otherwise.

### 3.6 (f) Bytes of the original driver

Pending.

## 4. Where the code contradicts the plan

Pending.
