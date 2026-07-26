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
#include <Wire.h>
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

 /* Clean up old BME280 drivers from previous init (reload, config change). */
#if SIMUT_SENSOR_BME280
 for (auto* drv : _bmeDrivers) { delete drv; }
 _bmeDrivers.clear( );
#endif

 bool spiInitialized = false;
#if SIMUT_SENSOR_BME280
 bool i2c0Initialized = false;
 bool i2c1Initialized = false;
#endif

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
 rs.mismatchRechecks = 0;

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

	/* ── Phase 1: I2C bus init (before per-pin GPIO config) ──
	 * v1.5.1+: Prefer hardware I2C (Wire/Wire1) when pins map to an
	 * I2C-capable peripheral. Falls back to PIO bit-bang for non-standard
	 * pin pairs. Hardware I2C uses zero PIO resources, eliminating
	 * contention with OneWirePIO (DS18B20) on pio0. */
#if SIMUT_SENSOR_BME280
	if (rs.type == TYPE_BME280) {
		uint8_t sda = PIN_UNUSED, scl = PIN_UNUSED;
		for (uint8_t pj = 0; pj < fmt.pinCount; pj++) {
			if (fmt.pins[pj].role == ROLE_I2C_SDA) sda = rs.config.pins[pj];
			if (fmt.pins[pj].role == ROLE_I2C_SCL) scl = rs.config.pins[pj];
		}

		if (sda != PIN_UNUSED && scl != PIN_UNUSED) {
			/* Track taken addresses per (sda,scl) bus — up to 2 sensors
			 * per bus (0x76 and 0x77). */
			struct BmeAddrTrack { uint8_t s, d; bool a76, a77; };
			static BmeAddrTrack _bmeBuses[8];
			static uint8_t _bmeBusCount = 0;
			BmeAddrTrack* bus = nullptr;
			for (uint8_t bi = 0; bi < _bmeBusCount; bi++) {
				if (_bmeBuses[bi].s == sda && _bmeBuses[bi].d == scl) {
					bus = &_bmeBuses[bi]; break;
				}
			}
			if (!bus && _bmeBusCount < 8) {
				bus = &_bmeBuses[_bmeBusCount++];
				bus->s = sda; bus->d = scl;
				bus->a76 = false; bus->a77 = false;
			}
			uint8_t addr = 0;
			if (bus) {
				if (!bus->a76)      { addr = BME280_ADDR_PRIMARY; bus->a76 = true; }
				else if (!bus->a77) { addr = 0x77;                bus->a77 = true; }
			}

			if (addr != 0) {
				int periph = i2cPeripheralForPins(sda, scl);
				int8_t drvIdx = -1;

				if (periph == 0) {
				if (!i2c0Initialized) {
					Wire.setSDA(sda);
					Wire.setSCL(scl);
					Wire.begin();
					i2c0Initialized = true;
				}
					drvIdx = _getOrCreateBmeDriver(Wire, addr);
				} else if (periph == 1) {
				if (!i2c1Initialized) {
					Wire1.setSDA(sda);
					Wire1.setSCL(scl);
					Wire1.begin();
					i2c1Initialized = true;
				}
					drvIdx = _getOrCreateBmeDriver(Wire1, addr);
				} else {
					/* Pins not I2C-capable — fall back to PIO bit-bang.
					 * Wave 2: this path costs ~1.6 ms of IRQs-off per I2C
					 * transaction on Core 0 (Wi-Fi/BT jitter — cause C1/C3
					 * in docs/CONCURRENCY.md). Make it LOUD so a silent
					 * regression to bit-bang never hides again. HW pairs:
					 * I2C0 SDA/SCL = 0/1, 4/5, 8/9, 12/13, 16/17, 20/21;
					 * I2C1 = 2/3, 6/7, 10/11, 14/15, 18/19, 26/27. */
					LOG_CODE(LOG_WARN, "SENSOR", SYS_OK, sda,
					         TRL("BME in bit-bang (pins have no hardware I2C) — see docs/CONCURRENCY.md"));
					drvIdx = _getOrCreateBmeDriver(sda, scl, addr);
				}

				if (drvIdx >= 0) {
					rs.bmeDriverIdx = drvIdx;
					rs.i2cAddr = addr;
				}
			}
		}
	}
