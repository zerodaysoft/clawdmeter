#pragma once

// Shared, board-agnostic battery-care policy. Reads battery % and PMU
// temperature through the power HAL and gates charging via
// power_hal_set_charging() to extend cell lifespan (hold at ~80%, pause on
// heat). No-op on boards without a battery (BoardCaps.has_battery == false).
// Tunables live in battery_care_cfg.h.

void battery_care_init(void);
void battery_care_tick(void);   // call once per loop; self-rate-limits
