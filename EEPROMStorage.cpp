/*
 * ============================================================
 *  Boiler Assistant – EEPROM Storage Module (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: EEPROMStorage.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    EEPROM-backed configuration storage for the Boiler Assistant
 *    controller. This module owns all persistent settings for:
 *
 *      • Combustion parameters (setpoint, deadband, clamps)
 *      • Ember Guardian thresholds and timer
 *      • Environmental logic (season starts, hysteresis, setpoints)
 *      • Boiler control (tank low/high, run mode)
 *      • Probe role mapping
 *      • Runtime WiFi credentials
 *
 *    Implements deterministic read/write helpers for multibyte
 *    values and enforces strict safety clamps to prevent invalid
 *    EEPROM data from destabilizing the burn engine.
 *
 *  Architectural Notes:
 *      - SystemData is the single source of truth for all fields.
 *      - EEPROM layout is fixed and version-stable.
 *      - All multibyte values use explicit little-endian encoding.
 *      - This module contains no UI or control logic.
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#include "EEPROMStorage.h"
#include "SystemData.h"
#include "RuntimeCredentials.h"
#include <EEPROM.h>

extern SystemData sys;
extern RuntimeCredentials runtimeCreds;

static const int EEPROM_CONFIG_END = 87;
static const int EEPROM_MAGIC_ADDR = 88;
static const int EEPROM_VERSION_ADDR = 89;
static const int EEPROM_CRC_ADDR = 90;
static const int EEPROM_PROBE_NAMES_ADDR = 400;
static const uint8_t EEPROM_MAGIC = 0xBA;
static const uint8_t EEPROM_VERSION = 3;

static void eeprom_markConfigValid();

/* ============================================================
 *  INTERNAL HELPERS FOR MULTIBYTE VALUES
 * ============================================================ */

static void eeprom_write16(int addr, int16_t value) {
    EEPROM.write(addr,     (uint8_t)(value & 0xFF));
    EEPROM.write(addr + 1, (uint8_t)((value >> 8) & 0xFF));
    eeprom_markConfigValid();
}

static int16_t eeprom_read16(int addr) {
    uint8_t lo = EEPROM.read(addr);
    uint8_t hi = EEPROM.read(addr + 1);
    return (int16_t)((hi << 8) | lo);
}

static uint16_t eeprom_configCrc() {
    uint16_t crc = 0xFFFF;
    for (int addr = 0; addr <= EEPROM_CONFIG_END; addr++) {
        crc ^= EEPROM.read(addr);
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
        }
    }
    return crc;
}

static bool eeprom_configValid() {
    uint16_t stored = EEPROM.read(EEPROM_CRC_ADDR) |
                      ((uint16_t)EEPROM.read(EEPROM_CRC_ADDR + 1) << 8);
    return EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC &&
           EEPROM.read(EEPROM_VERSION_ADDR) == EEPROM_VERSION &&
           stored == eeprom_configCrc();
}

static void eeprom_markConfigValid() {
    uint16_t crc = eeprom_configCrc();
    EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
    EEPROM.update(EEPROM_VERSION_ADDR, EEPROM_VERSION);
    EEPROM.update(EEPROM_CRC_ADDR, crc & 0xFF);
    EEPROM.update(EEPROM_CRC_ADDR + 1, crc >> 8);
}

static void eeprom_saveDefaultConfig() {
    eeprom_write16(0, sys.exhaustSetpoint);
    eeprom_write16(2, sys.boostTimeSeconds);
    eeprom_write16(4, sys.deadbandF);
    eeprom_write16(6, sys.clampMinPercent);
    eeprom_write16(8, sys.clampMaxPercent);
    EEPROM.update(10, sys.deadzoneFanMode);
    eeprom_write16(12, sys.emberGuardianTimerMinutes);
    eeprom_write16(14, sys.flueLowThreshold);
    eeprom_write16(16, sys.flueRecoveryThreshold);
    EEPROM.update(18, sys.envSeasonMode);
    EEPROM.update(19, sys.envAutoSeasonEnabled ? 1 : 0);
    EEPROM.update(20, sys.envModeLockoutSec / 3600UL);
    eeprom_saveEnvSeasonStarts();
    eeprom_saveEnvSeasonHyst();
    eeprom_saveEnvSeasonSetpoints();
    eeprom_write16(46, sys.tankLowSetpointF);
    eeprom_write16(48, sys.tankHighSetpointF);
    EEPROM.update(50, sys.controlMode);
    EEPROM.put(52, 1.0f);
    eeprom_saveProbeRoles();
    eeprom_saveEnvSeasonTankValues();
    eeprom_saveEnvSeasonClampValues();
    eeprom_markConfigValid();
}

