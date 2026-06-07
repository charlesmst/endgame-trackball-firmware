/*
 * Dynamically adjusts PAW3395 report rate based on the active ZMK endpoint:
 *   USB or ESB BLE slot (last profile)  → 1 ms  (1000 Hz)
 *   Regular BLE (profiles 0 … N-2)      → 8 ms  (~125 Hz)
 *
 * The ESB slot runs on the last BLE profile (ZMK_BLE_PROFILE_COUNT - 1),
 * same index the zmk-esb-endpoint module uses to activate ESB PTX mode.
 *
 * Why two subscriptions instead of just zmk_endpoint_changed:
 *   zmk_endpoint_changed is raised by update_current_endpoint() only when
 *   current_instance changes. At boot, current_instance is initialised from
 *   get_selected_instance() *before* NVS settings are loaded, so it lands on
 *   BLE profile 0. Settings then restore the true profile (e.g. ESB = N-1)
 *   without raising zmk_ble_active_profile_changed (ZMK does not emit it for
 *   the settings-restore path). Consequently current_instance stays stale at
 *   profile 0, and a later switch FROM ESB TO profile 0 looks like no change
 *   to update_current_endpoint(), so zmk_endpoint_changed never fires.
 *   Subscribing directly to zmk_ble_active_profile_changed and reading
 *   zmk_ble_active_profile_index() sidesteps the stale-cache entirely.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/endpoints.h>
#include <zmk/ble.h>
#include <paw3395.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lariska_rate, CONFIG_ZMK_LOG_LEVEL);

#define RATE_USB_MS  1   /* 1000 Hz */
#define RATE_ESB_MS  1   /* 1000 Hz — dongle polls at 1 kHz */
#define RATE_BLE_MS  8   /* ~125 Hz — fits BLE 7.5 ms min conn interval */

static void apply_rate(void) {
    const struct device *sensor = DEVICE_DT_GET(DT_NODELABEL(mou0));
    if (!device_is_ready(sensor)) {
        return;
    }

    int32_t interval_ms;
    struct zmk_endpoint_instance ep = zmk_endpoints_selected();
    if (ep.transport == ZMK_TRANSPORT_USB) {
        interval_ms = RATE_USB_MS;
    } else if (zmk_ble_active_profile_index() == ZMK_BLE_PROFILE_COUNT - 1) {
        interval_ms = RATE_ESB_MS;
    } else {
        interval_ms = RATE_BLE_MS;
    }

    struct sensor_value val = {.val1 = interval_ms};
    int err = sensor_attr_set(sensor, SENSOR_CHAN_ALL,
                              (enum sensor_attribute)PAW3395_ATTR_REPORT_INTERVAL_MS, &val);
    if (err) {
        LOG_WRN("rate set failed: %d", err);
    } else {
        LOG_INF("report rate: %d ms (%d Hz)", interval_ms, 1000 / interval_ms);
    }
}

static int rate_event_cb(const zmk_event_t *eh) {
    apply_rate();
    return ZMK_EV_EVENT_BUBBLE;
}

/* USB transport changes */
ZMK_LISTENER(lariska_rate_ep_listener, rate_event_cb);
ZMK_SUBSCRIPTION(lariska_rate_ep_listener, zmk_endpoint_changed);

/* BLE profile switches — including switching away from / back to the ESB slot */
ZMK_LISTENER(lariska_rate_ble_listener, rate_event_cb);
ZMK_SUBSCRIPTION(lariska_rate_ble_listener, zmk_ble_active_profile_changed);

/*
 * At boot, neither event fires for the settings-restored profile.
 * Run apply_rate() after the sensor and NVS have settled so the correct
 * rate is set without requiring a profile cycle.
 */
static void boot_rate_fn(struct k_work *work) {
    apply_rate();
}
static K_WORK_DELAYABLE_DEFINE(boot_rate_work, boot_rate_fn);

static int lariska_rate_init(void) {
    k_work_schedule(&boot_rate_work, K_MSEC(2000));
    return 0;
}

SYS_INIT(lariska_rate_init, APPLICATION, 91);
