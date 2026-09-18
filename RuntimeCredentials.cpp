/*
 * ============================================================
 *  Boiler Assistant – Runtime Credentials Module (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: RuntimeCredentials.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Defines the global RuntimeCredentials structure used by the
 *    WiFi/MQTT provisioning subsystem. This module contains no logic;
 *    it simply instantiates the global credentials object so that
 *    EEPROMStorage, WiFiProvisioning, and MQTTClient can access it.
 *
 *    Architectural Notes:
 *      - RuntimeCredentials is a POD struct stored in EEPROM.
 *      - This file provides the single global instance.
 *      - No initialization or logic belongs here.
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#include "RuntimeCredentials.h"

RuntimeCredentials runtimeCreds;
