/*
 * ============================================================
 *  Boiler Assistant – System State API (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: SystemState.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Defines all global enumerations and constants used by the
 *    Boiler Assistant controller. This header provides the
 *    authoritative definitions for:
 *
 *      • Burn engine states
 *      • Safety states
 *      • Run modes
 *      • Environmental seasons
 *      • UI state machine
 *      • Probe roles
 *
 *    Architectural Notes:
 *      - No logic belongs here — only enums and constants.
 *      - SystemData and SystemState.cpp implement behavior.
 *      - All modules must treat these enums as canonical.
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#ifndef SYSTEMSTATE_H
#define SYSTEMSTATE_H

/* ============================================================
 *  GLOBAL CONSTANTS
 * ============================================================ */

#ifndef MAX_WATER_PROBES
#define MAX_WATER_PROBES 8
#endif

#ifndef PROBE_ROLE_COUNT
#define PROBE_ROLE_COUNT 8
#endif

#ifndef PROBE_NAME_LENGTH
#define PROBE_NAME_LENGTH 16
#endif

/* ============================================================
 *  PROBE ROLE ENUM
 * ============================================================ */
typedef enum {
    PROBE_TANK       = 0,
    PROBE_RETURN     = 1,
    PROBE_SUPPLY     = 2,
    PROBE_OUTDOOR    = 3,
    PROBE_TANK_BOTTOM = 4,
    PROBE_TANK_TOP    = 5,
    PROBE_EXTRA       = 6,
    PROBE_SPARE       = 7
} ProbeRole;

/* ============================================================
 *  BURN ENGINE STATES
 * ============================================================ */
typedef enum {
    BURN_IDLE = 0,
    BURN_RAMP,
    BURN_HOLD,
    BURN_BOOST,
    BURN_EMBER_GUARD
} BurnState;

/* ============================================================
 *  RUN MODE
 * ============================================================ */
typedef enum {
    RUNMODE_CONTINUOUS = 0,
    RUNMODE_AUTO_TANK  = 1
} RunMode;

/* ============================================================
 *  SAFETY STATE
 * ============================================================ */
typedef enum {
    SAFETY_OK = 0,
    SAFETY_HIGHTEMP = 1,
    SAFETY_SENSOR_FAULT = 2
} SafetyState;

#define SENSOR_FAULT_EXHAUST 0x01
#define SENSOR_FAULT_TANK    0x02

/* ============================================================
 *  ENVIRONMENTAL SEASON
 * ============================================================ */
typedef enum {
    ENV_SEASON_SUMMER      = 0,
    ENV_SEASON_SPRING_FALL = 1,
    ENV_SEASON_WINTER      = 2,
    ENV_SEASON_EXTREME     = 3,
    ENV_SEASON_NONE        = 255
} EnvSeason;

/* ============================================================
 *  UI STATE MACHINE
 * ============================================================ */

typedef enum {

    UI_HOME = 0,

    /* A: Combustion */
    UI_COMBUSTION_MENU,
    UI_SETPOINT,
    UI_CLAMP_DEADBAND_MENU,
    UI_CLAMP_MIN,
    UI_CLAMP_MAX,
    UI_DEADBAND,
    UI_EMBER_GUARD_MENU,
    UI_EMBER_GUARD_TIMER,
    UI_FLUE_LOW,
    UI_FLUE_REC,
    UI_BOOST_TIME,
    UI_DEADZONE_FAN,

    /* B: Boiler Control */
    UI_BOILER_MENU,
    UI_RUNMODE,
    UI_TANK_LOW,
    UI_TANK_HIGH,
    UI_SAFETY_STATUS,
    UI_RUNMODE_CONFIRM_CONTINUOUS,

    /* C: Environment */
    UI_ENV_MENU,
    UI_SEASONS_MENU,
    UI_SEASON_DETAIL_MENU,
    UI_SEASON_DETAIL_MENU_2,

    /* Season edit screens */
    UI_SEASON_EDIT_START,
    UI_SEASON_EDIT_BUFFER,
    UI_SEASON_EDIT_SETPOINT,
    UI_SEASON_EDIT_TANKHIGH,
    UI_SEASON_EDIT_TANKLOW,
    UI_SEASON_EDIT_CLAMPMAX,

    /* Environment mode / lockout */
    UI_ENV_LOCKOUT,
    UI_ENV_MODE,
    UI_ENV_AUTOSEASON,
    UI_ENV_LOCKOUT_HOURS,

    /* D: Sensors & Network */
    UI_SENSORS_MENU,
    UI_WATER_PROBE_MENU,
    UI_BME_SCREEN,

    UI_NETWORKING,
    UI_NETWORK_INFO,
    UI_NET_FACTORY_RESET_CONFIRM_1,
    UI_NET_FACTORY_RESET_CONFIRM_2,

    UI_STATE_COUNT

} UIState;

#endif // SYSTEMSTATE_H

