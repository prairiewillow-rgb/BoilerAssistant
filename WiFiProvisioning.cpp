/*
 * ============================================================
 *  Boiler Assistant – WiFi Provisioning Module (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: WiFiProvisioning.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Deterministic WiFi provisioning subsystem for the Boiler
 *    Assistant controller. Implements the Total Domination
 *    Architecture (TDA) for all credential handling and AP-mode
 *    onboarding.
 *
 *    Responsibilities:
 *      • STA‑first connection using RuntimeCredentials
 *      • Automatic AP fallback with HTML provisioning portal
 *      • Safe credential parsing + EEPROM persistence
 *      • Factory reset with full credential wipe
 *      • Export MQTT credentials for MQTTClient.cpp
 *
 *    Architectural Notes:
 *      - No blocking delays beyond required WiFi operations
 *      - No dynamic allocation except small String buffers
 *      - SystemData is the single source of truth for WiFi status
 *      - AP mode is authoritative when STA fails or no creds exist
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#include "WiFiProvisioning.h"
#include "RuntimeCredentials.h"
#include "SystemData.h"
#include "EEPROMStorage.h"   // <-- required for persistence

#include <Arduino.h>
#include <WiFiS3.h>
#include <WiFiServer.h>

extern SystemData sys;

/* ============================================================
 *  MQTT GLOBALS (used by MQTTClient.cpp)
 * ============================================================ */

const char* prov_mqtt_server = nullptr;
const char* prov_mqtt_user   = nullptr;
const char* prov_mqtt_pass   = nullptr;

/* ============================================================
 *  INTERNAL STATE
 * ============================================================ */

static WiFiServer provServer(80);
static bool apMode   = false;
static bool newCreds = false;

/* Simple HTML portal */
static const char* PROV_HTML =
"<!DOCTYPE html><html><body>"
"<h2>Boiler Assistant WiFi Setup</h2>"
"<form method='POST'>"
"Your name:<br><input name='displayName' maxlength='31'><br><br>"
"WiFi SSID:<br><input name='ssid'><br>"
"WiFi Password:<br><input name='pass' type='password'><br><br>"
"MQTT Server:<br><input name='mqttServer'><br>"
"MQTT User:<br><input name='mqttUser'><br>"
"MQTT Password:<br><input name='mqttPass' type='password'><br><br>"
"Control/API Password:<br><input name='controlPass' type='password'><br><br>"
"<input type='submit' value='Save'>"
"</form></body></html>";

/* ============================================================
 *  FACTORY RESET
 * ============================================================ */

void wifi_prov_factoryReset() {
    Serial.println("WiFiProvisioning: FACTORY RESET");

    memset(&runtimeCreds, 0, sizeof(runtimeCreds));
    runtimeCreds.hasCredentials = false;

    sys.wifiOK = false;

    // Persist the cleared credentials
    eeprom_saveRuntimeCreds();

    Serial.println("WiFiProvisioning: rebooting after factory reset...");
    delay(1000);
    NVIC_SystemReset();
}

/* ============================================================
 *  AP MODE
 * ============================================================ */

static void startAP() {
    Serial.println("WiFiProvisioning: Starting AP mode...");

    WiFi.end();
    WiFi.disconnect();
    delay(200);

    WiFi.config(
        IPAddress(192,168,4,1),
        IPAddress(0,0,0,0),
        IPAddress(255,255,255,0)
    );

    int result = WiFi.beginAP("BoilerAssistant-Setup");

    Serial.print("WiFiProvisioning: AP start result = ");
    Serial.println(result);

    apMode = true;
    sys.wifiOK = false;

    provServer.begin();
}

/* ============================================================
 *  INIT: STA-first, AP-fallback
 * ============================================================ */

void wifi_prov_init() {
    Serial.println("WiFiProvisioning: init (STA-first, AP-fallback)");

    sys.wifiOK = false;
    WiFi.setHostname("boilerassistant");

    if (runtimeCreds.hasCredentials && runtimeCreds.ssid[0] != 0) {
        Serial.print("WiFiProvisioning: Using runtime SSID: ");
        Serial.println(runtimeCreds.ssid);

        WiFi.end();
        WiFi.disconnect();
        delay(200);

        WiFi.begin(runtimeCreds.ssid, runtimeCreds.pass);

        unsigned long start = millis();
        while (millis() - start < 8000) {
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("WiFiProvisioning: STA connected via runtime creds");
                Serial.print("WiFiProvisioning: IP: ");
                Serial.println(WiFi.localIP());
                Serial.println("WiFiProvisioning: Dashboard: http://boilerassistant/");

                apMode     = false;
                sys.wifiOK = true;

                prov_mqtt_server = runtimeCreds.mqttServer;
                prov_mqtt_user   = runtimeCreds.mqttUser;
                prov_mqtt_pass   = runtimeCreds.mqttPass;

                return;
            }
            delay(200);
        }

        Serial.println("WiFiProvisioning: Runtime STA failed → AP mode");
        startAP();
        return;
    }

    Serial.println("WiFiProvisioning: No runtime credentials → AP mode");
    startAP();
}

