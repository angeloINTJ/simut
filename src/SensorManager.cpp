/**
 * @file SensorManager.cpp
 * @brief Implementation of SensorManager — async sensor reads, scan, and data processing.
 * @details Implements parallel DS18B20 conversion with ROM verification,
 * fully asynchronous DHT22 reading via PIO state machine,
 * hardware scan across GPIO 0-16, error hysteresis (3 consecutive
 * failures to flag, 5 successes to recover), zero-trust hardware
 * mismatch blocking, and trimmed mean filtering.
 *
 * Sensor drivers are conditionally compiled via SIMUT_SENSOR_* flags.
 * See SensorConfig.h for the per-type feature switches.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "SensorManager.h"
#include "MetricsManager.h"
#include <algorithm>
#include <vector>
#include <cstring>


/**
 * Static sort buffer for trimmed mean calculation — zero heap allocation.
 * Safe: processPeriodicReads( ) is single-threaded on Core 0.
 */
static float _trimSortBuf[MOVING_AVG_WINDOW];

/**
 * @brief Compute trimmed mean from a ring buffer (removes 20% outliers).
 * Uses a static sort buffer to avoid heap allocation in the hot path.
 */
static float calculateTrimmedMean(const RingBuffer& ring) {
 if (ring.empty( )) return NAN;

 uint8_t size = ring.size( );
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


SensorManager::SensorManager( )
{
}

void SensorManager::begin( ) {
#if SIMUT_SENSOR_DS18B20
 _ds18.begin( );
#endif
#if SIMUT_SENSOR_DHT22
 _dht.begin( );
#endif
}

/**
 * @brief Build runtime sensor list from persistent configuration.
/**
 * @brief Build runtime sensor list from persistent configuration.
 * Iterates all 16 universal slots (GPIO0–GPIO15). Each active slot
 * becomes a RuntimeSensor with proper type, interval, and GPIO setup.
 */
void SensorManager::initRuntimeSensors(const SystemConfig &cfg) {
 _runtimeSensors.clear( );

 for (int i = 0; i < MAX_SENSORS; i++) {
 if (!cfg.sensors[i].active) continue;

 RuntimeSensor rs;
 rs.config = cfg.sensors[i];
 rs.calibrationOffset[0] = 0.0f;
 rs.calibrationOffset[1] = 0.0f;

 rs.avgValue[0] = NAN;
 rs.avgValue[1] = NAN;
 rs.lastReadTime = 0;
 rs.totalReadings = 0;
 rs.consecutiveErrors = 0;
 rs.consecutiveSuccess = 0;
 rs.inErrorState = false;
 rs.hardwareMismatch = false;
 rs.calibrationOffset[0] = 0.0f;

 /* Use explicit sensorType from config.
  * Fallback: ROM-based detection for sensors that haven't been re-saved
  * after migration (paranoid safety — should never trigger). */
 rs.type = (SensorType)rs.config.sensorType;
 if (rs.type == TYPE_NONE) {
 /* Legacy fallback: infer from ROM field. */
 bool isDs18 = false;
#if SIMUT_SENSOR_DS18B20
 for(int k=0; k<8; k++) if(rs.config.rom[k] != 0) isDs18 = true;
#endif
 rs.type = isDs18 ? TYPE_DS18B20 : TYPE_DHT22;
 }
 rs.readInterval = sensorDefaultIntervalMs(rs.type);

 if (!sensorTypeEnabled(rs.type)) {
 /* Type not compiled in — skip this sensor */
 continue;
 }

	/* v1.4.2: GPIO init driven by SensorFormat pin requirements.
		/* v1.4.2: GPIO setup via driver metadata. DHT22 needs pull-up on data pin. */
		if (rs.type == TYPE_DHT22) {
		 gpio_init(rs.config.pins[0]);
		 gpio_set_pulls(rs.config.pins[0], true, false);
		}

 _runtimeSensors.push_back(rs);
 }
 LOG_CODE(LOG_INFO, "SENSOR", SENSOR_RUNTIME_LOADED, _runtimeSensors.size( ), "");
}


/**
 * @brief Synchronize alarm thresholds from Flash config to runtime sensors.
 * Only copies alarm fields — preserves buffers, averages, and error counters.
 * Called after saving limits via display or web interface.
 */
void SensorManager::syncAlarmLimits(const SystemConfig &cfg) {
 for (auto &rs : _runtimeSensors) {
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (!cfg.sensors[i].active) continue;
 if (cfg.sensors[i].pins[0] == rs.config.pins[0]) {
 rs.config.tempMin = cfg.sensors[i].tempMin;
 rs.config.tempMax = cfg.sensors[i].tempMax;
 rs.config.humMin = cfg.sensors[i].humMin;
 rs.config.humMax = cfg.sensors[i].humMax;
 rs.config.alarmsActive = cfg.sensors[i].alarmsActive;
 break;
 }
 }
 }
}

