/*
 * HA KEY CONFIG - BLE Service V3
 *
 * V3:
 * - Device information: READ
 * - Key mapping: READ + WRITE
 * - Written mapping changes the REAL ZMK keymap
 * - Changes are saved using ZMK keymap storage
 * - Mapping survives power off / reboot
 *
 * Protocol V3 test IDs:
 *   1 = LEFT
 *   2 = RIGHT
 *   3 = UP
 *   4 = DOWN
 *   5 = ENTER
 *   6 = ESC
 *   7 = SPACE
 *   8 = TAB
 */

#include <errno.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/keymap.h>

#include <dt-bindings/zmk/keys.h>

LOG_MODULE_REGISTER(ha_config_ble, LOG_LEVEL_INF);

#define HA_KEY_COUNT 8

/* -------------------------------------------------------------------------- */
/* UUIDs                                                                      */
/* -------------------------------------------------------------------------- */

/* HA CONFIG SERVICE */
#define BT_UUID_HA_CONFIG_VAL                                                    \
    BT_UUID_128_ENCODE(0x7d4a0001, 0x7c3a, 0x4b9d, 0x9a51, 0x48414b455950)

#define BT_UUID_HA_CONFIG                                                       \
    BT_UUID_DECLARE_128(BT_UUID_HA_CONFIG_VAL)

/* DEVICE INFO */
#define BT_UUID_HA_DEVICE_INFO_VAL                                               \
    BT_UUID_128_ENCODE(0x7d4a0002, 0x7c3a, 0x4b9d, 0x9a51, 0x48414b455950)

#define BT_UUID_HA_DEVICE_INFO                                                   \
    BT_UUID_DECLARE_128(BT_UUID_HA_DEVICE_INFO_VAL)

/* KEY MAPPING */
#define BT_UUID_HA_KEYMAP_VAL                                                    \
    BT_UUID_128_ENCODE(0x7d4a0003, 0x7c3a, 0x4b9d, 0x9a51, 0x48414b455950)

#define BT_UUID_HA_KEYMAP                                                        \
    BT_UUID_DECLARE_128(BT_UUID_HA_KEYMAP_VAL)

/* -------------------------------------------------------------------------- */
/* Device information                                                         */
/* -------------------------------------------------------------------------- */

static const char ha_device_info[] = "HA-8KEY|CFG3|8";

/*
 * HA protocol IDs.
 *
 * These IDs are intentionally independent from GPIO numbers
 * and independent from the physical screen layout.
 */
enum ha_key_id {
    HA_KEY_LEFT  = 1,
    HA_KEY_RIGHT = 2,
    HA_KEY_UP    = 3,
    HA_KEY_DOWN  = 4,
    HA_KEY_ENTER = 5,
    HA_KEY_ESC   = 6,
    HA_KEY_SPACE = 7,
    HA_KEY_TAB   = 8,
};

/*
 * BLE representation of the current mapping.
 * Refreshed from the real ZMK keymap before every READ.
 */
static uint8_t ha_keymap[HA_KEY_COUNT];

/* -------------------------------------------------------------------------- */
/* HA ID <-> ZMK keycode                                                       */
/* -------------------------------------------------------------------------- */

static int ha_id_to_zmk_keycode(uint8_t id, uint32_t *keycode)
{
    if (keycode == NULL) {
        return -EINVAL;
    }

    switch (id) {
    case HA_KEY_LEFT:
        *keycode = LEFT;
        return 0;

    case HA_KEY_RIGHT:
        *keycode = RIGHT;
        return 0;

    case HA_KEY_UP:
        *keycode = UP;
        return 0;

    case HA_KEY_DOWN:
        *keycode = DOWN;
        return 0;

    case HA_KEY_ENTER:
        *keycode = ENTER;
        return 0;

    case HA_KEY_ESC:
        *keycode = ESC;
        return 0;

    case HA_KEY_SPACE:
        *keycode = SPACE;
        return 0;

    case HA_KEY_TAB:
        *keycode = TAB;
        return 0;

    default:
        return -EINVAL;
    }
}

