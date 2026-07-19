/**
 * @file test_bmx280/test_bmx280.ino
 * @brief Interactive BMx280 sensor test via serial commands.
 *
 * Commands (type in Serial Monitor at 115200 baud):
 *   chipid     — Read chip ID register (0xD0)
 *   read       — Read all values (T, P, H) + raw
 *   raw        — Read raw ADC values (no compensation)
 *   calib      — Dump calibration coefficients
 *   reg ADDR   — Read any register (hex, e.g. reg F4)
 *   status     — Read status register (0xF3)
 *   mode NAME  — Set mode: sleep, forced, normal
 *   pio N      — Switch PIO block: 0 or 1
 *   gpio       — Toggle GPIO-only / PIO+DMA mode
 *   scan       — Scan I2C bus (0x03-0x77)
 *   reset      — Soft-reset and re-initialize sensor
 *   help       — Show this help
 *
 * Hardware: BMP280/BME280 on GP0 (SDA), GP1 (SCL)
 */
#include <Arduino.h>
#include <BMx280PIO_RP2040.h>
#include <WirePIO.h>

BMx280PIO_RP2040 *_sensor = nullptr;
uint8_t  _sda = 0, _scl = 1;
uint8_t  _addr = 0x76;
PIO      _pio  = pio0;
bool     _gpioOnly = false;
bool     _inited = false;
Stream  *_debug = nullptr;

// Read a hex byte from Serial (e.g. "F3" -> 0xF3)
int readHexByte() {
    while (!Serial.available()) delay(10);
    char hi = Serial.read();
    while (!Serial.available()) delay(10);
    char lo = Serial.read();
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    return (hex(hi) << 4) | hex(lo);
}

void initSensor() {
    if (_sensor) { delete _sensor; _sensor = nullptr; }
    _inited = false;

    Serial.print("Init BMx280: SDA=GP"); Serial.print(_sda);
    Serial.print(" SCL=GP"); Serial.print(_scl);
    Serial.print(" addr=0x"); Serial.print(_addr, HEX);
    Serial.print(" PIO=pio"); Serial.print(_pio == pio0 ? 0 : 1);
    Serial.print(" mode="); Serial.println(_gpioOnly ? "GPIO" : "PIO+DMA");

    _sensor = new BMx280PIO_RP2040(_sda, _scl, _addr, 200000, _pio);
    if (_gpioOnly) _sensor->forceGPIO(true);
    if (_debug) _sensor->begin(_debug);
    else _sensor->begin();

    if (!_sensor->isInitialized()) {
        Serial.print("  FAILED! Error=");
        Serial.println(_sensor->getLastError());
        Serial.print("  isForcedGPIO="); Serial.println(_sensor->isForcedGPIO());
        delete _sensor; _sensor = nullptr;
        return;
    }
    _inited = true;
    Serial.println("  OK");
}

void cmdChipId() {
    if (!_inited) { Serial.println("Sensor not initialized"); return; }
    uint8_t cid = _sensor->getChipID();
    Serial.print("Chip ID: 0x"); Serial.print(cid, HEX);
    Serial.print(" ("); Serial.print(cid); Serial.println(")");
    Serial.print("isBME280: "); Serial.println(_sensor->isBME280() ? "true (BME280)" : "false (BMP280)");
    if (cid == 0x58) Serial.println("  -> BMP280");
    else if (cid == 0x60) Serial.println("  -> BME280");
    else Serial.println("  -> UNKNOWN");
    Serial.print("isForcedGPIO: "); Serial.println(_sensor->isForcedGPIO() ? "true" : "false");
}

void cmdRead() {
    if (!_inited) { Serial.println("Sensor not initialized"); return; }
    float t = _sensor->readTemperature();
    float p = _sensor->readPressure();
    float h = _sensor->readHumidity();
    Serial.print("Temperature: "); Serial.print(t, 2); Serial.println(" C");
    Serial.print("Pressure:    "); Serial.print(p, 2); Serial.println(" hPa");
    Serial.print("Humidity:    "); Serial.print(h, 2); Serial.println(" %");
    Serial.print("isnan(T): "); Serial.print(isnan(t) ? "true" : "false");
    Serial.print("  isnan(P): "); Serial.print(isnan(p) ? "true" : "false");
    Serial.print("  isnan(H): "); Serial.println(isnan(h) ? "true" : "false");

    // Also test readAll
    float t2, p2, h2;
    _sensor->readAll(&t2, &p2, &h2);
    Serial.print("readAll: T="); Serial.print(t2, 2);
    Serial.print(" P="); Serial.print(p2, 2);
    Serial.print(" H="); Serial.print(h2, 2);
    Serial.print(" (t==t2: "); Serial.print(t == t2 ? "Y" : "N");
    Serial.print(" p==p2: "); Serial.print(p == p2 ? "Y" : "N");
    Serial.print(" h==h2: "); Serial.println(h == h2 ? "Y)" : "N)");
}

void cmdRaw() {
    if (!_inited) { Serial.println("Sensor not initialized"); return; }
    // Force measurement, then read raw registers
    _sensor->takeForcedMeasurement();
    Serial.print("Status (0xF3): 0x");
    Serial.println(_sensor->readRegister(0xF3), HEX);
    Serial.print("CTRL_MEAS (0xF4): 0x");
    Serial.println(_sensor->readRegister(0xF4), HEX);
    Serial.print("CONFIG (0xF5): 0x");
    Serial.println(_sensor->readRegister(0xF5), HEX);
    if (_sensor->isBME280()) {
        Serial.print("CTRL_HUM (0xF2): 0x");
        Serial.println(_sensor->readRegister(0xF2), HEX);
    }
}

