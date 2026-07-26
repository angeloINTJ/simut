/**
 * @file SensorPresets.h
 * @brief Predefined display formats for common maker/STEM sensor types.
 *
 * Maps physical quantities (temperature, pressure, weight, light, etc.)
 * to their display representation: unit, decimal places, and icon.
 *
 * When adding a new sensor driver, just pick the matching preset:
 *   SensorValueFormat fmt = SensorPresets::TEMPERATURE_CELSIUS;
 *
 * Covers the most common units in DIY automation, environmental monitoring,
 * agriculture, laboratory, industrial sensing, and energy metering.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once

#include "SensorHelpers.h" /* SensorValueFormat definition */

namespace SensorPresets {

/* ===========================================================================
 * TEMPERATURE
 * =========================================================================== */
constexpr SensorValueFormat TEMPERATURE_CELSIUS    = {"°C",  1, "thermometer"};
constexpr SensorValueFormat TEMPERATURE_FAHRENHEIT = {"°F",  1, "thermometer"};
constexpr SensorValueFormat TEMPERATURE_KELVIN     = {"K",   1, "thermometer"};

/* ===========================================================================
 * HUMIDITY / MOISTURE
 * =========================================================================== */
constexpr SensorValueFormat HUMIDITY_PERCENT       = {"%",   0, "drop"};
constexpr SensorValueFormat HUMIDITY_ABSOLUTE      = {"g/m³", 1, "drop"};
constexpr SensorValueFormat DEW_POINT_CELSIUS      = {"°C",  1, "dewpoint"};

/* ===========================================================================
 * ATMOSPHERIC / GAS PRESSURE
 * =========================================================================== */
constexpr SensorValueFormat PRESSURE_HPA           = {"hPa", 1, "gauge"};
constexpr SensorValueFormat PRESSURE_KPA           = {"kPa", 2, "gauge"};
constexpr SensorValueFormat PRESSURE_PASCAL        = {"Pa",  0, "gauge"};
constexpr SensorValueFormat PRESSURE_BAR           = {"bar", 3, "gauge"};
constexpr SensorValueFormat PRESSURE_PSI           = {"psi", 2, "gauge"};
constexpr SensorValueFormat PRESSURE_MMHG          = {"mmHg", 1, "gauge"};
constexpr SensorValueFormat PRESSURE_ATM           = {"atm", 3, "gauge"};

/* ===========================================================================
 * MASS / WEIGHT
 * =========================================================================== */
constexpr SensorValueFormat WEIGHT_KILOGRAM        = {"kg",  3, "scale"};
constexpr SensorValueFormat WEIGHT_GRAM            = {"g",   1, "scale"};
constexpr SensorValueFormat WEIGHT_MILLIGRAM       = {"mg",  1, "scale"};
constexpr SensorValueFormat WEIGHT_POUND           = {"lb",  2, "scale"};
constexpr SensorValueFormat WEIGHT_OUNCE           = {"oz",  2, "scale"};

/* ===========================================================================
 * FORCE / TORQUE
 * =========================================================================== */
constexpr SensorValueFormat FORCE_NEWTON           = {"N",   2, "scale"};
constexpr SensorValueFormat FORCE_KGF              = {"kgf", 2, "scale"};
constexpr SensorValueFormat TORQUE_NM              = {"N·m", 2, "wrench"};

/* ===========================================================================
 * LIGHT / ILLUMINANCE
 * =========================================================================== */
constexpr SensorValueFormat LIGHT_LUX              = {"lx",   0, "bulb"};
constexpr SensorValueFormat LIGHT_KLUX             = {"klx",  1, "bulb"};
constexpr SensorValueFormat LIGHT_PAR              = {"µmol/m²/s", 0, "bulb"};
constexpr SensorValueFormat LIGHT_WATTS_PER_M2     = {"W/m²", 1, "bulb"};
constexpr SensorValueFormat LIGHT_UV_INDEX         = {"UVI",  1, "bulb"};

/* ===========================================================================
 * DISTANCE / LENGTH
 * =========================================================================== */
constexpr SensorValueFormat DISTANCE_METER          = {"m",   3, "ruler"};
constexpr SensorValueFormat DISTANCE_CENTIMETER     = {"cm",  1, "ruler"};
constexpr SensorValueFormat DISTANCE_MILLIMETER     = {"mm",  1, "ruler"};
constexpr SensorValueFormat DISTANCE_INCH           = {"in",  2, "ruler"};

/* ===========================================================================
 * LIQUID / FLUID LEVEL
 * =========================================================================== */
constexpr SensorValueFormat LEVEL_PERCENT           = {"%",   0, "vial"};
constexpr SensorValueFormat LEVEL_CENTIMETER        = {"cm",  1, "vial"};
constexpr SensorValueFormat LEVEL_MILLIMETER        = {"mm",  1, "vial"};

/* ===========================================================================
 * CHEMICAL — pH / ORP / CONDUCTIVITY / TDS / SALINITY
 * =========================================================================== */
constexpr SensorValueFormat CHEM_PH                 = {"pH",  2, "vial"};
constexpr SensorValueFormat CHEM_ORP_MV             = {"mV",  0, "vial"};
constexpr SensorValueFormat CHEM_EC_US_CM           = {"µS/cm", 0, "vial"};
constexpr SensorValueFormat CHEM_EC_MS_CM           = {"mS/cm", 2, "vial"};
constexpr SensorValueFormat CHEM_TDS_PPM            = {"ppm", 0, "vial"};
constexpr SensorValueFormat CHEM_SALINITY_PPT       = {"ppt", 1, "vial"};
constexpr SensorValueFormat CHEM_SALINITY_PSU       = {"PSU", 1, "vial"};

/* ===========================================================================
 * DISSOLVED GAS — O2 / CO2 / O3 / NH3
 * =========================================================================== */
constexpr SensorValueFormat GAS_DO_MG_L             = {"mg/L", 2, "bubbles"};
constexpr SensorValueFormat GAS_DO_PERCENT          = {"%",    1, "bubbles"};
constexpr SensorValueFormat GAS_CO2_PPM             = {"ppm",  0, "co2"};
constexpr SensorValueFormat GAS_O3_PPB              = {"ppb",  0, "cloud"};
constexpr SensorValueFormat GAS_NH3_PPM             = {"ppm",  1, "cloud"};
constexpr SensorValueFormat GAS_CO_PPM              = {"ppm",  1, "cloud"};

/* ===========================================================================
 * PARTICULATE MATTER — PM1.0 / PM2.5 / PM10
 * =========================================================================== */
constexpr SensorValueFormat DUST_PM1_UG_M3          = {"µg/m³", 1, "dust"};
constexpr SensorValueFormat DUST_PM25_UG_M3         = {"µg/m³", 1, "dust"};
constexpr SensorValueFormat DUST_PM10_UG_M3         = {"µg/m³", 1, "dust"};

/* ===========================================================================
 * VOLATILE ORGANIC COMPOUNDS (VOC) / AIR QUALITY INDEX
 * =========================================================================== */
constexpr SensorValueFormat VOC_PPB                 = {"ppb",  0, "cloud"};
constexpr SensorValueFormat VOC_UG_M3               = {"µg/m³", 1, "cloud"};
constexpr SensorValueFormat AIR_QUALITY_INDEX       = {"AQI",  0, "cloud"};
constexpr SensorValueFormat AIR_QUALITY_IAQ         = {"IAQ",  1, "cloud"};

/* ===========================================================================
 * VOLTAGE
 * =========================================================================== */
constexpr SensorValueFormat VOLTAGE_VOLT            = {"V",   2, "bolt"};
constexpr SensorValueFormat VOLTAGE_MILLIVOLT       = {"mV",  0, "bolt"};
constexpr SensorValueFormat VOLTAGE_MICROVOLT       = {"µV",  0, "bolt"};

/* ===========================================================================
 * CURRENT
 * =========================================================================== */
constexpr SensorValueFormat CURRENT_AMPERE          = {"A",   3, "bolt"};
constexpr SensorValueFormat CURRENT_MILLIAMPERE     = {"mA",  1, "bolt"};
constexpr SensorValueFormat CURRENT_MICROAMPERE     = {"µA",  0, "bolt"};

/* ===========================================================================
 * POWER
 * =========================================================================== */
constexpr SensorValueFormat POWER_WATT              = {"W",   2, "bolt"};
constexpr SensorValueFormat POWER_KILOWATT          = {"kW",  3, "bolt"};
constexpr SensorValueFormat POWER_MILLIWATT         = {"mW",  1, "bolt"};

/* ===========================================================================
 * ENERGY (accumulated)
 * =========================================================================== */
constexpr SensorValueFormat ENERGY_WATTHOUR         = {"Wh",  0, "meter"};
constexpr SensorValueFormat ENERGY_KILOWATTHOUR     = {"kWh", 2, "meter"};
constexpr SensorValueFormat ENERGY_JOULE            = {"J",   1, "meter"};

/* ===========================================================================
 * FREQUENCY / ROTATION
 * =========================================================================== */
constexpr SensorValueFormat FREQUENCY_HZ            = {"Hz",  1, "pulse"};
constexpr SensorValueFormat FREQUENCY_KHZ           = {"kHz", 2, "pulse"};
constexpr SensorValueFormat FREQUENCY_RPM           = {"RPM", 0, "pulse"};

/* ===========================================================================
 * FLOW RATE
 * =========================================================================== */
constexpr SensorValueFormat FLOW_LITRE_PER_MIN      = {"L/min", 2, "pipe" };
constexpr SensorValueFormat FLOW_LITRE_PER_HOUR     = {"L/h",  1, "pipe"};
constexpr SensorValueFormat FLOW_M3_PER_HOUR        = {"m³/h", 2, "pipe"};
constexpr SensorValueFormat FLOW_GALLON_PER_MIN     = {"GPM",  2, "pipe"};

/* ===========================================================================
 * VOLUME (accumulated)
 * =========================================================================== */
constexpr SensorValueFormat VOLUME_LITRE            = {"L",   1, "pipe"};
constexpr SensorValueFormat VOLUME_MILLILITRE       = {"mL",  1, "pipe"};
constexpr SensorValueFormat VOLUME_CUBIC_METER      = {"m³",  3, "pipe"};
constexpr SensorValueFormat VOLUME_GALLON           = {"gal", 2, "pipe"};

/* ===========================================================================
 * SPEED / VELOCITY
 * =========================================================================== */
constexpr SensorValueFormat SPEED_M_PER_S           = {"m/s",  1, "gauge"};
constexpr SensorValueFormat SPEED_KM_PER_H          = {"km/h", 1, "gauge"};
constexpr SensorValueFormat SPEED_MPH               = {"mph",  1, "gauge"};

/* ===========================================================================
 * SOUND
 * =========================================================================== */
constexpr SensorValueFormat SOUND_DB                = {"dB",  1, "speaker"};
constexpr SensorValueFormat SOUND_DBA               = {"dBA", 1, "speaker"};

/* ===========================================================================
 * ANGLE / INCLINATION
 * =========================================================================== */
constexpr SensorValueFormat ANGLE_DEGREE            = {"°",   1, "compass"};
constexpr SensorValueFormat ANGLE_RADIAN            = {"rad", 3, "compass"};
constexpr SensorValueFormat INCLINATION_PERCENT     = {"%",   1, "compass"};

/* ===========================================================================
 * SOIL
 * =========================================================================== */
constexpr SensorValueFormat SOIL_MOISTURE_PERCENT   = {"%",   0, "drop"};
constexpr SensorValueFormat SOIL_MOISTURE_CBAR      = {"cbar", 0, "drop"};
constexpr SensorValueFormat SOIL_TEMPERATURE        = {"°C",  1, "thermometer"};

/* ===========================================================================
 * RAINFALL
 * =========================================================================== */
constexpr SensorValueFormat RAIN_MM                 = {"mm",  1, "drop"};
constexpr SensorValueFormat RAIN_MM_PER_HOUR        = {"mm/h", 1, "drop"};

/* ===========================================================================
 * WIND
 * =========================================================================== */
constexpr SensorValueFormat WIND_SPEED_M_S          = {"m/s",  1, "flag"};
constexpr SensorValueFormat WIND_SPEED_KM_H         = {"km/h", 1, "flag"};
constexpr SensorValueFormat WIND_DIRECTION_DEG      = {"°",    0, "compass"};

/* ===========================================================================
 * RADIATION
 * =========================================================================== */
constexpr SensorValueFormat RADIATION_USV_H         = {"µSv/h", 2, "atom"};
constexpr SensorValueFormat RADIATION_CPM           = {"CPM",   0, "atom"};
constexpr SensorValueFormat RADIATION_BQ_M3         = {"Bq/m³", 1, "atom"};

/* ===========================================================================
 * BATTERY / STATE OF CHARGE
 * =========================================================================== */
constexpr SensorValueFormat BATTERY_PERCENT         = {"%",    0, "battery"};
constexpr SensorValueFormat BATTERY_VOLTAGE         = {"V",    2, "battery"};

/* ===========================================================================
 * PULSE / COUNT / RATE
 * =========================================================================== */
constexpr SensorValueFormat COUNT_PULSES            = {"pulses", 0, "pulse"};
constexpr SensorValueFormat COUNT_UNITS             = {"",     0, "counter"};
constexpr SensorValueFormat RATE_PER_SECOND         = {"/s",   1, "pulse"};
constexpr SensorValueFormat RATE_PER_MINUTE         = {"/min", 1, "pulse"};
constexpr SensorValueFormat RATE_PER_HOUR           = {"/h",   1, "pulse"};

/* ===========================================================================
 * LEAK / BINARY STATE
 * =========================================================================== */
constexpr SensorValueFormat STATE_OPEN_CLOSED       = {"",     0, "switch"};
constexpr SensorValueFormat STATE_ON_OFF            = {"",     0, "switch"};
constexpr SensorValueFormat STATE_DRY_WET           = {"",     0, "switch"};
constexpr SensorValueFormat STATE_PRESENT_ABSENT    = {"",     0, "switch"};

} /* namespace SensorPresets */
