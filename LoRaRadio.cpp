/*
 * ============================================================
 *  Boiler Assistant – LoRa Telemetry Module (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: LoRaRadio.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Lightweight, non‑blocking LoRa telemetry and command module
 *    implementing the Total Domination Architecture (TDA). This
 *    subsystem provides deterministic long‑range communication
 *    without ever blocking the real‑time control loop.
 *
 *    Features:
 *      • 16‑byte telemetry packet (exhaust, fan, burn state, env)
 *      • CRC‑8 integrity checking (poly 0x31)
 *      • Remote parameter updates (setpoint, clamps, thresholds)
 *      • 2‑second periodic transmit cycle
 *      • Fully non‑blocking operation
 *
 *    Telemetry Packet Layout (16 bytes):
 *      [0]      version
 *      [1–2]    exhaustSmoothF ×10
 *      [3]      fanFinal
 *      [4]      burnState
 *      [5–6]    envTempF ×10
 *      [7]      waterProbeCount
 *      [8–9]    waterTempF[0] ×10
 *      [10–11]  envHumidity ×10
 *      [12]     remoteChanged flag
 *      [13]     reserved
 *      [14]     reserved
 *      [15]     CRC‑8
 *
 *    Command Packet Layout:
 *      [0]   command ID
 *      [1–2] 16‑bit value
 *      [3]   CRC‑8
 *
 *  Architectural Notes:
 *      - All LoRa operations are non‑blocking
 *      - CRC‑8 ensures packet integrity
 *      - SystemData is the single source of truth
 *      - No UI, EEPROM, or burn logic lives here
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#include <Arduino.h>
#include "SystemState.h"          
#include "EnvironmentalLogic.h"  
#include "SystemData.h"           
#include "LoRaRadio.h"
#include "EEPROMStorage.h"
#include "ConfigValidation.h"
#include <LoRa.h>


/* ============================================================
 *  EXTERNAL SYSTEM DATA
 * ============================================================ */

extern SystemData sys;

/* ============================================================
 *  FORWARD DECLARATIONS
 * ============================================================ */

static void lora_sendTelemetry();
static void lora_handleCommand(uint8_t* pkt, uint8_t len);

/* ============================================================
 *  CRC‑8 (polynomial 0x31)
 * ============================================================ */

static uint8_t crc8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0xFF;
    while (len--) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
    }
    return crc;
}

/* ============================================================
 *  INITIALIZATION
 * ============================================================ */

void lora_init() {
    LoRa.begin(915E6);
}

/* ============================================================
 *  MAIN LOOP (NON‑BLOCKING)
 * ============================================================ */

void lora_loop() {

    // Receive packets
    int packetSize = LoRa.parsePacket();
    if (packetSize > 0) {
        uint8_t buf[32];
        if (packetSize > (int)sizeof(buf)) {
            while (LoRa.available()) LoRa.read();
            return;
        }
        int len = LoRa.readBytes(buf, packetSize);
        if (len == 4) lora_handleCommand(buf, len);
    }

    // Transmit telemetry every 2 seconds
    static unsigned long lastTx = 0;
    if (millis() - lastTx > 2000) {
        lora_sendTelemetry();
        lastTx = millis();
    }
}

/* ============================================================
 *  TELEMETRY PACKET (16 BYTES)
 * ============================================================ */

static void lora_sendTelemetry() {
    uint8_t pkt[16];

    pkt[0] = 0x01; // version

    uint16_t ex = sys.exhaustSmoothF * 10;
    pkt[1] = ex >> 8;
    pkt[2] = ex & 0xFF;

    pkt[3] = sys.fanFinal;
    pkt[4] = sys.burnState;

    uint16_t t = sys.envTempF * 10;
    pkt[5] = t >> 8;
    pkt[6] = t & 0xFF;

    pkt[7] = sys.waterProbeCount;

    uint16_t w = (sys.waterProbeCount > 0) ? (sys.waterTempF[0] * 10) : 0;
    pkt[8]  = w >> 8;
    pkt[9]  = w & 0xFF;

    uint16_t h = sys.envHumidity * 10;
    pkt[10] = h >> 8;
    pkt[11] = h & 0xFF;

    pkt[12] = sys.remoteChanged ? 1 : 0;
    pkt[13] = 0; // reserved
    pkt[14] = 0; // reserved

    pkt[15] = crc8(pkt, 15);

    LoRa.beginPacket();
    LoRa.write(pkt, 16);
    LoRa.endPacket();
}

/* ============================================================
 *  COMMAND HANDLER
 * ============================================================ */

static void lora_handleCommand(uint8_t* pkt, uint8_t len) {

    if (len < 4) return;
    if (crc8(pkt, len - 1) != pkt[len - 1]) return; // CRC fail

    uint8_t cmd = pkt[0];
    uint16_t value = (pkt[1] << 8) | pkt[2];

    switch (cmd) {
        case 0x01:
            if (!validExhaustSetpoint(value)) return;
            sys.exhaustSetpoint = value;
            eeprom_saveSetpoint(value);
            break;
        case 0x02:
            if (!validDeadband(value)) return;
            sys.deadbandF = value;
            eeprom_saveDeadband(value);
            break;
        case 0x03:
            if (!validFanClamp(value) || value > sys.clampMaxPercent) return;
            sys.clampMinPercent = value;
            eeprom_saveClampMin(value);
            break;
        case 0x04:
            if (!validFanClamp(value) || value < sys.clampMinPercent) return;
            sys.clampMaxPercent = value;
            eeprom_saveClampMax(value);
            break;
        case 0x05:
            if (!validBoostTime(value)) return;
            sys.boostTimeSeconds = value;
            eeprom_saveBoostTime(value);
            break;
        case 0x06:
            if (!validGuardianMinutes(value)) return;
            sys.emberGuardianTimerMinutes = value;
            eeprom_saveEmberGuardianMinutes(value);
            break;
        case 0x07:
            if (!validFlueThreshold(value) || value > sys.flueRecoveryThreshold) return;
            sys.flueLowThreshold = value;
            eeprom_saveFlueLow(value);
            break;
        case 0x08:
            if (!validFlueThreshold(value) || value < sys.flueLowThreshold) return;
            sys.flueRecoveryThreshold = value;
            eeprom_saveFlueRecovery(value);
            break;
        default: return;
    }

    sys.remoteChanged = true;
}
