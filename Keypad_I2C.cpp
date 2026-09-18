/*
 * ============================================================
 *  Boiler Assistant – Keypad I²C Driver (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: Keypad_I2C.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Non‑blocking I²C keypad scanner for the 4×4 matrix keypad
 *    connected through a PCF8574‑style expander. This module
 *    implements deterministic keypad behavior under the Total
 *    Domination Architecture (TDA), ensuring stable operator
 *    input without ever blocking the real‑time control loop.
 *
 *    Features:
 *      • 4×4 matrix scan (rows driven low, columns read)
 *      • Debounce filtering (40 ms stable requirement)
 *      • Stable key reporting (no repeats until release)
 *      • Zero blocking delays (only µs‑level settling)
 *      • Fully compatible with deterministic main loop timing
 *
 *    Notes:
 *      - scanMatrix() performs raw hardware scanning
 *      - keypad_read() applies debounce + stable reporting
 *      - No dynamic allocation, no Strings, no blocking calls
 *      - All timing uses millis() and remains non‑blocking
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#include "Keypad_I2C.h"

#define KEYPAD_ADDR 0x20

/* ============================================================
 *  INTERNAL STATE
 * ============================================================ */

static TwoWire *kb = nullptr;

static const char keymap[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

static char lastStableKey     = 0;
static char lastReportedKey   = 0;
static unsigned long lastChangeTime = 0;

/* ============================================================
 *  INITIALIZATION
 * ============================================================ */

void keypad_init(TwoWire &bus) {
    kb = &bus;
}

/* ============================================================
 *  RAW MATRIX SCAN (no debounce)
 * ============================================================
 *  Returns:
 *      - The raw key character if pressed
 *      - 0 if no key is pressed
 *
 *  Behavior:
 *      - Drives one row LOW at a time
 *      - Reads column bits from expander
 *      - 300 µs settling delay ensures stable read
 * ============================================================ */

static char scanMatrix() {
    if (!kb) return 0;

    for (int row = 0; row < 4; row++) {

        uint8_t rowMask = ~(1 << row);  // active-low row drive

        kb->beginTransmission(KEYPAD_ADDR);
        kb->write(rowMask);
        kb->endTransmission();

        delayMicroseconds(300);  // settling time

        kb->requestFrom(KEYPAD_ADDR, 1);
        if (!kb->available()) continue;

        uint8_t colData = kb->read();

        for (int col = 0; col < 4; col++) {
            if (!(colData & (1 << (col + 4)))) {
                return keymap[row][col];
            }
        }
    }

    return 0;
}

/* ============================================================
 *  DEBOUNCED KEYPAD READ
 * ============================================================
 *  Returns:
 *      - A single keypress event (char)
 *      - 0 when no new key is ready
 *
 *  Behavior:
 *      - Requires 40 ms of stable key state
 *      - Prevents repeats until key is released
 *      - Tracks last stable and last reported keys
 * ============================================================ */

char keypad_read() {
    char rawKey = scanMatrix();
    unsigned long now = millis();

    // Detect change in raw key state
    if (rawKey != lastStableKey) {
        lastStableKey = rawKey;
        lastChangeTime = now;
    }

    // Key pressed and stable
    if (rawKey != 0 && (now - lastChangeTime) > 40) {
        if (rawKey != lastReportedKey) {
            lastReportedKey = rawKey;
            return rawKey;
        }
    }

    // Key released and stable
    if (rawKey == 0 && lastReportedKey != 0 && (now - lastChangeTime) > 40) {
        lastReportedKey = 0;
    }

    return 0;
}