#if SIMUT_SENSOR_DS18B20
bool SensorManager::identifyPhysicalSensor(uint8_t gpio, uint8_t* romOut) {
 if (_ds18.readROM(gpio, romOut)) {
 return true;
 }
 return false;
}

#endif

void SensorManager::handleSensorResult(RuntimeSensor &s, bool success, float v1, float v2, const char* errorMsg) {
 LogCode code = SYS_OK;

 if (!success) {
 if (strstr(errorMsg, "Timeout") != nullptr) code = ERR_SENSOR_TIMEOUT;
 else if (strstr(errorMsg, "Checksum") != nullptr) code = ERR_SENSOR_CHECKSUM;
 else if (strstr(errorMsg, "CRC") != nullptr) code = ERR_SENSOR_CRC;
 else if (strstr(errorMsg, "Range") != nullptr) code = ERR_SENSOR_RANGE;
 else if (strstr(errorMsg, "Missing") != nullptr) code = ERR_SENSOR_MISSING;
 else if (strstr(errorMsg, "Mismatch") != nullptr) code = ERR_SENSOR_MISMATCH;
 else code = ERR_UNKNOWN;
 }

 if (success) {
 MetricsManager::instance( ).data( ).sensorReadsOk++;
 s.consecutiveErrors = 0;
 s.consecutiveSuccess++;


 if (s.inErrorState && s.consecutiveSuccess >= 5) {
 s.inErrorState = false;
 LOG_CODE(LOG_INFO, "SENSOR", LOG_SENSOR_REC, s.config.pins[0], TRL("Sensor recovered"));
 }

 if (!s.inErrorState) {
 /* ambient (DHT22) applies offset to both quantities;
 * other sensors (DS18B20) temperature only — calibrationOffset[1]
 * stays at 0 and the sum is a no-op. */
 addSample(s, v1 + s.calibrationOffset[0], v2 + s.calibrationOffset[1]);
 }
 }
 else {
 MetricsManager::instance( ).data( ).sensorReadsErr++;
 s.consecutiveSuccess = 0;
 s.consecutiveErrors++;


 if (!s.inErrorState && s.consecutiveErrors >= 3) {
 s.inErrorState = true;

 LOG_CODE(LOG_ERROR, "SENSOR", code, s.config.pins[0], String(errorMsg));
 }
 }
}

