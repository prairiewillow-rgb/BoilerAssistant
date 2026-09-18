/*
 * ============================================================
 *  Boiler Assistant – UI Module (v3.1 "Total Domination")
 *  ------------------------------------------------------------
 *  File: UI.cpp
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Full keypad‑driven LCD UI subsystem for the Boiler Assistant.
 *    Implements deterministic operator interaction for:
 *      • Home screen
 *      • Combustion menus
 *      • Tank setpoints
 *      • Environmental seasonal system
 *      • Probe role assignment
 *      • Networking & provisioning
 *      • Safety lockouts and Guardian logic
 *
 *    Architectural Notes:
 *      - No control logic lives here — UI only.
 *      - All state is read/written through SystemData (sys.*).
 *      - All EEPROM writes are delegated to EEPROMStorage.
 *      - Rendering is strictly 20×4 LCD, deterministic, no animations
 *        except the boot sequence.
 *
 *  Version:
 *      Boiler Assistant v3.1 "Total Domination"
 * ============================================================
 */

#include "UI.h"
#include "SystemState.h"
#include "SystemData.h"
#include "EEPROMStorage.h"
#include "EnvironmentalLogic.h"
#include "WiFiProvisioning.h"
#include "RuntimeCredentials.h"
#include <LiquidCrystal_PCF8574.h>
#include <Arduino.h>
#include <WiFiS3.h>
#include <EEPROM.h>

/* ============================================================
 *  COMPATIBILITY SHIMS (v2.2 → v3.0)
 * ============================================================ */
#ifndef MAX_WATER_PROBES
#define MAX_WATER_PROBES 8
#endif

#ifndef PROBE_ROLE_COUNT
#define PROBE_ROLE_COUNT 8
#endif

/* ============================================================
 *  EXTERNALS
 * ============================================================ */

extern void eeprom_saveProbeRoles();
extern RuntimeCredentials runtimeCreds;

// combustion EEPROM hooks
extern void eeprom_saveSetpoint(int v);
extern void eeprom_saveClampMin(int v);
extern void eeprom_saveClampMax(int v);
extern void eeprom_saveDeadzone(uint8_t mode);
extern void eeprom_saveFlueLow(int v);
extern void eeprom_saveFlueRecovery(int v);
extern void eeprom_saveBoostTime(int v);

// environmental EEPROM hooks
extern void eeprom_saveEnvSeasonStarts();
extern void eeprom_saveEnvSeasonHyst();
extern void eeprom_saveEnvSeasonSetpoints();
extern void eeprom_saveEnvSeasonTankValues();
extern void eeprom_saveEnvSeasonClampValues();
extern void eeprom_saveEnvSeasonMode(uint8_t mode);
extern void eeprom_saveEnvAutoSeason(bool en);
extern void eeprom_saveEnvLockoutHours(uint8_t hours);
extern void eeprom_saveEmberGuardianMinutes(int v);

// run mode / tank setpoint EEPROM hooks
extern void eeprom_saveRunMode(uint8_t mode);
extern void eeprom_saveTankLow(int16_t v);
extern void eeprom_saveTankHigh(int16_t v);

// Unified redraw flag
#define uiNeedRedraw sys.uiNeedsRefresh

extern UIState uiState;

// BurnEngine hook
extern void burnengine_startBoost();
extern void burnengine_resetSensorFault();

/* ============================================================
 *  EDIT BUFFERS
 * ============================================================ */
extern String newSetpointValue;
extern String boostTimeEditValue;
extern String deadbandEditValue;
extern String clampMinEditValue;
extern String clampMaxEditValue;
extern String emberGuardianEditValue;
extern String flueLowEditValue;
extern String flueRecEditValue;

static String tankLowEditValue;
static String tankHighEditValue;

static EnvSeason uiEditSeason = ENV_SEASON_SUMMER;
static String envSeasonEditValue;
static String envSetpointEditValue;
static String envLockoutEditValue;

/* ============================================================
 *  ENVIRONMENTAL UI SUPPORT
 * ============================================================ */
static const char* envSeasonLongName(EnvSeason s) {
    switch (s) {
        case ENV_SEASON_SUMMER:      return "SUMMER SETTINGS   ";
        case ENV_SEASON_SPRING_FALL: return "SPRING/FALL SET   ";
        case ENV_SEASON_WINTER:      return "WINTER SETTINGS   ";
        case ENV_SEASON_EXTREME:     return "EXTREME COLD SET  ";
        default:                     return "UNKNOWN SEASON    ";
    }
}



/* ============================================================
 *  PROBE ROLE NAMES
 * ============================================================ */
static const char* roleNames[] = {
    "MAIN TANK",
    "L1 SUPPLY",
    "L1 RETURN",
    "L2 SUPPLY",
    "L2 RETURN",
    "TANK BOTTOM",
    "TANK TOP",
    "EXTRA"
};

/* ============================================================
 *  PROBE ROLE UI STATE
 * ============================================================ */
static uint8_t selectedRole = 0;
static uint8_t selectedPhys = 0;

/* ============================================================
 *  LCD RENDERER
 * ============================================================ */
static LiquidCrystal_PCF8574* lcdRef = nullptr;

static void lcd4(const char* l1, const char* l2, const char* l3, const char* l4) {
    static char last[4][21] = {"", "", "", ""};
    const char* lines[4] = { l1, l2, l3, l4 };

    for (int i = 0; i < 4; i++) {
        if (strncmp(lines[i], last[i], 20) != 0) {
            lcdRef->setCursor(0, i);
            lcdRef->print("                    ");
            lcdRef->setCursor(0, i);
            lcdRef->print(lines[i]);
            strncpy(last[i], lines[i], 20);
            last[i][20] = '\0';
        }
    }
}

/* ============================================================
 *  SAFETY LOCKOUT SCREEN
 * ============================================================ */
static void ui_showSafetyLockout(int tankF)
{
    char line2[21];
    snprintf(line2, 21, " TANK TEMP: %3dF", tankF);

    if (sys.safetyState == SAFETY_SENSOR_FAULT) {
        const char* faultLine = " SENSOR UNKNOWN     ";
        if (sys.sensorFaultMask == SENSOR_FAULT_EXHAUST) {
            faultLine = " EXHAUST SENSOR BAD ";
        } else if (sys.sensorFaultMask == SENSOR_FAULT_TANK) {
            faultLine = " TANK SENSOR BAD    ";
        } else if (sys.sensorFaultMask == (SENSOR_FAULT_EXHAUST | SENSOR_FAULT_TANK)) {
            faultLine = " BOTH SENSORS BAD   ";
        }

        lcd4(
            " SENSOR FAULT LOCK ",
            faultLine,
            " SYSTEM STOPPED    ",
            " PRESS * TO RESET  "
        );
        return;
    }

    lcd4(
        " HIGH TEMP LOCKOUT ",
        line2,
        " SYSTEM STOPPED    ",
        " PRESS * TO RESET  "
    );
}

static void ui_showExhaustFallback()
{
    lcd4(
        " EXHAUST PROBE BAD ",
        " SYSTEM DEFAULT    ",
        " FAN RUNNING 100%  ",
        " PRESS * TO RESET  "
    );
}

