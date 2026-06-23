#include "../../hal/power_hal.h"
#include "../../hal/board_caps.h"
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <XPowersLib.h>

// AXP2101 PMU — identical chip and protocol to the S3 variant.

#define BATTERY_POLL_MS  2000
#define CHARGING_POLL_MS 500
#define PWR_POLL_MS      50

// The PMU instance is owned by board_init.cpp on this board — it has to
// come up before display_hal_init() to enable the LCD power rails. We
// reuse the same handle here for battery polling and PKEY IRQ wiring.
extern XPowersPMU board_pmu;
#define pmu board_pmu

static int      cached_pct        = -1;
static bool     cached_charging   = false;
static bool     cached_vbus       = false;
static bool     pwr_pressed_flag  = false;
static bool     pwr_long_flag     = false;
static bool     pwr_released_flag = false;
static uint32_t last_battery_ms   = 0;
static uint32_t last_charging_ms  = 0;
static uint32_t last_pwr_ms       = 0;

void power_hal_init(void) {
    // pmu.begin() already ran in board_init(); just configure battery +
    // IRQ wiring here.
    pmu.enableBattDetection();
    pmu.enableBattVoltageMeasure();

    // Mirror the Waveshare XiaoZhi BSP charging config so the on-chip
    // fuel gauge has the right reference numbers. Without a set charge current
    // the AXP2101 sits at its ~1mA default and never charges a connected cell;
    // getBatteryPercent() also reads near-empty. 4.2V target + gentle current +
    // die-temp measurement for the shared battery_care policy, which steps the
    // current down from this bootstrap as the pack fills.
    pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
    power_hal_set_charge_current_ma(board_caps().chg_fast_ma);
    pmu.enableTemperatureMeasure();
    pmu.enableCellbatteryCharge();

    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.clearIrqStatus();
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ
                | XPOWERS_AXP2101_PKEY_LONG_IRQ
                | XPOWERS_AXP2101_PKEY_POSITIVE_IRQ);

    // Default press-off is 6s, only 2s after the pair gesture arms at ~3s and
    // disarms at ~6s. Bump to 8s so a slightly-too-long hold doesn't shut the
    // device down mid-gesture.
    pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_8S);

    cached_charging = pmu.isCharging();
    cached_vbus     = pmu.isVbusIn();
    cached_pct = pmu.getBatteryPercent();
}

void power_hal_tick(void) {
    uint32_t now = millis(); 

    if (now - last_charging_ms >= CHARGING_POLL_MS) {
        last_charging_ms = now;
        cached_charging = pmu.isCharging();
        cached_vbus     = pmu.isVbusIn();
    }
    if (now - last_battery_ms >= BATTERY_POLL_MS) {
        last_battery_ms = now;
        cached_pct = pmu.getBatteryPercent();
    }
    if (now - last_pwr_ms >= PWR_POLL_MS) {
        last_pwr_ms = now;
        pmu.getIrqStatus();
        if (pmu.isPekeyShortPressIrq())    pwr_pressed_flag  = true;
        if (pmu.isPekeyLongPressIrq())     pwr_long_flag     = true;
        if (pmu.isPekeyPositiveIrq())      pwr_released_flag = true;
        pmu.clearIrqStatus();
    }
}

int  power_hal_battery_pct(void) { return cached_pct; }
bool power_hal_is_charging(void) { return cached_charging; }
bool power_hal_is_vbus_in(void)  { return cached_vbus; }

bool power_hal_pwr_pressed(void) {
    if (pwr_pressed_flag) { pwr_pressed_flag = false; return true; }
    return false;
}

bool power_hal_pwr_long_pressed(void) {
    if (pwr_long_flag) { pwr_long_flag = false; return true; }
    return false;
}

bool power_hal_pwr_released(void) {
    if (pwr_released_flag) { pwr_released_flag = false; return true; }
    return false;
}

void power_hal_set_charging(bool enable) {
    if (enable) pmu.enableCellbatteryCharge();
    else        pmu.disableCellbatteryCharge();
}

void power_hal_set_charge_current_ma(uint16_t ma) {
    // AXP2101 constant-charge steps (mA → enum), ascending. Pick the largest
    // step that doesn't exceed the request so we never charge harder than asked.
    static const struct { uint16_t ma; uint8_t cur; } STEPS[] = {
        {100, XPOWERS_AXP2101_CHG_CUR_100MA}, {125, XPOWERS_AXP2101_CHG_CUR_125MA},
        {150, XPOWERS_AXP2101_CHG_CUR_150MA}, {175, XPOWERS_AXP2101_CHG_CUR_175MA},
        {200, XPOWERS_AXP2101_CHG_CUR_200MA}, {300, XPOWERS_AXP2101_CHG_CUR_300MA},
        {400, XPOWERS_AXP2101_CHG_CUR_400MA}, {500, XPOWERS_AXP2101_CHG_CUR_500MA},
        {600, XPOWERS_AXP2101_CHG_CUR_600MA}, {700, XPOWERS_AXP2101_CHG_CUR_700MA},
        {800, XPOWERS_AXP2101_CHG_CUR_800MA}, {900, XPOWERS_AXP2101_CHG_CUR_900MA},
        {1000, XPOWERS_AXP2101_CHG_CUR_1000MA},
    };
    uint8_t pick = STEPS[0].cur;   // floor at the lowest supported step
    for (auto &s : STEPS) { if (s.ma <= ma) pick = s.cur; else break; }
    pmu.setChargerConstantCurr(pick);
}

float power_hal_temperature_c(void) {
    return pmu.getTemperature();
}
