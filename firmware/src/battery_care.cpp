#include "battery_care.h"
#include "battery_care_cfg.h"
#include "hal/power_hal.h"
#include "hal/board_caps.h"
#include <Arduino.h>
#include <math.h>

// Tracks whether we currently allow charging. Starts true: the board enables
// the charger at init, and this policy only ever tightens that.
static bool     charge_allowed  = true;
static int      last_current_ma = -1;   // last current we pushed; -1 = unset
static uint32_t last_tick_ms    = 0;

// Push a charge current only when it changes, so we don't spam I2C every tick.
static void apply_current(int ma) {
    if (ma == last_current_ma) return;
    last_current_ma = ma;
    power_hal_set_charge_current_ma((uint16_t)ma);
}

void battery_care_init(void) {
    charge_allowed  = true;
    last_current_ma = -1;
    last_tick_ms    = 0;
}

void battery_care_tick(void) {
    if (!board_caps().has_battery) return;

    uint32_t now = millis();
    if (now - last_tick_ms < BATT_CARE_TICK_MS) return;
    last_tick_ms = now;

    // Off the cable there's nothing to charge. Make sure the charger is armed
    // for the next plug-in, then bail (don't fight a discharging pack).
    if (!power_hal_is_vbus_in()) {
        if (!charge_allowed) {
            charge_allowed = true;
            power_hal_set_charging(true);
        }
        return;
    }

    int   pct  = power_hal_battery_pct();
    float temp = power_hal_temperature_c();

    // No cell present (running straight off USB): the gauge reports -1. There's
    // nothing to charge or stage, so leave the charger armed for a future
    // insertion and bail — don't churn I2C every tick on a phantom pack.
    if (pct < 0) {
        if (!charge_allowed) {
            charge_allowed = true;
            power_hal_set_charging(true);
        }
        return;
    }

    // Default: hold current state (hysteresis dead-bands keep it there).
    bool want = charge_allowed;

    if (!isnan(temp) && temp >= BATT_CARE_TEMP_PAUSE_C) {
        want = false;                              // too hot — pause
    } else if (isnan(temp) || temp <= BATT_CARE_TEMP_RESUME_C) {
        // Cool enough (or no sensor) — decide on state of charge.
        if (pct >= BATT_CARE_CEIL_PCT)        want = false;  // hold at ceiling
        else if (pct <= BATT_CARE_RESUME_PCT) want = true;   // top up
    }

    // Step the current down while charging as the pack fills: fast for the bulk,
    // taper past the first knee, trickle near full. The mA per stage are this
    // board's cell-sized values from BoardCaps.
    if (want) {
        const BoardCaps& c = board_caps();
        uint16_t ma = (pct >= BATT_CARE_TRICKLE_PCT) ? c.chg_trickle_ma
                    : (pct >= BATT_CARE_TAPER_PCT)   ? c.chg_taper_ma
                    :                                  c.chg_fast_ma;
        apply_current(ma);
    }

    if (want != charge_allowed) {
        charge_allowed = want;
        power_hal_set_charging(want);
        Serial.printf("battery_care: charging %s (pct=%d temp=%.1fC curr=%dmA)\n",
            want ? "ON" : "HOLD", pct, temp, last_current_ma);
    }
}