/* ============================================================
 *  BOOT SCREEN
 * ============================================================ */
static void ui_centerName(char* line)
{
    const char* name = runtimeCreds.displayName;
    size_t length = 0;
    while (length < 20 && name[length] >= 32 && name[length] <= 126) {
        length++;
    }

    memset(line, ' ', 20);
    line[20] = '\0';
    size_t start = (20 - length) / 2;
    memcpy(line + start, name, length);
}

static void showBootScreen() {
    char nameLine[21];
    ui_centerName(nameLine);

    lcdRef->clear();
    lcdRef->setCursor(0, 0); lcdRef->print("  BOILER ASSISTANT  ");
    delay(300);
    lcdRef->setCursor(0, 1); lcdRef->print("    INITIALIZING    ");
    delay(300);

    const char* bar[] = {
        "                    ","#                   ","##                  ",
        "###                 ","####                ","#####               ",
        "######              ","#######             ","########            ",
        "#########           ","##########          ","###########         ",
        "############        ","#############       ","##############      ",
        "###############     ","################    ","#################   ",
        "##################  ","################### ","********************"
    };

    for (int i = 0; i < 21; i++) {
        lcdRef->setCursor(0, 2);
        lcdRef->print(bar[i]);
        delay(70);
    }

    lcdRef->setCursor(0, 3);
    lcdRef->print("  SYSTEM CHECK OK   ");
    delay(800);

    lcdRef->clear();
    lcdRef->setCursor(0, 0); lcdRef->print("      WELCOME       ");
    lcdRef->setCursor(0, 1); lcdRef->print("                    ");
    lcdRef->setCursor(0, 1); lcdRef->print(nameLine);
    lcdRef->setCursor(0, 2); lcdRef->print("  LOADING SYSTEMS   ");
    lcdRef->setCursor(0, 3); lcdRef->print("        V3.1        ");
    delay(700);
}

/* ============================================================
 *  UI INIT
 * ============================================================ */
void ui_init() {
    static LiquidCrystal_PCF8574 lcd(0x27);
    lcdRef = &lcd;

    lcd.begin(20, 4);
    lcd.setBacklight(255);

    showBootScreen();

    uiState = UI_HOME;
    uiNeedRedraw = true;
}

/* ============================================================
 *  HOME SCREEN
 * ============================================================ */
static void ui_showHome(double exhaustF_unused, int fanPercent) {
    char l1[21], l2[21], l3[21], l4[21];

    if (sys.emberGuardianLatched && sys.burnState == BURN_EMBER_GUARD) {
        lcd4(
            "   EMBER GUARDIAN   ",
            "   DAMPER/FAN OFF   ",
            "      PRESS *       ",
            "     TO RESET       "
        );
        return;
    }

    int tankIndex = (sys.probeRoleMap[PROBE_TANK] < sys.waterProbeCount)
                    ? sys.probeRoleMap[PROBE_TANK]
                    : 0;

    int tankF = (int)(sys.waterTempF[tankIndex] + 0.5);

    if (sys.safetyState != SAFETY_OK) {
        ui_showSafetyLockout(tankF);
        return;
    }

    if (sys.exhaustFallbackActive) {
        ui_showExhaustFallback();
        return;
    }

    snprintf(l1, 21, "E/SPT:%3dF  W/H:%03dF",
             sys.exhaustSetpoint, sys.tankHighSetpointF);

    double dispF = sys.exhaustSmoothF;

    if (isnan(dispF))
        snprintf(l2, 21, "E/CUR:---   W/L:%03dF", sys.tankLowSetpointF);
    else
        snprintf(l2, 21, "E/CUR:%3dF  W/L:%03dF",
                 (int)(dispF + 0.5), sys.tankLowSetpointF);

    if (fanPercent <= 0)
        snprintf(l3, 21, "FAN:OFF     W/C:%03dF", tankF);
    else
        snprintf(l3, 21, "FAN:%3d%%    W/C:%03dF", fanPercent, tankF);

    switch (sys.burnState) {
        case BURN_IDLE:        snprintf(l4, 21, "IDLE          "); break;
        case BURN_RAMP:        snprintf(l4, 21, "RAMPING UP    "); break;
        case BURN_HOLD:        snprintf(l4, 21, "IN THE ZONE!! "); break;
        case BURN_BOOST:       snprintf(l4, 21, "BOOSTING      "); break;
        case BURN_EMBER_GUARD: snprintf(l4, 21, "EMBER GUARD   "); break;
        default:               snprintf(l4, 21, "UNKNOWN       "); break;
    }

    if (!sys.emberGuardianActive &&
        sys.emberGuardianTimerActive &&
        sys.emberGuardianTimerMinutes > 0)
    {
        unsigned long now = millis();
        unsigned long elapsed = now - sys.emberGuardianStartMs;
        unsigned long total = (unsigned long)sys.emberGuardianTimerMinutes * 60000UL;

        long remainingMs = (long)total - (long)elapsed;
        if (remainingMs < 0) remainingMs = 0;

        int remainingMin = (int)(remainingMs / 60000UL);

        snprintf(l4, 21, "EMBER GUARD IN %2dM", remainingMin);
    }

    lcd4(l1, l2, l3, l4);
}

/* ============================================================
 *  COMBUSTION MENU
 * ============================================================ */
static void ui_showCombustionMenu() {
    lcd4(
        "1: EXHAUST SETPOINT",
        "2: CLAMP/DBAND/BOOST",
        "3: DEADZONE FAN    ",
        "4: EMBER GUARDIAN  "
    );
}

static void ui_showClampDeadbandMenu() {
    char l1[21], l2[21], l3[21], l4[21];

    snprintf(l1, 21, "1: MIN CLAMP: %3d%%", sys.clampMinPercent);
    snprintf(l2, 21, "2: MAX CLAMP: %3d%%", sys.clampMaxPercent);
    snprintf(l3, 21, "3: DEADBAND:  %3dF",  sys.deadbandF);
    snprintf(l4, 21, "4: BOOST TIME     ");

    lcd4(l1, l2, l3, l4);
}

static void ui_showClampMin() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %3d%%", sys.clampMinPercent);
    snprintf(l3, 21, "NEW: %s", clampMinEditValue.c_str());

    lcd4(
        "SET MIN CLAMP     ",
        l2, l3,
        "*=BACK   #=SAVE    "
    );
}

static void ui_showBoostTime() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %3d SEC", sys.boostTimeSeconds);
    snprintf(l3, 21, "NEW: %s", boostTimeEditValue.c_str());

    lcd4(
        "SET BOOST TIME    ",
        l2,
        l3,
        "*=BACK   #=SAVE    "
    );
}

static void ui_showClampMax() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %3d%%", sys.clampMaxPercent);
    snprintf(l3, 21, "NEW: %s", clampMaxEditValue.c_str());

    lcd4(
        "SET MAX CLAMP     ",
        l2, l3,
        "*=BACK   #=SAVE    "
    );
}

static void ui_showDeadband() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %3dF", sys.deadbandF);
    snprintf(l3, 21, "NEW: %s", deadbandEditValue.c_str());

    lcd4(
        "SET DEADBAND      ",
        l2, l3,
        "*=BACK   #=SAVE    "
    );
}

