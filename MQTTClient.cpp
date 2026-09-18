/*
 * ============================================================
 *  Boiler Assistant – MQTT Client Module (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: MQTTClient.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Deterministic MQTT telemetry + command subsystem for the
 *    Boiler Assistant controller. Implements the Total Domination
 *    Architecture (TDA) for all network‑side communication.
 *
 *    Responsibilities:
 *      • Non‑blocking MQTT RX/TX loop
 *      • State, settings, water, and outdoor telemetry topics
 *      • Home Assistant auto‑discovery publishing
 *      • CRC‑validated remote command handling
 *      • Full SystemData integration (no legacy globals)
 *
 *    Architectural Notes:
 *      - All MQTT operations are non‑blocking
 *      - No dynamic allocation beyond ArduinoJson buffers
 *      - SystemData is the single source of truth
 *      - No burn logic, UI logic, or EEPROM logic lives here
 *      - Reconnect logic is rate‑limited and deterministic
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

//  ======================= MQTTClient.cpp =======================
//  Clean, unified, no legacy fields
//  ===============================================================

#include <Arduino.h>
#include "SystemState.h"
#include "EnvironmentalLogic.h"
#include "SystemData.h"

#include <ArduinoJson.h>
#include <WiFiS3.h>
#include <ArduinoMqttClient.h>

#include "MQTTClient.h"
#include "EEPROMStorage.h"
#include "WiFiProvisioning.h"
#include "RuntimeCredentials.h"
#include "ConfigValidation.h"
#include "BurnEngine.h"

#ifndef PROBE_ROLE_COUNT
#define PROBE_ROLE_COUNT 8
#endif

#ifndef MAX_WATER_PROBES
#define MAX_WATER_PROBES 8
#endif

extern SystemData sys;

extern const char* prov_mqtt_server;
extern const char* prov_mqtt_user;
extern const char* prov_mqtt_pass;

static const int   MQTT_PORT      = 1883;
static const char* MQTT_CLIENT_ID = "BoilerAssistant";

static const char* TOPIC_STATE    = "boiler/state";
static const char* TOPIC_SETTINGS = "boiler/settings";
static const char* TOPIC_WATER    = "boiler/water";
static const char* TOPIC_OUTDOOR  = "boiler/outdoor";
static const char* TOPIC_ALERT    = "boiler/alert";

static const char* HA_DISCOVERY_PREFIX = "homeassistant";
static const char* HA_DEVICE_ID        = "boiler_assistant";

static const char* HA_DEVICE_NAME  = "Boiler Assistant";
static const char* HA_DEVICE_MODEL = "UNO R4 WiFi";
static const char* HA_DEVICE_MFR   = "Karl";
static const char* HA_DEVICE_SW    = "3.3";

WiFiClient wifiClient;
MqttClient mqtt(wifiClient);

static unsigned long lastStateFastMs      = 0;
static unsigned long lastStateSlowMs      = 0;
static unsigned long lastWaterMs          = 0;
static unsigned long lastSettingsMs       = 0;
static unsigned long lastOutdoorBmeMs     = 0;
static unsigned long lastReconnectAttempt = 0;
static bool mqttAuthConfigured = false;
static bool lastHighTemp = false;
static bool lastTankFault = false;
static bool lastExhaustFallback = false;
static bool lastGuardian = false;

// Forward declarations
static void mqtt_publishState();
static void mqtt_publishSettings();
static void mqtt_publishWater();
static void mqtt_publishOutdoor();
static void mqtt_publishAlert(const char* code, const char* message,
                              bool active);
static void mqtt_publishAlertTransitions();
static void mqtt_onMessage(int messageSize);
static void mqtt_reconnect();
static void publishDiscovery();

static void publishDiscoverySensor(
    const char* objectId, const char* name, const char* stateTopic,
    const char* valueTemplate, const char* unit,
    const char* deviceClass, const char* icon = nullptr
);

static void publishDiscoveryNumber(
    const char* objectId, const char* name,
    const char* cmdTopic, const char* stateTopic,
    const char* unit, float minVal, float maxVal, float step,
    const char* deviceClass = nullptr, const char* icon = nullptr
);

static void publishDiscoverySwitch(
    const char* objectId, const char* name,
    const char* cmdTopic, const char* stateTopic,
    const char* icon = nullptr
);

static void handleCommandTopic(const String& topic, StaticJsonDocument<256>& doc);

// ============================================================
// INIT
// ============================================================

void mqtt_init() {
    mqttAuthConfigured = prov_mqtt_server != nullptr &&
                         prov_mqtt_server[0] != '\0' &&
                         prov_mqtt_user != nullptr &&
                         prov_mqtt_user[0] != '\0' &&
                         prov_mqtt_pass != nullptr &&
                         prov_mqtt_pass[0] != '\0';

    if (!mqttAuthConfigured) {
        Serial.println("MQTT: disabled because broker authentication is not configured");
        return;
    }

    mqtt.setId(MQTT_CLIENT_ID);
    mqtt.setUsernamePassword(prov_mqtt_user, prov_mqtt_pass);
    mqtt.setKeepAliveInterval(15);
    mqtt.onMessage(mqtt_onMessage);
}

// ============================================================
// LOOP
// ============================================================

void mqtt_loop() {
    if (wifi_prov_isAPMode()) return;
    if (!mqttAuthConfigured) return;
    if (!sys.wifiOK) return;
    if (WiFi.status() != WL_CONNECTED) return;

    mqtt_reconnect();
    if (!mqtt.connected()) return;

    mqtt.poll();

    unsigned long now = millis();

    if (now - lastWaterMs > 1000) {
        mqtt_publishWater();
        lastWaterMs = now;
    }

    if (now - lastStateFastMs > 1000) {
        mqtt_publishState();
        lastStateFastMs = now;
    }

    mqtt_publishAlertTransitions();

    if (now - lastStateSlowMs > 30000) {
        mqtt_publishState();
        lastStateSlowMs = now;
    }

    if (now - lastSettingsMs > 60000) {
        mqtt_publishSettings();
        lastSettingsMs = now;
    }

    if (now - lastOutdoorBmeMs > 1000) {
        mqtt_publishOutdoor();
        lastOutdoorBmeMs = now;
    }
}

// ============================================================
// RECONNECT
// ============================================================

static void mqtt_reconnect() {
    unsigned long now = millis();

    if (!sys.wifiOK) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (mqtt.connected()) return;
    if (now - lastReconnectAttempt < 30000) return;

    lastReconnectAttempt = now;

    if (mqtt.connect(prov_mqtt_server, MQTT_PORT)) {
        mqtt.subscribe("boiler/cmd/#");
        publishDiscovery();
    }
}

// ============================================================
// PUBLISHERS
// ============================================================

static void mqtt_publishState() {
    StaticJsonDocument<1536> doc;

    float tankTemp = NAN;
    bool tankSensorOK = false;
    if (sys.waterProbeCount > 0) {
        uint8_t tankProbe = sys.probeRoleMap[PROBE_TANK];
        if (tankProbe < sys.waterProbeCount) {
            tankTemp = sys.waterTempF[tankProbe];
            tankSensorOK = !isnan(tankTemp) &&
                           millis() - sys.waterTempLastGoodMs[tankProbe] <= 3000UL;
        }
    }

    doc["exhaust"]        = sys.exhaustSmoothF;
    doc["exhaust_smooth"] = sys.exhaustSmoothF;
    doc["exhaust_raw"]    = sys.exhaustRawF;
    doc["exhaust_fallback"] = sys.exhaustFallbackActive;
    doc["exhaust_alert"] = sys.exhaustFallbackActive
                                ? "EXHAUST SENSOR NEEDS REPLACEMENT - FAN 100% FALLBACK"
                                : "";
    doc["tank_temp"]      = tankTemp;
    doc["tank_sensor_ok"] = tankSensorOK;
    doc["fan"]            = sys.fanFinal;
    doc["fan_demand"]     = sys.fanDemand;
    doc["fan_final"]      = sys.fanFinal;
    doc["fan_pwm_percent"] = sys.fanFinal;
    doc["adaptive_slope"] = burnengine_getAdaptiveSlope();
    doc["state"]      = sys.burnState;
    doc["rssi"]       = WiFi.RSSI();

    const char* phaseText =
        (sys.burnState == BURN_IDLE)        ? "IDLE" :
        (sys.burnState == BURN_BOOST)       ? "BOOST" :
        (sys.burnState == BURN_RAMP)        ? "RAMP" :
        (sys.burnState == BURN_HOLD)        ? "HOLD" :
        (sys.burnState == BURN_EMBER_GUARD) ? "EMBER_GUARD" :
                                              "UNKNOWN";

    doc["state_text"] = phaseText;

    // ============================================================
    // Ember Guardian v3.3 unified model (ONLY new fields)
    // ============================================================

    doc["ember_guardian_active"] = sys.emberGuardianActive;

    long remainingMs = 0;
    if (sys.emberGuardianActive && sys.emberGuardianTimerMinutes > 0) {
        unsigned long now = millis();
        unsigned long total = (unsigned long)sys.emberGuardianTimerMinutes * 60000UL;
        long elapsed = (long)(now - sys.emberGuardianStartMs);
        remainingMs = total - elapsed;
        if (remainingMs < 0) remainingMs = 0;
    }

    int guardianSeconds = remainingMs / 1000;
    int guardianMinutes = remainingMs / 60000;

    doc["ember_guardian_seconds"] = guardianSeconds;
    doc["ember_guardian_minutes"] = guardianMinutes;

    char gtxt[32];
    snprintf(gtxt, sizeof(gtxt), "%d minutes remaining", guardianMinutes);
    doc["ember_guardian_remaining_text"] = gtxt;

    // Timing markers
    doc["boost_start"] = sys.boostStartMs;
    doc["ramp_start"]  = sys.rampStartMs;
    doc["hold_start"]  = sys.holdStartMs;
    doc["ember_start"] = sys.emberGuardianStartMs;

    // Boiler control
    doc["control_mode"]       = sys.controlMode;
    doc["safety_state"]       = sys.safetyState;
    doc["tank_low_setpoint"]  = sys.tankLowSetpointF;
    doc["tank_high_setpoint"] = sys.tankHighSetpointF;

    char buf[1024];
    size_t n = serializeJson(doc, buf);

    mqtt.beginMessage(TOPIC_STATE);
    mqtt.write((const uint8_t*)buf, n);
    mqtt.endMessage();
}

static void mqtt_publishAlert(const char* code, const char* message,
                              bool active)
{
    StaticJsonDocument<256> doc;
    doc["active"] = active;
    doc["code"] = code;
    doc["message"] = message;
    doc["timestamp_ms"] = millis();

    char buf[256];
    size_t n = serializeJson(doc, buf);
    mqtt.beginMessage(TOPIC_ALERT);
    mqtt.write((const uint8_t*)buf, n);
    mqtt.endMessage();
}

static void mqtt_publishAlertTransitions()
{
    bool highTemp = sys.safetyState == SAFETY_HIGHTEMP;
    bool tankFault = sys.safetyState == SAFETY_SENSOR_FAULT &&
                     (sys.sensorFaultMask & SENSOR_FAULT_TANK) != 0;
    bool exhaustFallback = sys.exhaustFallbackActive;
    bool guardian = sys.emberGuardianLatched ||
                    sys.burnState == BURN_EMBER_GUARD;

    if (highTemp != lastHighTemp) {
        mqtt_publishAlert("HIGH_TEMP",
                          highTemp ? "HIGH TEMPERATURE LOCKOUT" :
                                     "HIGH TEMPERATURE CLEARED",
                          highTemp);
        lastHighTemp = highTemp;
    }
    if (tankFault != lastTankFault) {
        mqtt_publishAlert("TANK_PROBE_FAULT",
                          tankFault ? "TANK PROBE FAULT - SYSTEM STOPPED" :
                                      "TANK PROBE FAULT CLEARED",
                          tankFault);
        lastTankFault = tankFault;
    }
    if (exhaustFallback != lastExhaustFallback) {
        mqtt_publishAlert("EXHAUST_PROBE_FAULT",
                          exhaustFallback ?
                              "EXHAUST PROBE FAULT - FAN 100% FALLBACK" :
                              "EXHAUST PROBE FAULT CLEARED",
                          exhaustFallback);
        lastExhaustFallback = exhaustFallback;
    }
    if (guardian != lastGuardian) {
        mqtt_publishAlert("EMBER_GUARDIAN",
                          guardian ? "EMBER GUARDIAN ACTIVE" :
                                     "EMBER GUARDIAN CLEARED",
                          guardian);
        lastGuardian = guardian;
    }
}

static void mqtt_publishSettings() {
    StaticJsonDocument<1024> doc;

    doc["setpoint"]   = sys.exhaustSetpoint;
    doc["deadband"]   = sys.deadbandF;
    doc["fan_min"]    = sys.clampMinPercent;
    doc["fan_max"]    = sys.clampMaxPercent;
    doc["boost_sec"]  = sys.boostTimeSeconds;
    doc["ember_min"]  = sys.emberGuardianTimerMinutes;
    doc["flue_low"]   = sys.flueLowThreshold;
    doc["flue_rec"]   = sys.flueRecoveryThreshold;
    doc["deadzone"]   = sys.deadzoneFanMode;

    doc["season_mode"] = sys.envSeasonMode;
    doc["auto_season"] = sys.envAutoSeasonEnabled;
    doc["lockout_hr"]  = (uint16_t)(sys.envModeLockoutSec / 3600UL);

    doc["summer_start"]  = sys.envSummerStartF;
    doc["spf_start"]     = sys.envSpringFallStartF;
    doc["winter_start"]  = sys.envWinterStartF;
    doc["extreme_start"] = sys.envExtremeStartF;

    doc["summer_buffer"]  = sys.envHystSummerF;
    doc["spf_buffer"]     = sys.envHystSpringFallF;
    doc["winter_buffer"]  = sys.envHystWinterF;
    doc["extreme_buffer"] = sys.envHystExtremeF;

    doc["summer_setpoint"]  = sys.envSetpointSummerF;
    doc["spf_setpoint"]     = sys.envSetpointSpringFallF;
    doc["winter_setpoint"]  = sys.envSetpointWinterF;
    doc["extreme_setpoint"] = sys.envSetpointExtremeF;

    doc["control_mode"] = sys.controlMode;
    doc["tank_low"]     = sys.tankLowSetpointF;
    doc["tank_high"]    = sys.tankHighSetpointF;

    char buf[1024];
    size_t n = serializeJson(doc, buf);

    mqtt.beginMessage(TOPIC_SETTINGS);
    mqtt.write((const uint8_t*)buf, n);
    mqtt.endMessage();
}

static void mqtt_publishWater() {
    StaticJsonDocument<768> doc;

    JsonArray arr = doc.createNestedArray("water");
    for (uint8_t i = 0; i < sys.waterProbeCount; i++) {
        JsonObject probe = arr.createNestedObject();
        probe["index"] = i;
        probe["name"] = sys.waterProbeNames[i];
        probe["temp_f"] = sys.waterTempF[i];
        probe["sensor_ok"] = millis() - sys.waterTempLastGoodMs[i] <= 3000UL;
        probe["controls_tank"] = sys.probeRoleMap[PROBE_TANK] == i;
    }

    doc["count"] = sys.waterProbeCount;

    char buf[256];
    size_t n = serializeJson(doc, buf);

    mqtt.beginMessage(TOPIC_WATER);
    mqtt.write((const uint8_t*)buf, n);
    mqtt.endMessage();
}

static void mqtt_publishOutdoor() {
    StaticJsonDocument<256> doc;

    doc["temp"] = sys.envTempF;
    doc["hum"]  = sys.envHumidity;
    doc["pres"] = sys.envPressure;

    char buf[256];
    size_t n = serializeJson(doc, buf);

    mqtt.beginMessage(TOPIC_OUTDOOR);
    mqtt.write((const uint8_t*)buf, n);
    mqtt.endMessage();
}

// ============================================================
// HOME ASSISTANT DISCOVERY
// ============================================================

static void publishDiscovery() {

    publishDiscoverySensor("exhaust", "Exhaust Temp", TOPIC_STATE,
                           "{{value_json.exhaust}}", "°F", "temperature", "mdi:fire");

    publishDiscoverySensor("fan", "Fan Speed", TOPIC_STATE,
                           "{{value_json.fan}}", "%", nullptr, "mdi:fan");

    publishDiscoverySensor("fan_final", "Fan Final Output", TOPIC_STATE,
                           "{{value_json.fan_final}}", "%", nullptr, "mdi:fan-speed-3");

    publishDiscoverySensor("wifi_signal", "WiFi Signal", TOPIC_STATE,
                           "{{value_json.rssi}}", "dBm", "signal_strength", "mdi:wifi");

    publishDiscoverySensor("burn_state", "Burn State", TOPIC_STATE,
                           "{{value_json.state}}", "", nullptr, "mdi:fire-alert");

    publishDiscoverySensor("state_text", "Burn Phase Text", TOPIC_STATE,
                           "{{value_json.state_text}}", "", nullptr, "mdi:fire");

    // ============================================================
    // Ember Guardian v3.3 — ONLY new fields
    // ============================================================

    publishDiscoverySensor("ember_guardian_active", "Ember Guardian Active",
                           TOPIC_STATE, "{{value_json.ember_guardian_active}}",
                           "", "power", "mdi:shield");

    publishDiscoverySensor("ember_guardian_seconds", "Ember Guardian Seconds Remaining",
                           TOPIC_STATE, "{{value_json.ember_guardian_seconds}}",
                           "s", "duration", "mdi:timer-sand");

    publishDiscoverySensor("ember_guardian_minutes", "Ember Guardian Minutes Remaining",
                           TOPIC_STATE, "{{value_json.ember_guardian_minutes}}",
                           "min", nullptr, "mdi:timer");

    publishDiscoverySensor("ember_guardian_remaining_text", "Ember Guardian Remaining Text",
                           TOPIC_STATE, "{{value_json.ember_guardian_remaining_text}}",
                           "", nullptr, "mdi:timer-sand-complete");

    publishDiscoverySwitch("ember_guardian_override", "Ember Guardian Override",
                           "boiler/cmd/ember_guardian_override",
                           TOPIC_STATE, "mdi:shield-off");

    // ============================================================
    // Water probes
    // ============================================================

    for (int i = 0; i < 4; i++) {
        char obj[16], name[32], tpl[64];
        snprintf(obj, sizeof(obj), "water_%d", i + 1);
        snprintf(name, sizeof(name), "Water Temp %d", i + 1);
        snprintf(tpl, sizeof(tpl), "{{value_json.water[%d]}}", i);

        publishDiscoverySensor(obj, name, TOPIC_WATER,
                               tpl, "°F", "temperature", "mdi:coolant-temperature");
    }

    // ============================================================
    // Outdoor BME280
    // ============================================================

    publishDiscoverySensor("outdoor_temp", "Outdoor Temp", TOPIC_OUTDOOR,
                           "{{value_json.temp}}", "°F", "temperature", "mdi:thermometer");

    publishDiscoverySensor("outdoor_hum", "Outdoor Humidity", TOPIC_OUTDOOR,
                           "{{value_json.hum}}", "%", "humidity", "mdi:water-percent");

    publishDiscoverySensor("outdoor_pres", "Outdoor Pressure", TOPIC_OUTDOOR,
                           "{{value_json.pres}}", "hPa", "pressure", "mdi:gauge");

    // ============================================================
    // Controls
    // ============================================================

    publishDiscoveryNumber("setpoint", "Exhaust Setpoint",
                           "boiler/cmd/setpoint", TOPIC_SETTINGS,
                           "°F", 200, 900, 1, "temperature", "mdi:fire");

    publishDiscoveryNumber("boost", "Boost Time",
                           "boiler/cmd/boost", TOPIC_SETTINGS,
                           "s", 5, 300, 5, nullptr, "mdi:rocket-launch");

    publishDiscoveryNumber("deadband", "Deadband",
                           "boiler/cmd/deadband", TOPIC_SETTINGS,
                           "°F", 1, 100, 1, nullptr, "mdi:arrow-expand-vertical");

    publishDiscoveryNumber("clamp_min", "Fan Clamp Min",
                           "boiler/cmd/clamp_min", TOPIC_SETTINGS,
                           "%", 0, 100, 1, nullptr, "mdi:fan");

    publishDiscoveryNumber("clamp_max", "Fan Clamp Max",
                           "boiler/cmd/clamp_max", TOPIC_SETTINGS,
                           "%", 0, 100, 1, nullptr, "mdi:fan");

    publishDiscoverySwitch("deadzone", "Deadzone Fan Mode",
                           "boiler/cmd/deadzone", TOPIC_SETTINGS,
                           "mdi:toggle-switch");

    publishDiscoveryNumber("ember", "Ember Guardian Minutes",
                           "boiler/cmd/ember", TOPIC_SETTINGS,
                           "min", 5, 60, 1, nullptr, "mdi:shield");

    publishDiscoveryNumber("flue_low", "Flue Low Threshold",
                           "boiler/cmd/flue_low", TOPIC_SETTINGS,
                           "°F", 50, 900, 5, nullptr, "mdi:thermometer-alert");

    publishDiscoveryNumber("flue_rec", "Flue Recovery Threshold",
                           "boiler/cmd/flue_rec", TOPIC_SETTINGS,
                           "°F", 50, 900, 5, nullptr, "mdi:thermometer-chevron-up");

    publishDiscoveryNumber("lockout", "Season Lockout Hours",
                           "boiler/cmd/lockout", TOPIC_SETTINGS,
                           "h", 1, 24, 1, nullptr, "mdi:timer-lock");

    publishDiscoverySwitch("auto_season", "Auto Season",
                           "boiler/cmd/auto_season", TOPIC_SETTINGS,
                           "mdi:calendar-sync");

    publishDiscoveryNumber("season_mode", "Season Mode",
                           "boiler/cmd/season_mode", TOPIC_SETTINGS,
                           "", 0, 2, 1, nullptr, "mdi:calendar");

    publishDiscoveryNumber("summer_setpoint", "Summer Setpoint",
                           "boiler/cmd/summer_setpoint", TOPIC_SETTINGS,
                           "°F", 200, 900, 1);

    publishDiscoveryNumber("spf_setpoint", "Spring/Fall Setpoint",
                           "boiler/cmd/spf_setpoint", TOPIC_SETTINGS,
                           "°F", 200, 900, 1);

    publishDiscoveryNumber("winter_setpoint", "Winter Setpoint",
                           "boiler/cmd/winter_setpoint", TOPIC_SETTINGS,
                           "°F", 200, 900, 1);

    publishDiscoveryNumber("extreme_setpoint", "Extreme Setpoint",
                           "boiler/cmd/extreme_setpoint", TOPIC_SETTINGS,
                           "°F", 200, 900, 1);

    // v3.1 Boiler Control discovery
    publishDiscoveryNumber("tank_low", "Tank Low Setpoint",
                           "boiler/cmd/tank_low", TOPIC_SETTINGS,
                           "°F", 80, 190, 1, nullptr, "mdi:water-boiler");

    publishDiscoveryNumber("tank_high", "Tank High Setpoint",
                           "boiler/cmd/tank_high", TOPIC_SETTINGS,
                           "°F", 80, 190, 1, nullptr, "mdi:water-boiler");

}

/* ============================================================
 *  DISCOVERY HELPERS
 * ============================================================ */