void SensorManager::update( ) {

 if (isScanning( )) {
#if SIMUT_SENSOR_DHT22
 _dht.update( );
#endif

 if (_scanState == IDLE || _scanState == COMPLETE) return;

 switch (_scanState) {
 case SETUP_PIN:
 gpio_init(_currentScanPin);
 gpio_set_pulls(_currentScanPin, true, false);
#if SIMUT_SENSOR_DS18B20
 _scanState = ONEWIRE_RESET;
#elif SIMUT_SENSOR_DHT22
 _scanState = DHT_REQUEST;
#else
 _scanState = NEXT_PIN;
#endif
 break;

#if SIMUT_SENSOR_DS18B20
 case ONEWIRE_RESET:
 _ds18.setPin(_currentScanPin);
 _ds18.sendReset( );
 _scanTimer = micros( );
 _scanState = ONEWIRE_WAIT;
 break;

 case ONEWIRE_WAIT:
 if (micros( ) - _scanTimer >= 1200) {
 if (_ds18.isSensorPresent( )) {
 ScanResult res;
 res.pin = _currentScanPin;
 res.type = TYPE_DS18B20;
 if (_ds18.readROM(_currentScanPin, res.rom)) {
 _scanResults.push_back(res);
 _scanState = NEXT_PIN;
 } else {
#if SIMUT_SENSOR_DHT22
 _scanState = DHT_REQUEST;
#else
 _scanState = NEXT_PIN;
#endif
 }
 } else {
#if SIMUT_SENSOR_DHT22
 _scanState = DHT_REQUEST;
#else
 _scanState = NEXT_PIN;
#endif
 }
 }
 break;
#endif /* SIMUT_SENSOR_DS18B20 */

#if SIMUT_SENSOR_DHT22
 case DHT_REQUEST:
 _dht.requestReading(_currentScanPin);
 _scanTimer = millis( );
 _scanState = DHT_WAIT;
 break;

 case DHT_WAIT: {
 DHT22PIO::State s = _dht.getState( );
 if (s == DHT22PIO::DATA_READY || s == DHT22PIO::ERROR_CHECKSUM) {
 ScanResult res;
 res.pin = _currentScanPin;
 res.type = TYPE_DHT22;
 memset(res.rom, 0, 8);
 _scanResults.push_back(res);
 _scanState = NEXT_PIN;
 } else if (s == DHT22PIO::ERROR_TIMEOUT || timeSince(_scanTimer, DHT22_READ_TIMEOUT_MS)) {
 _scanState = NEXT_PIN;
 }
 }
 break;
#endif /* SIMUT_SENSOR_DHT22 */

 case NEXT_PIN:
 _currentScanPin++;
 if (_currentScanPin > 16) {
#if SIMUT_SENSOR_DS18B20
 _ds18.setPin(PIN_ONEWIRE_DEFAULT);
#endif
 _scanState = COMPLETE;
 } else {
 _scanState = SETUP_PIN;
 }
 break;

 default: break;
 }
 return;
 }

 processPeriodicReads( );
}

/* =========================================================================== */
/* PERIODIC SENSOR READING STATE MACHINES */
/* =========================================================================== */
/**
 * @brief Execute async reading cycles for all sensor types.
 *
 * DS18B20: Parallel mass conversion — all sensors start simultaneously,
 * results collected after 750ms conversion time.
 * DHT22: Sequential one-at-a-time via PIO state machine (non-blocking).
 */
