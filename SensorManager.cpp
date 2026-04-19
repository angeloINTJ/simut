/**
 * @file    SensorManager.cpp
 * @brief   Implementation of SensorManager — async sensor reads, scan, and data processing.
 * @details Implements parallel DS18B20 conversion with ROM verification,
 * fully asynchronous DHT22 reading via PIO state machine,
 * hardware scan across GPIO 0-16, error hysteresis (3 consecutive
 * failures to flag, 5 successes to recover), zero-trust hardware
 * mismatch blocking, and trimmed mean filtering.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "SensorManager.h"
#include <algorithm>
#include <vector>
#include <cstring>


/**
 * Static sort buffer for trimmed mean calculation — zero heap allocation.
 * Safe: processPeriodicReads() is single-threaded on Core 0.
 */
static float _trimSortBuf[MOVING_AVG_WINDOW];

/**
 * @brief Compute trimmed mean from a ring buffer (removes 20% outliers).
 * Uses a static sort buffer to avoid heap allocation in the hot path.
 */
static float calculateTrimmedMean(const RingBuffer& ring) {
    if (ring.empty()) return NAN;

    uint8_t size = ring.size();
    ring.copyTo(_trimSortBuf);
    std::sort(_trimSortBuf, _trimSortBuf + size);

    if (size < 5) {
        float sum = 0;
        for (uint8_t i = 0; i < size; i++) sum += _trimSortBuf[i];
        return sum / size;
    }


    uint8_t trimCount = size / 5;
    float sum = 0;
    uint8_t count = 0;

    for (uint8_t i = trimCount; i < (size - trimCount); i++) {
        sum += _trimSortBuf[i];
        count++;
    }

    return (count > 0) ? (sum / count) : NAN;
}


SensorManager::SensorManager()
    : _oneWireBus(pio0),
      _ds18Sensor(_oneWireBus),
      _dhtBus(pio1),
      _dhtSensor(_dhtBus)
{
}

void SensorManager::begin() {
    _oneWireBus.begin(PIN_ONEWIRE_DEFAULT);
    _dhtBus.begin(PIN_DHT_DEFAULT);


    gpio_set_pulls(PIN_DHT_DEFAULT, true, false);
}

/**
 * @brief Build runtime sensor list from persistent configuration.
 * Always creates the ambient DHT22 sensor first (GPIO 10),
 * then adds all active configured sensors with proper type detection.
 */
void SensorManager::initRuntimeSensors(const SystemConfig &cfg) {
    _runtimeSensors.clear();


    RuntimeSensor ambient;
    ambient.config.active = true;
    ambient.config.gpio = PIN_DHT_DEFAULT;
    memset(ambient.config.rom, 0, 8);
    strcpy(ambient.config.friendlyName, "Ambiente_Fixo");
    strcpy(ambient.config.hwId, "AMB001");
    ambient.type = TYPE_DHT22;
    ambient.readInterval = 2000;
    ambient.bufferFull = false;
    ambient.calibrationOffset = 0.0f;
    ambient.inErrorState = false;
    ambient.lastReadTime = 0;
    ambient.totalReadings = 0;
    ambient.consecutiveErrors = 0;
    ambient.consecutiveSuccess = 0;
    ambient.hardwareMismatch = false;
    _runtimeSensors.push_back(ambient);


    for (int i = 0; i < MAX_SENSORS; i++) {
        if (cfg.sensors[i].active) {
            if (cfg.sensors[i].gpio == PIN_DHT_DEFAULT) continue;

            RuntimeSensor rs;
            rs.config = cfg.sensors[i];
            rs.calibrationOffset = 0.0f;

            rs.bufferFull = false;
            rs.avgValue1 = NAN;
            rs.avgValue2 = NAN;
            rs.lastReadTime = 0;
            rs.totalReadings = 0;
            rs.consecutiveErrors = 0;
            rs.consecutiveSuccess = 0;
            rs.inErrorState = false;
            rs.hardwareMismatch = false;
            rs.calibrationOffset = 0.0f;

            bool isDs18 = false;
            for(int k=0; k<8; k++) if(rs.config.rom[k] != 0) isDs18 = true;

            if (isDs18) {
                rs.type = TYPE_DS18B20;
                rs.readInterval = 1000;
            }
            else {
                rs.type = TYPE_DHT22;
                rs.readInterval = 2000;


                gpio_init(rs.config.gpio);
                gpio_set_pulls(rs.config.gpio, true, false);
            }

            _runtimeSensors.push_back(rs);
        }
    }
    LOG_CODE(LOG_INFO, "SENSOR", SENSOR_RUNTIME_LOADED, _runtimeSensors.size(), "");
}


