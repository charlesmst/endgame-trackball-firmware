/*
 * Dynamically adjusts PAW3395 report rate based on the active ZMK endpoint:
 *   USB or ESB BLE slot (last profile)  → 1 ms  (1000 Hz)
 *   Regular BLE (profiles 0 … N-2)      → 8 ms  (~125 Hz)
 *
 * The ESB slot runs on the last BLE profile (ZMK_BLE_PROFILE_COUNT - 1),
 * same index the zmk-esb-endpoint module uses to activate ESB PTX mode.
 * Because ESB is unidirectional RF and the dongle polls at 1000 Hz, running
 * the sensor at 1000 Hz there matches USB latency.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/endpoints.h>
#include <zmk/ble.h>
#include <paw3395.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lariska_rate, CONFIG_ZMK_LOG_LEVEL);

#define RATE_USB_MS  1   /* 1000 Hz */
#define RATE_ESB_MS  1   /* 1000 Hz — dongle polls at 1 kHz */
#define RATE_BLE_MS  8   /* ~125 Hz — fits BLE 7.5 ms min conn interval */

static void set_rate(struct zmk_endpoint_instance ep) {
    const struct device *sensor = DEVICE_DT_GET(DT_NODELABEL(mou0));
    if (!device_is_ready(sensor)) {
        return;
    }

    int32_t interval_ms;
    if (ep.transport == ZMK_TRANSPORT_USB) {
        interval_ms = RATE_USB_MS;
    } else if (ep.transport == ZMK_TRANSPORT_BLE &&
               ep.ble.profile_index == ZMK_BLE_PROFILE_COUNT - 1) {
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

static int endpoint_rate_cb(const zmk_event_t *eh) {
    const struct zmk_endpoint_changed *ev = as_zmk_endpoint_changed(eh);
    if (ev) {
        set_rate(ev->endpoint);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(lariska_rate_listener, endpoint_rate_cb);
ZMK_SUBSCRIPTION(lariska_rate_listener, zmk_endpoint_changed);

/* zmk_endpoint_changed is not raised at boot; poll once after ZMK settles. */
static void boot_check_fn(struct k_work *work) {
    set_rate(zmk_endpoints_selected());
}
static K_WORK_DELAYABLE_DEFINE(boot_check_work, boot_check_fn);

static int lariska_rate_init(void) {
    k_work_schedule(&boot_check_work, K_MSEC(2000));
    return 0;
}

SYS_INIT(lariska_rate_init, APPLICATION, 91);