static uint8_t zmk_keycode_to_ha_id(uint32_t keycode)
{
    switch (keycode) {
    case LEFT:
        return HA_KEY_LEFT;

    case RIGHT:
        return HA_KEY_RIGHT;

    case UP:
        return HA_KEY_UP;

    case DOWN:
        return HA_KEY_DOWN;

    case ENTER:
        return HA_KEY_ENTER;

    case ESC:
        return HA_KEY_ESC;

    case SPACE:
        return HA_KEY_SPACE;

    case TAB:
        return HA_KEY_TAB;

    default:
        /*
         * 0 means that the current ZMK binding is not represented
         * by the limited V3 test protocol.
         *
         * This is important because an existing Studio mapping may
         * contain hotkeys such as Alt+F4 or Win+L.
         */
        return 0;
    }
}

/* -------------------------------------------------------------------------- */
/* Read current REAL ZMK keymap                                                */
/* -------------------------------------------------------------------------- */

static int refresh_keymap_from_zmk(void)
{
    zmk_keymap_layer_id_t layer = zmk_keymap_layer_default();

    for (uint16_t i = 0; i < HA_KEY_COUNT; i++) {
        const struct zmk_behavior_binding *binding =
            zmk_keymap_get_layer_binding_at_idx(layer, i);

        if (binding == NULL) {
            LOG_ERR("HA Config: cannot read ZMK binding %u", i);
            return -EIO;
        }

        ha_keymap[i] = zmk_keycode_to_ha_id(binding->param1);
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
/* Apply mapping to REAL ZMK keymap                                            */
/* -------------------------------------------------------------------------- */

static int apply_keymap_to_zmk(const uint8_t *new_map)
{
    zmk_keymap_layer_id_t layer = zmk_keymap_layer_default();

    /*
     * Validate the complete packet FIRST.
     * This prevents half-written mappings.
     */
    for (uint16_t i = 0; i < HA_KEY_COUNT; i++) {
        uint32_t keycode;

        if (ha_id_to_zmk_keycode(new_map[i], &keycode) < 0) {
            LOG_ERR("HA Config: invalid key ID %u at button %u",
                    new_map[i], i + 1);
            return -EINVAL;
        }
    }

    /*
     * Update all eight real ZMK bindings.
     *
     * We copy the existing binding so the behavior itself remains
     * unchanged; only the keycode parameter is replaced.
     */
    for (uint16_t i = 0; i < HA_KEY_COUNT; i++) {
        uint32_t keycode;

        if (ha_id_to_zmk_keycode(new_map[i], &keycode) < 0) {
            return -EINVAL;
        }

        const struct zmk_behavior_binding *current =
            zmk_keymap_get_layer_binding_at_idx(layer, i);

        if (current == NULL) {
            LOG_ERR("HA Config: cannot get ZMK binding %u", i);
            return -EIO;
        }

        struct zmk_behavior_binding updated = *current;

        updated.param1 = keycode;
        updated.param2 = 0;

        int ret =
            zmk_keymap_set_layer_binding_at_idx(layer, i, updated);

        if (ret < 0) {
            LOG_ERR("HA Config: failed to set button %u (%d)",
                    i + 1, ret);
            return ret;
        }
    }

    /*
     * Save through ZMK's own persistent keymap storage.
     */
    int ret = zmk_keymap_save_changes();

    if (ret < 0) {
        LOG_ERR("HA Config: failed to save keymap (%d)", ret);
        return ret;
    }

    memcpy(ha_keymap, new_map, HA_KEY_COUNT);

    LOG_INF("HA Config: real keymap updated and saved");

    return 0;
}

/* -------------------------------------------------------------------------- */
/* GATT callbacks                                                              */
/* -------------------------------------------------------------------------- */

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
    int ret = refresh_keymap_from_zmk();

    if (ret < 0) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

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

    if (len != HA_KEY_COUNT) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    uint8_t new_map[HA_KEY_COUNT];

    memcpy(new_map, buf, HA_KEY_COUNT);

    int ret = apply_keymap_to_zmk(new_map);

    if (ret < 0) {
        LOG_ERR("HA Config: mapping rejected (%d)", ret);
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
