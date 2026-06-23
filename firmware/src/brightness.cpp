#include "brightness.h"
#include "idle.h"
#include <Preferences.h>
#include <Arduino.h>

// Named PWM levels for the four-step ramp, dimmest → brightest. BRT_HIGH (200)
// matches the prior hard-coded DISPLAY_DEFAULT_BRIGHTNESS, so cycling is purely
// additive.
#define BRT_LOW   64    // on battery, low charge
#define BRT_MID  128    // on battery, healthy charge
#define BRT_HIGH 200    // plugged in (also the boot default)
#define BRT_MAX  255    // manual-only top of the ramp

// The ramp the PWR button cycles through.
static const uint8_t LEVELS[] = {BRT_LOW, BRT_MID, BRT_HIGH, BRT_MAX};
#define LEVELS_COUNT (sizeof(LEVELS) / sizeof(LEVELS[0]))

// Indices into LEVELS, used as power-event auto targets. BRT_MAX (index 3) is
// intentionally NOT an auto target — it's reachable only by manually cycling
// with the PWR button.
#define IDX_LOW     0    // on battery, low charge
#define IDX_MID     1    // on battery, healthy charge
#define IDX_HIGH    2    // plugged in
#define DEFAULT_IDX IDX_HIGH
#define LOW_BATT_PCT 20  // at/below this (on battery) → low

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
//   plugged in            → high
//   unplugged, batt > 20% → mid
//   unplugged, batt ≤ 20% → low
// BRT_MAX (255) is never an auto target — it's manual-only. Auto changes
// aren't persisted (see apply_idx) — the saved level is the manual baseline.
// Call once per loop with the current power state.
void brightness_handle_power(bool vbus_in, int battery_pct) {
    static int8_t last_vbus = -1;   // -1 = uninitialised → establish on first call
    static bool   last_low  = false;

    bool low = (battery_pct >= 0 && battery_pct <= LOW_BATT_PCT);

    if ((int8_t)vbus_in != last_vbus) {           // plug / unplug edge
        if (vbus_in) apply_idx(IDX_HIGH, false);
        else         apply_idx(low ? IDX_LOW : IDX_MID, false);
        last_vbus = vbus_in;
        last_low  = low;
        return;
    }

    // On battery: drop to low the moment we cross down into the low band.
    if (!vbus_in && low && !last_low) {
        apply_idx(IDX_LOW, false);
    }
    last_low = low;
}
