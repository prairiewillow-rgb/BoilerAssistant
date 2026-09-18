/*
 * ============================================================
 *  Boiler Assistant – MQTT Client API (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: MQTT_Client.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Public interface for the WiFi/MQTT subsystem under the
 *    Total Domination Architecture (TDA). This module exposes
 *    the deterministic entry points used by the main loop to
 *    maintain MQTT connectivity and dispatch inbound commands.
 *
 *    Responsibilities:
 *      • mqtt_init() — initialize WiFi + MQTT client
 *      • mqtt_loop() — fully non‑blocking RX/TX handler
 *      • Auto‑reconnect logic (rate‑limited, deterministic)
 *      • Home Assistant Discovery support
 *      • Periodic telemetry publishers (state, settings, water, outdoor)
 *
 *    Architectural Notes:
 *      - All implementation resides in MQTTClient.cpp
 *      - No blocking calls allowed in mqtt_loop()
 *      - SystemData is the single source of truth
 *      - No burn logic, UI logic, or EEPROM logic belongs here
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

// Initialize WiFi + MQTT subsystem
void mqtt_init();

// Non‑blocking MQTT loop (called from main loop)
void mqtt_loop();

#endif