static void ui_showDeadzoneFanMenu() {
    bool modeAlwaysOn   = (sys.deadzoneFanMode == 1);
    bool modeAllowedOff = (sys.deadzoneFanMode == 0);

    char line2[21];
    char line3[21];

    snprintf(line2, sizeof(line2),
             "1:FAN ALWAYS ON %s",
             modeAlwaysOn ? "  <-" : " ");

    snprintf(line3, sizeof(line3),
             "2:FAN ALLOWED OFF %s",
             modeAllowedOff ? "<-" : " ");

    lcd4(
        "DEADZONE FAN RULES",
        line2,
        line3,
        "*=BACK             "
    );
}

/* ============================================================
 *  EXHAUST SETPOINT EDIT
 * ============================================================ */
static void ui_showSetpoint() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %3dF", sys.exhaustSetpoint);
    snprintf(l3, 21, "NEW: %s", newSetpointValue.c_str());

    lcd4(
        "EXHAUST SETPOINT  ",
        l2, l3,
        "*=BACK   #=SAVE    "
    );
}

/* ============================================================
 *  EMBER GUARDIAN MENU
 * ============================================================ */
static void ui_showEmberGuardianMenu() {
    lcd4(
        "EMBER GUARDIAN    ",
        "1: TIMER           ",
        "2: FLUE LOW THRESH ",
        "3: FLUE REC THRESH "
    );
}

static void ui_showEmberGuardianTimer() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %2d MIN", sys.emberGuardianTimerMinutes);
    snprintf(l3, 21, "NEW: %s", emberGuardianEditValue.c_str());

    lcd4(
        "EMBER TIMER       ",
        l2, l3,
        "*=BACK   #=SAVE    "
    );
}

/* ============================================================
 *  FLUE THRESHOLDS
 * ============================================================ */
static void ui_showFlueLow() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %3dF", sys.flueLowThreshold);
    snprintf(l3, 21, "NEW: %s", flueLowEditValue.c_str());

    lcd4(
        "FLUE LOW THRESH   ",
        l2, l3,
        "*=BACK   #=SAVE    "
    );
}

static void ui_showFlueRec() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %3dF", sys.flueRecoveryThreshold);
    snprintf(l3, 21, "NEW: %s", flueRecEditValue.c_str());

    lcd4(
        "FLUE RECOVERY THR ",
        l2, l3,
        "*=BACK   #=SAVE    "
    );
}

/* ============================================================
 *  BOILER CONTROL MENU
 * ============================================================ */
static void ui_showBoilerMenu() {
    lcd4(
        "1: RUN MODE        ",
        "2: LOW WATER SET   ",
        "3: HIGH WATER SET  ",
        "4: SAFETY STATUS   "
    );
}

static void ui_showRunMode() {
    char l2[21], l3[21];

    snprintf(l2, 21, "1: CONTINUOUS%s",
             (sys.controlMode == RUNMODE_CONTINUOUS ? " <-" : ""));

    snprintf(l3, 21, "2: AUTO TANK%s",
             (sys.controlMode == RUNMODE_AUTO_TANK ? "  <-" : ""));

    lcd4(
        "SELECT RUN MODE   ",
        l2, l3,
        "*=BACK             "
    );
}

static void ui_showTankLow() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %3dF", sys.tankLowSetpointF);
    snprintf(l3, 21, "NEW: %s", tankLowEditValue.c_str());

    lcd4(
        " LOW WATER SETPT   ",
        l2, l3,
        "*=BACK   #=SAVE    "
    );
}

static void ui_showTankHigh() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %3dF", sys.tankHighSetpointF);
    snprintf(l3, 21, "NEW: %s", tankHighEditValue.c_str());

    lcd4(
        "HIGH WATER SETPT  ",
        l2, l3,
        "*=BACK   #=SAVE    "
    );
}

/* ============================================================
 *  SAFETY STATUS SCREEN
 * ============================================================ */
static void ui_showSafetyStatus() {
    char l2[21], l3[21];

    int tankIndex = (sys.probeRoleMap[PROBE_TANK] < sys.waterProbeCount)
                    ? sys.probeRoleMap[PROBE_TANK]
                    : 0;

    int tankF = (int)(sys.waterTempF[tankIndex] + 0.5);

    if (sys.safetyState != SAFETY_OK) {
        snprintf(l2, 21, "STATE: LOCKOUT");
        snprintf(l3, 21, "TANK: %3dF", tankF);
        lcd4(" SAFETY STATUS     ", l2, l3, "*=RESET            ");
    } else {
        snprintf(l2, 21, "STATE: OK");
        snprintf(l3, 21, "HIGH LIMIT: 190F");
        lcd4("SAFETY STATUS     ", l2, l3, "*=BACK             ");
    }
}

static void ui_showRunModeConfirmContinuous() {
    lcd4(
        "!!CONTINUOUS  MODE!!",
        "!IGNORES  TANK TEMP!",
        "!!NO AUTO SHUTDOWN!!",
        "   A:YES    B:NO    "
    );
}

/* ============================================================
 *  ENVIRONMENT MENU
 * ============================================================ */
static void ui_showEnvMenu() {
    lcd4(
        "ENVIRONMENT       ",
        "1: SEASONS         ",
        "2: LOCKOUT/MODE    ",
        "*=BACK             "
    );
}

static void ui_showSeasonsMenu() {
    lcd4(
        "1: SUMMER          ",
        "2: SPRING/FALL     ",
        "3: WINTER          ",
        "4: EXTREME         "
    );
}

static void ui_showSeasonsMenuLine4() {
    lcd4(
        "SEASONS           ",
        "1: SUMMER          ",
        "2: SPRING/FALL     ",
        "3: WINTER          "
    );
}

static void ui_showSeasonDetailMenu() {
    lcd4(
        envSeasonLongName(uiEditSeason),
        "1: START TEMP      ",
        "2: HYSTERESIS      ",
        "3: EXHAUST SETPT   "
    );
}

static void ui_showSeasonDetailMenu2() {
    lcd4(
        envSeasonLongName(uiEditSeason),
        "4: TANK HIGH       ",
        "5: TANK LOW        ",
        "6: MAX CLAMP       "
    );
}

static void ui_showSeasonEditStart() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %3dF", *ui_getSeasonStartPtr(uiEditSeason));
    snprintf(l3, 21, "NEW: %s", envSeasonEditValue.c_str());

    lcd4(
        "EDIT START TEMP   ",
        l2, l3,
        "*=BACK   #=SAVE    "
    );
}

static void ui_showSeasonEditBuffer() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %3dF", *ui_getSeasonBufferPtr(uiEditSeason));
    snprintf(l3, 21, "NEW: %s", envSeasonEditValue.c_str());

    lcd4(
        "EDIT HYSTERESIS   ",
        l2, l3,
        "*=BACK   #=SAVE    "
    );
}

static void ui_showSeasonEditSetpoint() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %3dF", *ui_getSeasonSetpointPtr(uiEditSeason));
    snprintf(l3, 21, "NEW: %s", envSetpointEditValue.c_str());

    lcd4(
        "EDIT EXH SETPOINT ",
        l2, l3,
        "*=BACK   #=SAVE    "

    );
}

