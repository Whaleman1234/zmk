/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <math.h>
#include <stdlib.h>

#include <zephyr/logging/log.h>

#include <zephyr/drivers/led_strip.h>
#include <drivers/ext_power.h>

#include <zmk/rgb_underglow.h>

#include <zmk/activity.h>
#include <zmk/usb.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/position_state_changed.h>  // NEW: to listen to keypress events
#include <zmk/workqueue.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if !DT_HAS_CHOSEN(zmk_underglow)
#error "A zmk,underglow chosen node must be declared"
#endif

#define STRIP_CHOSEN DT_CHOSEN(zmk_underglow)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_CHOSEN, chain_length)

#define HUE_MAX 360
#define SAT_MAX 100
#define BRT_MAX 100

BUILD_ASSERT(CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN <= CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX,
             "ERROR: RGB underglow maximum brightness is less than minimum brightness");

enum rgb_underglow_effect {
    UNDERGLOW_EFFECT_SOLID,
    UNDERGLOW_EFFECT_BREATHE,
    UNDERGLOW_EFFECT_SPECTRUM,
    UNDERGLOW_EFFECT_SWIRL,
    UNDERGLOW_EFFECT_HEATMAP,      // NEW effect
    UNDERGLOW_EFFECT_NUMBER        // Used to track number of underglow effects
};

struct rgb_underglow_state {
    struct zmk_led_hsb color;
    uint8_t animation_speed;
    uint8_t current_effect;
    uint16_t animation_step;
    bool on;
};

static const struct device *led_strip;

static struct led_rgb pixels[STRIP_NUM_PIXELS];

static struct rgb_underglow_state state;

// --- Heatmap tracking state ---
#define NUM_KEYS 42  // You might want to link this to your keyboard’s actual key count
static uint32_t key_counts[NUM_KEYS] = {0};
static uint32_t total_presses = 0;
// Map from key index to LED index (adjust as needed)
static const uint8_t key_to_led[NUM_KEYS] = {
    0, 1, 2, 3, 4, 5, 6,
    7, 8, 9, 10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20,
    21, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34,
    35, 36, 37, 38, 39, 40, 41
};

// Convert hue/sat/brightness to RGB
static struct led_rgb hsb_to_rgb(struct zmk_led_hsb hsb) {
    float r = 0, g = 0, b = 0;

    uint8_t i = hsb.h / 60;
    float v = hsb.b / ((float)BRT_MAX);
    float s = hsb.s / ((float)SAT_MAX);
    float f = hsb.h / ((float)HUE_MAX) * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    switch (i % 6) {
    case 0:
        r = v; g = t; b = p;
        break;
    case 1:
        r = q; g = v; b = p;
        break;
    case 2:
        r = p; g = v; b = t;
        break;
    case 3:
        r = p; g = q; b = v;
        break;
    case 4:
        r = t; g = p; b = v;
        break;
    case 5:
        r = v; g = p; b = q;
        break;
    }

    struct led_rgb rgb = { .r = (uint8_t)(r * 255), .g = (uint8_t)(g * 255), .b = (uint8_t)(b * 255) };
    return rgb;
}

// --- Helper: find max key press count ---
static uint32_t heatmap_find_max_count(void) {
    uint32_t max = 0;
    for (int i = 0; i < NUM_KEYS; i++) {
        if (key_counts[i] > max) {
            max = key_counts[i];
        }
    }
    return max == 0 ? 1 : max;  // avoid division by zero
}

// --- Helper: convert usage to hue ---
static uint16_t heatmap_usage_to_hue(float usage) {
    // Map usage [0.0, 1.0] to hue [240 (blue) → 0 (red)]
    float hue = 240.0f - (usage * 240.0f);
    if (hue < 0) hue = 0;
    if (hue > 360) hue = 360;
    return (uint16_t)hue;
}

// NEW: the heatmap effect
static void zmk_rgb_underglow_effect_heatmap(void) {
    uint32_t max_count = heatmap_find_max_count();

    for (int i = 0; i < NUM_KEYS; i++) {
        uint8_t led_index = key_to_led[i];
        if (led_index >= STRIP_NUM_PIXELS) {
            continue; // skip invalid LED index
        }

        float usage = (float)key_counts[i] / (float)max_count;
        uint16_t hue = heatmap_usage_to_hue(usage);

        struct zmk_led_hsb hsb = {
            .h = hue,
            .s = 100,
            .b = state.color.b, // use user brightness
        };

        pixels[led_index] = hsb_to_rgb(hsb);
    }

    // Clear unused LEDs (if any)
    for (int i = NUM_KEYS; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = (struct led_rgb){.r = 0, .g = 0, .b = 0};
    }
}

