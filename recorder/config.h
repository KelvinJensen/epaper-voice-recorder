#ifndef CONFIG_H
#define CONFIG_H

#define EPD_SPI_NUM        SPI2_HOST
#define ESP32_I2C_DEV_NUM  I2C_NUM_0

#define EPD_WIDTH  200
#define EPD_HEIGHT 200
#define LVGL_SPIRAM_BUFF_LEN (EPD_WIDTH * EPD_HEIGHT * 2)

/* EPD SPI pins (bus shared with the TF card) */
#define EPD_DC_PIN    GPIO_NUM_15
#define EPD_CS_PIN    GPIO_NUM_7
#define EPD_SCK_PIN   GPIO_NUM_6
#define EPD_MOSI_PIN  GPIO_NUM_5
#define EPD_MISO_PIN  GPIO_NUM_4
#define EPD_RST_PIN   GPIO_NUM_11
#define EPD_BUSY_PIN  GPIO_NUM_10


/* Power control pins: TCA9554 expander outputs, active high */
#define I2C_EXIO_DEV_Address  0x20
#define EPD_PWR_PIN     0
#define Audio_PWR_PIN   1
#define AMP_PWR_PIN     3
#define VBAT_PWR_PIN    5
#define BAT_ADC_PIN     0   // ADC1_CHANNEL_0 on ESP32-C6

#define BOOT_BUTTON_PIN GPIO_NUM_9
#define PWR_BUTTON_PIN  GPIO_NUM_2

/* Deep-sleep wake-up pin: dedicated lower PWR button (LP GPIO) */
#define ext_wakeup_pin_1 GPIO_NUM_2

/* I2C bus */
#define ESP32_I2C_SDA_PIN GPIO_NUM_18
#define ESP32_I2C_SCL_PIN GPIO_NUM_8

/* LVGL tick timing */
#define EXAMPLE_LVGL_TICK_PERIOD_MS    5
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 100

/* I2C peripheral addresses */
#define I2C_RTC_DEV_Address        0x51
#define I2C_SHTC3_DEV_Address      0x70

/* Button GPIO aliases */
#define BTN_REC      9
#define BTN_PWR      2

/* TF card over SPI, sharing the EPD bus */
#define SD_CS   GPIO_NUM_3

/* Audio */
#define SAMPLE_RATE  16000
#define REC_BUF      (8 * 1024)
#define RECORD_CHUNK_MS      600000UL
#define RECORD_MAX_MS        7200000UL
#define RECORD_MIN_FREE_BYTES (2UL * 1024UL * 1024UL)

/* Storage paths */
#define NOTES_DIR  "/notes"
#define INDEX_FILE "/notes/index.csv"
#define TAG_FILE   "/notes/tags.txt"
#define MAX_TAGS   20

/* UI timing */
#define REC_HOLD_MS         350
#define BTN_LONG_MS         600
#define DOUBLE_MS           200
#define ULTRA_SLEEP_MS      120000UL
#define TRANSFER_IDLE_MS    900000UL
#define ERROR_SLEEP_AFTER_MS 30000UL
#define TICKER_INTERVAL_MS  2500

/* Battery warning */
#define BAT_CHECK_INTERVAL_MS  30000
#define BAT_LOW_THRESHOLD      15
#define BAT_RECOVER_THRESHOLD  20

/* Transcription (OpenAI) */
#define TRANSCRIBE_MODEL       "gpt-transcribe"
#define TRANSCRIBE_LANGUAGE    "da"

/* Text shown at the bottom of the idle screen */
#define DEVICE_LABEL           "VOICE RECORDER"

/* Time & firmware */
#define LOCAL_TZ_RULE          "CET-1CEST,M3.5.0,M10.5.0/3"
#ifndef RECORDER_VERSION
#define RECORDER_VERSION   "1.2.0-rc9"
#endif
#define FIRMWARE_VERSION       "v" RECORDER_VERSION
#define FW_VERSION             FIRMWARE_VERSION

#endif // CONFIG_H
