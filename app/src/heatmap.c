#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <math.h>

#include <zmk/events/key_event.h>
#include <zmk/rgb.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// -------- PARAMETERS --------
#define NUM_KEYS 42   // set this to the number of physical keys your board has
#define WARMUP 500
#define MIN_KEY_COUNT 5
#define GAMMA 1.5f
#define HUE_BLUE 240
#define HUE_RED 0

// -------- STATE --------
static uint32_t key_counts[NUM_KEYS] = {0};
static uint32_t total_presses = 0;

// -------- HELPERS --------
static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// -------- LOGIC --------
static void heatmap_on_keypress(uint8_t key_index) {
    if (key_index >= NUM_KEYS) return;
    key_counts[key_index]++;
    total_presses++;
}

static void heatmap_update_colors(void) {
    float vals[NUM_KEYS];
    float minX = 1e9f, maxX = -1e9f;

    // Precompute logs
    for (int i = 0; i < NUM_KEYS; i++) {
        vals[i] = logf(1.0f + (float)key_counts[i]);
        if (vals[i] < minX) minX = vals[i];
        if (vals[i] > maxX) maxX = vals[i];
    }

    float warmFactor = clampf((float)total_presses / (float)WARMUP, 0.0f, 1.0f);

    for (int i = 0; i < NUM_KEYS; i++) {
        uint32_t c = key_counts[i];
        struct rgb_color rgb;

        if (c < MIN_KEY_COUNT) {
            // Subtle blue for barely-used keys
            struct hsv_color hsv = { .h = 240, .s = 40, .v = 40 };
            zmk_rgb_hsv_to_rgb(&hsv, &rgb);
        } else {
            float s = (vals[i] - minX) / fmaxf(1e-6f, (maxX - minX));
            s = powf(s, GAMMA);
            s *= warmFactor;
            float hue = (float)HUE_BLUE * (1.0f - s) + (float)HUE_RED * s;
            struct hsv_color hsv = { .h = (uint16_t)hue, .s = 100, .v = 100 };
            zmk_rgb_hsv_to_rgb(&hsv, &rgb);
        }

        // Send to ZMK RGB API: this sets LED i to rgb
        // You may need to adjust this function depending on your LED driver.
        zmk_rgb_set_key_color(i, &rgb);
    }

    zmk_rgb_update();
}

// -------- EVENT HANDLER --------
static int heatmap_listener(const zmk_event_t *eh) {
    const struct key_event *ev = as_key_event(eh);

    if (ev && ev->pressed) {
        heatmap_on_keypress(ev->key);
        heatmap_update_colors();
        return ZMK_EV_EVENT_BUBBLE;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

// Register listener with ZMK
ZMK_LISTENER(heatmap, heatmap_listener);
ZMK_SUBSCRIPTION(heatmap, key_event);
