/*
 * ============================================================
 *  Boiler Assistant – System State Module (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: SystemState.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Defines deterministic startup and reset behavior for the
 *    Boiler Assistant controller. This module owns the high‑level
 *    burn engine state, safety state, and tank setpoint defaults
 *    used during initialization and subsystem resets.
 *
 *    Responsibilities:
 *      • systemstate_init()  — establish safe, predictable boot state
 *      • systemstate_reset() — restore burn engine to a known state
 *
 *    Architectural Notes:
 *      - SystemState never touches EEPROM or UI logic
 *      - SystemData (sys.*) is the single source of truth
 *      - Startup always enters BURN_RAMP for deterministic ignition
 *      - Safety always starts cleared (SAFETY_OK)
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#include "SystemState.h"
#include "SystemData.h"
#include <Arduino.h>

/* ============================================================
 *  GLOBAL SYSTEM DATA INSTANCE
 * ============================================================ */
extern SystemData sys;

/* ============================================================
 *  INITIALIZATION (v3)
 * ============================================================ */

void systemstate_init() {

    // Deterministic startup state
    sys.burnState = BURN_RAMP;

    // v3: Safety always starts clear
    sys.safetyState = SAFETY_OK;

    // v3: Default tank setpoints (safe conservative values)
    sys.tankLowSetpointF  = 140;   // typical low threshold
    sys.tankHighSetpointF = 180;   // typical high threshold
}

/* ============================================================
 *  RESET (used by BurnEngine or UI)
 * ============================================================ */

void systemstate_reset() {

    // Reset to a known safe state
    sys.burnState = BURN_RAMP;

    // v3: Reset safety lockout
    sys.safetyState = SAFETY_OK;

    // v3: Do NOT reset runMode or tank setpoints
    //     (these are operator preferences stored in EEPROM)
}