static void publishDiscoverySensor(
    const char* objectId,
    const char* name,
    const char* stateTopic,
    const char* valueTemplate,
    const char* unit,
    const char* deviceClass,
    const char* icon
) {
    char topic[128];
    snprintf(topic, sizeof(topic),
             "%s/sensor/%s/%s/config",
             HA_DISCOVERY_PREFIX, HA_DEVICE_ID, objectId);

    StaticJsonDocument<512> doc;

    doc["name"]    = name;
    doc["uniq_id"] = String(HA_DEVICE_ID) + "_" + objectId;
    doc["stat_t"]  = stateTopic;

    if (valueTemplate) doc["val_tpl"] = valueTemplate;
    if (unit)         doc["unit_of_meas"] = unit;
    if (deviceClass)  doc["dev_cla"] = deviceClass;
    if (icon)         doc["ic"] = icon;

    JsonObject dev = doc.createNestedObject("dev");
    dev["ids"]  = HA_DEVICE_ID;
    dev["name"] = HA_DEVICE_NAME;
    dev["mf"]   = HA_DEVICE_MFR;
    dev["mdl"]  = HA_DEVICE_MODEL;
    dev["sw"]   = HA_DEVICE_SW;

    char buf[512];
    serializeJson(doc, buf);

    mqtt.beginMessage(topic, true);
    mqtt.print(buf);
    mqtt.endMessage();
}

