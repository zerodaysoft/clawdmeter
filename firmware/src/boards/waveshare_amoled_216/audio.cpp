#include "../../hal/audio_hal.h"
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ES8311 codec + onboard speaker (playback only). I2S master clocks come from
// the ESP32-S3; the codec runs as I2S slave. MCLK = 256 * sample_rate, which
// keeps the es8311 clock-divider/OSR registers constant for *any* sample rate.
// Register sequence mirrors Espressif's esp-bsp es8311 driver for "MCLK from
// MCLK pin, slave, 16-bit".
//
// The sound is a WAV file on LittleFS (/notification.wav) so it can be swapped
// without rebuilding firmware: replace firmware/data/notification.wav and run
// `pio run -t uploadfs`. The player reads the format from the WAV header, so
// any 16-bit PCM mono/stereo file at a standard rate works. Output is always
// stereo to the I2S bus (mono sources are duplicated to L+R) — sending a real
// stereo frame is what makes the ES8311 play cleanly; a half-filled mono frame
// comes out faint and distorted.
//
// Playback is async on a background task so the UI + BLE loop never stall.

#define WAV_PATH "/notification.wav"

// ES8311 DAC output volume, 0..100% mapped to reg 0x32. The mapping is NOT
// linear in loudness — reg 0x32 is in 0.5 dB steps, where ~75% (0xBF) is 0 dB.
// Above that adds gain and the tiny speaker clips (gritty); well below ~60% is
// barely audible. ~80% is about as loud as the speaker takes cleanly.
#define AUDIO_VOLUME_PCT 80

// Parsed once at init from the WAV header. The I2S clock is set to this rate
// at init, so the file's native sample rate plays at correct pitch with no
// runtime reconfig. Swapping the file (uploadfs) takes effect on next boot.
struct WavInfo { uint32_t rate; uint16_t channels; uint16_t bits;
                 uint32_t data_off; uint32_t data_len; bool ok; };

static i2s_chan_handle_t tx_chan   = NULL;
static TaskHandle_t      play_task = NULL;
static WavInfo           g_wav     = {0, 0, 0, 0, 0, false};

// ---- ES8311 I2C register access (shared Wire bus) ----
static void es_w(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static uint8_t es_r(uint8_t reg) {
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((int)ES8311_I2C_ADDR, 1);
    return Wire.available() ? Wire.read() : 0;
}

// Constant register set for MCLK = 256*fs, slave, 16-bit (esp-bsp coeff row
// {pre_div=1,pre_multi=0,adc_div=1,dac_div=1,fs_mode=0,lrck=0x00ff,bclk_div=4,
//  adc_osr=0x10,dac_osr=0x10} — identical for every rate when MCLK tracks fs).
static void es8311_init_codec(void) {
    es_w(0x00, 0x1F); delay(20);   // reset
    es_w(0x00, 0x00);
    es_w(0x00, 0x80);              // power on, MCLK from pin

    es_w(0x01, 0x3F);              // enable all clocks, MCLK from MCLK pin
    { uint8_t r6 = es_r(0x06); r6 &= ~(1 << 5); es_w(0x06, r6); }  // SCLK not inverted

    { uint8_t r2 = es_r(0x02) & 0x07; r2 |= (1 - 1) << 5; r2 |= 0 << 3; es_w(0x02, r2); }
    es_w(0x03, (0 << 6) | 0x10);   // fs_mode<<6 | adc_osr
    es_w(0x04, 0x10);              // dac_osr
    es_w(0x05, ((1 - 1) << 4) | (1 - 1));
    { uint8_t r6 = es_r(0x06) & 0xE0; r6 |= (4 - 1); es_w(0x06, r6); }  // bclk_div
    { uint8_t r7 = es_r(0x07) & 0xC0; r7 |= 0;       es_w(0x07, r7); }  // lrck_h
    es_w(0x08, 0xFF);              // lrck_l

    { uint8_t r0 = es_r(0x00) & 0xBF; es_w(0x00, r0); }  // slave serial port
    es_w(0x09, 0x0C);              // SDP in  16-bit
    es_w(0x0A, 0x0C);              // SDP out 16-bit

    es_w(0x0D, 0x01);              // power up analog
    es_w(0x0E, 0x02);
    es_w(0x12, 0x00);              // power up DAC
    es_w(0x13, 0x10);              // enable output drive
    es_w(0x1C, 0x6A);
    es_w(0x37, 0x08);

    es_w(0x32, ((AUDIO_VOLUME_PCT * 256) / 100) - 1);  // DAC volume
    { uint8_t r31 = es_r(0x31); r31 &= ~((1 << 6) | (1 << 5)); es_w(0x31, r31); }  // unmute
}

static void i2s_setup(uint32_t sample_rate) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &tx_chan, NULL);

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)I2S_MCLK_GPIO,
            .bclk = (gpio_num_t)I2S_BCLK_GPIO,
            .ws   = (gpio_num_t)I2S_LRCK_GPIO,
            .dout = (gpio_num_t)I2S_DOUT_GPIO,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    i2s_channel_init_std_mode(tx_chan, &std_cfg);
    i2s_channel_enable(tx_chan);   // clocks run continuously; PA gates the speaker
}

