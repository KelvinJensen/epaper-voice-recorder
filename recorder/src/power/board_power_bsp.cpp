#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "board_power_bsp.h"
#include "../../config.h"
#include "../i2c_bsp/i2c_bsp.h"

// The C6 board switches these rails through the TCA9554 expander, so they only
// respond after i2c_master_Init(). All rails are active high.
board_power_bsp_t::board_power_bsp_t(uint8_t _epd_power_pin,uint8_t _audio_power_pin,uint8_t _vbat_power_pin) :
    epd_power_pin(_epd_power_pin),
    audio_power_pin(_audio_power_pin),
    vbat_power_pin(_vbat_power_pin) {
}

board_power_bsp_t::~board_power_bsp_t() {

}

void board_power_bsp_t::POWEER_EPD_ON() {
    exio_set_level(epd_power_pin,1);
}

void board_power_bsp_t::POWEER_EPD_OFF() {
    exio_set_level(epd_power_pin,0);
}

void board_power_bsp_t::POWEER_Audio_ON() {
    exio_set_level(audio_power_pin,1);
    exio_set_level(AMP_PWR_PIN,1);
}

void board_power_bsp_t::POWEER_Audio_OFF() {
    exio_set_level(AMP_PWR_PIN,0);
    exio_set_level(audio_power_pin,0);
}

void board_power_bsp_t::VBAT_POWER_ON() {
    exio_set_level(vbat_power_pin,1);
}

void board_power_bsp_t::VBAT_POWER_OFF() {
    exio_set_level(vbat_power_pin,0);
}
