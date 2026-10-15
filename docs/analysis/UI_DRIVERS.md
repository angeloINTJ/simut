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
| 3.4 (d) The study's glue | pending |
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

Pending.

### 3.5 (e) Immediate draws in touch handling

Pending.

### 3.6 (f) Bytes of the original driver

Pending.

## 4. Where the code contradicts the plan

Pending.
