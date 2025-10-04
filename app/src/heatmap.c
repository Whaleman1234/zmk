#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <math.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/rgb_underglow.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/led_strip.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define NUM_KEYS 42
#define WARMUP 500
#define MIN_KEY_COUNT 5
#define GAMMA 1.5f
#define HUE_BLUE 240
#define HUE_RED 0

static uint32_t key_counts[NUM_KEYS] = {0};
static uint32_t total_presses = 0;

// Adjust this mapping based on your LED wiring
static const uint8_t key_to_led[NUM_KEYS] = {
     0,  1,  2,  3,  4,  5,  6,
     7,  8,  9, 10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20,
    21, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34,
    35, 36, 37, 38, 39, 40, 41
};

struct hsv_color {
    uint16_t h;  // Hue: 0-360 degrees
    uint8_t s;   // Saturation: 0-100%
    uint8_t v;   // Value: 0-100%
};

// Converts HSV to RGB
static void zmk_rgb_hsv_to_rgb(const struct hsv_color *hsv, struct led_rgb *rgb) {
    float hh, p, q, t, ff;
    long i;
    float h = (float) hsv->h;
    float s = ((float) hsv->s) / 100.0f;
    float v = ((float) hsv->v) / 100.0f;

    if (s <= 0.0f) { // Achromatic (grey)
        rgb->r = rgb->g = rgb->b = (uint8_t)(v * 255);
        return;
    }
    hh = h;
    if (hh >= 360.0f) hh = 0.0f;
    hh /= 60.0f;
    i = (long) hh;
    ff = hh - i;
    p = v * (1.0f - s);
    q = v * (1.0f - (s * ff));
    t = v * (1.0f - (s * (1.0f - ff)));

    switch (i) {
        case 0:
            rgb->r = (uint8_t)(v * 255);
            rgb->g = (uint8_t)(t * 255);
            rgb->b = (uint8_t)(p * 255);
            break;
        case 1:
            rgb->r = (uint8_t)(q * 255);
            rgb->g = (uint8_t)(v * 255);
            rgb->b = (uint8_t)(p * 255);
            break;
        case 2:
            rgb->r = (uint8_t)(p * 255);
            rgb->g = (uint8_t)(v * 255);
            rgb->b = (uint8_t)(t * 255);
            break;
        case 3:
            rgb->r = (uint8_t)(p * 255);
            rgb->g = (uint8_t)(q * 255);
            rgb->b = (uint8_t)(v * 255);
            break;
        case 4:
            rgb->r = (uint8_t)(t * 255);
            rgb->g = (uint8_t)(p * 255);
            rgb->b = (uint8_t)(v * 255);
            break;
        case 5:
        default:
            rgb->r = (uint8_t)(v * 255);
            rgb->g = (uint8_t)(p * 255);
            rgb->b = (uint8_t)(q * 255);
            break;
    }
}

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void heatmap_on_keypress(uint8_t key_index) {
    if (key_index >= NUM_KEYS) return;
    key_counts[key_index]++;
    total_presses++;
}

static void heatmap_update_colors(void) {
    float vals[NUM_KEYS];
    float minX = 1e9f, maxX = -1e9f;

    for (int i = 0; i < NUM_KEYS; i++) {
        vals[i] = logf(1.0f + (float) key_counts[i]);
        if (vals[i] < minX) minX = vals[i];
        if (vals[i] > maxX) maxX = vals[i];
    }

    float warmFactor = clampf((float)total_presses / (float)WARMUP, 0.0f, 1.0f);

    for (int i = 0; i < NUM_KEYS; i++) {
        uint32_t c = key_counts[i];
        struct led_rgb rgb;

        if (c < MIN_KEY_COUNT) {
            struct hsv_color hsv = { .h = HUE_BLUE, .s = 40, .v = 40 };
            zmk_rgb_hsv_to_rgb(&hsv, &rgb);
        } else {
            float s = (vals[i] - minX) / fmaxf(1e-6f, (maxX - minX));
            s = powf(s, GAMMA);
            s *= warmFactor;

            float hue = (float) HUE_BLUE * (1.0f - s) + (float) HUE_RED * s;
            struct hsv_color hsv = {
                .h = (uint16_t) hue,
                .s = 100,
                .v = 100
            };
            zmk_rgb_hsv_to_rgb(&hsv, &rgb);
        }

        uint8_t led_idx = key_to_led[i];
        zmk_rgb_underglow_set_pixel(led_idx, rgb.r, rgb.g, rgb.b);
    }

    zmk_rgb_underglow_update();
}

static int heatmap_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev && ev->state) {
        if (ev->position < NUM_KEYS) {
            heatmap_on_keypress((uint8_t)ev->position);
            heatmap_update_colors();
        }
    }
    return 0;
}

ZMK_LISTENER(heatmap, heatmap_listener);
ZMK_SUBSCRIPTION(heatmap, zmk_position_state_changed);
