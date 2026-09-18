/*
 * ============================================================
 *  Boiler Assistant – Fan Control API (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: FanControl.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Public interface for deterministic fan control under the
 *    Total Domination Architecture (TDA). This module exposes
 *    the operator‑facing entry points used by the burn engine
 *    and main loop, while all internal logic resides in the
 *    FanControl.cpp implementation.
 *
 *    Responsibilities:
 *      • Adaptive fan control for RAMP and HOLD states
 *      • BOOST override (100% output)
 *      • IDLE fan‑off behavior
 *      • Deadzone fan modes:
 *            0 = fan allowed to turn OFF
 *            1 = fan always ON
 *      • Guardian hard‑kill (PWM = 0)
 *      • Deterministic smoothing of fan transitions
 *
 *  Architectural Notes:
 *      - This header exposes only the public API.
 *      - fan_compute() and all smoothing logic remain private.
 *      - SystemData (sys.*) is the single source of truth.
 *      - No UI, EEPROM, or WiFi logic belongs here.
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#ifndef FANCONTROL_H
#define FANCONTROL_H

#include <Arduino.h>
#include "SystemState.h"
#include "SystemData.h"

// Initialize fan control system
void fancontrol_init();

// Apply fan demand and return final fan percent
// (Handles Guardian hard-kill internally)
int fancontrol_apply(int demand);

#endif