/* ============================================================
 *  STATUS QUERIES
 * ============================================================ */

bool wifi_prov_has_credentials() {
    return runtimeCreds.hasCredentials;
}

bool wifi_prov_isAPMode() {
    return apMode;
}

/* ============================================================
 *  FORM PARSER
 * ============================================================ */

static void parseForm(const String& body) {
    auto getVal = [&](const String& key) {
        int p = body.indexOf(key + "=");
        if (p < 0) return String("");
        int s = p + key.length() + 1;
        int e = body.indexOf('&', s);
        if (e < 0) e = body.length();
        String v = body.substring(s, e);
        v.replace('+', ' ');
        return v;
    };

    String ssid       = getVal("ssid");
    String pass       = getVal("pass");
    String mqttServer = getVal("mqttServer");
    String mqttUser   = getVal("mqttUser");
    String mqttPass   = getVal("mqttPass");
    String controlPass = getVal("controlPass");
    String displayName = getVal("displayName");

    if (ssid.length() == 0 || pass.length() == 0) {
        Serial.println("WiFiProvisioning: missing SSID or password");
        return;
    }

    // Safe copies with guaranteed null-termination
    strncpy(runtimeCreds.ssid, ssid.c_str(), sizeof(runtimeCreds.ssid) - 1);
    runtimeCreds.ssid[sizeof(runtimeCreds.ssid) - 1] = '\0';

    strncpy(runtimeCreds.pass, pass.c_str(), sizeof(runtimeCreds.pass) - 1);
    runtimeCreds.pass[sizeof(runtimeCreds.pass) - 1] = '\0';

    strncpy(runtimeCreds.mqttServer, mqttServer.c_str(), sizeof(runtimeCreds.mqttServer) - 1);
    runtimeCreds.mqttServer[sizeof(runtimeCreds.mqttServer) - 1] = '\0';

    strncpy(runtimeCreds.mqttUser, mqttUser.c_str(), sizeof(runtimeCreds.mqttUser) - 1);
    runtimeCreds.mqttUser[sizeof(runtimeCreds.mqttUser) - 1] = '\0';

    strncpy(runtimeCreds.mqttPass, mqttPass.c_str(), sizeof(runtimeCreds.mqttPass) - 1);
    runtimeCreds.mqttPass[sizeof(runtimeCreds.mqttPass) - 1] = '\0';

    strncpy(runtimeCreds.controlPass, controlPass.c_str(), sizeof(runtimeCreds.controlPass) - 1);
    runtimeCreds.controlPass[sizeof(runtimeCreds.controlPass) - 1] = '\0';

    displayName.trim();
    displayName.toUpperCase();
    if (displayName.length() > 20) displayName.remove(20);
    if (displayName.length() >= sizeof(runtimeCreds.displayName)) {
        displayName.remove(sizeof(runtimeCreds.displayName) - 1);
    }
    strncpy(runtimeCreds.displayName, displayName.c_str(),
            sizeof(runtimeCreds.displayName) - 1);
    runtimeCreds.displayName[sizeof(runtimeCreds.displayName) - 1] = '\0';

    runtimeCreds.hasCredentials = true;
    newCreds = true;

    prov_mqtt_server = runtimeCreds.mqttServer;
    prov_mqtt_user   = runtimeCreds.mqttUser;
    prov_mqtt_pass   = runtimeCreds.mqttPass;

    // Persist credentials
    eeprom_saveRuntimeCreds();

    Serial.println("WiFiProvisioning: New credentials received and saved to EEPROM");
}

/* ============================================================
 *  LOOP: AP Portal
 * ============================================================ */

void wifi_prov_loop() {
    if (!apMode) return;

    WiFiClient client = provServer.available();
    if (!client) return;

    client.setTimeout(25);
    String req;
    while (client.available()) {
        req += (char)client.read();
    }

    if (req.length() == 0) return;

    Serial.println("WiFiProvisioning: RAW REQUEST BEGIN");
    Serial.println(req);
    Serial.println("WiFiProvisioning: RAW REQUEST END");

    if (req.startsWith("POST ")) {
        int bodyPos = req.indexOf("\r\n\r\n");
        if (bodyPos > 0) {
            String body = req.substring(bodyPos + 4);
            parseForm(body);
        }

        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.println();
        client.println("<html><body><h3>Saved. Rebooting...</h3></body></html>");
        client.stop();

        Serial.println("WiFiProvisioning: RESETTING NOW");
        delay(500);
        NVIC_SystemReset();
        return;
    }

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println();
    client.print(PROV_HTML);
    client.stop();
}
