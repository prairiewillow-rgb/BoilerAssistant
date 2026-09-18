/*
 * ============================================================
 *  Boiler Assistant – WiFi JSON API Module (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: WiFiAPI.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Deterministic, non‑blocking WiFi + HTTP JSON API subsystem
 *    for the UNO R4 WiFi. Implements the Total Domination
 *    Architecture (TDA) for all network‑side operator access.
 *
 *    Responsibilities:
 *      • Safe WiFi auto‑retry (5s cooldown)
 *      • Minimal HTTP server on port 80
 *      • JSON endpoints:
 *          - GET  /api/state
 *          - GET  /api/settings
 *          - POST /api/set
 *      • Remote write‑back to SystemData with remoteChanged flag
 *
 *    Architectural Notes:
 *      - No blocking delays
 *      - No dynamic allocation beyond ArduinoJson buffers
 *      - Provisioning-aware: disabled in AP mode
 *      - SystemData is the single source of truth
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#include "WiFiAPI.h"
#include "SystemData.h"
#include "RuntimeCredentials.h"
#include "WiFiProvisioning.h"
#include "EEPROMStorage.h"
#include "ConfigValidation.h"
#include "DashboardHTML.h"
#include "BurnEngine.h"

#include <WiFiS3.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

extern SystemData sys;

/* ============================================================
 *  WiFi Credentials (from provisioning)
 * ============================================================ */

static const char* getWifiSSID() {
    if (runtimeCreds.hasCredentials && runtimeCreds.ssid[0] != 0)
        return runtimeCreds.ssid;
    return "";
}

static const char* getWifiPASS() {
    if (runtimeCreds.hasCredentials && runtimeCreds.pass[0] != 0)
        return runtimeCreds.pass;
    return "";
}

/* ============================================================
 *  HTTP Server
 * ============================================================ */

WiFiServer server(80);

/* ============================================================
 *  Retry Timer
 * ============================================================ */

static unsigned long lastWifiAttempt = 0;

/* ============================================================
 *  JSON Documents
 * ============================================================ */

static StaticJsonDocument<1024> stateDoc;
static StaticJsonDocument<512> settingsDoc;

/* ============================================================
 *  Helpers
 * ============================================================ */

static void sendJson(WiFiClient& client, const String& json) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.print(json);
}

static void sendNotFound(WiFiClient& client) {
    client.println("HTTP/1.1 404 Not Found");
    client.println("Connection: close");
    client.println();
}

static void sendUnauthorized(WiFiClient& client) {
    client.println("HTTP/1.1 401 Unauthorized");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"error\":\"authentication required\"}");
}

static bool hasApiToken(const String& headers) {
    if (runtimeCreds.controlPass[0] == '\0') return false;

    String expected = "X-Boiler-Token: ";
    expected += runtimeCreds.controlPass;
    return headers.indexOf(expected) >= 0;
}

static int requestContentLength(const String& headers) {
    int marker = headers.indexOf("Content-Length:");
    if (marker < 0) return 0;

    marker += 15;
    while (marker < headers.length() && headers[marker] == ' ') marker++;

    int end = headers.indexOf('\n', marker);
    if (end < 0) end = headers.length();
    return headers.substring(marker, end).toInt();
}

/* ============================================================
 *  JSON Builders
 * ============================================================ */

