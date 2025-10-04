#pragma once

#include <stdint.h>
#include <stdbool.h>

struct zmk_led_hsb {
    uint16_t h;
    uint8_t s;
    uint8_t b;
};

// Add these API functions:

// Set color of a pixel by RGB struct
struct led_rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

int zmk_rgb_underglow_set_pixel(uint8_t index, struct led_rgb color);
int zmk_rgb_underglow_update(void);

// Existing function declarations ...
int zmk_rgb_underglow_toggle(void);
int zmk_rgb_underglow_get_state(bool *state);
int zmk_rgb_underglow_on(void);
int zmk_rgb_underglow_off(void);
int zmk_rgb_underglow_cycle_effect(int direction);
int zmk_rgb_underglow_calc_effect(int direction);
int zmk_rgb_underglow_select_effect(int effect);
struct zmk_led_hsb zmk_rgb_underglow_calc_hue(int direction);
struct zmk_led_hsb zmk_rgb_underglow_calc_sat(int direction);
struct zmk_led_hsb zmk_rgb_underglow_calc_brt(int direction);
int zmk_rgb_underglow_change_hue(int direction);
int zmk_rgb_underglow_change_sat(int direction);
int zmk_rgb_underglow_change_brt(int direction);
int zmk_rgb_underglow_change_spd(int direction);
int zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color);
