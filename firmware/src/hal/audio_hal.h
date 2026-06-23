#pragma once

// Audio output abstraction. Boards with a codec + speaker (BOARD_HAS_AUDIO)
// implement this; boards without provide no-op stubs so shared code can call
// it unconditionally. Playback is asynchronous — audio_hal_play_notify()
// returns immediately and the sound renders on a background task so the UI and
// BLE loop never stall.

void audio_hal_init(void);          // bring up codec + I2S TX (no-op if no audio)
void audio_hal_play_notify(void);   // play the built-in "attention" sound, async
