/*
 * HA 8KEY Auto Host V2
 *
 * Automatic Bluetooth host roaming for ZMK.
 *
 * Behaviour:
 *  - Keep the current/last-used host while it is connected.
 *  - If it disappears, try the other bonded profiles automatically.
 *  - If no bonded host is available, expose an empty profile for new pairing.
 *  - If any known host connects, immediately make that profile active.
 *
 * Does NOT clear or overwrite existing bonds.
 */

#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/ble.h>

LOG_MODULE_REGISTER(ha_auto_host, CONFIG_HA_AUTO_HOST_LOG_LEVEL);

static struct k_work_delayable auto_host_work;

/* How long we advertise each bonded profile before trying the next one. */
#define HA_BONDED_PROFILE_DWELL_MS 4000

static int64_t profile_since;

/* Find any profile that is actually connected. Prefer the active one. */
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

/* Find the next occupied/bonded profile after the current one. */
static int find_next_bonded_profile(int current) {
    for (int offset = 1; offset <= ZMK_BLE_PROFILE_COUNT; offset++) {
        int i = (current + offset) % ZMK_BLE_PROFILE_COUNT;

        if (!zmk_ble_profile_is_open((uint8_t)i)) {
            return i;
        }
    }

    return -ENODEV;
}

/* Find an empty profile which can accept a new pairing. */
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
    int64_t now = k_uptime_get();

    /*
     * A host is connected.
     * Make its profile active and stay there.
     */
    if (connected >= 0) {
        profile_since = now;

        if (connected != active) {
            LOG_INF("HA Auto Host: connected profile %d -> active", connected);
            zmk_ble_prof_select((uint8_t)connected);
        }

        goto out;
    }

    if (profile_since == 0) {
        profile_since = now;
        goto out;
    }

    /*
     * No host is connected.
     *
     * If the current profile is bonded, give that host a few seconds
     * to reconnect. Then rotate through the other bonded profiles.
     */
    if (!zmk_ble_profile_is_open((uint8_t)active)) {
        if ((now - profile_since) >= HA_BONDED_PROFILE_DWELL_MS) {
            int next = find_next_bonded_profile(active);

            if (next >= 0 && next != active) {
                LOG_INF("HA Auto Host: trying bonded profile %d", next);
                zmk_ble_prof_select((uint8_t)next);
                profile_since = now;
                goto out;
            }

            /*
             * No other bonded profile exists.
             * Fall through to an empty profile after the configured delay.
             */
            if ((now - profile_since) >=
                CONFIG_HA_AUTO_HOST_NEW_PAIR_DELAY_MS) {
                int open = find_open_profile();

                if (open >= 0 && open != active) {
                    LOG_INF("HA Auto Host: opening profile %d for pairing", open);
                    zmk_ble_prof_select((uint8_t)open);
                    profile_since = now;
                }
            }
        }

        goto out;
    }

    /*
     * We are already on an empty profile.
     * Leave it discoverable for new pairing.
     *
     * Periodically return to a bonded profile so an existing PC
     * that becomes available can reconnect automatically.
     */
    if ((now - profile_since) >=
        CONFIG_HA_AUTO_HOST_NEW_PAIR_DELAY_MS) {

        int next = find_next_bonded_profile(active);

        if (next >= 0) {
            LOG_INF("HA Auto Host: retrying bonded profile %d", next);
            zmk_ble_prof_select((uint8_t)next);
            profile_since = now;
        }
    }

out:
    k_work_reschedule(&auto_host_work,
                      K_MSEC(CONFIG_HA_AUTO_HOST_POLL_MS));
}

static int ha_auto_host_init(void) {
    profile_since = 0;

    k_work_init_delayable(&auto_host_work, auto_host_tick);

    k_work_schedule(&auto_host_work,
                    K_MSEC(CONFIG_HA_AUTO_HOST_START_DELAY_MS));

    return 0;
}

SYS_INIT(ha_auto_host_init, APPLICATION, 90);
