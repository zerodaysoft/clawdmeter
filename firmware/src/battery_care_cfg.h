#pragma once

// Battery-care charging policy tunables. Mirrors idle_cfg.h — all knobs live
// here so nothing is hard-coded in battery_care.cpp. The policy is shared and
// board-agnostic; it drives the charger through the power HAL.
//
// Longevity without giving up usable capacity: charge fast for the bulk, step
// the current down as the pack fills so the top-up is gentle, stop just shy of
// 100% so the cell never floats at full, and pause charging if the PMU runs
// hot. The per-stage currents are board/cell-specific and live in BoardCaps
// (caps.cpp); the SoC thresholds below are chemistry-level policy, shared.

// State-of-charge ceiling. Stop charging at/above CEIL, resume below RESUME.
// CEIL just shy of 100% avoids holding the cell at full; the gap to RESUME is
// hysteresis so it doesn't micro-cycle around the threshold.
#define BATT_CARE_CEIL_PCT       99
#define BATT_CARE_RESUME_PCT     95

// Current step-down knees (SoC %). Below TAPER → fast; [TAPER, TRICKLE) → taper;
// at/above TRICKLE → trickle. The actual mA per stage come from BoardCaps.
#define BATT_CARE_TAPER_PCT      70
#define BATT_CARE_TRICKLE_PCT    90

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
