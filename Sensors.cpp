/*
 * ============================================================
 *  Boiler Assistant – Sensor Module (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: Sensors.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Unified sensor subsystem for the Boiler Assistant controller.
 *    Implements deterministic acquisition of:
 *
 *      • MAX31855 exhaust thermocouple
 *      • DS18B20 water probes (up to MAX_WATER_PROBES)
 *      • BME280 outdoor environmental sensor
 *
 *    All live values are written directly into SystemData (sys.*),
 *    following the Total Domination Architecture (TDA):
 *      - No dynamic allocation
 *      - No blocking delays beyond sensor‑required µs waits
 *      - Deterministic smoothing and caching for exhaust readings
 *      - Probe roles resolved through sys.probeRoleMap
 *
 *  Architectural Notes:
 *      - Exhaust readings use a 250 ms cache to avoid MAX31855 spam
 *      - Water probes use 20% smoothing for stable tank readings
 *      - BME280 values are read only when envSensorOK is true
 *      - This module contains no UI, MQTT, or EEPROM logic
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#include "Sensors.h"
#include "SystemData.h"
#include "SystemState.h"
#include "EEPROMStorage.h"
#include "Pinout.h"

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_BME280.h>
#include <Adafruit_MAX31855.h>

/* ============================================================
 *  GLOBALS
 * ============================================================ */

extern SystemData sys;

// DS18B20 bus + driver
static OneWire oneWire(PIN_DS18B20_DATA);
static DallasTemperature waterSensors(&oneWire);
static DeviceAddress probeAddr[MAX_WATER_PROBES];
static DeviceAddress tankProbeAddress;
static bool tankProbeAddressValid = false;

// BME280
static Adafruit_BME280 bme;

static Adafruit_MAX31855 max31855(
    PIN_MAX31855_SCK,
    PIN_TC1_CS,
    PIN_MAX31855_MISO
);

static unsigned long lastExhaustReadMs = 0;
static double lastExhaustF = NAN;

/* ============================================================
 *  EXHAUST SENSOR (MAX31855)
 * ============================================================ */

double exhaust_readF_cached() {
    unsigned long now = millis();
    if (now - lastExhaustReadMs < 250) {
        return lastExhaustF;
    }

    lastExhaustReadMs = now;

    double c = max31855.readCelsius();

    if (isnan(c)) {
        // Hold the last good value through brief bus/thermocouple glitches.
        sys.exhaustSensorOK = !isnan(lastExhaustF) &&
                              now - sys.exhaustLastGoodMs <= 1000UL;
        return lastExhaustF;
    }

    sys.exhaustSensorOK = true;
    sys.exhaustLastGoodMs = now;

    lastExhaustF = c * 9.0 / 5.0 + 32.0;
    return lastExhaustF;
}

/* ============================================================
 *  WATER PROBE SCAN
 * ============================================================ */

void scanWaterProbes() {
    DeviceAddress oldAddr[MAX_WATER_PROBES];
    char oldNames[MAX_WATER_PROBES][PROBE_NAME_LENGTH];
    uint8_t oldCount = sys.waterProbeCount;
    for (uint8_t i = 0; i < oldCount && i < MAX_WATER_PROBES; i++) {
        memcpy(oldAddr[i], probeAddr[i], 8);
        memcpy(oldNames[i], sys.waterProbeNames[i], PROBE_NAME_LENGTH);
    }

    if (!tankProbeAddressValid && sys.probeRoleMap[PROBE_TANK] < oldCount) {
        memcpy(tankProbeAddress,
               oldAddr[sys.probeRoleMap[PROBE_TANK]], 8);
        tankProbeAddressValid = true;
    }

    DeviceAddress found[MAX_WATER_PROBES];
    uint8_t foundCount = 0;
    sys.waterProbeCount = 0;
    oneWire.reset_search();

    DeviceAddress addr;

    while (oneWire.search(addr)) {
        if (foundCount < MAX_WATER_PROBES) {
            memcpy(found[foundCount], addr, 8);
            foundCount++;
        }
    }

    for (uint8_t i = 0; i < foundCount; i++) {
        memcpy(probeAddr[i], found[i], 8);
        bool wasKnown = false;
        for (uint8_t old = 0; old < oldCount; old++) {
            if (memcmp(found[i], oldAddr[old], 8) == 0) {
                memcpy(sys.waterProbeNames[i], oldNames[old], PROBE_NAME_LENGTH);
                wasKnown = true;
                break;
            }
        }
        if (!wasKnown && oldCount != 0) {
            snprintf(sys.waterProbeNames[i], PROBE_NAME_LENGTH,
                     "Probe %u", i + 1);
        }
    }
    sys.waterProbeCount = foundCount;

    if (tankProbeAddressValid) {
        sys.probeRoleMap[PROBE_TANK] = MAX_WATER_PROBES;
        for (uint8_t i = 0; i < foundCount; i++) {
            if (memcmp(probeAddr[i], tankProbeAddress, 8) == 0) {
                sys.probeRoleMap[PROBE_TANK] = i;
                break;
            }
        }
    }

    for (uint8_t i = 0; i < sys.waterProbeCount; i++) {
        waterSensors.setResolution(probeAddr[i], 9);
    }
}

void sensors_rescanWaterProbes() {
    scanWaterProbes();
}

/* ============================================================
 *  WATER PROBE READ
 * ============================================================ */

void sensors_readWaterProbes() {
    if (sys.waterProbeCount == 0) return;

    waterSensors.requestTemperatures();
    unsigned long now = millis();

    for (uint8_t i = 0; i < sys.waterProbeCount; i++) {
        float c = waterSensors.getTempC(probeAddr[i]);

        if (c > -55 && c < 125) {
            float newF = c * 9.0f / 5.0f + 32.0f;

            if (isnan(sys.waterTempF[i])) {
                sys.waterTempF[i] = newF;
            } else {
                sys.waterTempF[i] = sys.waterTempF[i] * 0.8f + newF * 0.2f;
            }

            sys.waterTempLastGoodMs[i] = now;
        }
    }
}

/* ============================================================
 *  BME280 READ
 * ============================================================ */

void sensors_readBME280() {
    if (!sys.envSensorOK) return;

    float t = bme.readTemperature();
    float h = bme.readHumidity();
    float p = bme.readPressure();

    if (!isnan(t)) sys.envTempF    = t * 9.0f / 5.0f + 32.0f;
    if (!isnan(h)) sys.envHumidity = h;
    if (!isnan(p)) sys.envPressure = p / 100.0f;
}

/* ============================================================
 *  INIT
 * ============================================================ */

bool sensors_init() {
    // BME280
    bool ok = bme.begin(0x76);
    sys.envSensorOK = ok;

    // DS18B20
    waterSensors.begin();
    waterSensors.setWaitForConversion(false);

    scanWaterProbes();

    return ok;
}

