/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/rgb_underglow.h>
#include <zmk/heatmap.h>

#define NUM_KEYS 42  // Adjust this to match your keyboard layout
#define HUE_MAX 360
#define SAT_MAX 100
#define BRT_MAX 100

extern struct zmk_led_hsb state_color;  // Defined in rgb_underglow.c
extern struct led_rgb pixels[];         // Defined in rgb_underglow.c

// Map key indices to LED indices (adjust as needed)
static const uint8_t key_to_led[NUM_KEYS] = {
    0, 1, 2, 3, 4, 5, 6,
    7, 8, 9, 10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20,
    21, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34,
    35, 36, 37, 38, 39, 40, 41
};

static uint32_t key_counts[NUM_KEYS] = {0};

// --- Convert HSB to RGB ---
static struct led_rgb hsb_to_rgb(struct zmk_led_hsb hsb) {
    float r = 0, g = 0, b = 0;

    uint8_t i = hsb.h / 60;
    float v = hsb.b / (float)BRT_MAX;
    float s = hsb.s / (float)SAT_MAX;
    float f = (hsb.h / (float)HUE_MAX) * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    case 5: r = v; g = p; b = q; break;
    }

    return (struct led_rgb){
        .r = (uint8_t)(r * 255),
        .g = (uint8_t)(g * 255),
        .b = (uint8_t)(b * 255)
    };
}

// --- Find max count ---
static uint32_t heatmap_find_max_count(void) {
    uint32_t max = 1;
    for (int i = 0; i < NUM_KEYS; i++) {
        if (key_counts[i] > max) {
            max = key_counts[i];
        }
    }
    return max;
}

// --- Convert usage to hue ---
static uint16_t heatmap_usage_to_hue(float usage) {
    float hue = 240.0f - (usage * 240.0f);
    if (hue < 0) hue = 0;
    if (hue > 360) hue = 360;
    return (uint16_t)hue;
}

// --- Main heatmap effect ---
void zmk_rgb_underglow_effect_heatmap(void) {
    uint32_t max_count = heatmap_find_max_count();

    for (int i = 0; i < NUM_KEYS; i++) {
        uint8_t led_index = key_to_led[i];
        float usage = (float)key_counts[i] / (float)max_count;
        uint16_t hue = heatmap_usage_to_hue(usage);

        struct zmk_led_hsb hsb = {
            .h = hue,
            .s = 100,
            .b = state_color.b
        };

        struct led_rgb rgb = hsb_to_rgb(hsb);
        zmk_rgb_underglow_set_pixel(led_index, rgb);
    }

    zmk_rgb_underglow_update();
}

// --- Listen for key presses ---
static int heatmap_listener(const zmk_event_t *eh) {
    if (as_zmk_position_state_changed(eh)) {
        const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
        if (ev->state && ev->position < NUM_KEYS) {
            key_counts[ev->position]++;
        }
    }
    return 0;
}

ZMK_LISTENER(heatmap, heatmap_listener);
ZMK_SUBSCRIPTION(heatmap, zmk_position_state_changed);
