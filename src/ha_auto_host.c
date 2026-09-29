/*
 * HA 8KEY Auto Host V3
 *
 * Based on stable V1.
 * Important:
 * - Never change BLE profiles while USB is connected.
 * - Keep ZMK Studio / USB HID untouched.
 * - Keep current host while connected.
 * - If another known profile is already connected, select it.
 * - If no host is connected, allow an empty profile for pairing.
 */

#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/ble.h>
#include <zmk/usb.h>

LOG_MODULE_REGISTER(ha_auto_host, CONFIG_HA_AUTO_HOST_LOG_LEVEL);

static struct k_work_delayable auto_host_work;
static int64_t no_host_since;

/*
 * Return true whenever USB is actually connected/configured.
 * While this is true Auto Host must not touch BLE profiles.
 */
static bool usb_is_active(void) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    enum zmk_usb_conn_state state = zmk_usb_get_conn_state();

    return state != ZMK_USB_CONN_NONE;
#else
    return false;
#endif
}

static int find_connected_profile(void) {
    int active = zmk_ble_active_profile_index();

    if (active >= 0 &&
        active < ZMK_BLE_PROFILE_COUNT &&
        zmk_ble_profile_is_connected((uint8_t)active)) {
        return active;
    }

    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        if (zmk_ble_profile_is_connected((uint8_t)i)) {
            return i;
        }
    }

    return -ENODEV;
}

static int find_open_profile(void) {
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        if (zmk_ble_profile_is_open((uint8_t)i)) {
            return i;
        }
    }

    return -ENOSPC;
}

static void auto_host_tick(struct k_work *work) {
    ARG_UNUSED(work);

    /*
     * CRITICAL V3 RULE:
     * When USB is connected, do absolutely nothing to BLE profiles.
     * This protects ZMK Studio and USB HID sessions.
     */
    if (usb_is_active()) {
        no_host_since = 0;
        goto out;
    }

    int active = zmk_ble_active_profile_index();
    int connected = find_connected_profile();

    /*
     * Current host or another bonded host is connected.
     * If another profile connected, make it active.
     */
    if (connected >= 0) {
        no_host_since = 0;

        if (connected != active) {
            LOG_INF("HA V3: selecting connected profile %d", connected);
            zmk_ble_prof_select((uint8_t)connected);
        }

        goto out;
    }

    /*
     * No Bluetooth host is connected.
     */
    if (no_host_since == 0) {
        no_host_since = k_uptime_get();
    }

    /*
     * After the normal V1 delay, expose an empty profile
     * so a completely new PC can pair.
     */
    if ((k_uptime_get() - no_host_since) >=
        CONFIG_HA_AUTO_HOST_NEW_PAIR_DELAY_MS) {

        int open = find_open_profile();

        if (open >= 0 && open != active) {
            LOG_INF("HA V3: selecting open profile %d", open);
            zmk_ble_prof_select((uint8_t)open);
        }

        no_host_since = k_uptime_get();
    }

out:
    k_work_reschedule(&auto_host_work,
                      K_MSEC(CONFIG_HA_AUTO_HOST_POLL_MS));
}

static int ha_auto_host_init(void) {
    no_host_since = 0;

    k_work_init_delayable(&auto_host_work, auto_host_tick);

    k_work_schedule(&auto_host_work,
                    K_MSEC(CONFIG_HA_AUTO_HOST_START_DELAY_MS));

    return 0;
}

SYS_INIT(ha_auto_host_init, APPLICATION, 90);
