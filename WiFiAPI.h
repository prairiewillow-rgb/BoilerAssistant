/*
 * ============================================================
 *  Boiler Assistant – WiFi JSON API (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: WiFiAPI.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Public interface for the WiFi + HTTP JSON API subsystem.
 *    This module exposes deterministic entry points for:
 *
 *      • wifiapi_init() — initialize WiFi hardware + HTTP server
 *      • wifiapi_loop() — non‑blocking retry + request handler
 *
 *    Responsibilities:
 *      - Maintain non‑blocking WiFi auto‑retry logic
 *      - Serve lightweight JSON endpoints for:
 *          • Live telemetry
 *          • Settings
 *          • Network diagnostics
 *      - Integrate cleanly with MQTT and LoRa without blocking
 *
 *    Architectural Notes:
 *      - All implementation resides in WiFiAPI.cpp
 *      - Provisioning-aware: disabled in AP mode
 *      - SystemData is the single source of truth
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#pragma once

// Initialize WiFi + HTTP JSON API (non‑blocking)
void wifiapi_init();

// Run WiFi retry + HTTP server loop (non‑blocking)
void wifiapi_loop();