void cmdCalib() {
    if (!_inited) { Serial.println("Sensor not initialized"); return; }
    Serial.println("Calibration registers (raw dump):");
    Serial.println("  Addr: +0 +1 +2 +3 +4 +5 +6 +7 +8 +9 +A +B +C +D +E +F");
    for (int base = 0x88; base <= 0xE7; base += 16) {
        Serial.print("  0x"); Serial.print(base, HEX); Serial.print(": ");
        for (int off = 0; off < 16; off++) {
            uint8_t v = _sensor->readRegister(base + off);
            if (v < 16) Serial.print('0');
            Serial.print(v, HEX); Serial.print(' ');
        }
        Serial.println();
    }
}

void cmdReg() {
    uint8_t reg = readHexByte();
    uint8_t val = _sensor->readRegister(reg);
    Serial.print("Reg 0x"); Serial.print(reg, HEX);
    Serial.print(" = 0x"); Serial.print(val, HEX);
    Serial.print(" ("); Serial.print(val); Serial.println(")");
}

void cmdStatus() {
    if (!_inited) { Serial.println("Sensor not initialized"); return; }
    uint8_t st = _sensor->readRegister(0xF3);
    Serial.print("Status (0xF3): 0x"); Serial.print(st, HEX);
    Serial.print(" meas="); Serial.print((st >> 3) & 1);  // measuring bit
    Serial.print(" im_update="); Serial.println(st & 1);   // im_update bit
}

void cmdMode() {
    String m = Serial.readStringUntil('\n'); m.trim(); m.toLowerCase();
    if (m == "sleep") {
        _sensor->setMode(BME280_MODE_SLEEP);
        Serial.println("Mode: SLEEP");
    } else if (m == "forced") {
        _sensor->setMode(BME280_MODE_FORCED);
        Serial.println("Mode: FORCED (one-shot, then sleep)");
    } else if (m == "normal") {
        _sensor->setMode(BME280_MODE_NORMAL);
        Serial.println("Mode: NORMAL (continuous)");
    } else {
        Serial.println("Invalid. Use: sleep, forced, normal");
    }
}

void cmdPio() {
    int n = Serial.parseInt();
    if (n == 0) _pio = pio0;
    else if (n == 1) _pio = pio1;
    else { Serial.println("Use 0 or 1"); return; }
    Serial.print("Switching to pio"); Serial.println(n);
    initSensor();
}

void cmdGpio() {
    _gpioOnly = !_gpioOnly;
    Serial.print("GPIO-only mode: "); Serial.println(_gpioOnly ? "ON" : "OFF");
    initSensor();
}

void cmdScan() {
    Serial.println("I2C Scan (GPIO bit-bang):");
    Serial.print("  ");
    for (int i = 0; i < 16; i++) { Serial.print("  "); Serial.print(i, HEX); }
    for (int row = 0; row < 8; row++) {
        Serial.print("\n  ");
        Serial.print(row, HEX); Serial.print("0:");
        for (int col = 0; col < 16; col++) {
            uint8_t addr = (row << 4) | col;
            if (addr < 0x03 || addr > 0x77) {
                Serial.print("   ");
                continue;
            }
            WirePIO scanner(_sda, _scl, 100000);
            scanner.setPIO(_pio);
            scanner.begin(WIREPIO_MODE_GPIO_ONLY);
            scanner.beginTransmission(addr);
            int err = scanner.endTransmission();
            scanner.end();
            if (err == 0) {
                Serial.print(' ');
                if (addr < 16) Serial.print('0');
                Serial.print(addr, HEX);
            } else {
                Serial.print(" --");
            }
        }
    }
    Serial.println();
}

void cmdReset() {
    Serial.println("Resetting sensor...");
    delete _sensor; _sensor = nullptr; _inited = false;
    initSensor();
}

void cmdHelp() {
    Serial.println(F(
        "\n=== BMx280 Test Commands ===\n"
        " chipid       Read chip ID (0xD0) + isBME280\n"
        " read         Read T(C), P(hPa), H(%) + readAll comparison\n"
        " raw          Read raw status/config registers\n"
        " calib        Dump all calibration registers (0x88-0xE7)\n"
        " reg XX       Read register at hex address (e.g. reg F4)\n"
        " status       Read status register (0xF3)\n"
        " mode NAME    Set mode: sleep | forced | normal\n"
        " pio N        Switch PIO block: 0 or 1\n"
        " gpio         Toggle GPIO-only / PIO+DMA\n"
        " scan         Scan I2C bus (GPIO bit-bang)\n"
        " reset        Soft-reset and re-init sensor\n"
        " help         Show this help\n"
    ));
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== BMx280 Interactive Test ===");
    Serial.println("SDA=GP0 SCL=GP1 115200 baud");
    cmdHelp();
    initSensor();
    if (_inited) cmdChipId();
}

void loop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        // Consume rest of line
        while (Serial.available()) Serial.read();

        if (cmd == "help" || cmd == "h" || cmd == "?") cmdHelp();
        else if (cmd == "chipid" || cmd == "cid") cmdChipId();
        else if (cmd == "read" || cmd == "r") cmdRead();
        else if (cmd == "raw") cmdRaw();
        else if (cmd == "calib") cmdCalib();
        else if (cmd.startsWith("reg ") || cmd.startsWith("r ")) cmdReg();
        else if (cmd == "status" || cmd == "st") cmdStatus();
        else if (cmd.startsWith("mode ")) cmdMode();
        else if (cmd.startsWith("pio ")) cmdPio();
        else if (cmd == "gpio") cmdGpio();
        else if (cmd == "scan") cmdScan();
        else if (cmd == "reset") cmdReset();
        else if (cmd.length() > 0) {
            Serial.print("Unknown: "); Serial.println(cmd);
            Serial.println("Type 'help' for commands");
        }
    }
    delay(50);
}
