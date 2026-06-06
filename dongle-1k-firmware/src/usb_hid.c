/*
 * Copyright (c) 2025 efogdev
 * SPDX-License-Identifier: MIT
 *
 * USB HID device with 3 report IDs matching ZMK's HID report bodies:
 *   ID 1: keyboard  — 8 bytes (modifiers + reserved + 6 keycodes)
 *   ID 2: consumer  — 2 bytes (16-bit usage)
 *   ID 3: mouse     — 9 bytes (buttons + dx(2) + dy(2) + wheel_y(2) + wheel_x(2))
 */

#include <zephyr/kernel.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include "usb_hid.h"

/* Zephyr's hid.h omits consumer page constants */
#define HID_USAGE_GEN_CONSUMER         0x0C
#define HID_USAGE_GEN_CONSUMER_CONTROL 0x01

/* Zephyr's hid.h lacks PUSH/POP and physical-range macros; match ZMK's shims. */
#ifndef HID_ITEM_TAG_PUSH
#define HID_ITEM_TAG_PUSH 0xA
#endif
#ifndef HID_ITEM_TAG_POP
#define HID_ITEM_TAG_POP  0xB
#endif
#define HID_PUSH HID_ITEM(HID_ITEM_TAG_PUSH, HID_ITEM_TYPE_GLOBAL, 0)
#define HID_POP  HID_ITEM(HID_ITEM_TAG_POP,  HID_ITEM_TYPE_GLOBAL, 0)
#ifndef HID_PHYSICAL_MIN8
#define HID_PHYSICAL_MIN8(a) HID_ITEM(HID_ITEM_TAG_PHYSICAL_MIN, HID_ITEM_TYPE_GLOBAL, 1), a
#endif
#ifndef HID_PHYSICAL_MAX8
#define HID_PHYSICAL_MAX8(a) HID_ITEM(HID_ITEM_TAG_PHYSICAL_MAX, HID_ITEM_TYPE_GLOBAL, 1), a
#endif

/* Generic Desktop Resolution Multiplier (HUT 1.12 §4) */
#define HID_USAGE_GD_RESOLUTION_MULTIPLIER 0x48

/* wValue layout for GET_REPORT / SET_REPORT control transfers. */
#define HID_GET_REPORT_TYPE_MASK 0xff00
#define HID_GET_REPORT_ID_MASK   0x00ff
#define HID_REPORT_TYPE_FEATURE  0x300

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dongle_usb, LOG_LEVEL_INF);

#define REPORT_ID_KB        CONFIG_DONGLE_HID_REPORT_ID_KB
#define REPORT_ID_CONSUMER  CONFIG_DONGLE_HID_REPORT_ID_CONSUMER
#define REPORT_ID_MOUSE     CONFIG_DONGLE_HID_REPORT_ID_MOUSE

