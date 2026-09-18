/*
 * ============================================================
 *  Boiler Assistant – Fan Control Module (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: FanControl.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Deterministic fan control logic for all burn states. This module
 *    implements the Total Domination Architecture (TDA) for fan output,
 *    ensuring stable, predictable behavior across BOOST, RAMP, HOLD,
 *    IDLE, and SAFETY transitions.
 *
 *    Responsibilities:
 *      • Clamp Mode (fan always on within min/max limits)
 *      • Fan‑off Mode with hysteresis and re‑enable thresholds
 *      • BOOST and SAFETY overrides
 *      • State‑transition smoothing between RAMP/HOLD
 *      • Full SystemData migration (no legacy globals)
 *
 *  Architectural Notes:
 *      - FanControl owns all fan smoothing and hysteresis logic.
 *      - SystemData (sys.*) is the single source of truth.
 *      - This module never touches UI, EEPROM, or WiFi logic.
 *      - Output is always deterministic and operator‑visible.
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#include "FanControl.h"
#include "SystemState.h"
#include "SystemData.h"
#include <Arduino.h>

 *  INTERNAL MEMORY
 * ============================================================ */
static int       lastFan       = 0;
static bool      fanOn         = false;
static BurnState prevBurnState = BURN_IDLE;

// Ramp limiter memory
static int lastOutput = 0;
static unsigned long fanKickUntil = 0;
static const unsigned long FAN_START_KICK_MS = 750UL;

/* ============================================================
 *  INIT
 * ============================================================ */
void fancontrol_init() {
    lastFan       = 0;
    fanOn         = false;
    prevBurnState = sys.burnState;
    lastOutput    = 0;
    fanKickUntil  = 0;
}

/* ============================================================
 *  HANDLE STATE TRANSITIONS
 * ============================================================ */
static void fancontrol_handleStateChange() {
    if (sys.burnState != prevBurnState) {

        // Reset smoothing when leaving HOLD
        if (prevBurnState == BURN_HOLD && sys.burnState == BURN_RAMP) {
            lastFan = sys.clampMaxPercent;
            fanOn   = true;
        }

        // Reset on BOOST, IDLE, SAFETY
        if (sys.burnState == BURN_BOOST ||
            sys.burnState == BURN_IDLE) {

            lastFan = 0;
            fanOn   = false;
        }

        prevBurnState = sys.burnState;
    }
}

/* ============================================================
 *  MAIN FAN COMPUTE FUNCTION
 * ============================================================ */
int fan_compute(int demand) {

    fancontrol_handleStateChange();

    if (sys.exhaustFallbackActive &&
        (sys.burnState == BURN_BOOST ||
         sys.burnState == BURN_RAMP ||
         sys.burnState == BURN_HOLD)) {
        fanOn = true;
        lastOutput = 100;
        fanKickUntil = 0;
        return 100;
    }

    if (sys.safetyState != SAFETY_OK) {
        fanOn = false;
        lastOutput = 0;
        fanKickUntil = 0;
        return 0;
    }

    if (sys.emberGuardianLatched || sys.burnState == BURN_EMBER_GUARD) {
        fanOn = false;
        lastOutput = 0;
        fanKickUntil = 0;
        return 0;
    }

    // BOOST override
    if (sys.burnState == BURN_BOOST) {
        fanOn = true;
        return 100;
    }

    // ============================================================
    // MODE 1: Clamp Mode (fan always on)
    // ============================================================
    if (sys.deadzoneFanMode == 1) {
        fanOn = true;

        int fan = demand;
        if (fan < sys.clampMinPercent) fan = sys.clampMinPercent;
        if (fan > sys.clampMaxPercent) fan = sys.clampMaxPercent;

        if (lastOutput == 0) {
            if (fanKickUntil == 0) fanKickUntil = millis() + FAN_START_KICK_MS;
            if (millis() < fanKickUntil) return 100;
            fanKickUntil = 0;
        }

        // Ramp limiter
        int delta = fan - lastOutput;
        if (delta > 3)  delta = 3;
        if (delta < -3) delta = -3;

        fan = lastOutput + delta;
        lastOutput = fan;

        return fan;
    }

    // ============================================================
    // MODE 0: Fan-Off Mode (your exact rule)
    // ============================================================

    // Fan OFF when demand < clampMinPercent
    if (demand < sys.clampMinPercent) {
        fanOn = false;
        lastOutput = 0;
        fanKickUntil = 0;
        return 0;
    }

    // Fan ON when demand >= clampMinPercent + 10
    if (demand >= (sys.clampMinPercent + 10)) {
        fanOn = true;
    }

    // Output
    if (!fanOn) {
        lastOutput = 0;
        fanKickUntil = 0;
        return 0;
    }

    int fan = demand;
    if (fan < sys.clampMinPercent) fan = sys.clampMinPercent;
    if (fan > sys.clampMaxPercent) fan = sys.clampMaxPercent;

    if (lastOutput == 0) {
        if (fanKickUntil == 0) fanKickUntil = millis() + FAN_START_KICK_MS;
        if (millis() < fanKickUntil) return 100;
        fanKickUntil = 0;
    }

    // ============================================================
    // Output Ramp Limiter (smooth fan transitions)
    // ============================================================
    int delta = fan - lastOutput;
    if (delta > 3)  delta = 3;
    if (delta < -3) delta = -3;

    fan = lastOutput + delta;
    lastOutput = fan;

    return fan;
}

/* ============================================================
 *  WRAPPER
 * ============================================================ */
int fancontrol_apply(int demand) {
    return fan_compute(demand);
}