/* ============================================================
 *  INIT — LOAD ALL SETTINGS YOU SAVE
 * ============================================================ */

void eeprom_init() {
    // === COMBUSTION SETTINGS ===
    sys.exhaustSetpoint      = eeprom_read16(0);
    sys.boostTimeSeconds     = eeprom_read16(2);
    sys.deadbandF            = eeprom_read16(4);
    sys.clampMinPercent      = eeprom_read16(6);
    sys.clampMaxPercent      = eeprom_read16(8);
    sys.deadzoneFanMode      = EEPROM.read(10);

    // === EMBER GUARDIAN ===
    sys.emberGuardianTimerMinutes = eeprom_read16(12);
    sys.flueLowThreshold          = eeprom_read16(14);
    sys.flueRecoveryThreshold     = eeprom_read16(16);

    // === ENVIRONMENTAL LOGIC (ONLY FIELDS YOU ACTUALLY SAVE) ===
    sys.envSummerStartF      = eeprom_read16(22);
    sys.envSpringFallStartF  = eeprom_read16(24);
    sys.envWinterStartF      = eeprom_read16(26);
    sys.envExtremeStartF     = eeprom_read16(28);

    sys.envHystSummerF       = eeprom_read16(30);
    sys.envHystSpringFallF   = eeprom_read16(32);
    sys.envHystWinterF       = eeprom_read16(34);
    sys.envHystExtremeF      = eeprom_read16(36);

    sys.envSetpointSummerF     = eeprom_read16(38);
    sys.envSetpointSpringFallF = eeprom_read16(40);
    sys.envSetpointWinterF     = eeprom_read16(42);
    sys.envSetpointExtremeF    = eeprom_read16(44);

    sys.envSeasonMode        = EEPROM.read(18);
    sys.envAutoSeasonEnabled = EEPROM.read(19) != 0;
    sys.envModeLockoutSec    = (uint32_t)EEPROM.read(20) * 3600UL;

    sys.envTankHighSummerF     = eeprom_read16(68);
    sys.envTankHighSpringFallF = eeprom_read16(70);
    sys.envTankHighWinterF     = eeprom_read16(72);
    sys.envTankHighExtremeF    = eeprom_read16(74);
    sys.envTankLowSummerF      = eeprom_read16(76);
    sys.envTankLowSpringFallF  = eeprom_read16(78);
    sys.envTankLowWinterF      = eeprom_read16(80);
    sys.envTankLowExtremeF     = eeprom_read16(82);
    sys.envClampMaxSummerPercent     = EEPROM.read(84);
    sys.envClampMaxSpringFallPercent = EEPROM.read(85);
    sys.envClampMaxWinterPercent     = EEPROM.read(86);
    sys.envClampMaxExtremePercent    = EEPROM.read(87);

    // === BOILER CONTROL ===
    sys.tankLowSetpointF     = eeprom_read16(46);
    sys.tankHighSetpointF    = eeprom_read16(48);
    sys.controlMode          = (RunMode)EEPROM.read(50);

    // === PROBE ROLES ===
    // Default: Tank probe = physical probe 0
    for (int i = 0; i < PROBE_ROLE_COUNT; i++) {
        sys.probeRoleMap[i] = EEPROM.read(60 + i);
        if (sys.probeRoleMap[i] >= MAX_WATER_PROBES) {
            sys.probeRoleMap[i] = 0;
        }
    }
    eeprom_loadProbeNames();
    // === RUNTIME CREDENTIALS ===
    for (unsigned i = 0; i < sizeof(RuntimeCredentials); i++) {
        ((uint8_t*)&runtimeCreds)[i] = EEPROM.read(100 + i);
    }
    bool displayNameValid = false;
    for (uint8_t i = 0; i < sizeof(runtimeCreds.displayName); i++) {
        uint8_t value = (uint8_t)runtimeCreds.displayName[i];
        if (value == '\0') {
            displayNameValid = true;
            break;
        }
        if (value < 32 || value > 126) {
            break;
        }
    }
    if (!displayNameValid) {
        runtimeCreds.displayName[0] = '\0';
    }

    /* ========================================================
     *  SAFETY CLAMPS — PREVENT INVALID EEPROM VALUES
     * ======================================================== */

    // BOOST TIME — critical for Guardian → BOOST behavior
    if (sys.boostTimeSeconds < 5 || sys.boostTimeSeconds > 600) {
        sys.boostTimeSeconds = 30;   // safe default
    }

    // Exhaust setpoint sanity
    if (sys.exhaustSetpoint < 200 || sys.exhaustSetpoint > 900) {
        sys.exhaustSetpoint = 400;
    }

    // Deadband sanity
    if (sys.deadbandF < 1 || sys.deadbandF > 100) {
        sys.deadbandF = 10;
    }

    // Fan clamp sanity
    if (sys.clampMinPercent < 0 || sys.clampMinPercent > 100) {
        sys.clampMinPercent = 10;
    }
    if (sys.clampMaxPercent < 0 || sys.clampMaxPercent > 100) {
        sys.clampMaxPercent = 90;
    }

    // Guardian thresholds sanity
    if (sys.emberGuardianTimerMinutes < 1 || sys.emberGuardianTimerMinutes > 120) {
        sys.emberGuardianTimerMinutes = 10;
    }
    if (sys.flueLowThreshold < 50 || sys.flueLowThreshold > 500) {
        sys.flueLowThreshold = 120;
    }
    if (sys.flueRecoveryThreshold < 50 || sys.flueRecoveryThreshold > 500) {
        sys.flueRecoveryThreshold = 180;
    }

    if (sys.envSeasonMode > 2) sys.envSeasonMode = 0;
    if (sys.envModeLockoutSec > 99UL * 3600UL) sys.envModeLockoutSec = 0;

    if (sys.envClampMaxSummerPercent > 100) sys.envClampMaxSummerPercent = 40;
    if (sys.envClampMaxSpringFallPercent > 100) sys.envClampMaxSpringFallPercent = 50;
    if (sys.envClampMaxWinterPercent > 100) sys.envClampMaxWinterPercent = 60;
    if (sys.envClampMaxExtremePercent > 100) sys.envClampMaxExtremePercent = 70;

    if (sys.envTankLowSummerF >= sys.envTankHighSummerF) {
        sys.envTankLowSummerF = 150;
        sys.envTankHighSummerF = 170;
    }
    if (sys.envTankLowSpringFallF >= sys.envTankHighSpringFallF) {
        sys.envTankLowSpringFallF = 155;
        sys.envTankHighSpringFallF = 175;
    }
    if (sys.envTankLowWinterF >= sys.envTankHighWinterF) {
        sys.envTankLowWinterF = 160;
        sys.envTankHighWinterF = 180;
    }
    if (sys.envTankLowExtremeF >= sys.envTankHighExtremeF) {
        sys.envTankLowExtremeF = 165;
        sys.envTankHighExtremeF = 185;
    }

    if (!eeprom_configValid()) {
        systemdata_init();
        eeprom_saveDefaultConfig();
    }
}

