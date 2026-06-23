#include "brightness.h"
#include "idle.h"
#include <Preferences.h>
#include <Arduino.h>

// Four-step ramp. The default (index 2) is 200 — identical to the prior
// hard-coded DISPLAY_DEFAULT_BRIGHTNESS, so cycling is purely additive.
static const uint8_t LEVELS[] = {64, 128, 200, 255};
#define LEVELS_COUNT (sizeof(LEVELS) / sizeof(LEVELS[0]))
#define DEFAULT_IDX  2

// Power-event brightness targets (indices into LEVELS).
#define IDX_MIN      0    // 64  — on battery, low
#define IDX_MID      1    // 128 — on battery, healthy
#define IDX_MAX      3    // 255 — plugged in
#define LOW_BATT_PCT 20   // at/below this (on battery) → min

static uint8_t cur_idx = DEFAULT_IDX;

// Apply a level: route through idle (it owns the panel) and update cur_idx so
// the next manual cycle is relative. Persist only manual changes — auto power
// events leave the saved level (the user's baseline) untouched.
static void apply_idx(uint8_t idx, bool persist) {
    cur_idx = idx;
    if (persist) {
        Preferences prefs;
        prefs.begin("clawdmeter", false);
        prefs.putUChar("brt_idx", cur_idx);
        prefs.end();
    }
    idle_set_awake_brightness(LEVELS[cur_idx]);
}

void brightness_init(void) {
    Preferences prefs;
    prefs.begin("clawdmeter", true);
    uint8_t saved_idx = prefs.getUChar("brt_idx", 0xFF);
    prefs.end();

    if (saved_idx < LEVELS_COUNT) cur_idx = saved_idx;
    idle_set_awake_brightness(LEVELS[cur_idx]);
    Serial.printf("Brightness init: level=%u (idx=%u)\n", LEVELS[cur_idx], cur_idx);
}

void brightness_cycle(void) {
    apply_idx((cur_idx + 1) % LEVELS_COUNT, true);
    Serial.printf("Brightness cycled: level=%u (idx=%u)\n", LEVELS[cur_idx], cur_idx);
}

uint8_t brightness_get(void) {
    return LEVELS[cur_idx];
}

// Event-driven auto brightness. Fires only on transitions so a manual button
// press (brightness_cycle) sticks until the next plug/unplug or low-battery
// crossing:
//   plugged in            → max
//   unplugged, batt > 20% → mid
//   unplugged, batt ≤ 20% → min
// Auto changes aren't persisted (see apply_idx) — the saved level is the
// manual baseline. Call once per loop with the current power state.
void brightness_handle_power(bool vbus_in, int battery_pct) {
    static int8_t last_vbus = -1;   // -1 = uninitialised → establish on first call
    static bool   last_low  = false;

    bool low = (battery_pct >= 0 && battery_pct <= LOW_BATT_PCT);

    if ((int8_t)vbus_in != last_vbus) {           // plug / unplug edge
        if (vbus_in) apply_idx(IDX_MAX, false);
        else         apply_idx(low ? IDX_MIN : IDX_MID, false);
        last_vbus = vbus_in;
        last_low  = low;
        return;
    }

    // On battery: dim to min the moment we cross down into the low band.
    if (!vbus_in && low && !last_low) {
        apply_idx(IDX_MIN, false);
    }
    last_low = low;
}