static void publishDiscoveryNumber(
    const char* objectId,
    const char* name,
    const char* cmdTopic,
    const char* stateTopic,
    const char* unit,
    float minVal,
    float maxVal,
    float step,
    const char* deviceClass,
    const char* icon
) {
    char topic[128];
    snprintf(topic, sizeof(topic),
             "%s/number/%s/%s/config",
             HA_DISCOVERY_PREFIX, HA_DEVICE_ID, objectId);

    StaticJsonDocument<512> doc;

    doc["name"]    = name;
    doc["uniq_id"] = String(HA_DEVICE_ID) + "_" + objectId;
    doc["cmd_t"]   = cmdTopic;
    doc["stat_t"]  = stateTopic;

    doc["min"]  = minVal;
    doc["max"]  = maxVal;
    doc["step"] = step;

    if (unit)        doc["unit_of_meas"] = unit;
    if (deviceClass) doc["dev_cla"]      = deviceClass;
    if (icon)        doc["ic"]           = icon;

    JsonObject dev = doc.createNestedObject("dev");
    dev["ids"]  = HA_DEVICE_ID;
    dev["name"] = HA_DEVICE_NAME;
    dev["mf"]   = HA_DEVICE_MFR;
    dev["mdl"]  = HA_DEVICE_MODEL;
    dev["sw"]   = HA_DEVICE_SW;

    char buf[512];
    serializeJson(doc, buf);

    mqtt.beginMessage(topic, true);
    mqtt.print(buf);
    mqtt.endMessage();
}