/**
 * @brief Synchronize alarm thresholds from Flash config to runtime sensors.
 * Only copies alarm fields — preserves buffers, averages, and error counters.
 * Called after saving limits via display or web interface.
 */
void SensorManager::syncAlarmLimits(const SystemConfig &cfg) {
    for (auto &rs : _runtimeSensors) {

        if (strcmp(rs.config.hwId, "AMB001") == 0) {
            rs.config.tempMin      = cfg.ambientSensor.tempMin;
            rs.config.tempMax      = cfg.ambientSensor.tempMax;
            rs.config.humMin       = cfg.ambientSensor.humMin;
            rs.config.humMax       = cfg.ambientSensor.humMax;
            rs.config.alarmsActive = cfg.ambientSensor.alarmsActive;
            continue;
        }


        for (int i = 0; i < MAX_SENSORS; i++) {
            if (!cfg.sensors[i].active) continue;
            if (cfg.sensors[i].gpio == rs.config.gpio) {
                rs.config.tempMin      = cfg.sensors[i].tempMin;
                rs.config.tempMax      = cfg.sensors[i].tempMax;
                rs.config.humMin       = cfg.sensors[i].humMin;
                rs.config.humMax       = cfg.sensors[i].humMax;
                rs.config.alarmsActive = cfg.sensors[i].alarmsActive;
                break;
            }
        }
    }
}

bool SensorManager::identifyPhysicalSensor(uint8_t gpio, uint8_t* romOut) {
    if (_ds18Sensor.readROM(gpio, romOut)) {
        return true;
    }
    return false;
}

bool SensorManager::checkRomMatch(const uint8_t* romRead, const uint8_t* romConfig) {
    for (int i=0; i<8; i++) {
        if (romRead[i] != romConfig[i]) return false;
    }
    return true;
}

void SensorManager::handleSensorResult(RuntimeSensor &s, bool success, float v1, float v2, const char* errorMsg) {
    LogCode code = SYS_OK;

    if (!success) {
        if (strstr(errorMsg, "Timeout") != nullptr)       code = ERR_SENSOR_TIMEOUT;
        else if (strstr(errorMsg, "Checksum") != nullptr)  code = ERR_SENSOR_CHECKSUM;
        else if (strstr(errorMsg, "CRC") != nullptr)       code = ERR_SENSOR_CRC;
        else if (strstr(errorMsg, "Range") != nullptr)     code = ERR_SENSOR_RANGE;
        else if (strstr(errorMsg, "Missing") != nullptr)   code = ERR_SENSOR_MISSING;
        else if (strstr(errorMsg, "Mismatch") != nullptr)  code = ERR_SENSOR_MISMATCH;
        else code = ERR_UNKNOWN;
    }

    if (success) {
        s.consecutiveErrors = 0;
        s.consecutiveSuccess++;


        if (s.inErrorState && s.consecutiveSuccess >= 5) {
            s.inErrorState = false;
            LOG_CODE(LOG_INFO, "SENSOR", LOG_SENSOR_REC, s.config.gpio, TRL("Sensor recovered", "Sensor recuperado"));
        }

        if (!s.inErrorState) {
            addSample(s, v1 + s.calibrationOffset, v2);
        }
    }
    else {
        s.consecutiveSuccess = 0;
        s.consecutiveErrors++;


        if (!s.inErrorState && s.consecutiveErrors >= 3) {
            s.inErrorState = true;

            LOG_CODE(LOG_ERROR, "SENSOR", code, s.config.gpio, String(errorMsg));
        }
    }
}

