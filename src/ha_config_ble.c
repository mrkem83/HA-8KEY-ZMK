/*
 * HA KEY CONFIG - BLE Service V2
 *
 * Wireless configuration protocol for HA keypads.
 * V2:
 * - Device information: READ
 * - Key mapping: READ + WRITE
 *
 * V2 only tests wireless mapping transfer.
 * It does NOT change HID key output yet.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ha_config_ble, LOG_LEVEL_INF);

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

/* KEY MAPPING */
#define BT_UUID_HA_KEYMAP_VAL \
    BT_UUID_128_ENCODE(0x7d4a0003, 0x7c3a, 0x4b9d, 0x9a51, 0x48414b455950)

#define BT_UUID_HA_KEYMAP \
    BT_UUID_DECLARE_128(BT_UUID_HA_KEYMAP_VAL)


static const char ha_device_info[] = "HA-8KEY|CFG2|8";

/*
 * V2 test mapping.
 * One byte per physical Button ID.
 *
 * Button:
 * 1 2 3 4 5 6 7 8
 */
static uint8_t ha_keymap[8] = {
    1, 2, 3, 4, 5, 6, 7, 8
};


/* READ DEVICE INFO */
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


/* READ KEYMAP */
static ssize_t read_keymap(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr,
                           void *buf,
                           uint16_t len,
                           uint16_t offset)
{
    return bt_gatt_attr_read(conn,
                             attr,
                             buf,
                             len,
                             offset,
                             ha_keymap,
                             sizeof(ha_keymap));
}


/* WRITE KEYMAP */
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

    if (len != sizeof(ha_keymap)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    memcpy(ha_keymap, buf, sizeof(ha_keymap));

    LOG_INF("HA Config: received new 8-key mapping");

    return len;
}


/* BLE SERVICE */
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
