#pragma once

// Battery-care charging policy tunables. Mirrors idle_cfg.h — all knobs live
// here so nothing is hard-coded in battery_care.cpp. The policy is shared and
// board-agnostic; it drives the charger through power_hal_set_charging().
//
// Apple/Samsung-style longevity: charge gently (board sets the current), hold
// at a ceiling well below 100% so the pack doesn't sit at full, and pause
// charging if the PMU runs hot.

// State-of-charge ceiling. Stop charging at/above CEIL, resume below RESUME.
// The gap is hysteresis so charging doesn't chatter around the threshold.
#define BATT_CARE_CEIL_PCT       80
#define BATT_CARE_RESUME_PCT     75

// Thermal guard (AXP2101 *die* temperature, °C — not the cell). The die idles
// around 40–45 °C on this board (AMOLED + ESP32 heat) and the chip's own
// regulation only kicks in at 60–120 °C, so the pause must sit well above
// idle or it traps charging permanently. Pause above PAUSE, resume below
// RESUME. Boards that can't measure temp report NAN and the guard is skipped.
#define BATT_CARE_TEMP_PAUSE_C   60.0f
#define BATT_CARE_TEMP_RESUME_C  50.0f

// How often the policy re-evaluates (charging changes slowly; no need to spam
// I2C every loop).
#define BATT_CARE_TICK_MS        2000
