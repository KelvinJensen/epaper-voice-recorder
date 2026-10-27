#include <stdio.h>
#include "audio_bsp.h"
#ifdef AUDIO_BSP_HOST_TEST
#include "../../../scripts/audio_bsp_mocks.h"
#else
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../codec_board/codec_board.h"
#include "../codec_board/codec_init.h"
#include "../esp_codec_dev/include/esp_codec_dev.h"
#include "esp_heap_caps.h"
#endif

static esp_codec_dev_handle_t playback = NULL;
static esp_codec_dev_handle_t record   = NULL;
static bool playback_open = false, record_open = false;
static uint8_t requested_volume = 0;

static esp_codec_dev_sample_info_t sample_info = {
    .sample_rate = 16000, .channel = 2, .bits_per_sample = 16,
};

void audio_idle_suspend(void)
{
    if (record_open && esp_codec_dev_close(record) == ESP_CODEC_DEV_OK) record_open = false;
    if (playback_open && esp_codec_dev_close(playback) == ESP_CODEC_DEV_OK) playback_open = false;
}

void audio_prepare_sleep(void)
{
    // Close both directions while the codec still has power. Muting alone
    // does not stop its ADC/DAC. A deep-sleep wake initializes them afresh.
    audio_idle_suspend();
}

void audio_bsp_init(void)
{
    set_codec_board_type("C6_ePaper_1_54");
    codec_init_cfg_t codec_cfg = {
        .in_mode   = CODEC_I2S_MODE_STD,
        .out_mode  = CODEC_I2S_MODE_STD,
        .in_use_tdm  = false,
        .reuse_dev   = false,
    };
    ESP_ERROR_CHECK(init_codec(&codec_cfg));
    playback = get_playback_handle();
    record   = get_record_handle();
}

void audio_play_init(void)
{
    esp_codec_dev_set_out_vol(playback, 100.0);
    esp_codec_dev_set_in_gain(record, 45.0);
    // Handles stay initialized, but ADC/DAC and I2S directions are opened
    // lazily. Home does not need a continuously running microphone or DAC.
    requested_volume = 0;
    // Board initialization enables the I2S clocks for codec setup. Open/close
    // once so both directions are actually disabled, not merely unmarked.
    playback_open = esp_codec_dev_open(playback, &sample_info) == ESP_CODEC_DEV_OK;
    record_open = esp_codec_dev_open(record, &sample_info) == ESP_CODEC_DEV_OK;
    if (playback_open) esp_codec_dev_set_out_vol(playback, 0);
    if (record_open) esp_codec_dev_set_in_gain(record, 45.0);
    audio_idle_suspend();
}

void audio_playback_set_vol(uint8_t vol)
{
    requested_volume = vol;
    if (playback_open) esp_codec_dev_set_out_vol(playback, vol);
}

int audio_playback_read(void *data_ptr, uint32_t len)
{
    if (!record_open) {
        const int result = esp_codec_dev_open(record, &sample_info);
        if (result != ESP_CODEC_DEV_OK) return result;
        record_open = true;
        if (esp_codec_dev_set_in_gain(record, 45.0) != ESP_CODEC_DEV_OK) {
            audio_idle_suspend(); return ESP_CODEC_DEV_WRONG_STATE;
        }
    }
    return esp_codec_dev_read(record, data_ptr, len);
}

int audio_playback_write(void *data_ptr, uint32_t len)
{
    if (!playback_open) {
        const int result = esp_codec_dev_open(playback, &sample_info);
        if (result != ESP_CODEC_DEV_OK) return result;
        playback_open = true;
        if (esp_codec_dev_set_out_vol(playback, requested_volume) != ESP_CODEC_DEV_OK) {
            audio_idle_suspend(); return ESP_CODEC_DEV_WRONG_STATE;
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    return esp_codec_dev_write(playback, data_ptr, len);
}
