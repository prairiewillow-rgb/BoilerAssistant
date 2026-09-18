/*
 * ============================================================
 *  Boiler Assistant – WiFi Provisioning API (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: WiFiProvisioning.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Public interface for the WiFi provisioning subsystem.
 *    This module exposes deterministic entry points for:
 *
 *      • wifi_prov_init()  — STA‑first initialization with AP fallback
 *      • wifi_prov_loop()  — AP‑mode HTML portal handler
 *      • wifi_prov_isAPMode() — query active provisioning mode
 *      • wifi_prov_has_credentials() — query stored credentials
 *      • wifi_prov_factoryReset() — full credential wipe + reboot
 *
 *    Architectural Notes:
 *      - All implementation resides in WiFiProvisioning.cpp
 *      - Provisioning is authoritative when STA fails or no creds exist
 *      - SystemData is the single source of truth for WiFi status
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <Arduino.h>

void wifi_prov_init();
void wifi_prov_loop();
bool wifi_prov_isAPMode();
bool wifi_prov_has_credentials();

/* ============================================================
 *  Factory Reset API
 * ============================================================ */
void wifi_prov_factoryReset();

#endif
