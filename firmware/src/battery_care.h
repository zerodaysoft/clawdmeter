#pragma once

// Shared, board-agnostic battery-care policy. Reads battery % and PMU
// temperature through the power HAL to extend cell lifespan: steps the charge
// current down as the pack fills (fast → taper → trickle), stops just shy of
// 100% so it never floats at full, and pauses on heat. No-op on boards without
// a battery (BoardCaps.has_battery == false). SoC thresholds live in
// battery_care_cfg.h; the per-stage currents live in BoardCaps (caps.cpp).

void battery_care_init(void);
void battery_care_tick(void);   // call once per loop; self-rate-limits