static String buildStateJson() {
    stateDoc.clear();

    const char* burnText =
        (sys.burnState == BURN_IDLE) ? "IDLE" :
        (sys.burnState == BURN_BOOST) ? "BOOST" :
        (sys.burnState == BURN_RAMP) ? "RAMP" :
        (sys.burnState == BURN_HOLD) ? "HOLD" :
        (sys.burnState == BURN_EMBER_GUARD) ? "EMBER_GUARD" : "UNKNOWN";
    const char* safetyText =
        (sys.safetyState == SAFETY_OK) ? "OK" :
        (sys.safetyState == SAFETY_HIGHTEMP) ? "HIGH_TEMP" : "SENSOR_FAULT";
    bool tankSensorOK = false;
    float tankTemp = NAN;
    if (sys.waterProbeCount > 0) {
        uint8_t tankProbe = sys.probeRoleMap[PROBE_TANK];
        if (tankProbe < sys.waterProbeCount) {
            tankTemp = sys.waterTempF[tankProbe];
            tankSensorOK = !isnan(tankTemp) &&
                           millis() - sys.waterTempLastGoodMs[tankProbe] <= 3000UL;
        }
    }

    stateDoc["exhaust_raw"]   = sys.exhaustRawF;
    stateDoc["exhaust_smooth"] = sys.exhaustSmoothF;
    stateDoc["fan"]            = sys.fanFinal;
    stateDoc["fan_demand"]     = sys.fanDemand;
    stateDoc["fan_final"]      = sys.fanFinal;
    stateDoc["fan_pwm_percent"] = sys.fanFinal;
    stateDoc["adaptive_slope"] = burnengine_getAdaptiveSlope();
    stateDoc["burn_state"]     = sys.burnState;
    stateDoc["burn_state_text"] = burnText;
    stateDoc["safety_state"] = sys.safetyState;
    stateDoc["safety_text"] = safetyText;
    stateDoc["exhaust_fallback"] = sys.exhaustFallbackActive;
    stateDoc["alert"] = sys.exhaustFallbackActive
                              ? "EXHAUST SENSOR NEEDS REPLACEMENT - FAN 100% FALLBACK"
                              : (sys.safetyState == SAFETY_OK ? "" : safetyText);
    stateDoc["tank_temp"] = tankTemp;
    stateDoc["tank_sensor_ok"] = tankSensorOK;
    stateDoc["exhaust_sensor_ok"] = sys.exhaustSensorOK;
    stateDoc["wifi_ok"] = sys.wifiOK;
    stateDoc["display_name"] = runtimeCreds.displayName;
    stateDoc["tank_low"] = sys.tankLowSetpointF;
    stateDoc["tank_high"] = sys.tankHighSetpointF;

    stateDoc["rssi"]           = WiFi.RSSI();

    JsonObject env = stateDoc.createNestedObject("env");
    env["temp_f"]   = sys.envTempF;
    env["humidity"] = sys.envHumidity;
    env["pressure"] = sys.envPressure;

    JsonArray water = stateDoc.createNestedArray("water");
    for (uint8_t i = 0; i < sys.waterProbeCount; i++) {
        JsonObject probe = water.createNestedObject();
        probe["index"] = i;
        probe["name"] = sys.waterProbeNames[i];
        probe["temp_f"] = sys.waterTempF[i];
        probe["sensor_ok"] = millis() - sys.waterTempLastGoodMs[i] <= 3000UL;
        probe["controls_tank"] = sys.probeRoleMap[PROBE_TANK] == i;
    }

    String out;
    serializeJson(stateDoc, out);
    return out;
}

static String buildSettingsJson() {
    settingsDoc.clear();

    settingsDoc["exhaust_setpoint"] = sys.exhaustSetpoint;
    settingsDoc["deadband"]         = sys.deadbandF;
    settingsDoc["boost_time"]       = sys.boostTimeSeconds;
    settingsDoc["clamp_min"]        = sys.clampMinPercent;
    settingsDoc["clamp_max"]        = sys.clampMaxPercent;
    settingsDoc["deadzone_fan"]     = sys.deadzoneFanMode;
    settingsDoc["ember_minutes"]    = sys.emberGuardianTimerMinutes;
    settingsDoc["flue_low"]         = sys.flueLowThreshold;
    settingsDoc["flue_recovery"]    = sys.flueRecoveryThreshold;
    settingsDoc["tank_low"]          = sys.tankLowSetpointF;
    settingsDoc["tank_high"]         = sys.tankHighSetpointF;
    String out;
    serializeJson(settingsDoc, out);
    return out;
}

/* ============================================================
 *  POST /api/set
 * ============================================================ */

