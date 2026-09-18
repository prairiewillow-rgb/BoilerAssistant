/*
 * ============================================================
 *  Boiler Assistant – Environmental Logic (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: EnvironmentalLogic.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Public interface and shared types for the environmental
 *      / seasonal logic subsystem.
 *
 *      v3.1 extends the active environment state to drive:
 *          • Exhaust setpoint (per season)
 *          • Max fan clamp (per season)
 *          • Tank HIGH / LOW water setpoints (per season)
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#ifndef ENVIRONMENTALLOGIC_H
#define ENVIRONMENTALLOGIC_H

#include <Arduino.h>
#include "SystemState.h"   // EnvSeason now defined here

/* ============================================================
 *  COMPATIBILITY SHIM (v2.2 → v3.0)
 * ============================================================ */
#ifndef ENV_SEASON_NONE
#define ENV_SEASON_NONE ((EnvSeason)255)
#endif

/* ============================================================
 *  ACTIVE ENVIRONMENT STATE
 *  ------------------------------------------------------------
 *  NOTE:
 *      v3.0 stores all active environmental state inside
 *      SystemData sys:
 *
 *          // Season selection
 *          sys.envActiveSeason
 *
 *          // Exhaust control
 *          sys.envActiveSetpointF
 *          sys.envActiveClampPercent   // seasonal max clamp
 *
 *          // Water control (NEW in v3.0)
 *          sys.envActiveTankHighF      // seasonal tank HIGH setpoint
 *          sys.envActiveTankLowF       // seasonal tank LOW setpoint
 *
 *          // Optional future use (kept for compatibility)
 *          sys.envActiveRampProfile
 *          sys.envFanBiasPercent       // kept but typically 0 for OWB
 *
 *      Therefore, no extern variables are declared here.
 * ============================================================ */

/* ============================================================
 *  API
 * ============================================================ */

/**
 * Initialize environmental / seasonal logic.
 * Must be called once at startup, after SystemData is loaded.
 */
void env_logic_init();

/**
 * Periodic environmental update.
 *
 * nowMs:
 *      Current millis() timestamp, used for any future
 *      time‑based lockouts or hysteresis. v3.0 may ignore
 *      it internally but the parameter is kept for ABI
 *      stability and future expansion.
 */
void env_logic_update(unsigned long nowMs);

#endif // ENVIRONMENTALLOGIC_H
