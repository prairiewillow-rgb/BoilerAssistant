#ifndef CONFIG_VALIDATION_H
#define CONFIG_VALIDATION_H

inline bool validExhaustSetpoint(int value) {
    return value >= 200 && value <= 900;
}

inline bool validBoostTime(int value) {
    return value >= 5 && value <= 600;
}

inline bool validDeadband(int value) {
    return value >= 1 && value <= 100;
}

inline bool validFanClamp(int value) {
    return value >= 0 && value <= 100;
}

inline bool validGuardianMinutes(int value) {
    return value >= 1 && value <= 120;
}

inline bool validFlueThreshold(int value) {
    return value >= 50 && value <= 500;
}

inline bool validTankSetpoint(int value) {
    return value >= 40 && value <= 240;
}

inline bool validSeasonStart(int value) {
    return value >= -100 && value <= 150;
}

inline bool validSeasonHysteresis(int value) {
    return value >= 0 && value <= 50;
}

inline bool validLockoutHours(int value) {
    return value >= 0 && value <= 99;
}

#endif