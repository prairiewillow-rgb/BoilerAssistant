/*
 * ============================================================
 *  Boiler Assistant – LoRa Telemetry API (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: LoRaRadio.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Public interface for the LoRa telemetry and command subsystem.
 *    This module provides deterministic long‑range communication
 *    under the Total Domination Architecture (TDA), ensuring that
 *    telemetry and remote commands never interfere with the
 *    real‑time burn engine or control loop.
 *
 *    Responsibilities:
 *      • lora_init() — initialize LoRa radio hardware
 *      • lora_loop() — fully non‑blocking RX/TX handler
 *      • Broadcast compact 16‑byte telemetry packets
 *      • Receive CRC‑validated command packets
 *      • Update SystemData fields from remote commands
 *
 *    Architectural Notes:
 *      - All packet encoding/decoding implemented in LoRaRadio.cpp
 *      - Telemetry interval fixed at 2 seconds
 *      - No blocking delays, no dynamic allocation
 *      - SystemData is the single source of truth
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#ifndef LORA_RADIO_H
#define LORA_RADIO_H

// Initialize LoRa radio hardware
void lora_init();

// Non‑blocking RX/TX loop (called from main loop)
void lora_loop();

#endif