void SensorManager::update() {

    if (isScanning()) {
        _dhtSensor.update();

        if (_scanState == IDLE || _scanState == COMPLETE) return;

        switch (_scanState) {
            case SETUP_PIN:
                gpio_init(_currentScanPin);
                gpio_set_pulls(_currentScanPin, true, false);
                _scanState = ONEWIRE_RESET;
                break;

            case ONEWIRE_RESET:
                _oneWireBus.setPin(_currentScanPin);
                _oneWireBus.sendReset();
                _scanTimer = micros();
                _scanState = ONEWIRE_WAIT;
                break;

            case ONEWIRE_WAIT:
                if (micros() - _scanTimer >= 1200) {
                    if (_oneWireBus.isSensorPresent()) {
                        ScanResult res;
                        res.pin = _currentScanPin;
                        res.type = TYPE_DS18B20;
                        if (_ds18Sensor.readROM(_currentScanPin, res.rom)) {
                            _scanResults.push_back(res);
                            _scanState = NEXT_PIN;
                        } else {
                            _scanState = DHT_REQUEST;
                        }
                    } else {
                        _scanState = DHT_REQUEST;
                    }
                }
                break;

            case DHT_REQUEST:
                _dhtSensor.requestReading(_currentScanPin);
                _scanTimer = millis();
                _scanState = DHT_WAIT;
                break;

            case DHT_WAIT: {
                    DHT22PIO::State s = _dhtSensor.getState();
                    if (s == DHT22PIO::DATA_READY || s == DHT22PIO::ERROR_CHECKSUM) {
                        ScanResult res;
                        res.pin = _currentScanPin;
                        res.type = TYPE_DHT22;
                        memset(res.rom, 0, 8);
                        _scanResults.push_back(res);
                        _scanState = NEXT_PIN;
                    } else if (s == DHT22PIO::ERROR_TIMEOUT || (millis()-_scanTimer > 150)) {
                        _scanState = NEXT_PIN;
                    }
                }
                break;

            case NEXT_PIN:
                _currentScanPin++;
                if (_currentScanPin > 16) {
                    _oneWireBus.setPin(PIN_ONEWIRE_DEFAULT);
                    _scanState = COMPLETE;
                } else {
                    _scanState = SETUP_PIN;
                }
                break;

            default: break;
        }
        return;
    }

    processPeriodicReads();
}

/* =========================================================================== */
/*                  PERIODIC SENSOR READING STATE MACHINES                   */
/* =========================================================================== */
/**
 * @brief Execute async reading cycles for all sensor types.
 *
 * DS18B20: Parallel mass conversion — all sensors start simultaneously,
 *          results collected after 750ms conversion time.
 * DHT22:  Sequential one-at-a-time via PIO state machine (non-blocking).
 */