static void ui_showSeasonEditTankHigh() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %3dF", *ui_getSeasonTankHighPtr(uiEditSeason));
    snprintf(l3, 21, "NEW: %s", envSeasonEditValue.c_str());

    lcd4(
        "EDIT TANK HIGH    ",
        l2, l3,
        "*=BACK   #=SAVE    "
    );
}

static void ui_showSeasonEditTankLow() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %3dF", *ui_getSeasonTankLowPtr(uiEditSeason));
    snprintf(l3, 21, "NEW: %s", envSeasonEditValue.c_str());

    lcd4(
        "EDIT TANK LOW     ",
        l2, l3,
        "*=BACK   #=SAVE    "
    );
}

static void ui_showSeasonEditClampMax() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %3d%%", (int)*ui_getSeasonClampMaxPtr(uiEditSeason));
    snprintf(l3, 21, "NEW: %s", envSeasonEditValue.c_str());

    lcd4(
        "EDIT MAX CLAMP    ",
        l2, l3,
        "*=BACK   #=SAVE    "
    );
}

/* ============================================================
 *  LOCKOUT / MODE
 * ============================================================ */
static void ui_showEnvLockoutMenu() {
    lcd4(
        "LOCKOUT / MODE    ",
        "1: MODE            ",
        "2: AUTO-SEASON     ",
        "3: LOCKOUT HOURS   "
    );
}

static void ui_showEnvMode() {
    char l2[21], l3[21], l4[21];

    snprintf(l2, 21, "1: OFF%s",
             (sys.envSeasonMode == 0 ? "   <-" : ""));

    snprintf(l3, 21, "2: USER%s",
             (sys.envSeasonMode == 1 ? "  <-" : ""));

    snprintf(l4, 21, "3: AUTO%s",
             (sys.envSeasonMode == 2 ? "  <-" : ""));

    lcd4(
        "ENVIRONMENT MODE",
        l2, l3, l4
    );
}


static void ui_showEnvAutoSeason() {
    char l2[21];
    snprintf(l2, 21, "CURRENT: %s",
             sys.envAutoSeasonEnabled ? "ON " : "OFF");

    lcd4(
        "AUTO-SEASON       ",
        l2,
        "*=TOGGLE           ",
        "#=BACK             "
    );
}

static void ui_showEnvLockoutHours() {
    char l2[21], l3[21];
    snprintf(l2, 21, "CURRENT: %2lu",
             (unsigned long)(sys.envModeLockoutSec / 3600UL));
    snprintf(l3, 21, "NEW: %s", envLockoutEditValue.c_str());

    lcd4(
        "LOCKOUT HOURS     ",
        l2, l3,
        "*=BACK   #=SAVE    "
    );
}

/* ============================================================
 *  SENSORS & NETWORK MENU (D)
 * ============================================================ */
static void ui_showSensorsMenu() {
    lcd4(
        "SENSORS & NETWORK ",
        "1: PROBE ROLES     ",
        "2: BME280 STATUS   ",
        "3: NETWORKING      "
    );
}

static void ui_showProbeRoleScreen(uint8_t role, uint8_t physIndex) {
    char l2[21], l3[21], l4[21];

    snprintf(l2, 21, "ROLE : %-16s", roleNames[role]);
    snprintf(l3, 21, "PROBE: %d", physIndex);

    if (physIndex < sys.waterProbeCount)
        snprintf(l4, 21, "TEMP : %4.1fF", sys.waterTempF[physIndex]);
    else
        snprintf(l4, 21, "TEMP: ---.-F");

    lcd4(
        "WATER PROBE ROLES       ",
        l2, l3, l4
    );
}

static void ui_showBME() {
    char l2[21], l3[21], l4[21];

    if (!sys.envSensorOK) {
        lcd4(
            "BME280 ERROR      ",
            "CHECK WIRING      ",
            "                   ",
            "*=BACK             "
        );
        return;
    }

    snprintf(l2, 21, "OUT TEMP:      %3.1fF", sys.envTempF);
    snprintf(l3, 21, "HUMIDITY:      %2.1f%%", sys.envHumidity);
    snprintf(l4, 21, "PRESSURE:   %3.1fhPa", sys.envPressure);

    lcd4(
        "BME280 STATUS      ",
        l2, l3, l4
    );
}

static void ui_showNetworkingMenu() {
    lcd4(
        "NETWORKING        ",
        "1: NETWORK INFO    ",
        "2: FACTORY RESET   ",
        "*=BACK             "
    );
}

static bool ui_wifi_is_unconfigured() {
    return !runtimeCreds.hasCredentials;
}

static void ui_showNetworkInfo() {
    char l2[21], l3[21], l4[21];

    if (ui_wifi_is_unconfigured()) {
        lcd4(
            "NETWORK INFO      ",
            "NO WIFI CONFIG    ",
            "RUN SETUP AP      ",
            "*=BACK             "
        );
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        lcd4(
            "NETWORK INFO      ",
            "WIFI: NOT CONN    ",
            "CHECK ROUTER      ",
            "*=BACK             "
        );
        return;
    }

    IPAddress ip = WiFi.localIP();
    int rssi = WiFi.RSSI();

    snprintf(l2, 21, "IP:%3d.%3d.%3d.%3d", ip[0], ip[1], ip[2], ip[3]);
    snprintf(l3, 21, "WIFI: CONNECTED");
    snprintf(l4, 21, "RSSI:%4ddBm *=BACK", rssi);

    lcd4(
        "NETWORK INFO      ",
        l2, l3, l4
    );
}

static void ui_showNetFactoryResetConfirm1() {
    lcd4(
        "RESET NETWORK?    ",
        "WIFI/MQTT         ",
        "A: YES            ",
        "B: NO             "
    );
}

static void ui_showNetFactoryResetConfirm2() {
    lcd4(
        "CONFIRM RESET     ",
        "THIS ERASES ALL   ",
        "A: CONFIRM        ",
        "B: CANCEL         "
    );
}

/* ============================================================
 *  PUBLIC: HANDLE KEYPAD INPUT
 * ============================================================ */
