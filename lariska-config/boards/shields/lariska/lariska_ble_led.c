/*
 * Lariska BLE status LED indicator — red channel (led0, P0.16).
 *
 *   Slow blink  (500 ms on / 1500 ms off):  BLE endpoint, host unreachable.
 *   Fast blink  (200 ms on /  200 ms off):  BLE endpoint, pairing (open profile).
 *   Double blink (2 × 120 ms):              One-shot when switching to / booting on
 *                                           BLE profile 3 (index 2, the ESB slot).
 *   Off:                                    USB endpoint, or BLE profile connected.
 *
 * The LED fires a short 3-pulse boot-flash at start-up to confirm the
 * hardware path works regardless of BLE state.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/endpoints.h>
#include <zmk/ble.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lariska_ble_led, CONFIG_ZMK_LOG_LEVEL);

/* Red LED: led0 → pwm_led_0 → pwm0 ch0, P0.16, PWM_POLARITY_INVERTED.
 * With INVERTED: pulse = period → pin LOW → LED ON; pulse = 0 → pin HIGH → LED OFF. */
static const struct pwm_dt_spec m_red = PWM_DT_SPEC_GET(DT_ALIAS(led0));

/* LDO enable pin (P1.2, active-high): powers the RGB LED circuit.
 * Must be driven HIGH before lighting the LED; off otherwise to save power. */
static const struct gpio_dt_spec m_ldo = GPIO_DT_SPEC_GET(DT_ALIAS(ldo), gpios);

#define ESB_PROFILE_IDX (ZMK_BLE_PROFILE_COUNT - 1)

/* ── LED primitives ─────────────────────────────────────────────────────── */

static inline void led_on(void) {
    gpio_pin_set_dt(&m_ldo, 1);
    pwm_set_dt(&m_red, m_red.period, m_red.period);
}

static inline void led_off(void) {
    pwm_set_dt(&m_red, m_red.period, 0);
    gpio_pin_set_dt(&m_ldo, 0);
}

/* ── Blink engine ───────────────────────────────────────────────────────── */

typedef enum { BLINK_OFF = 0, BLINK_SLOW, BLINK_FAST } blink_mode_t;

static blink_mode_t m_mode       = BLINK_OFF;
static bool         m_phase_on   = false;
static bool         m_dbl_active = false;
static bool         m_initialized = false;
static uint8_t      m_prev_prof  = 0xFF; /* invalid sentinel */

static void blink_fn(struct k_work *w);
static K_WORK_DELAYABLE_DEFINE(m_blink_work, blink_fn);

static void blink_schedule(void) {
    uint32_t delay;
    if (m_mode == BLINK_SLOW) {
        if (m_phase_on) { led_on();  delay = 500;  }
        else            { led_off(); delay = 1500; }
    } else { /* BLINK_FAST */
        if (m_phase_on) { led_on();  delay = 200; }
        else            { led_off(); delay = 200; }
    }
    m_phase_on = !m_phase_on;
    k_work_schedule(&m_blink_work, K_MSEC(delay));
}

static void blink_fn(struct k_work *w) {
    ARG_UNUSED(w);
    if (!m_dbl_active && m_mode != BLINK_OFF) {
        blink_schedule();
    }
}

static void set_blink_mode(blink_mode_t mode) {
    if (m_dbl_active || mode == m_mode) {
        return;
    }
    m_mode     = mode;
    m_phase_on = true; /* always start with LED ON so the first pulse is immediately visible */
    k_work_cancel_delayable(&m_blink_work);
    if (mode == BLINK_OFF) {
        led_off();
    } else {
        blink_schedule();
    }
}

/* ── Double-blink ───────────────────────────────────────────────────────── */

/* Steps alternate ON/OFF starting at index 0 (ON).
 * ON 120 ms, OFF 150 ms, ON 120 ms, OFF 150 ms → done */
static const uint16_t DBL_MS[] = { 120, 150, 120, 150 };
#define DBL_COUNT ARRAY_SIZE(DBL_MS)

static uint8_t m_dbl_step = 0;

static void dbl_fn(struct k_work *w);
static K_WORK_DELAYABLE_DEFINE(m_dbl_work, dbl_fn);