/* ============================================================
 *  CORE COMBUSTION SAVES
 * ============================================================ */

void eeprom_saveSetpoint(int v) {
    eeprom_write16(0, (int16_t)v);
}

void eeprom_saveBoostTime(int v) {
    eeprom_write16(2, (int16_t)v);
}

void eeprom_saveDeadband(int v) {
    eeprom_write16(4, (int16_t)v);
}

void eeprom_saveClampMin(int v) {
    eeprom_write16(6, (int16_t)v);
}

void eeprom_saveClampMax(int v) {
    eeprom_write16(8, (int16_t)v);
}

void eeprom_saveDeadzone(int v) {
    EEPROM.write(10, (uint8_t)v);
    eeprom_markConfigValid();
}

float eeprom_loadAdaptiveSlope() {
    float value = 1.0f;
    EEPROM.get(52, value);
    if (isnan(value) || value < 0.5f || value > 2.0f) {
        return 1.0f;
    }
    return value;
}

void eeprom_saveAdaptiveSlope(float value) {
    value = constrain(value, 0.5f, 2.0f);
    EEPROM.put(52, value);
    eeprom_markConfigValid();
}

/* ============================================================
 *  EMBER GUARDIAN SAVES
 * ============================================================ */

void eeprom_saveEmberGuardianMinutes(int v) {
    eeprom_write16(12, (int16_t)v);
}

void eeprom_saveFlueLow(int v) {
    eeprom_write16(14, (int16_t)v);
}

void eeprom_saveFlueRecovery(int v) {
    eeprom_write16(16, (int16_t)v);
}

/* ============================================================
 *  PROBE ROLES
 * ============================================================ */

void eeprom_saveProbeRoles() {
    for (int i = 0; i < PROBE_ROLE_COUNT; i++) {
        EEPROM.write(60 + i, sys.probeRoleMap[i]);
    }
    eeprom_markConfigValid();
}