static void handleApiSet(WiFiClient& client, const String& body) {
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        sendJson(client, "{\"error\":\"invalid JSON\"}");
        return;
    }

    int exhaustSetpoint = sys.exhaustSetpoint;
    int deadband = sys.deadbandF;
    int boostTime = sys.boostTimeSeconds;
    int clampMin = sys.clampMinPercent;
    int clampMax = sys.clampMaxPercent;
    int deadzoneFan = sys.deadzoneFanMode;
    int guardianMinutes = sys.emberGuardianTimerMinutes;
    int flueLow = sys.flueLowThreshold;
    int flueRecovery = sys.flueRecoveryThreshold;
    int tankLow = sys.tankLowSetpointF;
    int tankHigh = sys.tankHighSetpointF;

    if (doc.containsKey("exhaust_setpoint")) exhaustSetpoint = doc["exhaust_setpoint"];
    if (doc.containsKey("deadband")) deadband = doc["deadband"];
    if (doc.containsKey("boost_time")) boostTime = doc["boost_time"];
    if (doc.containsKey("clamp_min")) clampMin = doc["clamp_min"];
    if (doc.containsKey("clamp_max")) clampMax = doc["clamp_max"];
    if (doc.containsKey("deadzone_fan")) deadzoneFan = doc["deadzone_fan"];
    if (doc.containsKey("ember_minutes")) guardianMinutes = doc["ember_minutes"];
    if (doc.containsKey("flue_low")) flueLow = doc["flue_low"];
    if (doc.containsKey("flue_recovery")) flueRecovery = doc["flue_recovery"];
    if (doc.containsKey("tank_low")) tankLow = doc["tank_low"];
    if (doc.containsKey("tank_high")) tankHigh = doc["tank_high"];
    if (!validExhaustSetpoint(exhaustSetpoint) ||
        !validDeadband(deadband) ||
        !validBoostTime(boostTime) ||
        !validFanClamp(clampMin) ||
        !validFanClamp(clampMax) ||
        clampMin > clampMax ||
        (deadzoneFan != 0 && deadzoneFan != 1) ||
        !validGuardianMinutes(guardianMinutes) ||
        !validFlueThreshold(flueLow) ||
        !validFlueThreshold(flueRecovery) ||
        flueRecovery < flueLow ||
        !validTankSetpoint(tankLow) ||
        !validTankSetpoint(tankHigh) ||
        tankLow >= tankHigh ||
        tankHigh >= 190) {
        sendJson(client, "{\"error\":\"configuration value out of range\"}");
        return;
    }

    bool changed = false;

    if (doc.containsKey("exhaust_setpoint")) {
        sys.exhaustSetpoint = exhaustSetpoint;
        eeprom_saveSetpoint(exhaustSetpoint);
        changed = true;
    }
    if (doc.containsKey("deadband")) {
        sys.deadbandF = deadband;
        eeprom_saveDeadband(deadband);
        changed = true;
    }
    if (doc.containsKey("boost_time")) {
        sys.boostTimeSeconds = boostTime;
        eeprom_saveBoostTime(boostTime);
        changed = true;
    }
    if (doc.containsKey("clamp_min")) {
        sys.clampMinPercent = clampMin;
        eeprom_saveClampMin(clampMin);
        changed = true;
    }
    if (doc.containsKey("clamp_max")) {
        sys.clampMaxPercent = clampMax;
        eeprom_saveClampMax(clampMax);
        changed = true;
    }
    if (doc.containsKey("deadzone_fan")) {
        sys.deadzoneFanMode = deadzoneFan;
        eeprom_saveDeadzone(deadzoneFan);
        changed = true;
    }
    if (doc.containsKey("ember_minutes")) {
        sys.emberGuardianTimerMinutes = guardianMinutes;
        eeprom_saveEmberGuardianMinutes(guardianMinutes);
        changed = true;
    }
    if (doc.containsKey("flue_low")) {
        sys.flueLowThreshold = flueLow;
        eeprom_saveFlueLow(flueLow);
        changed = true;
    }
    if (doc.containsKey("flue_recovery")) {
        sys.flueRecoveryThreshold = flueRecovery;
        eeprom_saveFlueRecovery(flueRecovery);
        changed = true;
    }
    if (doc.containsKey("tank_low")) {
        sys.tankLowSetpointF = tankLow;
        eeprom_saveTankLow(tankLow);
        changed = true;
    }
    if (doc.containsKey("tank_high")) {
        sys.tankHighSetpointF = tankHigh;
        eeprom_saveTankHigh(tankHigh);
        changed = true;
    }
    if (changed) {
        sys.remoteChanged = true;
    }

    sendJson(client, "{\"ok\":true}");
}

static void handleApiProbe(WiFiClient& client, const String& body) {
    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, body)) {
        sendJson(client, "{\"error\":\"invalid JSON\"}");
        return;
    }

    int index = doc["index"] | -1;
    const char* name = doc["name"] | "";
    if (index < 0 || index >= MAX_WATER_PROBES || name[0] == '\0') {
        sendJson(client, "{\"error\":\"invalid probe name\"}");
        return;
    }

    strncpy(sys.waterProbeNames[index], name, PROBE_NAME_LENGTH - 1);
    sys.waterProbeNames[index][PROBE_NAME_LENGTH - 1] = '\0';
    eeprom_saveProbeName((uint8_t)index);
    sys.remoteChanged = true;
    sendJson(client, "{\"ok\":true}");
}