static const uint8_t hid_desc[] = {
    /* ---- Keyboard ---- */
    HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP),
    HID_USAGE(HID_USAGE_GEN_DESKTOP_KEYBOARD),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
        HID_REPORT_ID(REPORT_ID_KB),
        /* Modifier keys: 8 bits */
        HID_USAGE_PAGE(HID_USAGE_GEN_KEYBOARD),
        HID_USAGE_MIN8(0xE0),
        HID_USAGE_MAX8(0xE7),
        HID_LOGICAL_MIN8(0),
        HID_LOGICAL_MAX8(1),
        HID_REPORT_SIZE(1),
        HID_REPORT_COUNT(8),
        HID_INPUT(0x02),    /* Data, Var, Abs */
        /* Reserved byte */
        HID_REPORT_SIZE(8),
        HID_REPORT_COUNT(1),
        HID_INPUT(0x01),    /* Const */
        /* Keycodes: 6 bytes */
        HID_USAGE_MIN8(0x00),
        HID_USAGE_MAX8(0xFF),
        HID_LOGICAL_MIN8(0x00),
        HID_LOGICAL_MAX8(0xFF),
        HID_REPORT_SIZE(8),
        HID_REPORT_COUNT(6),
        HID_INPUT(0x00),    /* Data, Array */
    HID_END_COLLECTION,

    /* ---- Consumer control ---- */
    HID_USAGE_PAGE(HID_USAGE_GEN_CONSUMER),
    HID_USAGE(HID_USAGE_GEN_CONSUMER_CONTROL),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
        HID_REPORT_ID(REPORT_ID_CONSUMER),
        HID_USAGE_MIN16(0x00, 0x00),
        HID_USAGE_MAX16(0xFF, 0x03),
        HID_LOGICAL_MIN16(0x00, 0x00),
        HID_LOGICAL_MAX16(0xFF, 0x03),
        HID_REPORT_SIZE(16),
        HID_REPORT_COUNT(1),
        HID_INPUT(0x00),    /* Data, Array */
    HID_END_COLLECTION,

    /* ---- Mouse ---- */
    HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP),
    HID_USAGE(HID_USAGE_GEN_DESKTOP_MOUSE),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
        HID_USAGE(HID_USAGE_GEN_DESKTOP_POINTER),
        HID_COLLECTION(HID_COLLECTION_PHYSICAL),
            HID_REPORT_ID(REPORT_ID_MOUSE),
            /* Buttons: 8 bits (CONFIG_DONGLE_MOUSE_BUTTON_COUNT used + pad) */
            HID_USAGE_PAGE(HID_USAGE_GEN_BUTTON),
            HID_USAGE_MIN8(1),
            HID_USAGE_MAX8(CONFIG_DONGLE_MOUSE_BUTTON_COUNT),
            HID_LOGICAL_MIN8(0),
            HID_LOGICAL_MAX8(1),
            HID_REPORT_SIZE(1),
            HID_REPORT_COUNT(CONFIG_DONGLE_MOUSE_BUTTON_COUNT),
            HID_INPUT(0x02),
            HID_REPORT_SIZE(1),
            HID_REPORT_COUNT(8 - CONFIG_DONGLE_MOUSE_BUTTON_COUNT),
            HID_INPUT(0x01),    /* Const pad */
            /* X, Y: 16-bit each */
            HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP),
            HID_USAGE(HID_USAGE_GEN_DESKTOP_X),
            HID_USAGE(HID_USAGE_GEN_DESKTOP_Y),
            HID_LOGICAL_MIN16(0x01, 0x80),  /* -32767 */
            HID_LOGICAL_MAX16(0xFF, 0x7F),  /*  32767 */
            HID_REPORT_SIZE(16),
            HID_REPORT_COUNT(2),
            HID_INPUT(0x06),    /* Data, Var, Rel */
            /* Wheel Y — no resolution multiplier; 1 raw unit = 1 scroll notch,
             * matching ZMK USB/BLE (CONFIG_ZMK_POINTING_SMOOTH_SCROLLING=n). */
            HID_COLLECTION(HID_COLLECTION_LOGICAL),
                HID_USAGE(HID_USAGE_GEN_DESKTOP_WHEEL),
                HID_LOGICAL_MIN16(0x01, 0x80),
                HID_LOGICAL_MAX16(0xFF, 0x7F),
                HID_PHYSICAL_MIN8(0x00),
                HID_PHYSICAL_MAX8(0x00),
                HID_REPORT_SIZE(16),
                HID_REPORT_COUNT(1),
                HID_INPUT(0x06),    /* Data, Var, Rel */
            HID_END_COLLECTION,
            /* AC Pan */
            HID_COLLECTION(HID_COLLECTION_LOGICAL),
                HID_USAGE_PAGE(HID_USAGE_GEN_CONSUMER),
                0x0A, 0x38, 0x02,   /* Usage AC Pan (16-bit, consumer page) */
                HID_LOGICAL_MIN16(0x01, 0x80),
                HID_LOGICAL_MAX16(0xFF, 0x7F),
                HID_PHYSICAL_MIN8(0x00),
                HID_PHYSICAL_MAX8(0x00),
                HID_REPORT_SIZE(16),
                HID_REPORT_COUNT(1),
                HID_INPUT(0x06),    /* Data, Var, Rel */
            HID_END_COLLECTION,
        HID_END_COLLECTION,
    HID_END_COLLECTION,
};

static const struct device *hdev;
static bool m_configured;

#define TX_QUEUE_DEPTH CONFIG_DONGLE_USB_TX_QUEUE_DEPTH

struct pending_report {
    uint8_t report_id;
    uint8_t len;
    uint8_t data[32];
};

static struct pending_report tx_queue[TX_QUEUE_DEPTH];
static uint8_t tx_head;
static uint8_t tx_tail;
static bool ep_busy;
static struct k_spinlock tx_lock;