#endif /* SIMUT_SENSOR_BME280 */

	/* ── Phase 2: per-pin GPIO configuration ── */
	for (uint8_t pi = 0; pi < fmt.pinCount && pi < MAX_SENSOR_PINS; pi++) {
		uint8_t gpio = rs.config.pins[pi];
		if (gpio == PIN_UNUSED) continue;

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
	  /* BME280 is I2C — not detectable via GPIO probing. PIO bit-bang
	   * on default I2C pins (4=SDA, 5=SCL) probes both addresses.
	   * PIO works on any pin pair — users with non-default wiring
	   * can still configure manually after scan. */
	  {
	   BMx280PIO_RP2040 probe(4, 5, BME280_ADDR_PRIMARY);
	   if (probe.begin()) {
	    ScanResult res;
	    res.pin = 255; /* I2C — no single GPIO */
	    res.type = TYPE_BME280;
	    memset(res.rom, 0, 8);
	    _scanResults.push_back(res);
	   }
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
 /* Wave 2 (sensor doc issue #3): the quarantine used to be permanent
  * until reboot or manual recalibration. Every 10th skipped cycle,
  * re-read the ROM — if the CONFIGURED chip is back on the pin (user
  * swapped the right sensor back), lift the quarantine and let the
  * normal read path below run this very cycle. A different chip
  * keeps failing the match and stays quarantined (safety preserved). */
 if (++s.mismatchRechecks >= 10) {
 s.mismatchRechecks = 0;
 uint8_t romNow[8];
 if (_ds18.readROM(s.config.pins[0], romNow) &&
     _ds18.checkRomMatch(romNow, s.config.rom)) {
 s.hardwareMismatch = false;
 s.inErrorState = false;
 s.consecutiveErrors = 0;
 LOG_CODE(LOG_INFO, "SENSOR", SYS_OK, s.config.pins[0], TRL("Hardware match restored"));
 }
 }
 }

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

 /* ROM verification every 5 reads — skip if config ROM is all zeros
  * (unpaired sensor). A zero ROM means "accept any DS18B20 on this pin". */
 bool romIsZero = true;
 for (int k = 0; k < 8; k++) if (s.config.rom[k] != 0) romIsZero = false;

 if (!romIsZero && s.totalReadings % 5 == 0) {
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
 /* ── BME280: multi-driver forced-mode via PIO ──
  * Each driver operates independently — one can be in WAITING
  * while another is IDLE. No shared bus contention. */
 for (size_t di = 0; di < _bmeDrivers.size(); di++) {
  auto *drv = _bmeDrivers[di];

  if (drv->state == BME280Driver::BME_IDLE) {
   for (size_t i = 0; i < _runtimeSensors.size( ); i++) {
    auto &s = _runtimeSensors[i];
    if (s.type == TYPE_BME280 && s.bmeDriverIdx == (int8_t)di
        && (now - s.lastReadTime >= s.readInterval)) {
     drv->reset( );
     drv->requestReading( );
     drv->timer = millis( );
     drv->currentSensorIdx = i;
     drv->state = BME280Driver::BME_WAITING;
     break;
    }
   }
  }
  else if (drv->state == BME280Driver::BME_WAITING) {
   if (drv->currentSensorIdx >= 0 && drv->currentSensorIdx < (int)_runtimeSensors.size( )) {
    auto &s = _runtimeSensors[drv->currentSensorIdx];

    if (timeSince(drv->timer, BME280_MEAS_TIME_MS)) {
     float t, h, p;
     if (drv->getResults(t, h, p)) {
      /* BME280: v1=temp, v2=humidity (pressure available via API) */
     if (!drv->isBME( )) h = NAN;  /* BMP280: cached flag, not a live I2C read */
      handleSensorResult(s, true, t, h, "");
      /* Store pressure in CH_PRESS channel buffer */
      if (!isnan(p)) {
       s.buffers[CH_PRESS].push(p);
       if (s.buffers[CH_PRESS].full( )) {
        s.avgValue[CH_PRESS] = calculateTrimmedMean(s.buffers[CH_PRESS]);
       } else {
        float sortBuf[MOVING_AVG_WINDOW];
        s.buffers[CH_PRESS].copyTo(sortBuf);
        float sumP = 0;
        for (uint8_t i = 0; i < s.buffers[CH_PRESS].size( ); i++) sumP += sortBuf[i];
        s.avgValue[CH_PRESS] = sumP / s.buffers[CH_PRESS].size( );
       }
      }
     } else {
      handleSensorResult(s, false, 0, 0, "I2C Read Error");
     }
     drv->reset( );
     s.lastReadTime = millis( );
    }
   } else {
    drv->state = BME280Driver::BME_IDLE;
   }
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
 /* isfinite, not !isnan: INFINITY passes isnan and poisoned the whole
  * averaging chain (BMP280 humidity compensation yields inf; newlib-
  * nano printf masked it by printing NaN as "inf" during forensics). */
 if (isfinite(v1)) {
 sensor.buffers[0].push(v1);
 }
 if (sensorHasHumidity(sensor.type) && isfinite(v2)) {
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
 if (!_bmeDrivers.empty()) _bmeDrivers[0]->requestReading();
}

bool SensorManager::readBmeBlocking(float &t, float &h, float &p) {
 if (_bmeDrivers.empty()) return false;
 _bmeDrivers[0]->requestReading();
 delay(BME280_MEAS_TIME_MS);
 return _bmeDrivers[0]->getResults(t, h, p);
}
#endif

bool SensorManager::pollAsyncResult(String &msg) { return false; }

#if SIMUT_SENSOR_BME280
int8_t SensorManager::_getOrCreateBmeDriver(uint8_t sda, uint8_t scl, uint8_t addr) {
 /* PIO fallback — used when pins don't map to hardware I2C.
  * Dynamically allocates a BME280Driver; caller (initRuntimeSensors)
  * owns cleanup via _bmeDrivers vector. */
 auto* drv = new (std::nothrow) BME280Driver();
 if (!drv) return -1;
 if (Serial) { Serial.print("[DBG] BME PIO init addr=0x"); Serial.println((int)addr, HEX); }
 if (drv->begin(sda, scl, addr)) {
  _bmeDrivers.push_back(drv);
  LOG_CODE(LOG_INFO, "SENSOR", SYS_OK, 0,
   String("BME280 PIO driver OK 0x") + String(addr, HEX));
  return (int8_t)(_bmeDrivers.size() - 1);
 }
 if (Serial) Serial.println("[DBG] BME PIO begin failed");
 delete drv;
 return -1;
}

int8_t SensorManager::_getOrCreateBmeDriver(TwoWire &wire, uint8_t addr) {
 /* Hardware I2C — uses RP2040 built-in I2C peripheral (Wire/Wire1).
  * Zero PIO resources, zero DMA channels. Reliable and fast.
  * Dynamically allocates a BME280Driver; caller (initRuntimeSensors)
  * owns cleanup via _bmeDrivers vector. */
 auto* drv = new (std::nothrow) BME280Driver();
 if (!drv) return -1;
 if (Serial) { Serial.print("[DBG] BME HW I2C init addr=0x"); Serial.println((int)addr, HEX); }
 if (drv->begin(wire, addr)) {
  _bmeDrivers.push_back(drv);
  LOG_CODE(LOG_INFO, "SENSOR", SYS_OK, 0,
   String("BME280 HW I2C driver OK 0x") + String(addr, HEX));
  return (int8_t)(_bmeDrivers.size() - 1);
 }
 if (Serial) Serial.println("[DBG] BME HW I2C begin failed");
 delete drv;
 return -1;
}
#endif

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