void SensorManager::processPeriodicReads() {
    uint32_t now = millis();


    if (_dsState == DS_IDLE) {
        bool needsRead = false;
        for (auto &s : _runtimeSensors) {
            if (s.type == TYPE_DS18B20 && (now - s.lastReadTime >= s.readInterval)) {
                needsRead = true; break;
            }
        }

        if (needsRead) {
            for (auto &s : _runtimeSensors) {
                if (s.type == TYPE_DS18B20) _ds18Sensor.requestTemperatures(s.config.gpio);
            }
            _dsTimer = now;
            _dsState = DS_WAITING;
        }
    }
    else if (_dsState == DS_WAITING) {
        if (now - _dsTimer >= DS_CONVERSION_TIME) {
            for (auto &s : _runtimeSensors) {
                if (s.type == TYPE_DS18B20) {


                    if (s.hardwareMismatch) {
                        if (!s.inErrorState) {
                            LOG_CODE(LOG_ERROR, "SENSOR", ERR_SENSOR_MISMATCH, s.config.gpio, TRL("Hardware Mismatch (Access Denied)", "Divergencia de hardware (Acesso Negado)"));
                        }
                        s.inErrorState = true;
                        s.buffer1.clear();
                        s.bufferFull = false;
                        s.avgValue1 = NAN;
                        s.consecutiveSuccess = 0;
                        s.lastReadTime = now;
                        __atomic_store_n(&_newDataAvailable, true, __ATOMIC_RELEASE);
                        continue;
                    }

                    s.totalReadings++;
                    bool romVerified = true;
                    const char* failReason = "";


                    if (s.totalReadings % 5 == 0) {
                        uint8_t currentRom[8];
                        if (_ds18Sensor.readROM(s.config.gpio, currentRom)) {
                            if (!checkRomMatch(currentRom, s.config.rom)) {
                                romVerified = false;
                                failReason = "ROM Mismatch";
                                s.hardwareMismatch = true;
                            }
                        } else {
                            romVerified = false; failReason = "ROM Read Failed";
                        }
                    }

                    if (romVerified) {
                        float tempC = 0.0f;
                        bool success = _ds18Sensor.getTemperatureValidated(s.config.gpio, tempC, DS18B20PIO::CELSIUS);

                        if (!success) handleSensorResult(s, false, 0, 0, "CRC/Read Error");
                        else if (tempC < -50 || tempC > 150) handleSensorResult(s, false, 0, 0, "Out of Range");
                        else handleSensorResult(s, true, tempC, NAN, "");
                        s.lastReadTime = now;
                    } else {
                        handleSensorResult(s, false, 0, 0, failReason);
                        s.lastReadTime = now;
                    }
                }
            }
            _dsState = DS_IDLE;
        }
    }


    if (_dhtState == DHT_IDLE) {

        for (size_t i = 0; i < _runtimeSensors.size(); i++) {
            auto &s = _runtimeSensors[i];
            if (s.type == TYPE_DHT22 && (now - s.lastReadTime >= s.readInterval)) {
                _dhtSensor.reset();
                _dhtSensor.requestReading(s.config.gpio);

                _dhtTimer = millis();
                _dhtCurrentSensorIdx = i;
                _dhtState = DHT_WAITING;
                break;
            }
        }
    }
    else if (_dhtState == DHT_WAITING) {

        if (_dhtCurrentSensorIdx >= 0 && _dhtCurrentSensorIdx < (int)_runtimeSensors.size()) {
            auto &s = _runtimeSensors[_dhtCurrentSensorIdx];

            _dhtSensor.update();
            DHT22PIO::State st = _dhtSensor.getState();

            if (st == DHT22PIO::DATA_READY) {
                float t, h;
                if (_dhtSensor.getResults(t, h)) {
                    handleSensorResult(s, true, t, h, "");
                } else {
                    handleSensorResult(s, false, 0, 0, "Checksum Error");
                }
                _dhtSensor.reset();
                s.lastReadTime = millis();
                _dhtState = DHT_IDLE;
            }
            else if (st == DHT22PIO::ERROR_TIMEOUT || st == DHT22PIO::ERROR_CHECKSUM) {
                const char* errMsg = (st == DHT22PIO::ERROR_TIMEOUT) ? "Sensor Timeout" : "Checksum Error";
                handleSensorResult(s, false, 0, 0, errMsg);
                _dhtSensor.reset();
                s.lastReadTime = millis();
                _dhtState = DHT_IDLE;
            }

            else if (millis() - _dhtTimer > 100) {
                handleSensorResult(s, false, 0, 0, "Sensor Timeout");
                _dhtSensor.reset();
                s.lastReadTime = millis();
                _dhtState = DHT_IDLE;
            }
        } else {

            _dhtState = DHT_IDLE;
        }
    }
}

/**
 * @brief Add a new reading to the sensor's ring buffer and update averages.
 * Uses trimmed mean when buffer is full, simple mean otherwise.
 * Sets the atomic _newDataAvailable flag for cross-core notification.
 */