void ui_handleKey(char k, double exhaustF, int fanPercent)
{
    if (!k) return;

    uiNeedRedraw = true;

    /* GLOBAL EMBER GUARDIAN RESET HANDLER */
    if (sys.emberGuardianLatched) {
        if (k == '*') {

            sys.emberGuardianActive      = false;
            sys.emberGuardianLatched     = false;
            sys.emberGuardianTimerActive = false;
            sys.emberGuardianStartMs     = 0;

            sys.boostActive  = false;
            sys.boostStartMs = 0;
            sys.burnState    = BURN_IDLE;

            uiState = UI_HOME;
            return;
        }
    }

    /* EXHAUST PROBE FALLBACK ACKNOWLEDGEMENT */
    if (sys.exhaustFallbackActive) {
        if (k == '*') {
            burnengine_resetSensorFault();
            sys.exhaustFallbackActive = false;
            sys.sensorFaultMask &= (uint8_t)~SENSOR_FAULT_EXHAUST;
            uiState = UI_HOME;
        }
        return;
    }

    /* GLOBAL SAFETY LOCKOUT HANDLER */
    if (sys.safetyState != SAFETY_OK) {
        if (k == '*') {
            burnengine_resetSensorFault();
            sys.safetyState = SAFETY_OK;
            sys.sensorFaultMask = 0;
            sys.burnState   = BURN_IDLE;
            uiState         = UI_HOME;
        }
        return;
    }

    /* MAIN UI STATE MACHINE */
    switch (uiState)
    {
        /* HOME SCREEN */
        case UI_HOME:
            switch (k) {
                case 'A': uiState = UI_COMBUSTION_MENU; break;
                case 'B': uiState = UI_BOILER_MENU;     break;
                case 'C': uiState = UI_ENV_MENU;        break;
                case 'D': uiState = UI_SENSORS_MENU;    break;
                default: break;
            }
            break;

        /* COMBUSTION MENU */
        case UI_COMBUSTION_MENU:
            switch (k) {
                case '1':
                    newSetpointValue = "";
                    uiState = UI_SETPOINT;
                    break;

                case '2':
                    uiState = UI_CLAMP_DEADBAND_MENU;
                    break;

                case '3':
                    uiState = UI_DEADZONE_FAN;
                    break;

                case '4':
                    uiState = UI_EMBER_GUARD_MENU;
                    break;

                case '*':
                case '#':
                    uiState = UI_HOME;
                    break;
            }
            break;

        /* DEADZONE FAN SUBMENU */
        case UI_DEADZONE_FAN:
            switch (k) {
                case '1':
                    sys.deadzoneFanMode = 1;
                    eeprom_saveDeadzone(1);
                    uiState = UI_COMBUSTION_MENU;
                    break;

                case '2':
                    sys.deadzoneFanMode = 0;
                    eeprom_saveDeadzone(0);
                    uiState = UI_COMBUSTION_MENU;
                    break;

                case '*':
                case '#':
                    uiState = UI_COMBUSTION_MENU;
                    break;
            }
            break;

        /* CLAMP & DEADBAND SUBMENU */
        case UI_CLAMP_DEADBAND_MENU:
            switch (k) {
                case '1':
                    clampMinEditValue = "";
                    uiState = UI_CLAMP_MIN;
                    break;

                case '2':
                    clampMaxEditValue = "";
                    uiState = UI_CLAMP_MAX;
                    break;

                case '3':
                    deadbandEditValue = "";
                    uiState = UI_DEADBAND;
                    break;

                case '4':
                    boostTimeEditValue = "";
                    uiState = UI_BOOST_TIME;
                    break;

                case '*':
                case '#':
                    uiState = UI_COMBUSTION_MENU;
                    break;
            }
            break;

        /* EXHAUST SETPOINT EDIT */
        case UI_SETPOINT:
            if (k >= '0' && k <= '9') {
                newSetpointValue += k;
            }
            else if (k == '#') {
                if (newSetpointValue.length()) {
                    int v = newSetpointValue.toInt();
                    if (v < 200) v = 200;
                    if (v > 900) v = 900;
                    sys.exhaustSetpoint = v;
                    eeprom_saveSetpoint(v);
                }
                newSetpointValue = "";
                uiState = UI_COMBUSTION_MENU;
            }
            else if (k == '*') {
                newSetpointValue = "";
                uiState = UI_COMBUSTION_MENU;
            }
            break;

        /* CLAMP MIN EDIT */
        case UI_CLAMP_MIN:
            if (k >= '0' && k <= '9') {
                clampMinEditValue += k;
            }
            else if (k == '#') {
                if (clampMinEditValue.length()) {
                    int v = clampMinEditValue.toInt();
                    if (v < 0) v = 0;
                    if (v > 100) v = 100;
                    sys.clampMinPercent = v;
                    eeprom_saveClampMin(v);
                }
                clampMinEditValue = "";
                uiState = UI_CLAMP_DEADBAND_MENU;
            }
            else if (k == '*') {
                clampMinEditValue = "";
                uiState = UI_CLAMP_DEADBAND_MENU;
            }
            break;

        /* CLAMP MAX EDIT */
        case UI_CLAMP_MAX:
            if (k >= '0' && k <= '9') {
                clampMaxEditValue += k;
            }
            else if (k == '#') {
                if (clampMaxEditValue.length()) {
                    int v = clampMaxEditValue.toInt();
                    if (v < 0) v = 0;
                    if (v > 100) v = 100;
                    sys.clampMaxPercent = v;
                    eeprom_saveClampMax(v);
                }
                clampMaxEditValue = "";
                uiState = UI_CLAMP_DEADBAND_MENU;
            }
            else if (k == '*') {
                clampMaxEditValue = "";
                uiState = UI_CLAMP_DEADBAND_MENU;
            }
            break;

        /* DEADBAND EDIT */
        case UI_DEADBAND:
            if (k >= '0' && k <= '9') {
                deadbandEditValue += k;
            }
            else if (k == '#') {
                if (deadbandEditValue.length()) {
                    int v = deadbandEditValue.toInt();
                    if (v < 1) v = 1;
                    if (v > 100) v = 100;
                    sys.deadbandF = v;
                }
                deadbandEditValue = "";
                uiState = UI_CLAMP_DEADBAND_MENU;
            }
            else if (k == '*') {
                deadbandEditValue = "";
                uiState = UI_CLAMP_DEADBAND_MENU;
            }
            break;

        /* BOOST TIME EDIT */
        case UI_BOOST_TIME:
            if (k >= '0' && k <= '9') {
                boostTimeEditValue += k;
            }
            else if (k == '#') {
                if (boostTimeEditValue.length()) {
                    int v = boostTimeEditValue.toInt();
                    if (v < 0) v = 0;
                    if (v > 600) v = 600;

                    sys.boostTimeSeconds = v;
                    eeprom_saveBoostTime(sys.boostTimeSeconds);
                }

                boostTimeEditValue = "";
                uiState = UI_CLAMP_DEADBAND_MENU;
            }
            else if (k == '*') {
                boostTimeEditValue = "";
                uiState = UI_CLAMP_DEADBAND_MENU;
            }
            break;

        /* EMBER GUARDIAN MENU */
        case UI_EMBER_GUARD_MENU:
            switch (k) {
                case '1':
                    emberGuardianEditValue = "";
                    uiState = UI_EMBER_GUARD_TIMER;
                    break;

                case '2':
                    flueLowEditValue = "";
                    uiState = UI_FLUE_LOW;
                    break;

                case '3':
                    flueRecEditValue = "";
                    uiState = UI_FLUE_REC;
                    break;

                case '*':
                case '#':
                    uiState = UI_COMBUSTION_MENU;
                    break;
            }
            break;

        /* EMBER GUARD TIMER EDIT */
        case UI_EMBER_GUARD_TIMER:
            if (k >= '0' && k <= '9') {
                emberGuardianEditValue += k;
            }
            else if (k == '#') {
                if (emberGuardianEditValue.length()) {
                    int v = emberGuardianEditValue.toInt();
                    if (v < 1) v = 1;
                    if (v > 120) v = 120;
                    sys.emberGuardianTimerMinutes = v;
                    eeprom_saveEmberGuardianMinutes(v);
                }
                emberGuardianEditValue = "";
                uiState = UI_EMBER_GUARD_MENU;
            }
            else if (k == '*') {
                emberGuardianEditValue = "";
                uiState = UI_EMBER_GUARD_MENU;
            }
            break;

        /* FLUE LOW EDIT */
        case UI_FLUE_LOW:
            if (k >= '0' && k <= '9') {
                flueLowEditValue += k;
            }
            else if (k == '#') {
                if (flueLowEditValue.length()) {
                    int v = flueLowEditValue.toInt();
                    if (v < 50) v = 50;
                    if (v > 500) v = 500;
                    sys.flueLowThreshold = v;
                    eeprom_saveFlueLow(v);
                }
                flueLowEditValue = "";
                uiState = UI_EMBER_GUARD_MENU;
            }
            else if (k == '*') {
                flueLowEditValue = "";
                uiState = UI_EMBER_GUARD_MENU;
            }
            break;

        /* FLUE RECOVERY EDIT */
        case UI_FLUE_REC:
            if (k >= '0' && k <= '9') {
                flueRecEditValue += k;
            }
            else if (k == '#') {
                if (flueRecEditValue.length()) {
                    int v = flueRecEditValue.toInt();
                    if (v < 50) v = 50;
                    if (v > 500) v = 500;
                    sys.flueRecoveryThreshold = v;
                    eeprom_saveFlueRecovery(v);
                }
                flueRecEditValue = "";
                uiState = UI_EMBER_GUARD_MENU;
            }
            else if (k == '*') {
                flueRecEditValue = "";
                uiState = UI_EMBER_GUARD_MENU;
            }
            break;

        /* BOILER CONTROL MENU */
        case UI_BOILER_MENU:
            switch (k) {
                case '1':
                    uiState = UI_RUNMODE;
                    break;

                case '2':
                    tankLowEditValue = "";
                    uiState = UI_TANK_LOW;
                    break;

                case '3':
                    tankHighEditValue = "";
                    uiState = UI_TANK_HIGH;
                    break;

                case '4':
                    uiState = UI_SAFETY_STATUS;
                    break;

                case '*':
                case '#':
                    uiState = UI_HOME;
                    break;
            }
            break;

        /* RUN MODE MENU */
        case UI_RUNMODE:
            switch (k) {
                case '1':
                    uiState = UI_RUNMODE_CONFIRM_CONTINUOUS;
                    uiNeedRedraw = true;
                    break;

                case '2':
                    sys.controlMode = RUNMODE_AUTO_TANK;
                    eeprom_saveRunMode(RUNMODE_AUTO_TANK);
                    uiState = UI_RUNMODE;
                    uiNeedRedraw = true;
                    break;

                case '*':
                case '#':
                    uiState = UI_BOILER_MENU;
                    uiNeedRedraw = true;
                    break;
            }
            break;

        /* CONFIRM CONTINUOUS MODE */
        case UI_RUNMODE_CONFIRM_CONTINUOUS:
            switch (k) {
                case 'A':
                    sys.controlMode = RUNMODE_CONTINUOUS;
                    eeprom_saveRunMode(RUNMODE_CONTINUOUS);
                    burnengine_startBoost();
                    uiState = UI_RUNMODE;
                    uiNeedRedraw = true;
                    break;

                case 'B':
                case '*':
                    uiState = UI_RUNMODE;
                    uiNeedRedraw = true;
                    break;
            }
            break;

        /* TANK LOW EDIT */
        case UI_TANK_LOW:
            if (k >= '0' && k <= '9') {
                tankLowEditValue += k;
            }
            else if (k == '#') {
                if (tankLowEditValue.length()) {
                    int v = tankLowEditValue.toInt();
                    if (v < 60) v = 60;
                    if (v > 200) v = 200;
                    sys.tankLowSetpointF = v;
                    eeprom_saveTankLow(v);
                }
                tankLowEditValue = "";
                uiState = UI_BOILER_MENU;
            }
            else if (k == '*') {
                tankLowEditValue = "";
                uiState = UI_BOILER_MENU;
            }
            break;

        /* TANK HIGH EDIT */
        case UI_TANK_HIGH:
            if (k >= '0' && k <= '9') {
                tankHighEditValue += k;
            }
            else if (k == '#') {
                if (tankHighEditValue.length()) {
                    int v = tankHighEditValue.toInt();
                    if (v < 80) v = 80;
                    if (v > 210) v = 210;
                    sys.tankHighSetpointF = v;
                    eeprom_saveTankHigh(v);
                }
                tankHighEditValue = "";
                uiState = UI_BOILER_MENU;
            }
            else if (k == '*') {
                tankHighEditValue = "";
                uiState = UI_BOILER_MENU;
            }
            break;

        /* SAFETY STATUS */
        case UI_SAFETY_STATUS:
            if (k == '*' || k == '#') {
                uiState = UI_BOILER_MENU;
            }
            break;

        /* ENVIRONMENT MENU */
        case UI_ENV_MENU:
            switch (k) {
                case '1': uiState = UI_SEASONS_MENU; break;
                case '2': uiState = UI_ENV_LOCKOUT;  break;
                case '*':
                case '#':
                    uiState = UI_HOME;
                    break;
            }
            break;

        /* SEASONS MENU */
        case UI_SEASONS_MENU:
            switch (k) {
                case '1': uiEditSeason = ENV_SEASON_SUMMER;      uiState = UI_SEASON_DETAIL_MENU; break;
                case '2': uiEditSeason = ENV_SEASON_SPRING_FALL; uiState = UI_SEASON_DETAIL_MENU; break;
                case '3': uiEditSeason = ENV_SEASON_WINTER;      uiState = UI_SEASON_DETAIL_MENU; break;
                case '4': uiEditSeason = ENV_SEASON_EXTREME;     uiState = UI_SEASON_DETAIL_MENU; break;

                case '*':
                case '#':
                    uiState = UI_ENV_MENU;
                    break;
            }
            break;

/* ============================================================
 *  SEASON DETAIL MENU (PAGE 1)
 * ============================================================ */
case UI_SEASON_DETAIL_MENU:
    switch (k) {

        case '1':     // Edit season start temperature
            envSeasonEditValue = "";
            uiState = UI_SEASON_EDIT_START;
            break;

        case '2':     // Edit hysteresis buffer
            envSeasonEditValue = "";
            uiState = UI_SEASON_EDIT_BUFFER;
            break;

        case '3':     // Edit exhaust setpoint
            envSetpointEditValue = "";
            uiState = UI_SEASON_EDIT_SETPOINT;
            break;

        case '#':     // Go to PAGE 2
            uiState = UI_SEASON_DETAIL_MENU_2;
            break;

        case '*':     // Back to seasons list
            uiState = UI_SEASONS_MENU;
            break;
    }
    break;

/* ============================================================
 *  SEASON DETAIL MENU (PAGE 2)
 * ============================================================ */
case UI_SEASON_DETAIL_MENU_2:
    switch (k) {

        case '4':     // Edit Tank HIGH
            envSeasonEditValue = "";
            uiState = UI_SEASON_EDIT_TANKHIGH;
            break;

        case '5':     // Edit Tank LOW
            envSeasonEditValue = "";
            uiState = UI_SEASON_EDIT_TANKLOW;
            break;

        case '6':     // Edit ClampMax
            envSeasonEditValue = "";
            uiState = UI_SEASON_EDIT_CLAMPMAX;
            break;

        case '*':     // Back to PAGE 1
            uiState = UI_SEASON_DETAIL_MENU;
            break;
    }
    break;

/* ============================================================
 *  SEASON START TEMP EDIT
 * ============================================================ */
case UI_SEASON_EDIT_START:
    if (k >= '0' && k <= '9') {
        envSeasonEditValue += k;
    }
    else if (k == '#') {
        if (envSeasonEditValue.length()) {
            int v = envSeasonEditValue.toInt();
            *ui_getSeasonStartPtr(uiEditSeason) = v;
            eeprom_saveEnvSeasonStarts();
        }
        envSeasonEditValue = "";
        uiState = UI_SEASON_DETAIL_MENU;
    }
    else if (k == '*') {
        envSeasonEditValue = "";
        uiState = UI_SEASON_DETAIL_MENU;
    }
    break;

/* ============================================================
 *  SEASON HYSTERESIS EDIT
 * ============================================================ */
case UI_SEASON_EDIT_BUFFER:
    if (k >= '0' && k <= '9') {
        envSeasonEditValue += k;
    }
    else if (k == '#') {
        if (envSeasonEditValue.length()) {
            int v = envSeasonEditValue.toInt();
            *ui_getSeasonBufferPtr(uiEditSeason) = v;
            eeprom_saveEnvSeasonHyst();
        }
        envSeasonEditValue = "";
        uiState = UI_SEASON_DETAIL_MENU;
    }
    else if (k == '*') {
        envSeasonEditValue = "";
        uiState = UI_SEASON_DETAIL_MENU;
    }
    break;

/* ============================================================
 *  SEASON EXHAUST SETPOINT EDIT
 * ============================================================ */
case UI_SEASON_EDIT_SETPOINT:
    if (k >= '0' && k <= '9') {
        envSetpointEditValue += k;
    }
    else if (k == '#') {
        if (envSetpointEditValue.length()) {
            int v = envSetpointEditValue.toInt();
            *ui_getSeasonSetpointPtr(uiEditSeason) = v;
            eeprom_saveEnvSeasonSetpoints();
        }
        envSetpointEditValue = "";
        uiState = UI_SEASON_DETAIL_MENU;
    }
    else if (k == '*') {
        envSetpointEditValue = "";
        uiState = UI_SEASON_DETAIL_MENU;
    }
    break;

/* ============================================================
 *  SEASON TANK HIGH EDIT
 * ============================================================ */
case UI_SEASON_EDIT_TANKHIGH:
    if (k >= '0' && k <= '9') {
        envSeasonEditValue += k;
    }
    else if (k == '#') {
        if (envSeasonEditValue.length()) {
            int v = envSeasonEditValue.toInt();
            *ui_getSeasonTankHighPtr(uiEditSeason) = v;
            eeprom_saveEnvSeasonTankValues();
        }
        envSeasonEditValue = "";
        uiState = UI_SEASON_DETAIL_MENU_2;
    }
    else if (k == '*') {
        envSeasonEditValue = "";
        uiState = UI_SEASON_DETAIL_MENU_2;
    }
    break;

/* ============================================================
 *  SEASON TANK LOW EDIT
 * ============================================================ */
case UI_SEASON_EDIT_TANKLOW:
    if (k >= '0' && k <= '9') {
        envSeasonEditValue += k;
    }
    else if (k == '#') {
        if (envSeasonEditValue.length()) {
            int v = envSeasonEditValue.toInt();
            *ui_getSeasonTankLowPtr(uiEditSeason) = v;
            eeprom_saveEnvSeasonTankValues();
        }
        envSeasonEditValue = "";
        uiState = UI_SEASON_DETAIL_MENU_2;
    }
    else if (k == '*') {
        envSeasonEditValue = "";
        uiState = UI_SEASON_DETAIL_MENU_2;
    }
    break;

/* ============================================================
 *  SEASON CLAMPMAX EDIT
 * ============================================================ */
case UI_SEASON_EDIT_CLAMPMAX:
    if (k >= '0' && k <= '9') {
        envSeasonEditValue += k;
    }
    else if (k == '#') {
        if (envSeasonEditValue.length()) {
            int v = envSeasonEditValue.toInt();
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            *ui_getSeasonClampMaxPtr(uiEditSeason) = (uint8_t)v;
            eeprom_saveEnvSeasonClampValues();
        }
        envSeasonEditValue = "";
        uiState = UI_SEASON_DETAIL_MENU_2;
    }
    else if (k == '*') {
        envSeasonEditValue = "";
        uiState = UI_SEASON_DETAIL_MENU_2;
    }
    break;


        /* LOCKOUT / MODE */
        case UI_ENV_LOCKOUT:
            switch (k) {
                case '1': uiState = UI_ENV_MODE;          break;
                case '2': uiState = UI_ENV_AUTOSEASON;    break;
                case '3':
                    envLockoutEditValue = "";
                    uiState = UI_ENV_LOCKOUT_HOURS;
                    break;

                case '*':
                case '#':
                    uiState = UI_ENV_MENU;
                    break;
            }
            break;

        /* ENV MODE */
        case UI_ENV_MODE:
            switch (k) {
                case '1':
                    sys.envSeasonMode = 0;
                    eeprom_saveEnvSeasonMode(sys.envSeasonMode);
                    break;

                case '2':
                    sys.envSeasonMode = 1;
                    eeprom_saveEnvSeasonMode(sys.envSeasonMode);
                    break;

                case '3':
                    sys.envSeasonMode = 2;
                    eeprom_saveEnvSeasonMode(sys.envSeasonMode);
                    break;

                case '*':
                case '#':
                    uiState = UI_ENV_LOCKOUT;
                    break;
            }
            break;

        /* AUTO-SEASON */
        case UI_ENV_AUTOSEASON:
            switch (k) {
                case '*':
                    sys.envAutoSeasonEnabled = !sys.envAutoSeasonEnabled;
                    eeprom_saveEnvAutoSeason(sys.envAutoSeasonEnabled);
                    break;

                case '#':
                    uiState = UI_ENV_LOCKOUT;
                    break;
            }
            break;

        /* LOCKOUT HOURS EDIT */
        case UI_ENV_LOCKOUT_HOURS:
            if (k >= '0' && k <= '9') {
                envLockoutEditValue += k;
            }
            else if (k == '#') {
                if (envLockoutEditValue.length()) {
                    int v = envLockoutEditValue.toInt();
                    if (v < 0) v = 0;
                    if (v > 99) v = 99;
                    sys.envModeLockoutSec = (uint32_t)v * 3600UL;
                    eeprom_saveEnvLockoutHours((uint8_t)v);
                }
                envLockoutEditValue = "";
                uiState = UI_ENV_LOCKOUT;
            }
            else if (k == '*') {
                envLockoutEditValue = "";
                uiState = UI_ENV_LOCKOUT;
            }
            break;

        /* SENSORS & NETWORK */
        case UI_SENSORS_MENU:
            switch (k) {
                case '1':
                    selectedRole = 0;
                    selectedPhys = sys.probeRoleMap[selectedRole];
                    uiState = UI_WATER_PROBE_MENU;
                    break;

                case '2':
                    uiState = UI_BME_SCREEN;
                    break;

                case '3':
                    uiState = UI_NETWORKING;
                    break;

                case '*':
                case '#':
                    uiState = UI_HOME;
                    break;
            }
            break;

        /* WATER PROBE ROLE ASSIGNMENT */
        case UI_WATER_PROBE_MENU:
            switch (k) {
                case '0': case '1': case '2': case '3':
                case '4': case '5': case '6': case '7':
                    selectedRole = (uint8_t)(k - '0');
                    if (selectedRole >= PROBE_ROLE_COUNT)
                        selectedRole = PROBE_ROLE_COUNT - 1;
                    selectedPhys = sys.probeRoleMap[selectedRole];
                    break;

                case 'A':
                    if (selectedPhys > 0)
                        selectedPhys--;
                    break;

                case 'B':
                    if (selectedPhys + 1 < sys.waterProbeCount)
                        selectedPhys++;
                    break;

                case '#':
                    sys.probeRoleMap[selectedRole] = selectedPhys;
                    eeprom_saveProbeRoles();
                    break;

                case '*':
                    uiState = UI_SENSORS_MENU;
                    break;
            }
            break;

        /* BME280 SCREEN */
        case UI_BME_SCREEN:
            if (k == '*' || k == '#') {
                uiState = UI_SENSORS_MENU;
            }
            break;

        /* NETWORKING MENU */
        case UI_NETWORKING:
            switch (k) {
                case '1':
                    uiState = UI_NETWORK_INFO;
                    break;

                case '2':
                    uiState = UI_NET_FACTORY_RESET_CONFIRM_1;
                    break;

                case '*':
                case '#':
                    uiState = UI_SENSORS_MENU;
                    break;
            }
            break;

        /* NETWORK INFO */
        case UI_NETWORK_INFO:
            if (k == '*' || k == '#') {
                uiState = UI_NETWORKING;
            }
            break;

        /* NETWORK RESET CONFIRMATION (STEP 1) */
        case UI_NET_FACTORY_RESET_CONFIRM_1:
            switch (k) {
                case 'A':
                    uiState = UI_NET_FACTORY_RESET_CONFIRM_2;
                    break;

                case 'B':
                case '*':
                case '#':
                    uiState = UI_NETWORKING;
                    break;
            }
            break;

        /* NETWORK RESET CONFIRMATION (STEP 2) */
        case UI_NET_FACTORY_RESET_CONFIRM_2:
            switch (k) {
                case 'A':
                    wifi_prov_factoryReset();
                    uiState = UI_NETWORKING;
                    break;

                case 'B':
                case '*':
                case '#':
                    uiState = UI_NETWORKING;
                    break;
            }
            break;
    }
}

