/*
 * ============================================================
 *  Boiler Assistant – Burn Engine Public API (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: BurnEngine.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Public interface for the Burn Engine subsystem. This header
 *    exposes the deterministic control entry points used by the
 *    main loop, UI, and any operator‑triggered actions.
 *
 *    The Burn Engine implements two internal logic paths:
 *
 *      • AUTO TANK ENGINE
 *          - Tank‑driven start/stop
 *          - Automatically enters IDLE when tank reaches high setpoint
 *
 *      • CONTINUOUS ENGINE
 *          - Ignores tank temperature entirely
 *          - Never auto‑stops
 *          - Never auto‑enters IDLE
 *
 *    burnengine_compute() automatically dispatches to the correct
 *    engine based on sys.controlMode, following the Total Domination
 *    Architecture (TDA) contract.
 *
 *  Architectural Notes:
 *      - This header exposes only the public API; all internal logic
 *        resides in BurnEngine.cpp.
 *      - SystemData is the single source of truth for all parameters.
 *      - BOOST, RAMP, HOLD, IDLE, and EMBER GUARD transitions are
 *        fully owned by the Burn Engine module.
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#ifndef BURNENGINE_H
#define BURNENGINE_H

#include <Arduino.h>
#include "SystemState.h"
#include "SystemData.h"

// Initialize burn engine state
void burnengine_init();

// Force a BOOST start (used by UI or AUTO TANK logic)
void burnengine_startBoost();

// Clear the pending sensor-fault confirmation timers after operator reset
void burnengine_resetSensorFault();
void burnengine_resetAlarms();

// Current learned multiplier used by adaptive fan control
float burnengine_getAdaptiveSlope();

// Main compute function (dispatcher)
int burnengine_compute();

#endif