void SensorManager::processPeriodicReads( ) {
 uint32_t now = millis( );

#if SIMUT_SENSOR_DS18B20
 /* ── DS18B20: parallel batch read ── */
 if (_ds18.state == DS18B20Driver::DS_IDLE) {
 bool needsRead = false;
 for (auto &s : _runtimeSensors) {
 if (s.type == TYPE_DS18B20 && (now - s.lastReadTime >= s.readInterval)) {
 needsRead = true; break;
 }
 }

 if (needsRead) {
 for (auto &s : _runtimeSensors) {
 if (s.type == TYPE_DS18B20) _ds18.requestTemperatures(s.config.pins[0]);
 }
 _ds18.timer = now;
 _ds18.state = DS18B20Driver::DS_WAITING;
 }
 }
 else if (_ds18.state == DS18B20Driver::DS_WAITING) {
 if (now - _ds18.timer >= DS18B20_CONVERSION_TIME_MS) {
 for (auto &s : _runtimeSensors) {
 if (s.type == TYPE_DS18B20) {


 if (s.hardwareMismatch) {
 if (!s.inErrorState) {
 LOG_CODE(LOG_ERROR, "SENSOR", ERR_SENSOR_MISMATCH, s.config.pins[0], TRL("Hardware Mismatch (Access Denied)"));
 }
 s.inErrorState = true;
 s.buffers[0].clear( );
 s.avgValue[0] = NAN;
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
 if (_ds18.readROM(s.config.pins[0], currentRom)) {
 if (!_ds18.checkRomMatch(currentRom, s.config.rom)) {
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
 bool success = _ds18.getTemperatureValidated(s.config.pins[0], tempC);

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
 _ds18.state = DS18B20Driver::DS_IDLE;
 }
 }
#endif /* SIMUT_SENSOR_DS18B20 */

#if SIMUT_SENSOR_DHT22
 /* ── DHT22: sequential one-at-a-time ── */
 if (_dht.state == DHT22Driver::DHT_IDLE) {

 for (size_t i = 0; i < _runtimeSensors.size( ); i++) {
 auto &s = _runtimeSensors[i];
 if (s.type == TYPE_DHT22 && (now - s.lastReadTime >= s.readInterval)) {
 _dht.reset( );
 _dht.requestReading(s.config.pins[0]);

 _dht.timer = millis( );
 _dht.currentSensorIdx = i;
 _dht.state = DHT22Driver::DHT_WAITING;
 break;
 }
 }
 }
 else if (_dht.state == DHT22Driver::DHT_WAITING) {

 if (_dht.currentSensorIdx >= 0 && _dht.currentSensorIdx < (int)_runtimeSensors.size( )) {
 auto &s = _runtimeSensors[_dht.currentSensorIdx];

 _dht.update( );
 DHT22PIO::State st = _dht.getState( );

 if (st == DHT22PIO::DATA_READY) {
 float t, h;
 if (_dht.getResults(t, h)) {
 handleSensorResult(s, true, t, h, "");
 } else {
 handleSensorResult(s, false, 0, 0, "Checksum Error");
 }
 _dht.reset( );
 s.lastReadTime = millis( );
 _dht.state = DHT22Driver::DHT_IDLE;
 }
 else if (st == DHT22PIO::ERROR_TIMEOUT || st == DHT22PIO::ERROR_CHECKSUM) {
 const char* errMsg = (st == DHT22PIO::ERROR_TIMEOUT) ? "Sensor Timeout" : "Checksum Error";
 handleSensorResult(s, false, 0, 0, errMsg);
 _dht.reset( );
 s.lastReadTime = millis( );
 _dht.state = DHT22Driver::DHT_IDLE;
 }

 else if (timeSince(_dht.timer, DHT22_READ_TIMEOUT_MS)) {
 handleSensorResult(s, false, 0, 0, "Sensor Timeout");
 _dht.reset( );
 s.lastReadTime = millis( );
 _dht.state = DHT22Driver::DHT_IDLE;
 }
 } else {

 _dht.state = DHT22Driver::DHT_IDLE;
 }
 }
#endif /* SIMUT_SENSOR_DHT22 */
}

/**
 * @brief Add a new reading to the sensor's ring buffer and update averages.
 * Uses trimmed mean when buffer is full, simple mean otherwise.
 * Sets the atomic _newDataAvailable flag for cross-core notification.
 */
void SensorManager::addSample(RuntimeSensor &sensor, float v1, float v2) {
 if (!isnan(v1)) {
 sensor.buffers[0].push(v1);
 }
 if (sensorHasHumidity(sensor.type) && !isnan(v2)) {
 sensor.buffers[1].push(v2);
 }

 if (!sensor.buffers[0].empty( )) {
 if (sensor.buffers[0].full( )) {
 sensor.avgValue[0] = calculateTrimmedMean(sensor.buffers[0]);
 if (sensorHasHumidity(sensor.type)) sensor.avgValue[1] = calculateTrimmedMean(sensor.buffers[1]);
 } else {

 float sortBuf[MOVING_AVG_WINDOW];
 sensor.buffers[0].copyTo(sortBuf);
 float sum1 = 0;
 for (uint8_t i = 0; i < sensor.buffers[0].size( ); i++) sum1 += sortBuf[i];
 sensor.avgValue[0] = sum1 / sensor.buffers[0].size( );

 if (sensorHasHumidity(sensor.type) && !sensor.buffers[1].empty( )) {
 sensor.buffers[1].copyTo(sortBuf);
 float sum2 = 0;
 for (uint8_t i = 0; i < sensor.buffers[1].size( ); i++) sum2 += sortBuf[i];
 sensor.avgValue[1] = sum2 / sensor.buffers[1].size( );
 }
 }

 __atomic_store_n(&_newDataAvailable, true, __ATOMIC_RELEASE);
 }
}

bool SensorManager::hasNewReadings( ) {

 bool expected = true;
 return __atomic_compare_exchange_n(&_newDataAvailable, &expected,
 false, false,
 __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

const std::vector<RuntimeSensor>& SensorManager::getRuntimeSensors( ) const {
 return _runtimeSensors;
}


void SensorManager::startScan( ) {
 if (_scanState != IDLE && _scanState != COMPLETE) return;
 _scanResults.clear( );
 _currentScanPin = 0;
 _scanState = SETUP_PIN;
}

bool SensorManager::isScanning( ) {
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

#if SIMUT_SENSOR_DS18B20
bool SensorManager::setDs18Resolution(DS18B20PIO::Resolution res) {
 return _ds18.setResolution(res);
}

void SensorManager::requestDs18Reading( ) {
 _ds18.requestTemperatures(PIN_ONEWIRE_DEFAULT);
}

bool SensorManager::readDs18(float &temp) {
 return _ds18.getTemperatureValidated(PIN_ONEWIRE_DEFAULT, temp);
}
#endif

#if SIMUT_SENSOR_DHT22
void SensorManager::requestDhtReading( ) {
 _dht.requestReading(10); /* legacy: default GPIO for DHT22 */
}


bool SensorManager::readDhtBlocking(float &t, float &h) {
 delayMicroseconds(100);
 _dht.requestReading(10); /* legacy: default GPIO for DHT22 */

 uint32_t start = millis( );
 while (millis( ) - start < 2500) {
 _dht.update( );
 DHT22PIO::State s = _dht.getState( );
 if (s == DHT22PIO::DATA_READY) return _dht.getResults(t, h);
 if (s == DHT22PIO::ERROR_TIMEOUT || s == DHT22PIO::ERROR_CHECKSUM) return false;
 }
 return false;
}
#endif

bool SensorManager::pollAsyncResult(String &msg) { return false; }

void SensorManager::applyCalibration(uint8_t gpio, String newHwId, float offset, String newName) {
 for (auto &s : _runtimeSensors) {
 if (s.config.pins[0] == gpio) {
 if (newHwId.length( ) > 0) {
 safeCopy(s.config.hwId, newHwId.c_str( ), sizeof(s.config.hwId));
 }
 if (newName.length( ) > 0) {
 safeCopy(s.config.friendlyName, newName.c_str( ), sizeof(s.config.friendlyName));
 }
 s.calibrationOffset[0] = offset;
 break;
 }
 }
}

/* Apply temperature + humidity offsets to the first DHT22 sensor found.
 * ID/name are persisted via applyCalibration in the caller. */
void SensorManager::applyAmbientCalibration(float offsetT, float offsetH) {
 for (auto &s : _runtimeSensors) {
#if SIMUT_SENSOR_DHT22
 if (s.type == TYPE_DHT22) {
#else
 /* When DHT22 is not compiled-in, apply to any sensor (fallback) */
 {
#endif
 s.calibrationOffset[0] = offsetT;
 s.calibrationOffset[1] = offsetH;
 break;
 }
 }
}

void SensorManager::setHardwareMismatch(uint8_t gpio, bool isMismatch) {
 for (auto &s : _runtimeSensors) {
 if (s.config.pins[0] == gpio) {
 s.hardwareMismatch = isMismatch;
 }
 }
 }