static int submit_locked(const uint8_t report_id, const uint8_t *data, const uint8_t len) {
    uint8_t buf[1 + sizeof(((struct pending_report *)0)->data)];
    const uint8_t copy_len = MIN(len, (uint8_t)(sizeof(buf) - 1));
    buf[0] = report_id;
    memcpy(&buf[1], data, copy_len);

    uint32_t wrote;
    const int err = hid_int_ep_write(hdev, buf, 1 + copy_len, &wrote);
    if (err) {
        const k_spinlock_key_t key = k_spin_lock(&tx_lock);
        ep_busy = false;
        k_spin_unlock(&tx_lock, key);
        LOG_ERR("hid_int_ep_write: %d", err);
    }
    return err;
}

static void int_in_ready_cb(const struct device *dev) {
    ARG_UNUSED(dev);
    const k_spinlock_key_t key = k_spin_lock(&tx_lock);
    if (tx_head == tx_tail) {
        ep_busy = false;
        k_spin_unlock(&tx_lock, key);
        return;
    }
    const struct pending_report r = tx_queue[tx_head];
    tx_head = (tx_head + 1) % TX_QUEUE_DEPTH;
    k_spin_unlock(&tx_lock, key);
    (void)submit_locked(r.report_id, r.data, r.len);
}

static const struct hid_ops ops = {
    .int_in_ready = int_in_ready_cb,
};

static void usb_status_cb(const enum usb_dc_status_code status, const uint8_t *param) {
    switch (status) {
    case USB_DC_RESET:
        if (m_configured) {
            LOG_INF("USB reset");
        }
        m_configured = false;
        break;
    case USB_DC_CONFIGURED:
        if (!m_configured) {
            const k_spinlock_key_t key = k_spin_lock(&tx_lock);
            ep_busy = false;
            tx_head = tx_tail = 0;
            k_spin_unlock(&tx_lock, key);
            m_configured = true;
            LOG_INF("USB HID configured");
        }
        break;
    case USB_DC_SUSPEND:
        LOG_INF("USB suspend");
        break;
    case USB_DC_RESUME:
        LOG_INF("USB resume");
        break;
    case USB_DC_DISCONNECTED:
        LOG_INF("USB disconnected");
        m_configured = false;
        break;
    case USB_DC_CONNECTED:
        LOG_INF("USB connected");
        break;
    default:
        break;
    }
}

int usb_hid_dongle_init(void) {
    hdev = device_get_binding("HID_0");
    if (!hdev) {
        LOG_ERR("USB HID device not found");
        return -ENODEV;
    }

    usb_hid_register_device(hdev, hid_desc, sizeof(hid_desc), &ops);
    if (usb_hid_set_proto_code(hdev, HID_BOOT_IFACE_CODE_NONE)) {
        LOG_WRN("Failed to set protocol code");
    }

    int err = usb_hid_init(hdev);
    if (err) {
        LOG_ERR("usb_hid_init: %d", err);
        return err;
    }

    err = usb_enable(usb_status_cb);
    if (err) {
        LOG_ERR("usb_enable: %d", err);
        return err;
    }

    LOG_INF("USB HID registered (KB=%u CONS=%u MOUSE=%u, desc=%u bytes)",
            REPORT_ID_KB, REPORT_ID_CONSUMER, REPORT_ID_MOUSE,
            (unsigned)sizeof(hid_desc));
    return 0;
}

int usb_hid_send(const uint8_t report_type, const uint8_t *data, const uint8_t len) {
    if (!m_configured) {
        return -ENOTCONN;
    }
    if (len > sizeof(((struct pending_report *)0)->data)) {
        return -EMSGSIZE;
    }

    const k_spinlock_key_t key = k_spin_lock(&tx_lock);
    if (!ep_busy) {
        ep_busy = true;
        k_spin_unlock(&tx_lock, key);
        return submit_locked(report_type, data, len);
    }

    const uint8_t next = (tx_tail + 1) % TX_QUEUE_DEPTH;
    if (next == tx_head) {
        k_spin_unlock(&tx_lock, key);
        LOG_WRN("TX queue full, dropping id=%u", report_type);
        return -ENOBUFS;
    }
    tx_queue[tx_tail].report_id = report_type;
    tx_queue[tx_tail].len = len;
    memcpy(tx_queue[tx_tail].data, data, len);
    tx_tail = next;
    k_spin_unlock(&tx_lock, key);
    return 0;
}
