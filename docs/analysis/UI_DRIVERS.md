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
| 3.1 (a) Members written inside draw bodies | pending |
| 3.2 (b) Callers outside the display units | pending |
| 3.3 (c) Every function, classified | pending |
| 3.4 (d) The study's glue | done |
| 3.5 (e) Immediate draws in touch handling | pending |
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

Pending.

### 3.2 (b) Callers outside the display units

Pending.

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

Pending.

### 3.6 (f) Bytes of the original driver

Pending.

## 4. Where the code contradicts the plan

Pending.