// Central tick function: choose effect
static void zmk_rgb_underglow_tick(struct k_work *work) {
    switch (state.current_effect) {
    case UNDERGLOW_EFFECT_SOLID:
        zmk_rgb_underglow_effect_solid();
        break;
    case UNDERGLOW_EFFECT_BREATHE:
        zmk_rgb_underglow_effect_breathe();
        break;
    case UNDERGLOW_EFFECT_SPECTRUM:
        zmk_rgb_underglow_effect_spectrum();
        break;
    case UNDERGLOW_EFFECT_SWIRL:
        zmk_rgb_underglow_effect_swirl();
        break;
    case UNDERGLOW_EFFECT_HEATMAP:
        zmk_rgb_underglow_effect_heatmap();
        break;
    default:
        // fallback
        zmk_rgb_underglow_effect_solid();
        break;
    }

    int err = led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
    if (err < 0) {
        LOG_ERR("Failed to update the RGB strip (%d)", err);
    }
}

K_WORK_DEFINE(underglow_tick_work, zmk_rgb_underglow_tick);

static void zmk_rgb_underglow_tick_handler(struct k_timer *timer) {
    if (!state.on) {
        return;
    }
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_tick_work);
}

K_TIMER_DEFINE(underglow_tick, zmk_rgb_underglow_tick_handler, NULL);

#if IS_ENABLED(CONFIG_SETTINGS)
static int rgb_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    int rc;

    if (settings_name_steq(name, "state", &next) && !next) {
        if (len != sizeof(state)) {
            return -EINVAL;
        }

        rc = read_cb(cb_arg, &state, sizeof(state));
        if (rc >= 0) {
            if (state.on) {
                k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(50));
            }
            return 0;
        }
        return rc;
    }

    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(rgb_underglow, "rgb/underglow", NULL, rgb_settings_set, NULL, NULL);

static void zmk_rgb_underglow_save_state_work(struct k_work *_work) {
    settings_save_one("rgb/underglow/state", &state, sizeof(state));
}

static struct k_work_delayable underglow_save_work;
#endif

static int zmk_rgb_underglow_init(void) {
    led_strip = DEVICE_DT_GET(STRIP_CHOSEN);

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
    if (!device_is_ready(ext_power)) {
        LOG_ERR("External power device \"%s\" is not ready", ext_power->name);
        return -ENODEV;
    }
#endif

    state = (struct rgb_underglow_state){
        .color = {
            .h = CONFIG_ZMK_RGB_UNDERGLOW_HUE_START,
            .s = CONFIG_ZMK_RGB_UNDERGLOW_SAT_START,
            .b = CONFIG_ZMK_RGB_UNDERGLOW_BRT_START,
        },
        .animation_speed = CONFIG_ZMK_RGB_UNDERGLOW_SPD_START,
        .current_effect = CONFIG_ZMK_RGB_UNDERGLOW_EFF_START,
        .animation_step = 0,
        .on = IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_ON_START)
    };

#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_init_delayable(&underglow_save_work, zmk_rgb_underglow_save_state_work);
#endif

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)
    state.on = zmk_usb_is_powered();
#endif

    if (state.on) {
        k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(50));
    }

    return 0;
}

// Event listener for activity, USB, and position state changed events
static int rgb_underglow_event_listener(const zmk_event_t *eh) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE)
    if (as_zmk_activity_state_changed(eh)) {
        return rgb_underglow_auto_state(zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE);
    }
#endif

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)
    if (as_zmk_usb_conn_state_changed(eh)) {
        return rgb_underglow_auto_state(zmk_usb_is_powered());
    }
#endif

    // Listen to key press events for heatmap effect
    if (as_zmk_position_state_changed(eh)) {
        const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
        if (ev->state) {
            if (ev->position < NUM_KEYS) {
                key_counts[ev->position]++;
                total_presses++;
            }
        }
    }

    return -ENOTSUP;
}

ZMK_LISTENER(rgb_underglow, rgb_underglow_event_listener);

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE)
ZMK_SUBSCRIPTION(rgb_underglow, zmk_activity_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)
ZMK_SUBSCRIPTION(rgb_underglow, zmk_usb_conn_state_changed);
#endif
ZMK_SUBSCRIPTION(rgb_underglow, zmk_position_state_changed);

SYS_INIT(zmk_rgb_underglow_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