// Minimal WAV header parse: walk the RIFF chunks (skips filler chunks like the
// 'FLLR' that afconvert inserts) to find 'fmt ' and 'data'.
static WavInfo parse_wav(File& f) {
    WavInfo w = {0, 0, 0, 0, 0, false};
    uint8_t hdr[12];
    if (f.read(hdr, 12) != 12) return w;
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return w;

    uint32_t pos = 12;
    const uint32_t size = f.size();
    while (pos + 8 <= size) {
        f.seek(pos);
        uint8_t ch[8];
        if (f.read(ch, 8) != 8) break;
        uint32_t csz = ch[4] | (ch[5] << 8) | (ch[6] << 16) | ((uint32_t)ch[7] << 24);
        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (f.read(fmt, 16) == 16) {
                w.channels = fmt[2] | (fmt[3] << 8);
                w.rate     = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
                w.bits     = fmt[14] | (fmt[15] << 8);
            }
        } else if (memcmp(ch, "data", 4) == 0) {
            w.data_off = pos + 8;
            w.data_len = csz;
            w.ok = (w.rate > 0 && w.bits == 16 && (w.channels == 1 || w.channels == 2));
            break;
        }
        pos += 8 + csz + (csz & 1);  // chunks are word-aligned
    }
    return w;
}

static void play_wav(void) {
    if (!g_wav.ok) { Serial.println("audio: no playable " WAV_PATH); return; }

    File f = LittleFS.open(WAV_PATH, "r");
    if (!f) { Serial.println("audio: " WAV_PATH " open failed"); return; }

    digitalWrite(AUDIO_PA_GPIO, HIGH);
    delay(5);  // amp settle (anti-pop)

    const size_t FRAMES = 256;
    int16_t inbuf[FRAMES * 2];   // up to stereo frames
    int16_t outbuf[FRAMES * 2];  // always stereo out
    uint32_t remaining = g_wav.data_len;
    f.seek(g_wav.data_off);

    while (remaining > 0) {
        size_t want = FRAMES * g_wav.channels * sizeof(int16_t);
        if (want > remaining) want = remaining;
        int n = f.read((uint8_t*)inbuf, want);
        if (n <= 0) break;
        remaining -= n;

        size_t frames = n / (sizeof(int16_t) * g_wav.channels);
        for (size_t i = 0; i < frames; i++) {
            int16_t l = (g_wav.channels == 1) ? inbuf[i] : inbuf[2 * i];
            int16_t r = (g_wav.channels == 1) ? inbuf[i] : inbuf[2 * i + 1];
            outbuf[2 * i]     = l;
            outbuf[2 * i + 1] = r;
        }
        size_t written;
        i2s_channel_write(tx_chan, outbuf, frames * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    }

    // Flush silence so the DAC ends on zero, then drop the amp.
    static const int16_t silence[256] = {0};
    size_t written;
    i2s_channel_write(tx_chan, silence, sizeof(silence), &written, pdMS_TO_TICKS(100));
    digitalWrite(AUDIO_PA_GPIO, LOW);
    f.close();
}

static void play_task_fn(void* /*arg*/) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        play_wav();
    }
}

void audio_hal_init(void) {
    pinMode(AUDIO_PA_GPIO, OUTPUT);
    digitalWrite(AUDIO_PA_GPIO, LOW);

    if (!LittleFS.begin(false)) {
        Serial.println("audio: LittleFS mount failed (run 'pio run -t uploadfs')");
    } else {
        File f = LittleFS.open(WAV_PATH, "r");
        if (f) { g_wav = parse_wav(f); f.close(); }
        if (g_wav.ok) {
            Serial.printf("audio: %s = %luHz, %uch, 16-bit\n",
                WAV_PATH, (unsigned long)g_wav.rate, g_wav.channels);
        } else {
            Serial.println("audio: no valid " WAV_PATH " (need 16-bit PCM)");
        }
    }

    es8311_init_codec();
    i2s_setup(g_wav.ok ? g_wav.rate : 16000);

    xTaskCreatePinnedToCore(play_task_fn, "audio", 6144, NULL, 4, &play_task, 1);
    Serial.println("Audio (ES8311) init OK");
}

void audio_hal_play_notify(void) {
    if (play_task) xTaskNotifyGive(play_task);
}
