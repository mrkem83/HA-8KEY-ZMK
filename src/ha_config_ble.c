/*
 * HA KEY CONFIG - BLE Service V4
 *
 * Universal keyboard mapping protocol.
 *
 * Goals:
 * - No HA-specific table such as 01=LEFT, 02=RIGHT.
 * - Read the REAL ZMK runtime keymap.
 * - Write ZMK encoded keycodes directly.
 * - Supports normal keyboard keys and modifier combinations
 *   supported by ZMK &kp.
 * - Save changes using ZMK persistent keymap storage.
 *
 * BLE packet for 8 buttons:
 *   8 x uint32_t = 32 bytes
 *
 * Each uint32_t is the ZMK encoded keycode stored in param1
 * of the existing &kp behavior.
 *
 * Byte order over BLE: little-endian.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <zmk/behavior.h>
#include <zmk/keymap.h>

LOG_MODULE_REGISTER(ha_config_ble, LOG_LEVEL_INF);

#define HA_KEY_COUNT 8
#define HA_KEY_BYTES 4
#define HA_KEYMAP_PACKET_SIZE (HA_KEY_COUNT * HA_KEY_BYTES)

/* -------------------------------------------------------------------------- */
/* UUIDs                                                                      */
/* -------------------------------------------------------------------------- */

/* HA CONFIG SERVICE */
#define BT_UUID_HA_CONFIG_VAL \
    BT_UUID_128_ENCODE(0x7d4a0001, 0x7c3a, 0x4b9d, 0x9a51, 0x48414b455950)

#define BT_UUID_HA_CONFIG \
    BT_UUID_DECLARE_128(BT_UUID_HA_CONFIG_VAL)

/* DEVICE INFO */
#define BT_UUID_HA_DEVICE_INFO_VAL \
    BT_UUID_128_ENCODE(0x7d4a0002, 0x7c3a, 0x4b9d, 0x9a51, 0x48414b455950)

#define BT_UUID_HA_DEVICE_INFO \
    BT_UUID_DECLARE_128(BT_UUID_HA_DEVICE_INFO_VAL)

/* UNIVERSAL KEYMAP */
#define BT_UUID_HA_KEYMAP_VAL \
    BT_UUID_128_ENCODE(0x7d4a0003, 0x7c3a, 0x4b9d, 0x9a51, 0x48414b455950)

#define BT_UUID_HA_KEYMAP \
    BT_UUID_DECLARE_128(BT_UUID_HA_KEYMAP_VAL)

/* -------------------------------------------------------------------------- */
/* Device information                                                         */
/* -------------------------------------------------------------------------- */

static const char ha_device_info[] = "HA-8KEY|CFG4|8|UKP1";

/*
 * BLE packet.
 *
 * Button 1 = bytes  0..3
 * Button 2 = bytes  4..7
 * ...
 * Button 8 = bytes 28..31
 *
 * Each value is uint32 little-endian.
 */
static uint8_t ha_keymap_packet[HA_KEYMAP_PACKET_SIZE];

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static void packet_put_keycode(uint8_t button, uint32_t keycode)
{
    sys_put_le32(keycode,
                 &ha_keymap_packet[button * HA_KEY_BYTES]);
}

static uint32_t packet_get_keycode(const uint8_t *packet, uint8_t button)
{
    return sys_get_le32(&packet[button * HA_KEY_BYTES]);
}

/* -------------------------------------------------------------------------- */
/* Read REAL ZMK runtime keymap                                                */
/* -------------------------------------------------------------------------- */

