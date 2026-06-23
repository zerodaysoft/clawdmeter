#include "../../hal/board_caps.h"
#include "board.h"

static const BoardCaps caps = {
    .name = BOARD_NAME,
    .width = LCD_WIDTH,
    .height = LCD_HEIGHT,
    .button_count = 1,
    .has_rotation = false,
    .has_battery = true,
    .has_imu = true,
    // Optional ~1000mAh kit cell: 0.5C / 0.2C / 0.1C step-down. Lower these if a
    // much smaller battery is fitted.
    .chg_fast_ma = 500,
    .chg_taper_ma = 200,
    .chg_trickle_ma = 100,
};

const BoardCaps& board_caps(void) { return caps; }