/* ============================================================
 *  PUBLIC: RENDER SCREEN BASED ON STATE
 * ============================================================ */
void ui_showScreen(UIState st, double exhaustF, int fanPercent)
{
    switch (st)
    {
        /* HOME */
        case UI_HOME:
            ui_showHome(exhaustF, fanPercent);
            break;

        /* COMBUSTION */
        case UI_COMBUSTION_MENU:        ui_showCombustionMenu(); break;
        case UI_SETPOINT:               ui_showSetpoint(); break;
        case UI_CLAMP_DEADBAND_MENU:    ui_showClampDeadbandMenu(); break;
        case UI_CLAMP_MIN:              ui_showClampMin(); break;
        case UI_CLAMP_MAX:              ui_showClampMax(); break;
        case UI_DEADBAND:               ui_showDeadband(); break;
        case UI_DEADZONE_FAN:           ui_showDeadzoneFanMenu(); break;
        case UI_EMBER_GUARD_MENU:       ui_showEmberGuardianMenu(); break;
        case UI_EMBER_GUARD_TIMER:      ui_showEmberGuardianTimer(); break;
        case UI_FLUE_LOW:               ui_showFlueLow(); break;
        case UI_FLUE_REC:               ui_showFlueRec(); break;
        case UI_BOOST_TIME:             ui_showBoostTime(); break;

        /* BOILER CONTROL */
        case UI_BOILER_MENU:            ui_showBoilerMenu(); break;
        case UI_RUNMODE:                ui_showRunMode(); break;
        case UI_TANK_LOW:               ui_showTankLow(); break;
        case UI_TANK_HIGH:              ui_showTankHigh(); break;
        case UI_SAFETY_STATUS:          ui_showSafetyStatus(); break;
        case UI_RUNMODE_CONFIRM_CONTINUOUS:
            ui_showRunModeConfirmContinuous();
            break;

        /* ENVIRONMENT */
        case UI_ENV_MENU:               ui_showEnvMenu(); break;
        case UI_SEASONS_MENU:           ui_showSeasonsMenu(); break;
        case UI_SEASON_DETAIL_MENU:     ui_showSeasonDetailMenu(); break;
        case UI_SEASON_DETAIL_MENU_2:   ui_showSeasonDetailMenu2(); break;
        case UI_SEASON_EDIT_START:      ui_showSeasonEditStart(); break;
        case UI_SEASON_EDIT_BUFFER:     ui_showSeasonEditBuffer(); break;
        case UI_SEASON_EDIT_SETPOINT:   ui_showSeasonEditSetpoint(); break;
        case UI_SEASON_EDIT_TANKHIGH:   ui_showSeasonEditTankHigh(); break;
        case UI_SEASON_EDIT_TANKLOW:    ui_showSeasonEditTankLow(); break;
        case UI_SEASON_EDIT_CLAMPMAX:   ui_showSeasonEditClampMax(); break;
        case UI_ENV_LOCKOUT:            ui_showEnvLockoutMenu(); break;
        case UI_ENV_MODE:               ui_showEnvMode(); break;
        case UI_ENV_AUTOSEASON:         ui_showEnvAutoSeason(); break;
        case UI_ENV_LOCKOUT_HOURS:      ui_showEnvLockoutHours(); break;

        /* SENSORS & NETWORK */
        case UI_SENSORS_MENU:                   ui_showSensorsMenu(); break;
        case UI_WATER_PROBE_MENU:               ui_showProbeRoleScreen(selectedRole, selectedPhys); break;
        case UI_BME_SCREEN:                     ui_showBME(); break;
        case UI_NETWORKING:                     ui_showNetworkingMenu(); break;
        case UI_NETWORK_INFO:                   ui_showNetworkInfo(); break;
        case UI_NET_FACTORY_RESET_CONFIRM_1:    ui_showNetFactoryResetConfirm1(); break;
        case UI_NET_FACTORY_RESET_CONFIRM_2:    ui_showNetFactoryResetConfirm2(); break;

        default:
            ui_showHome(exhaustF, fanPercent);
            break;
    }
}
