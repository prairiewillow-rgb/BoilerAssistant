/*
 * ============================================================
 *  Boiler Assistant – Sensor API (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: Sensors.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Public interface for the unified sensor subsystem. Provides
 *    deterministic access to:
 *
 *      • MAX31855 exhaust thermocouple (cached reads)
 *      • DS18B20 water probes (scan + read)
 *      • BME280 outdoor environmental sensor
 *
 *    Architectural Notes:
 *      - All live values are written directly into SystemData (sys.*)
 *      - No dynamic allocation or blocking delays
 *      - Probe roles resolved through sys.probeRoleMap
 *      - All implementation resides in Sensors.cpp
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "SystemState.h"
#include "SystemData.h"

// Initialize BME280, DS18B20, MAX31855
bool sensors_init();

// Read MAX31855 (cached)
double exhaust_readF_cached();

// Scan DS18B20 probes and populate sys.waterProbeCount
void scanWaterProbes();

// Re-scan the DS18B20 bus occasionally for probes that were reconnected.
void sensors_rescanWaterProbes();

// Read DS18B20 water probes into sys.waterTempF[]
void sensors_readWaterProbes();

// Read BME280 into sys.envTempF / sys.envHumidity / sys.envPressure
void sensors_readBME280();

#endif
