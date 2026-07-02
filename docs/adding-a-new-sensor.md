# Adding a New Sensor to SIMUT

SIMUT collects real-world measurements from physical sensors on a Raspberry Pi Pico W
and pushes them to a local dashboard, history log, and optional telemetry endpoint.
If you maintain a cold-chain logger, greenhouse node, or small lab instrument, this
guide is the shortest path to wiring **your** sensor in without forking half the firmware.

Flash is tight (~98% on a 2 MB part). New drivers must earn their KB.

---

## Mental model

Each sensor type is a **driver** under `src/sensors/`. The rest of the firmware
(dashboard, web API, telemetry batching, calibration UI) reads **metadata** from
`SensorFormat::forType()` — not hardcoded `if (type == DHT22)` branches.

```
  GPIO slot config  →  SensorManager read loop  →  avgValue[] buffers
         ↓                      ↓                         ↓
  SensorFormat metadata   YourDriver::update()     Web + tel export
         ↓
  SensorPanelDispatch → YourDriver::renderPanel()
```

Adding a sensor is usually **three touch points** (sometimes four if you need TFT icons):

| Step | File | What |
|------|------|------|
| 1 | `src/sensors/YourDriver.h` | Hardware init, async read, optional panel render |
| 2 | `src/sensors/SensorHelpers.h` | `SensorFormat::forType()` entry (channels + pins) |
| 3 | `src/sensors/SensorConfig.h` | `#define SIMUT_SENSOR_YOURS 1` feature flag |
| 4 | `src/sensors/SensorPanelDispatch.h` | `case TYPE_YOURS:` if you implement `renderPanel` |

Disable unused drivers in `platformio.ini` to reclaim flash:

```ini
build_flags =
    -DSIMUT_SENSOR_DS18B20=0
    -DSIMUT_SENSOR_DHT22=0
    -DSIMUT_SENSOR_BME280=1
    -DSIMUT_SENSOR_YOURS=1
```

---

## Step 1 — Pick a `SensorType` value

Open `src/SystemDefs_Records.h` and add an enum entry **before** `TYPE_COUNT`:

```cpp
TYPE_SOIL_MOISTURE = 12,  // example — use next free ID
```

Register a human name in `sensorTypeName()` inside `SensorHelpers.h`.

---

## Step 2 — Declare channels and pins

In `SensorHelpers.h`, extend `SensorFormat::forType()`. Example for a single ADC soil probe:

```cpp
#if SIMUT_SENSOR_SOIL_MOISTURE
case TYPE_SOIL_MOISTURE: {
    static const PinRequirement pins[] = {
        { ROLE_ANALOG, "ADC", 0 }
    };
    static const SensorValueFormat vals[] = {
        { "%", 1, ICON_DROP }   // icon IDs in SensorPresets.h
    };
    return { 1, 1, pins, vals };
}
#endif
```

- `pinCount` / `valueCount` must match the arrays.
- Channels map to `CH_TEMP`, `CH_HUM`, `CH_PRESS`, `CH_LUX` or custom indices consumed by your driver.
- I2C sensors use two pins (`ROLE_I2C_SDA`, `ROLE_I2C_SCL`). See `BME280Driver.h`.

After this, CLI `show sensor types`, GPIO map, and telemetry JSON pick up the new type automatically.

---

## Step 3 — Write the driver header

Drivers are header-only structs with an async state machine (same pattern as DS18B20 / DHT22 / BME280).

Skeleton:

```cpp
#pragma once
#if SIMUT_SENSOR_SOIL_MOISTURE

#include <Arduino.h>

struct SoilMoistureDriver {
    enum State { IDLE, WAITING };
    State state = IDLE;
    uint32_t timer = 0;
    int currentSensorIdx = -1;

    void begin() { /* one-time ADC setup */ }

  bool startRead(int idx, const SensorRecord& cfg) {
        (void)cfg;
        currentSensorIdx = idx;
        state = WAITING;
        timer = millis();
        return true;
    }

    bool isDone() const { return state == IDLE; }

    void cancel() { state = IDLE; currentSensorIdx = -1; }

    float getResult() const { return _last; }

private:
    float _last = NAN;
};

#endif
```

Hook the driver into `SensorManager.cpp` the same way existing `#if SIMUT_SENSOR_*` blocks do:
scan, init slot, poll `startRead` / `isDone`, write into `readings[idx].avgValue[CH_*]`.

**Rules learned in production:**

- Never block inside `startRead` — RP2040 Core 0 also serves WiFi and HTTP.
- Prefer forced-mode I2C (see BME280) over continuous sampling; it saves power in battery installs.
- PIO slots are scarce (2 used). Bit-bang I2C on any GPIO is issue #67 territory.

---

## Step 4 — Display (optional but nice)

If `SIMUT_DISPLAY_TFT=1`, add:

```cpp
void SoilMoisture_renderPanel(GFXcanvas16* cv, float pct, bool valid, ...);
```

Register in `SensorPanelDispatch.h`. Reuse primitives from `SensorDrawing.h` when possible —
duplicated layout code costs flash *and* drifts from themes.

---

## Step 5 — Configure on device

1. Wire per `docs/WIRING.md` (GPIO 0–15 are universal slots).
2. USB serial CLI (no auth): assign type and pins, e.g.  
   `sensor 3 pin 10` or I2C pair via web `/config`.
3. `sensor scan` — confirm the bus sees the chip.
4. Set alarms: `conf sensor tmin 3 5.0` (field names depend on channels).
5. Point telemetry: `conf tel server your-collector.local` then `tel sync`.

Hold touch 3 s at boot for AP mode if WiFi isn't provisioned yet.

---

## Step 6 — Verify

```bash
pio run -e pico_w_release
pio test -e native          # validators
pio test -e native_cli      # CLI parser (USB path)
```

On hardware: watch `show metrics` and confirm readings move when you disturb the sensor.

---

## Flash checklist

| Check | Target |
|-------|--------|
| Build with only your sensor enabled | Saves max KB |
| `pio run` flash % | Must stay < 100% |
| `test/test_validators` | 49/49 pass |
| No new `<Wire.h>` unless unavoidable | Wire pulls Arduino I2C stack |

---

## Examples in-tree

| Driver | Protocol | Channels | File |
|--------|----------|----------|------|
| DS18B20 | 1-Wire PIO | Temp | `DS18B20Driver.h` |
| DHT22 | Single-wire PIO | Temp + Hum | `DHT22Driver.h` |
| BME280 | I2C bare-metal | Temp + Hum + Press | `BME280Driver.h` |

---

## Getting help

- Wiring photos / pin questions → [Discussion #48](https://github.com/angeloINTJ/simut/discussions/48)
- Architecture roadmap → [Discussion #49](https://github.com/angeloINTJ/simut/discussions/49)
- Plugin architecture tracking → Issue #63

If you add a driver for a sensor used in agriculture, food safety, or community environmental monitoring, open a PR — those deployments are why the "U" in SIMUT exists.
