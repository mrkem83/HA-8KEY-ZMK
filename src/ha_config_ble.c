/*
 * HA KEY CONFIG - BLE Service V1
 *
 * Wireless configuration service for HA keypads.
 * Independent from physical key layout.
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ha_config_ble, LOG_LEVEL_INF);

/*
 * HA Config Service UUID
 * 7d4a0001-7c3a-4b9d-9a51-48414b455950
 */
#define BT_UUID_HA_CONFIG_VAL \
    BT_UUID_128_ENCODE(0x7d4a0001, 0x7c3a, 0x4b9d, 0x9a51, 0x48414b455950)

#define BT_UUID_HA_CONFIG BT_UUID_DECLARE_128(BT_UUID_HA_CONFIG_VAL)

/*
 * Device Info Characteristic
 * 7d4a0002-7c3a-4b9d-9a51-48414b455950
 */
#define BT_UUID_HA_DEVICE_INFO_VAL \
    BT_UUID_128_ENCODE(0x7d4a0002, 0x7c3a, 0x4b9d, 0x9a51, 0x48414b455950)

#define BT_UUID_HA_DEVICE_INFO \
    BT_UUID_DECLARE_128(BT_UUID_HA_DEVICE_INFO_VAL)

static const char ha_device_info[] = "HA-8KEY|CFG1|8";

static ssize_t read_device_info(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                void *buf,
                                uint16_t len,
                                uint16_t offset) {
    const char *value = attr->user_data;

    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             value, strlen(value));
}

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
    )
);

