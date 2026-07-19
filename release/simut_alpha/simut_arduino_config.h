#pragma once
#define SIMUT_DISPLAY_TFT   0
#define SIMUT_DISPLAY_ALPHA 1
#define HD44780_MODE_PARALLEL 1
#ifndef HD44780_RS
#define HD44780_RS 16
#endif
#ifndef HD44780_EN
#define HD44780_EN 17
#endif
#ifndef HD44780_D4
#define HD44780_D4 18
#endif
#ifndef HD44780_D5
#define HD44780_D5 19
#endif
#ifndef HD44780_D6
#define HD44780_D6 20
#endif
#ifndef HD44780_D7
#define HD44780_D7 21
#endif
#define SIMUT_SENSOR_DS18B20 1
#define SIMUT_SENSOR_DHT22   1
#define SIMUT_SENSOR_BME280  1
#define SIMUT_MDNS 1
