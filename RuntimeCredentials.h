/*
 * ============================================================
 *  Boiler Assistant – Runtime Credentials API (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: RuntimeCredentials.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Defines the RuntimeCredentials structure used by the WiFi and
 *    MQTT provisioning subsystem. This header exposes the POD struct
 *    and the global instance used by:
 *
 *      • WiFiProvisioning
 *      • EEPROMStorage
 *      • MQTTClient
 *
 *    Architectural Notes:
 *      - This struct is stored directly in EEPROM.
 *      - No logic belongs here — only the definition and extern.
 *      - All fields are fixed‑size arrays for deterministic storage.
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#pragma once

struct RuntimeCredentials {
    bool hasCredentials = false;

    char ssid[32]       = {0};
    char pass[64]       = {0};

    char mqttServer[64] = {0};
    char mqttUser[32]   = {0};
    char mqttPass[64]   = {0};

    // Control/API password. Kept at the same struct position for EEPROM compatibility.
    char controlPass[32] = {0};

    char displayName[32] = {0};
};

extern RuntimeCredentials runtimeCreds;
