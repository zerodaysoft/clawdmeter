#include "../../hal/audio_hal.h"

// No audio support on this board yet. The ES8311 codec may be populated, but
// the I2S wiring/driver isn't ported. Mirror boards/waveshare_amoled_216/
// audio.cpp to add it, and set BOARD_HAS_AUDIO 1 in board.h.
void audio_hal_init(void) {}
void audio_hal_play_notify(void) {}
