/*
 * ============================================================
 *  Boiler Assistant – Main Firmware (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: BoilerAssistant_3_Total Domination.ino
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Core deterministic firmware for the Boiler Assistant controller.
 *    Version 3.1 continues the Total Domination Architecture (TDA):
 *      - SystemData as the single source of truth
 *      - Deterministic, non-blocking main loop
 *      - Unified keypad-driven UI with numeric selection everywhere
 *      - Fully transparent operator-facing logic and documentation
 *
 *    Subsystems coordinated by this module:
 *      - Environmental sensing (BME280)
 *      - Temperature sensing (DS18B20)
 *      - Burn engine logic (demand, ramp, hold, safety)
 *      - Fan control (PWM, clamp logic, deadzone modes)
 *      - UI rendering (LCD) + keypad-driven operator interface
 *      - EEPROM-backed configuration and seasonal profiles
 *      - WiFi provisioning (STA-first, AP-fallback)
 *      - WiFi API + MQTT telemetry (async, non-blocking)
 *
 *  v3.1 Additions:
 *      - Adaptive fan curve with persistent learning
 *      - Fan-off deadband with one-PWM variable-speed control
 *      - Exhaust-probe fallback with 100% fan output
 *      - Mode-aware tank-probe safety handling
 *      - Address-stable water probes with periodic rescanning
 *      - Named monitor-only probes for MQTT and dashboard telemetry
 *      - Live diagnostics through the web dashboard and MQTT
 *      - Authenticated remote alarm reset; run mode remains local-only
 *
 *  Architectural Notes:
 *      - Main loop is strictly deterministic and non-blocking
 *      - All subsystems operate on timed or event-driven cadence
 *      - WiFi + MQTT run asynchronously to avoid blocking control logic
 *      - UI executes last to ensure stable system state before rendering
 *      - Pinout.h and SystemState.h are the authoritative hardware/state contracts
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <WDT.h>

#include "SystemState.h"          // MUST be first project header
#include "EnvironmentalLogic.h"   // MUST be second
#include "SystemData.h"           // MUST be after both
#include "UI.h"

#include "EEPROMStorage.h"
#include "Sensors.h"
#include "BurnEngine.h"
#include "FanControl.h"
#include "Keypad_I2C.h"
#include "Pinout.h"

#include <WiFiS3.h>
#include "WiFiAPI.h"
#include "MQTTClient.h"
#include "WiFiProvisioning.h"

/* ============================================================
 *  COMPATIBILITY SHIMS (v2.2 → v3.x)
 * ============================================================ */
#ifndef MAX_WATER_PROBES
#define MAX_WATER_PROBES 8
#endif

#ifndef PROBE_ROLE_COUNT
#define PROBE_ROLE_COUNT 8
#endif

/* ============================================================
 *  GLOBAL STATE (minimal shims + runtime)
 * ============================================================ */

// UI state
UIState uiState      = UI_HOME;
bool    uiNeedRedraw = true;

// UI edit buffers
String newSetpointValue;
String boostTimeEditValue;
String deadbandEditValue;
String clampMinEditValue;
String clampMaxEditValue;
String emberGuardianEditValue;
String flueLowEditValue;
String flueRecEditValue;
String tankLowEditValue;
String tankHighEditValue;
String envSeasonEditValue;
String envSetpointEditValue;
String envLockoutEditValue;

/* Forward declarations */
double exhaust_readF_cached();

/* Local implementation of exhaust smoothing */
double smoothExhaustF(double rawF) {
    if (isnan(rawF)) {
        return sys.exhaustSmoothF;
    }

    if (isnan(sys.exhaustSmoothF)) {
        sys.exhaustSmoothF = rawF;
    } else {
        sys.exhaustSmoothF = sys.exhaustSmoothF * 0.8 + rawF * 0.2;
    }
    return sys.exhaustSmoothF;
}

/* ============================================================
 *  SETUP
 * ============================================================ */

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(PIN_DAMPER, OUTPUT);
    digitalWrite(PIN_DAMPER, HIGH);   // default CLOSED

    pinMode(PIN_FAN_PWM, OUTPUT);
    analogWrite(PIN_FAN_PWM, 0);

    Serial.println();
    Serial.println("=== Boiler Assistant v3.1 Boot ===");

    Wire.begin();
    Wire.setClock(400000);

    // SystemData must be initialized before EEPROM populates it
    systemdata_init();

    // Load all EEPROM-backed settings into sys.*
    eeprom_init();

    // Sensors + logic
    sensors_init();
    env_logic_init();
    burnengine_init();
    fancontrol_init();
    keypad_init(Wire);
    ui_init();

    // Provisioning: STA-first, AP-fallback
    wifi_prov_init();

    if (!wifi_prov_isAPMode()) {
        wifiapi_init();
        mqtt_init();
    }

    WDT.begin(8000);
}

/* ============================================================
 *  LOOP
 * ============================================================ */

void loop() {

    WDT.refresh();
    unsigned long now = millis();

    // 0) Keypad
    char k = keypad_read();
    if (k) {
        double rawExhKey = exhaust_readF_cached();
        double smoothedKeyExhaust = smoothExhaustF(rawExhKey);
        ui_handleKey(k, smoothedKeyExhaust, sys.fanFinal);
        uiNeedRedraw = true;
    }

    // 1) Sensors
    static unsigned long lastBME = 0;
    if (now - lastBME > 3000) {
        sensors_readBME280();
        env_logic_update(now);
        lastBME = now;
    }

    static unsigned long lastWaterRead = 0;
    if (now - lastWaterRead > 500) {
        sensors_readWaterProbes();
        lastWaterRead = now;
    }

    static unsigned long lastProbeScan = 0;
    if (now - lastProbeScan >= 60000UL) {
        sensors_rescanWaterProbes();
        lastProbeScan = now;
    }

    // 2) Burn engine – exhaust pipeline
    double rawExh = exhaust_readF_cached();
    sys.exhaustRawF = rawExh;                    // live raw flue temp for Guardian
    double smoothedExh = smoothExhaustF(rawExh);
    sys.exhaustSmoothF = smoothedExh;            // preserve float precision for control

    int demand = burnengine_compute();
    sys.fanDemand = demand;

    // 3) Fan control (single source of truth)
    int fanPercent = fancontrol_apply(demand);

    int pwm = map(fanPercent, 0, 100, 0, 255);
    analogWrite(PIN_FAN_PWM, pwm);

    // 4) Update SystemData snapshot for UI / WiFi / MQTT

    sys.fanFinal = fanPercent;

    sys.uptimeMs = now;

    // 5) WiFi + MQTT (only when NOT in AP mode)
    if (!wifi_prov_isAPMode()) {
        wifiapi_loop();
        mqtt_loop();
    }

    // 6) UI
    ui_showScreen(uiState, sys.exhaustSmoothF, fanPercent);

    // 7) Provisioning AP handler
    wifi_prov_loop();
}