static void handleApiReset(WiFiClient& client) {
    if (sys.safetyState == SAFETY_HIGHTEMP) {
        sendJson(client, "{\"error\":\"high-temperature lockout requires local reset\"}");
        return;
    }
    burnengine_resetAlarms();
    sys.remoteChanged = true;
    sendJson(client, "{\"ok\":true}");
}

/* ============================================================
 *  WiFi Init (provisioning-aware)
 * ============================================================ */

void wifiapi_init() {
    if (wifi_prov_isAPMode()) {
        Serial.println("WiFiAPI: skipped (AP mode active)");
        return;
    }

    Serial.println("WiFiAPI: init");

    const char* ssid = getWifiSSID();
    const char* pass = getWifiPASS();

    if (ssid[0] == 0) {
        Serial.println("WiFiAPI: no credentials → skipping");
        sys.wifiOK = false;
        return;
    }

    Serial.print("WiFiAPI: connecting to SSID: ");
    Serial.println(ssid);

    WiFi.begin(ssid, pass);
    lastWifiAttempt = millis();

    server.begin();
}

/* ============================================================
 *  WiFi Loop (non‑blocking auto‑retry)
 * ============================================================ */

void wifiapi_loop() {

    if (wifi_prov_isAPMode()) {
        sys.wifiOK = false;
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        sys.wifiOK = false;

        unsigned long now = millis();

        if (now - lastWifiAttempt > 5000) {
            lastWifiAttempt = now;

            const char* ssid = getWifiSSID();
            const char* pass = getWifiPASS();

            if (ssid[0] == 0) return;

            Serial.print("WiFiAPI: retrying SSID ");
            Serial.println(ssid);

            WiFi.begin(ssid, pass);
        }

        return;
    }

    IPAddress ip = WiFi.localIP();
    if (ip == IPAddress(0, 0, 0, 0)) {
        sys.wifiOK = false;
        return;
    }

    sys.wifiOK = true;

    static bool printed = false;
    if (!printed) {
        printed = true;
        Serial.print("WiFiAPI: WiFi connected. IP: ");
        Serial.println(ip);
    }

    WiFiClient client = server.available();
    if (!client) return;

    if (!client.available()) {
        client.stop();
        return;
    }

    client.setTimeout(25);
    String req = client.readStringUntil('\r');
    client.readStringUntil('\n');

    String headers;
    while (client.available()) {
        String headerLine = client.readStringUntil('\n');
        headers += headerLine;
        if (headerLine == "\r" || headerLine.length() == 0) break;
    }

    String body = "";
    if (req.startsWith("POST")) {
        int contentLength = requestContentLength(headers);
        unsigned long bodyStart = millis();

        while ((contentLength == 0 || body.length() < contentLength) &&
               millis() - bodyStart < 100UL) {
            while (client.available() &&
                   (contentLength == 0 || body.length() < contentLength)) {
                body += (char)client.read();
            }
        }
    }

    if (req.startsWith("GET / HTTP") || req.startsWith("GET /dashboard")) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html; charset=utf-8");
        client.println("Cache-Control: no-store");
        client.println("Connection: close");
        client.println();
        client.write((const uint8_t*)DASHBOARD_HTML, strlen_P(DASHBOARD_HTML));
    }
    else if (req.startsWith("GET /api/state")) {
        sendJson(client, buildStateJson());
    }
    else if (req.startsWith("GET /api/settings")) {
        sendJson(client, buildSettingsJson());
    }
    else if (req.startsWith("POST /api/set")) {
        if (hasApiToken(headers)) {
            handleApiSet(client, body);
        } else {
            sendUnauthorized(client);
        }
    }
    else if (req.startsWith("POST /api/probe")) {
        if (hasApiToken(headers)) {
            handleApiProbe(client, body);
        } else {
            sendUnauthorized(client);
        }
    }
    else if (req.startsWith("POST /api/reset")) {
        if (hasApiToken(headers)) {
            handleApiReset(client);
        } else {
            sendUnauthorized(client);
        }
    }
    else {
        sendNotFound(client);
    }

    client.stop();
}