static void publishDiscoverySwitch(
    const char* objectId,
    const char* name,
    const char* cmdTopic,
    const char* stateTopic,
    const char* icon
) {
    char topic[128];
    snprintf(topic, sizeof(topic),
             "%s/switch/%s/%s/config",
             HA_DISCOVERY_PREFIX, HA_DEVICE_ID, objectId);

    StaticJsonDocument<512> doc;

    doc["name"]    = name;
    doc["uniq_id"] = String(HA_DEVICE_ID) + "_" + objectId;
    doc["cmd_t"]   = cmdTopic;
    doc["stat_t"]  = stateTopic;

    if (icon) doc["ic"] = icon;

    JsonObject dev = doc.createNestedObject("dev");
    dev["ids"]  = HA_DEVICE_ID;
    dev["name"] = HA_DEVICE_NAME;
    dev["mf"]   = HA_DEVICE_MFR;
    dev["mdl"]  = HA_DEVICE_MODEL;
    dev["sw"]   = HA_DEVICE_SW;

    char buf[512];
    serializeJson(doc, buf);

    mqtt.beginMessage(topic, true);
    mqtt.print(buf);
    mqtt.endMessage();
}

/* ============================================================
 *  COMMAND HANDLER
 * ============================================================ */

static void mqtt_onMessage(int messageSize) {
    String topic = mqtt.messageTopic();

    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, mqtt)) {
        return;
    }

    handleCommandTopic(topic, doc);
}