void eeprom_loadProbeNames() {
    for (uint8_t probe = 0; probe < MAX_WATER_PROBES; probe++) {
        int address = EEPROM_PROBE_NAMES_ADDR + probe * PROBE_NAME_LENGTH;
        for (uint8_t i = 0; i < PROBE_NAME_LENGTH; i++) {
            sys.waterProbeNames[probe][i] = EEPROM.read(address + i);
        }
        sys.waterProbeNames[probe][PROBE_NAME_LENGTH - 1] = '\0';
        if (sys.waterProbeNames[probe][0] == '\0' ||
            (uint8_t)sys.waterProbeNames[probe][0] == 0xFF) {
            snprintf(sys.waterProbeNames[probe], PROBE_NAME_LENGTH,
                     "Probe %u", probe + 1);
        }
    }
}

void eeprom_saveProbeName(uint8_t index) {
    if (index >= MAX_WATER_PROBES) return;
    int address = EEPROM_PROBE_NAMES_ADDR + index * PROBE_NAME_LENGTH;
    for (uint8_t i = 0; i < PROBE_NAME_LENGTH; i++) {
        EEPROM.update(address + i, sys.waterProbeNames[index][i]);
    }
}

/* ============================================================
 *  ENVIRONMENTAL LOGIC SAVES
 * ============================================================ */

void eeprom_saveEnvSeasonMode(uint8_t mode) {
    EEPROM.write(18, mode);
    eeprom_markConfigValid();
}

void eeprom_saveEnvAutoSeason(bool en) {
    EEPROM.write(19, en ? 1 : 0);
    eeprom_markConfigValid();
}

void eeprom_saveEnvLockoutHours(uint8_t hours) {
    EEPROM.write(20, hours);
    eeprom_markConfigValid();
}

void eeprom_saveEnvSeasonStarts() {
    eeprom_write16(22, sys.envSummerStartF);
    eeprom_write16(24, sys.envSpringFallStartF);
    eeprom_write16(26, sys.envWinterStartF);
    eeprom_write16(28, sys.envExtremeStartF);
}

void eeprom_saveEnvSeasonHyst() {
    eeprom_write16(30, sys.envHystSummerF);
    eeprom_write16(32, sys.envHystSpringFallF);
    eeprom_write16(34, sys.envHystWinterF);
    eeprom_write16(36, sys.envHystExtremeF);
}

void eeprom_saveEnvSeasonSetpoints() {
    eeprom_write16(38, sys.envSetpointSummerF);
    eeprom_write16(40, sys.envSetpointSpringFallF);
    eeprom_write16(42, sys.envSetpointWinterF);
    eeprom_write16(44, sys.envSetpointExtremeF);
}

void eeprom_saveEnvSeasonTankValues() {
    eeprom_write16(68, sys.envTankHighSummerF);
    eeprom_write16(70, sys.envTankHighSpringFallF);
    eeprom_write16(72, sys.envTankHighWinterF);
    eeprom_write16(74, sys.envTankHighExtremeF);
    eeprom_write16(76, sys.envTankLowSummerF);
    eeprom_write16(78, sys.envTankLowSpringFallF);
    eeprom_write16(80, sys.envTankLowWinterF);
    eeprom_write16(82, sys.envTankLowExtremeF);
}

void eeprom_saveEnvSeasonClampValues() {
    EEPROM.write(84, sys.envClampMaxSummerPercent);
    EEPROM.write(85, sys.envClampMaxSpringFallPercent);
    EEPROM.write(86, sys.envClampMaxWinterPercent);
    EEPROM.write(87, sys.envClampMaxExtremePercent);
    eeprom_markConfigValid();
}

/* ============================================================
 *  BOILER CONTROL SAVES
 * ============================================================ */

void eeprom_saveTankLow(int v) {
    eeprom_write16(46, (int16_t)v);
}

void eeprom_saveTankHigh(int v) {
    eeprom_write16(48, (int16_t)v);
}

void eeprom_saveRunMode(uint8_t mode) {
    EEPROM.write(50, mode);
    eeprom_markConfigValid();
}

/* ============================================================
 *  RUNTIME CREDENTIALS
 * ============================================================ */

void eeprom_saveRuntimeCreds() {
    for (unsigned i = 0; i < sizeof(RuntimeCredentials); i++) {
        EEPROM.write(100 + i, ((uint8_t*)&runtimeCreds)[i]);
    }
}

