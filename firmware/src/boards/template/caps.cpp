#include "../../hal/board_caps.h"
#include "board.h"

static const BoardCaps caps = {
    .name = BOARD_NAME,
    .width = LCD_WIDTH,
    .height = LCD_HEIGHT,
    .button_count = (uint8_t)(1 + BOARD_HAS_SECONDARY_BUTTON),
    .has_rotation = (bool)BOARD_HAS_ROTATION,
    .has_battery  = (bool)BOARD_HAS_BATTERY,
    .has_imu      = (bool)BOARD_HAS_IMU,
    // Charge currents for the battery-care step-down (mA), sized to your cell.
    // Leave 0 if the board has no controllable charger. Typical 1000mAh LiPo:
    // fast 500 (0.5C), taper 200 (0.2C), trickle 100 (0.1C).
    .chg_fast_ma    = 0,
    .chg_taper_ma   = 0,
    .chg_trickle_ma = 0,
};

const BoardCaps& board_caps(void) { return caps; }
