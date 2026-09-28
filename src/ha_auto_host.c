/*
 * HA 8KEY Auto Host V1
 * Automatic ZMK BLE host/profile selection.
 */
#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/ble.h>

LOG_MODULE_REGISTER(ha_auto_host, CONFIG_HA_AUTO_HOST_LOG_LEVEL);

static struct k_work_delayable auto_host_work;
static int64_t no_host_since;

static int find_connected_profile(void) {
    int active = zmk_ble_active_profile_index();

    if (active >= 0 && active < ZMK_BLE_PROFILE_COUNT &&
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

    int active = zmk_ble_active_profile_index();
    int connected = find_connected_profile();

    if (connected >= 0) {
        no_host_since = 0;

        if (connected != active) {
            LOG_INF("Selecting connected BLE profile %d", connected);
            zmk_ble_prof_select((uint8_t)connected);
        }

        goto out;
    }

    if (no_host_since == 0) {
        no_host_since = k_uptime_get();
    }

    if ((k_uptime_get() - no_host_since) >= CONFIG_HA_AUTO_HOST_NEW_PAIR_DELAY_MS) {
        int open = find_open_profile();

        if (open >= 0 && open != active) {
            LOG_INF("No bonded host connected; selecting open profile %d", open);
            zmk_ble_prof_select((uint8_t)open);
        }
    }

out:
    k_work_reschedule(&auto_host_work, K_MSEC(CONFIG_HA_AUTO_HOST_POLL_MS));
}

static int ha_auto_host_init(void) {
    no_host_since = 0;
    k_work_init_delayable(&auto_host_work, auto_host_tick);
    k_work_schedule(&auto_host_work, K_MSEC(CONFIG_HA_AUTO_HOST_START_DELAY_MS));
    return 0;
}

SYS_INIT(ha_auto_host_init, APPLICATION, 90);