static void dbl_fn(struct k_work *w) {
    ARG_UNUSED(w);
    if (m_dbl_step >= DBL_COUNT) {
        m_dbl_active = false;
        led_off();
        m_mode = BLINK_OFF;
        /* ESB slot stays off; connection events will override for true BLE. */
        return;
    }
    if (m_dbl_step % 2 == 0) { led_on(); } else { led_off(); }
    k_work_schedule(&m_dbl_work, K_MSEC(DBL_MS[m_dbl_step]));
    m_dbl_step++;
}

static void trigger_double_blink(void) {
    k_work_cancel_delayable(&m_blink_work);
    k_work_cancel_delayable(&m_dbl_work);
    m_dbl_active = true;
    m_dbl_step   = 0;
    m_mode       = BLINK_OFF;
    led_off();
    k_work_schedule(&m_dbl_work, K_NO_WAIT);
}

/* ── State assessment ───────────────────────────────────────────────────── */

static void update_state(void) {
    if (m_dbl_active) {
        return;
    }
    struct zmk_endpoint_instance ep = zmk_endpoints_selected();

    if (ep.transport != ZMK_TRANSPORT_BLE) {
        set_blink_mode(BLINK_OFF);
        return;
    }
    /* ESB slot masquerades as BLE with the last profile index. No ongoing blink there. */
    if (ep.ble.profile_index == ESB_PROFILE_IDX) {
        set_blink_mode(BLINK_OFF);
        return;
    }
    if (zmk_ble_active_profile_is_open()) {
        set_blink_mode(BLINK_FAST);
    } else if (!zmk_ble_active_profile_is_connected()) {
        set_blink_mode(BLINK_SLOW);
    } else {
        set_blink_mode(BLINK_OFF);
    }
}

/* ── ZMK event listener ─────────────────────────────────────────────────── */

static int ble_led_event_cb(const zmk_event_t *eh) {
    const struct zmk_ble_active_profile_changed *ble = as_zmk_ble_active_profile_changed(eh);
    if (ble) {
        bool switched = m_initialized && (ble->index != m_prev_prof);
        if (switched && ble->index == ESB_PROFILE_IDX) {
            trigger_double_blink();
        } else if (m_initialized) {
            update_state();
        }
        m_prev_prof = ble->index;
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_endpoint_changed(eh) && m_initialized) {
        update_state();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(lariska_ble_led, ble_led_event_cb);
ZMK_SUBSCRIPTION(lariska_ble_led, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(lariska_ble_led, zmk_endpoint_changed);

/* ── Boot flash + state init ────────────────────────────────────────────── */

/* 3-pulse boot flash: confirms the LED hardware path is alive.
 * ON 80ms, OFF 100ms, ON 80ms, OFF 100ms, ON 80ms, OFF → then normal state. */
static const uint16_t BOOT_MS[] = { 80, 100, 80, 100, 80, 100 };
#define BOOT_COUNT ARRAY_SIZE(BOOT_MS)

static uint8_t m_boot_step = 0;
static bool    m_boot_flash_done = false;

static void boot_flash_fn(struct k_work *w);
static K_WORK_DELAYABLE_DEFINE(m_boot_flash_work, boot_flash_fn);

static void boot_flash_fn(struct k_work *w) {
    ARG_UNUSED(w);
    if (m_boot_step < BOOT_COUNT) {
        if (m_boot_step % 2 == 0) { led_on(); } else { led_off(); }
        k_work_schedule(&m_boot_flash_work, K_MSEC(BOOT_MS[m_boot_step]));
        m_boot_step++;
        return;
    }
    /* Flash done — now start the real BLE state logic. */
    led_off();
    m_boot_flash_done = true;

    uint8_t cur = (uint8_t)zmk_ble_active_profile_index();
    m_prev_prof   = cur;
    m_initialized = true;

    if (cur == ESB_PROFILE_IDX) {
        /* Booted already on ESB slot — fire double-blink as an orientation cue. */
        trigger_double_blink();
    } else {
        update_state();
    }
}

static int lariska_ble_led_init(void) {
    gpio_pin_configure_dt(&m_ldo, GPIO_OUTPUT_INACTIVE);
    pwm_set_dt(&m_red, m_red.period, 0); /* PWM off, pin HIGH (LED off) */
    /* Start boot flash after 500 ms so ZMK's BLE stack has time to settle. */
    k_work_schedule(&m_boot_flash_work, K_MSEC(500));
    return 0;
}

SYS_INIT(lariska_ble_led_init, APPLICATION, 92);
