#pragma once
#include <stdint.h>

// User-controlled display brightness, persisted to NVS. The middle (PWR)
// button short-press cycles through the levels via brightness_cycle().
// idle owns the actual panel brightness, so this routes the chosen level
// through idle_set_awake_brightness().
void    brightness_init(void);    // load saved level from NVS and apply
void    brightness_cycle(void);   // advance to next level, save, apply
uint8_t brightness_get(void);     // current PWM level (0..255)

// Event-driven auto brightness from power state (plugged → high; on battery →
// mid, or low when ≤20%). The top level (max) is manual-only and never an auto
// target. Fires only on plug/unplug + low-battery transitions, so a manual
// brightness_cycle() press overrides until the next event. Auto changes are not
// persisted. Call once per loop.
void    brightness_handle_power(bool vbus_in, int battery_pct);