static int refresh_packet_from_zmk(void)
{
    zmk_keymap_layer_id_t layer = zmk_keymap_layer_default();

    for (uint16_t i = 0; i < HA_KEY_COUNT; i++) {
        const struct zmk_behavior_binding *binding =
            zmk_keymap_get_layer_binding_at_idx(layer, i);

        if (binding == NULL) {
            LOG_ERR("HA V4: cannot read button %u", i + 1);
            return -EIO;
        }

        /*
         * HA V4 is intentionally aimed at keyboard / hotkey mapping.
         *
         * The existing behavior itself is preserved.
         * We expose its param1 exactly as ZMK stores it.
         */
        packet_put_keycode(i, binding->param1);
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
/* Write REAL ZMK runtime keymap                                               */
/* -------------------------------------------------------------------------- */

static int apply_packet_to_zmk(const uint8_t *packet)
{
    zmk_keymap_layer_id_t layer = zmk_keymap_layer_default();

    /*
     * First verify that all 8 current bindings exist.
     * Do this before modifying anything.
     */
    for (uint16_t i = 0; i < HA_KEY_COUNT; i++) {
        const struct zmk_behavior_binding *binding =
            zmk_keymap_get_layer_binding_at_idx(layer, i);

        if (binding == NULL) {
            LOG_ERR("HA V4: missing binding for button %u", i + 1);
            return -EIO;
        }
    }

    /*
     * Preserve the existing ZMK behavior for every button.
     * Only replace param1 with the new encoded ZMK keycode.
     *
     * For the HA keypad base layer these are &kp bindings.
     * This means letters, numbers, arrows, F-keys,
     * modifiers and modifier+key combinations remain native ZMK.
     */
    for (uint16_t i = 0; i < HA_KEY_COUNT; i++) {
        const struct zmk_behavior_binding *current =
            zmk_keymap_get_layer_binding_at_idx(layer, i);

        struct zmk_behavior_binding updated = *current;

        updated.param1 = packet_get_keycode(packet, i);
        updated.param2 = 0;

        int ret =
            zmk_keymap_set_layer_binding_at_idx(layer, i, updated);

        if (ret < 0) {
            LOG_ERR("HA V4: failed setting button %u (%d)",
                    i + 1, ret);
            return ret;
        }
    }

    /*
     * Save using ZMK's own runtime keymap persistence.
     * This is the same persistent keymap infrastructure
     * used by ZMK Studio.
     */
    int ret = zmk_keymap_save_changes();

    if (ret < 0) {
        LOG_ERR("HA V4: save failed (%d)", ret);
        return ret;
    }

    memcpy(ha_keymap_packet, packet, HA_KEYMAP_PACKET_SIZE);

    LOG_INF("HA V4: universal keymap updated and saved");

    return 0;
}

/* -------------------------------------------------------------------------- */
/* GATT callbacks                                                              */
/* -------------------------------------------------------------------------- */

static ssize_t read_device_info(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                void *buf,
                                uint16_t len,
                                uint16_t offset)
{
    const char *value = attr->user_data;

    return bt_gatt_attr_read(conn,
                             attr,
                             buf,
                             len,
                             offset,
                             value,
                             strlen(value));
}

static ssize_t read_keymap(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr,
                           void *buf,
                           uint16_t len,
                           uint16_t offset)
{
    int ret = refresh_packet_from_zmk();

    if (ret < 0) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    return bt_gatt_attr_read(conn,
                             attr,
                             buf,
                             len,
                             offset,
                             ha_keymap_packet,
                             sizeof(ha_keymap_packet));
}

static ssize_t write_keymap(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            const void *buf,
                            uint16_t len,
                            uint16_t offset,
                            uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);

    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len != HA_KEYMAP_PACKET_SIZE) {
        LOG_ERR("HA V4: invalid packet size %u, expected %u",
                len, HA_KEYMAP_PACKET_SIZE);

        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    uint8_t new_packet[HA_KEYMAP_PACKET_SIZE];

    memcpy(new_packet, buf, HA_KEYMAP_PACKET_SIZE);

    int ret = apply_packet_to_zmk(new_packet);

    if (ret < 0) {
        LOG_ERR("HA V4: mapping rejected (%d)", ret);
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    return len;
}

/* -------------------------------------------------------------------------- */
/* BLE Service                                                                 */
/* -------------------------------------------------------------------------- */

BT_GATT_SERVICE_DEFINE(
    ha_config_service,

    BT_GATT_PRIMARY_SERVICE(BT_UUID_HA_CONFIG),

    BT_GATT_CHARACTERISTIC(
        BT_UUID_HA_DEVICE_INFO,
        BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ,
        read_device_info,
        NULL,
        (void *)ha_device_info
    ),

    BT_GATT_CHARACTERISTIC(
        BT_UUID_HA_KEYMAP,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
        read_keymap,
        write_keymap,
        NULL
    )
);
