#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <math.h>

#include <zmk/event_manager.h>
#include <zmk/events/key_event.h>
#include <zmk/rgb.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Key-to-LED index mapping for Corne MX v3.0.1 (42 keys)
// Left half: keys 0–20
// Right half: keys 21–41
// Adjust if your firmware has a different LED wiring order
static const uint8_t key_to_led[NUM_KEYS] = {
     0,  1,  2,  3,  4,  5,  6,  // Row 1 left
     7,  8,  9, 10, 11, 12, 13,  // Row 2 left
    14, 15, 16, 17, 18, 19, 20,  // Row 3 left

    21, 22, 23, 24, 25, 26, 27,  // Row 1 right
    28, 29, 30, 31, 32, 33, 34,  // Row 2 right
    35, 36, 37, 38, 39, 40, 41   // Row 3 right
};

// -------- PARAMETERS --------
#define NUM_KEYS 42   // Corne has 42 physical keys
#define WARMUP 500    // presses before heatmap fully "activates"
#define MIN_KEY_COUNT 5
#define GAMMA 1.5f
#define HUE_BLUE 240  // hue values in degrees
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

    // Precompute logarithmic "weights" for scaling
    for (int i = 0; i < NUM_KEYS; i++) {
        vals[i] = logf(1.0f + (float)key_counts[i]);
        if (vals[i] < minX) minX = vals[i];
        if (vals[i] > maxX) maxX = vals[i];
    }

    // Warmup factor: prevents immediate "red" at startup
    float warmFactor = clampf((float)total_presses / (float)WARMUP, 0.0f, 1.0f);

    for (int i = 0; i < NUM_KEYS; i++) {
        uint32_t c = key_counts[i];
        struct rgb_color rgb;

        if (c < MIN_KEY_COUNT) {
            // Subtle blue for barely-used keys
            struct hsv_color hsv = { .h = HUE_BLUE, .s = 40, .v = 40 };
            zmk_rgb_hsv_to_rgb(&hsv, &rgb);
        } else {
            float s = (vals[i] - minX) / fmaxf(1e-6f, (maxX - minX));
            s = powf(s, GAMMA);    // gamma correction
            s *= warmFactor;       // apply warmup scaling

            float hue = (float)HUE_BLUE * (1.0f - s) + (float)HUE_RED * s;
            struct hsv_color hsv = {
                .h = (uint16_t)hue,
                .s = 100,
                .v = 100
            };
            zmk_rgb_hsv_to_rgb(&hsv, &rgb);
        }

        // Apply color to the LED mapped to key i
        // (You may need to define zmk_rgb_set_key_color() → wrapper for underglow or per-key)
        zmk_rgb_set_key_color(key_to_led[i], &rgb);
    }

    // Push updates to the LEDs
    zmk_rgb_update();
}

// -------- EVENT HANDLER --------
static int heatmap_listener(const zmk_event_t *eh) {
    const struct key_event *ev = as_key_event(eh);

    if (ev && ev->pressed) {
        heatmap_on_keypress(ev->key);
        heatmap_update_colors();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

// -------- REGISTER LISTENER --------
ZMK_LISTENER(heatmap, heatmap_listener);
ZMK_SUBSCRIPTION(heatmap, key_event);
