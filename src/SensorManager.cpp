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
#if SIMUT_SENSOR_BME280
 /* BME280 begin() deferred to initRuntimeSensors() — needs I2C bus ready. */
#endif
}

/**
 * @brief Build runtime sensor list from persistent configuration.
 * Iterates all 16 universal slots (GPIO0–GPIO15). Each active slot
 * becomes a RuntimeSensor with proper type, interval, and GPIO setup.
 *
 * GPIO pins are configured via gpioInitForRole() using driver metadata
 * from SensorFormat::forType(). Multi-pin sensors (I2C, SPI) have ALL
 * their pins initialized, not just pins[0]. Bus peripherals (Wire, SPI)
 * are initialized once when the first sensor requiring them is found.
 */
void SensorManager::initRuntimeSensors(const SystemConfig &cfg) {
 _runtimeSensors.clear( );

 bool i2cInitialized = false;
 bool spiInitialized = false;

 for (int i = 0; i < MAX_SENSORS; i++) {
 if (!cfg.sensors[i].active) continue;

 RuntimeSensor rs;
 rs.config = cfg.sensors[i];
 for (int ch = 0; ch < MAX_SENSOR_CHANNELS; ch++) {
  rs.calibrationOffset[ch] = 0.0f;
  rs.avgValue[ch] = NAN;
 }
 rs.lastReadTime = 0;
 rs.totalReadings = 0;
 rs.consecutiveErrors = 0;
 rs.consecutiveSuccess = 0;
 rs.inErrorState = false;
 rs.hardwareMismatch = false;

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

	/* v1.4.2+: GPIO init driven by SensorFormat pin requirements.
	 * All declared pins are configured via gpioInitForRole().
	 * Bus peripherals (I2C, SPI) are initialized once at first use. */
	auto fmt = SensorFormat::forType(rs.type);
	for (uint8_t pi = 0; pi < fmt.pinCount && pi < MAX_SENSOR_PINS; pi++) {
		uint8_t gpio = rs.config.pins[pi];
		if (gpio == PIN_UNUSED) continue;

		/* I2C bus init — once, using pins from first I2C sensor found.
		 * Auto-selects I2C0 (Wire) or I2C1 (Wire1) based on GPIO assignment.
		 * Any GPIO 0-15 works — the correct peripheral is detected at runtime. */
		if ((fmt.pins[pi].role == ROLE_I2C_SDA || fmt.pins[pi].role == ROLE_I2C_SCL)
		    && !i2cInitialized) {
			uint8_t sda = gpio, scl = gpio;
			for (uint8_t pj = 0; pj < fmt.pinCount; pj++) {
				if (fmt.pins[pj].role == ROLE_I2C_SDA) sda = rs.config.pins[pj];
				if (fmt.pins[pj].role == ROLE_I2C_SCL) scl = rs.config.pins[pj];
			}
		#if SIMUT_SENSOR_BME280
			int i2cBus = i2cPeripheralForPins(sda, scl);
			if (i2cBus == 0) {
				Wire.setSDA(sda);
				Wire.setSCL(scl);
				Wire.begin();
				_bme.setBus(Wire);
			} else if (i2cBus == 1) {
				Wire1.setSDA(sda);
				Wire1.setSCL(scl);
				Wire1.begin();
				_bme.setBus(Wire1);
			}
			/* If i2cBus == -1, pins don't share an I2C peripheral —
			 * the sensor will fail at first read. User gets a clear
			 * error via handleSensorResult() at runtime. */
			if (i2cBus >= 0) {
				_bme.begin(); /* Now safe — I2C bus is ready */
			}
			i2cInitialized = true;
		#endif
		}

		/* SPI bus init — once (future: BMP388, etc.) */
		if ((fmt.pins[pi].role == ROLE_SPI_SCK || fmt.pins[pi].role == ROLE_SPI_MOSI)
		    && !spiInitialized) {
			/* SPI peripheral init would go here — no SPI sensors implemented yet */
			spiInitialized = true; /* Reserve; prevents repeated init attempts */
		}

		gpioInitForRole(gpio, fmt.pins[pi].role, fmt.pins[pi].flags);
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
#if SIMUT_SENSOR_BME280
 _scanState = BME_SCAN_CHECK;
#else
#if SIMUT_SENSOR_DS18B20
 _ds18.setPin(PIN_ONEWIRE_DEFAULT);
#endif
 _scanState = COMPLETE;
#endif
 } else {
 _scanState = SETUP_PIN;
 }
 break;

#if SIMUT_SENSOR_BME280
 case BME_SCAN_CHECK: {
  /* BME280 is I2C — not detectable via GPIO probing. Check the
   * I2C bus once after all pins are scanned. */
  uint8_t chipId = 0;
  Wire.beginTransmission(BME280_I2C_ADDR_PRIMARY);
  bool primaryOk = (Wire.endTransmission() == 0);
  if (primaryOk || true) {
   /* Probe chip ID register at primary address */
   Wire.beginTransmission(BME280_I2C_ADDR_PRIMARY);
   Wire.write(BME280_REG_CHIP_ID);
   Wire.endTransmission();
   Wire.requestFrom(BME280_I2C_ADDR_PRIMARY, (uint8_t)1);
   if (Wire.available()) chipId = Wire.read();
  }
  if (chipId != BME280_CHIP_ID) {
   /* Try secondary address */
   Wire.beginTransmission(BME280_I2C_ADDR_SECONDARY);
   Wire.write(BME280_REG_CHIP_ID);
   Wire.endTransmission();
   Wire.requestFrom(BME280_I2C_ADDR_SECONDARY, (uint8_t)1);
   if (Wire.available()) chipId = Wire.read();
  }
  if (chipId == BME280_CHIP_ID) {
   ScanResult res;
   res.pin = 255; /* I2C — no single GPIO */
   res.type = TYPE_BME280;
   memset(res.rom, 0, 8);
   _scanResults.push_back(res);
  }
#if SIMUT_SENSOR_DS18B20
  _ds18.setPin(PIN_ONEWIRE_DEFAULT);
#endif
  _scanState = COMPLETE;
  break;
 }
#endif /* SIMUT_SENSOR_BME280 */

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

#if SIMUT_SENSOR_BME280
 /* ── BME280: sequential forced-mode via I2C ── */
 if (_bme.state == BME280Driver::BME_IDLE) {
  for (size_t i = 0; i < _runtimeSensors.size( ); i++) {
   auto &s = _runtimeSensors[i];
   if (s.type == TYPE_BME280 && (now - s.lastReadTime >= s.readInterval)) {
    _bme.reset( );
    _bme.requestReading( );
    _bme.timer = millis( );
    _bme.currentSensorIdx = i;
    _bme.state = BME280Driver::BME_WAITING;
    break;
   }
  }
 }
 else if (_bme.state == BME280Driver::BME_WAITING) {
  if (_bme.currentSensorIdx >= 0 && _bme.currentSensorIdx < (int)_runtimeSensors.size( )) {
   auto &s = _runtimeSensors[_bme.currentSensorIdx];

   if (timeSince(_bme.timer, BME280_MEAS_TIME_MS)) {
    float t, h, p;
    if (_bme.getResults(t, h, p)) {
     /* BME280: v1=temp, v2=humidity (pressure available via API) */
     handleSensorResult(s, true, t, h, "");
     /* Store pressure in CH_PRESS channel buffer */
     if (!isnan(p)) {
      s.buffers[CH_PRESS].push(p);
      if (s.buffers[CH_PRESS].full( )) {
       s.avgValue[CH_PRESS] = calculateTrimmedMean(s.buffers[CH_PRESS]);
      }
     }
    } else {
     handleSensorResult(s, false, 0, 0, "I2C Read Error");
    }
    _bme.reset( );
    s.lastReadTime = millis( );
    _bme.state = BME280Driver::BME_IDLE;
   }
  } else {
   _bme.state = BME280Driver::BME_IDLE;
  }
 }
#endif /* SIMUT_SENSOR_BME280 */
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
 /* Apply to first active DS18B20 — PIO broadcasts to all on the bus.
  * Falls back to GPIO 0 if no runtime sensors loaded yet (boot config). */
 uint8_t pin = 0;
 for (const auto &s : _runtimeSensors) {
  if (s.type == TYPE_DS18B20 && s.config.pins[0] != PIN_UNUSED) {
   pin = s.config.pins[0]; break;
  }
 }
 return _ds18.setResolution(pin, res);
}

void SensorManager::requestDs18Reading( ) {
 uint8_t pin = 0;
 for (const auto &s : _runtimeSensors) {
  if (s.type == TYPE_DS18B20 && s.config.pins[0] != PIN_UNUSED) {
   pin = s.config.pins[0]; break;
  }
 }
 _ds18.requestTemperatures(pin);
}

bool SensorManager::readDs18(float &temp) {
 uint8_t pin = 0;
 for (const auto &s : _runtimeSensors) {
  if (s.type == TYPE_DS18B20 && s.config.pins[0] != PIN_UNUSED) {
   pin = s.config.pins[0]; break;
  }
 }
 return _ds18.getTemperatureValidated(pin, temp);
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

#if SIMUT_SENSOR_BME280
void SensorManager::requestBmeReading( ) {
 _bme.requestReading( );
}

bool SensorManager::readBmeBlocking(float &t, float &h, float &p) {
 _bme.requestReading( );
 delay(BME280_MEAS_TIME_MS);
 return _bme.getResults(t, h, p);
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