/* ============================================================
 *  TOPIC DISPATCHER
 * ============================================================ */

static void handleCommandTopic(const String& topic, StaticJsonDocument<256>& doc) {

    if (!doc.containsKey("value")) return;
    auto val = doc["value"];

    // ---------------- CORE SETTINGS ----------------

    if (topic.endsWith("/setpoint")) {
        int v = val.as<int>();
        if (!validExhaustSetpoint(v)) return;
        eeprom_saveSetpoint(v);
        sys.exhaustSetpoint = v;
        return;
    }

    if (topic.endsWith("/boost")) {
        int v = val.as<int>();
        if (!validBoostTime(v)) return;
        eeprom_saveBoostTime(v);
        sys.boostTimeSeconds = v;
        return;
    }

    if (topic.endsWith("/deadband")) {
        int v = val.as<int>();
        if (!validDeadband(v)) return;
        eeprom_saveDeadband(v);
        sys.deadbandF = v;
        return;
    }

    if (topic.endsWith("/clamp_min")) {
        int v = val.as<int>();
        if (!validFanClamp(v) || v > sys.clampMaxPercent) return;
        eeprom_saveClampMin(v);
        sys.clampMinPercent = v;
        return;
    }

    if (topic.endsWith("/clamp_max")) {
        int v = val.as<int>();
        if (!validFanClamp(v) || v < sys.clampMinPercent) return;
        eeprom_saveClampMax(v);
        sys.clampMaxPercent = v;
        return;
    }

    if (topic.endsWith("/deadzone")) {
        bool v = val.as<bool>();
        eeprom_saveDeadzone(v ? 1 : 0);
        sys.deadzoneFanMode = v ? 1 : 0;
        return;
    }

    if (topic.endsWith("/ember")) {
        int v = val.as<int>();
        if (!validGuardianMinutes(v)) return;
        eeprom_saveEmberGuardianMinutes(v);
        sys.emberGuardianTimerMinutes = v;
        return;
    }

    if (topic.endsWith("/flue_low")) {
        int v = val.as<int>();
        if (!validFlueThreshold(v) || v > sys.flueRecoveryThreshold) return;
        eeprom_saveFlueLow(v);
        sys.flueLowThreshold = v;
        return;
    }

    if (topic.endsWith("/flue_rec")) {
        int v = val.as<int>();
        if (!validFlueThreshold(v) || v < sys.flueLowThreshold) return;
        eeprom_saveFlueRecovery(v);
        sys.flueRecoveryThreshold = v;
        return;
    }

    // ---------------- SEASONAL LOGIC ----------------

    if (topic.endsWith("/season_mode")) {
        int mode = val.as<int>();
        if (mode < 0 || mode > 2) return;
        eeprom_saveEnvSeasonMode(mode);
        sys.envSeasonMode = mode;
        return;
    }

    if (topic.endsWith("/auto_season")) {
        bool en = val.as<bool>();
        eeprom_saveEnvAutoSeason(en);
        sys.envAutoSeasonEnabled = en;
        return;
    }

    if (topic.endsWith("/lockout")) {
        int hr = val.as<int>();
        if (!validLockoutHours(hr)) return;
        eeprom_saveEnvLockoutHours(hr);
        sys.envModeLockoutSec = (uint32_t)hr * 3600UL;
        return;
    }

    if (topic.endsWith("/summer_start")) {
        int v = val.as<int>();
        if (!validSeasonStart(v)) return;
        sys.envSummerStartF = v;
        eeprom_saveEnvSeasonStarts();
        return;
    }

    if (topic.endsWith("/spf_start")) {
        int v = val.as<int>();
        if (!validSeasonStart(v)) return;
        sys.envSpringFallStartF = v;
        eeprom_saveEnvSeasonStarts();
        return;
    }

    if (topic.endsWith("/winter_start")) {
        int v = val.as<int>();
        if (!validSeasonStart(v)) return;
        sys.envWinterStartF = v;
        eeprom_saveEnvSeasonStarts();
        return;
    }

    if (topic.endsWith("/extreme_start")) {
        int v = val.as<int>();
        if (!validSeasonStart(v)) return;
        sys.envExtremeStartF = v;
        eeprom_saveEnvSeasonStarts();
        return;
    }

    if (topic.endsWith("/summer_buffer")) {
        int v = val.as<int>();
        if (!validSeasonHysteresis(v)) return;
        sys.envHystSummerF = v;
        eeprom_saveEnvSeasonHyst();
        return;
    }

    if (topic.endsWith("/spf_buffer")) {
        int v = val.as<int>();
        if (!validSeasonHysteresis(v)) return;
        sys.envHystSpringFallF = v;
        eeprom_saveEnvSeasonHyst();
        return;
    }

    if (topic.endsWith("/winter_buffer")) {
        int v = val.as<int>();
        if (!validSeasonHysteresis(v)) return;
        sys.envHystWinterF = v;
        eeprom_saveEnvSeasonHyst();
        return;
    }

    if (topic.endsWith("/extreme_buffer")) {
        int v = val.as<int>();
        if (!validSeasonHysteresis(v)) return;
        sys.envHystExtremeF = v;
        eeprom_saveEnvSeasonHyst();
        return;
    }

    if (topic.endsWith("/summer_setpoint")) {
        int v = val.as<int>();
        if (!validExhaustSetpoint(v)) return;
        sys.envSetpointSummerF = v;
        eeprom_saveEnvSeasonSetpoints();
        return;
    }

    if (topic.endsWith("/spf_setpoint")) {
        int v = val.as<int>();
        if (!validExhaustSetpoint(v)) return;
        sys.envSetpointSpringFallF = v;
        eeprom_saveEnvSeasonSetpoints();
        return;
    }

    if (topic.endsWith("/winter_setpoint")) {
        int v = val.as<int>();
        if (!validExhaustSetpoint(v)) return;
        sys.envSetpointWinterF = v;
        eeprom_saveEnvSeasonSetpoints();
        return;
    }

    if (topic.endsWith("/extreme_setpoint")) {
        int v = val.as<int>();
        if (!validExhaustSetpoint(v)) return;
        sys.envSetpointExtremeF = v;
        eeprom_saveEnvSeasonSetpoints();
        return;
    }

    // ---------------- PROBE ROLE ASSIGNMENT ----------------

    if (topic.endsWith("/probe_role")) {
        if (!doc.containsKey("role") || !doc.containsKey("phys")) return;

        int role = doc["role"].as<int>();
        int phys = doc["phys"].as<int>();

        if (role >= 0 && role < PROBE_ROLE_COUNT &&
            phys >= 0 && phys < MAX_WATER_PROBES) {

            sys.probeRoleMap[role] = phys;
            eeprom_saveProbeRoles();
        }
        return;
    }

    // ---------------- BOILER CONTROL ----------------

    if (topic.endsWith("/tank_low")) {
        int v = val.as<int>();
        if (!validTankSetpoint(v) || v >= sys.tankHighSetpointF) return;
        sys.tankLowSetpointF = v;
        eeprom_saveTankLow(v);
        return;
    }

    if (topic.endsWith("/tank_high")) {
        int v = val.as<int>();
        if (!validTankSetpoint(v) || v <= sys.tankLowSetpointF) return;
        sys.tankHighSetpointF = v;
        eeprom_saveTankHigh(v);
        return;
    }

    // ---------------- EMBER GUARDIAN OVERRIDE ----------------

    if (topic.endsWith("/ember_guardian_override")) {
        bool v = val.as<bool>();
        if (v) {
            sys.emberGuardianActive  = false;
            sys.emberGuardianStartMs = 0;

            if (sys.burnState == BURN_EMBER_GUARD) {
                sys.burnState       = BURN_HOLD;
                sys.holdTimerActive = true;
                sys.holdStartMs     = millis();
            }
        }
        return;
    }

    // ---------------- FACTORY RESET ----------------

    if (topic.endsWith("/factory_reset")) {
        if (doc.containsKey("confirm") && doc["confirm"] == true) {
            wifi_prov_factoryReset();
        }
        return;
    }
}
