/*
 * ============================================================
 *  Boiler Assistant – Hardware Pinout 
 *  ------------------------------------------------------------
 *  File: Pinout.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      Centralized hardware pin definitions for the UNO R4 WiFi
 *      platform used by Boiler Assistant. Ensures:
 *        • Single source of truth for all hardware mappings
 *        • Clean separation between logic and hardware layout
 *        • Safe, explicit pin usage for I2C, SPI, PWM, relays,
 *          and DS18B20 OneWire sensors.
 *
 *      Power Notes:
 *        - ALL 5V devices (LCD, keypad, relays, MAX31855 boards,
 *          DS18B20 sensors) must use the UNO R4's regulated 5V
 *          and GND rails. Do NOT mix external supplies.
 *
 *      ⚠ WARNING — BME280 SENSOR VOLTAGE:
 *        - The BME280 module used for outdoor sensing is a
 *          **3.3V‑ONLY device**.
 *        - NEVER connect it to 5V power or 5V I2C lines unless
 *          the breakout board includes a regulator + level shifter.
 *        - Direct 5V wiring will permanently damage the sensor.
 *
 *      Notes:
 *        - Primary I2C bus (A4/A5) drives LCD, BME280, keypad.
 *        - DS18B20 sensors share a single OneWire bus on D8.
 *        - MAX31855 thermocouples use hardware SPI (D12/D13).
 *        - Fan output uses a PWM‑capable pin on UNO R4.
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#ifndef PINOUT_H
#define PINOUT_H

/* ============================================================
 *  PRIMARY I2C BUS (LCD + BME280 + Keypad)
 * ============================================================ */

#define PIN_I2C1_SDA   A4
#define PIN_I2C1_SCL   A5

/* ============================================================
 *  DIGITAL OUTPUTS
 * ============================================================ */

// Fan PWM output (UNO R4 PWM-capable)
#define PIN_FAN_PWM        D5

// Damper relay (active LOW)
#define PIN_DAMPER         D6

/* ============================================================
 *  DS18B20 WATER TEMPERATURE SENSORS (OneWire bus)
 * ============================================================ */

#define PIN_DS18B20_DATA   D8

/* ============================================================
 *  SPI – MAX31855 Thermocouples
 *  UNO R4 SPI pins:
 *      SCK  = D13
 *      MISO = D12
 *      MOSI = D11 (unused by MAX31855)
 * ============================================================ */

#define PIN_MAX31855_MISO  D12  // DO  (Data Out → MCU MISO)
#define PIN_MAX31855_SCK   D13  // CLK (Clock from MCU)

// Chip selects (one per thermocouple)
#define PIN_TC1_CS         D7   // CS (Chip Select) – Exhaust probe
#define PIN_TC2_CS         D3
#define PIN_TC3_CS         D4
#define PIN_TC4_CS         D2   // Keep separate from fan PWM on D5

#endif