void SensorManager::addSample(RuntimeSensor &sensor, float v1, float v2) {
    if (!isnan(v1)) {
        sensor.buffer1.push(v1);
    }
    if (sensor.type == TYPE_DHT22 && !isnan(v2)) {
        sensor.buffer2.push(v2);
    }

    if (!sensor.buffer1.empty()) {
        if (sensor.buffer1.full()) {
            sensor.bufferFull = true;
            sensor.avgValue1 = calculateTrimmedMean(sensor.buffer1);
            if (sensor.type == TYPE_DHT22) sensor.avgValue2 = calculateTrimmedMean(sensor.buffer2);
        } else {

            float sortBuf[MOVING_AVG_WINDOW];
            sensor.buffer1.copyTo(sortBuf);
            float sum1 = 0;
            for (uint8_t i = 0; i < sensor.buffer1.size(); i++) sum1 += sortBuf[i];
            sensor.avgValue1 = sum1 / sensor.buffer1.size();

            if (sensor.type == TYPE_DHT22 && !sensor.buffer2.empty()) {
                sensor.buffer2.copyTo(sortBuf);
                float sum2 = 0;
                for (uint8_t i = 0; i < sensor.buffer2.size(); i++) sum2 += sortBuf[i];
                sensor.avgValue2 = sum2 / sensor.buffer2.size();
            }
        }

        __atomic_store_n(&_newDataAvailable, true, __ATOMIC_RELEASE);
    }
}

bool SensorManager::hasNewReadings() {

    bool expected = true;
    return __atomic_compare_exchange_n(&_newDataAvailable, &expected,
                                       false, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

const std::vector<RuntimeSensor>& SensorManager::getRuntimeSensors() const {
    return _runtimeSensors;
}


void SensorManager::startScan() {
    if (_scanState != IDLE && _scanState != COMPLETE) return;
    _scanResults.clear();
    _currentScanPin = 0;
    _scanState = SETUP_PIN;
}

bool SensorManager::isScanning() {
    return (_scanState != IDLE && _scanState != COMPLETE);
}

bool SensorManager::getScanResults(std::vector<ScanResult> &results) {
    if (_scanState == COMPLETE) {
        results = _scanResults;
        _scanState = IDLE;
        return true;
    }
    return false;
}

bool SensorManager::setDs18Resolution(DS18B20PIO::Resolution res) {
    return _ds18Sensor.setResolution(PIN_ONEWIRE_DEFAULT, res);
}

void SensorManager::requestDs18Reading() {
    _ds18Sensor.requestTemperatures(PIN_ONEWIRE_DEFAULT);
}

bool SensorManager::readDs18(float &temp) {
    return _ds18Sensor.getTemperatureValidated(PIN_ONEWIRE_DEFAULT, temp, DS18B20PIO::CELSIUS);
}

void SensorManager::requestDhtReading() {
    _dhtSensor.requestReading(PIN_DHT_DEFAULT);
}


bool SensorManager::readDhtBlocking(float &t, float &h) {
    delayMicroseconds(100);
    _dhtSensor.requestReading(PIN_DHT_DEFAULT);

    uint32_t start = millis();
    while (millis() - start < 2500) {
        _dhtSensor.update();
        DHT22PIO::State s = _dhtSensor.getState();
        if (s == DHT22PIO::DATA_READY) return _dhtSensor.getResults(t, h);
        if (s == DHT22PIO::ERROR_TIMEOUT || s == DHT22PIO::ERROR_CHECKSUM) return false;
    }
    return false;
}

bool SensorManager::pollAsyncResult(String &msg) { return false; }

void SensorManager::applyCalibration(uint8_t gpio, String newHwId, float offset, String newName) {
    for (auto &s : _runtimeSensors) {
        if (s.config.gpio == gpio) {
            if (newHwId.length() > 0) {
                safeCopy(s.config.hwId, newHwId.c_str(), sizeof(s.config.hwId));
            }
            if (newName.length() > 0) {
                safeCopy(s.config.friendlyName, newName.c_str(), sizeof(s.config.friendlyName));
            }
            s.calibrationOffset = offset;
            break;
        }
    }
}

void SensorManager::setHardwareMismatch(uint8_t gpio, bool isMismatch) {
    for (auto &s : _runtimeSensors) {
        if (s.config.gpio == gpio) {
            s.hardwareMismatch = isMismatch;
        }
    }
}
