/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Includes */
#include "gatt_svc.h"
#include "common.h"
#include "heart_rate.h"
#include "led.h"
#include "sound_meter.h"

/* Private function declarations */
static int heart_rate_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int led_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static int sound_meter_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);


typedef struct {
    bool heart_rate_used;
    bool heart_rate_indicate_enabled;
    bool sound_meter_used;
    bool sound_meter_used_indicate_enabled;
    uint16_t conn_handle;
    
} hr_sub_t;

static hr_sub_t s_hr_subs[CONFIG_BT_NIMBLE_MAX_CONNECTIONS];


/* Private variables */
/* Heart rate service */
// Heart Rate ServiceのUUIDは0x180D
static const ble_uuid16_t heart_rate_svc_uuid = BLE_UUID16_INIT(0x180D);

// heart_rate_chr_val[0]は0のまま、[1]は心拍数を入れる
// chrはcharacteristic
static uint8_t heart_rate_chr_val[2] = {0};
static uint16_t heart_rate_chr_val_handle;
// 0x2A37はHeart Rate MeasurementのUUID
static const ble_uuid16_t heart_rate_chr_uuid = BLE_UUID16_INIT(0x2A37);

// heart_rate_chr_conn_handleは接続ハンドル。BLE_HS_CONN_HANDLE_NONEは接続なし
static bool heart_rate_chr_conn_handle_inited = false;

/* Automation IO service */
static const ble_uuid16_t auto_io_svc_uuid = BLE_UUID16_INIT(0x1815);
static uint16_t led_chr_val_handle;
static const ble_uuid128_t led_chr_uuid =
    BLE_UUID128_INIT(0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15, 0xde, 0xef,
                     0x12, 0x12, 0x25, 0x15, 0x00, 0x00);

static const ble_uuid128_t sound_meter_svc_uuid =
    BLE_UUID128_INIT(0xb2, 0xb4, 0xed, 0x21, 0x50, 0xc3, 0x00, 0xb4, 0x33, 0x4e,0xe5, 0x70, 0x26, 0xac, 0x00, 0xd4);
static uint8_t sound_level_chr_val[2]={0};
static uint16_t sound_level_chr_val_handle;
static bool sound_level_chr_conn_handle_inited = false;
static const ble_uuid128_t sound_level_chr_uuid = 
    BLE_UUID128_INIT(0x2a, 0xe3, 0x26, 0x73, 0x06, 0x0e, 0x93, 0xa3, 0x3b, 0x41,0x4c, 0x69, 0x92, 0x65, 0x96, 0xf0);

/* GATT services table */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    /* Heart rate service */
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &heart_rate_svc_uuid.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {/* Heart rate characteristic */
              .uuid = &heart_rate_chr_uuid.u,
              .access_cb = heart_rate_chr_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_INDICATE,
              .val_handle = &heart_rate_chr_val_handle},
             {
                 0, /* No more characteristics in this service. */
             }}},

    /* Automation IO service */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &auto_io_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){/* LED characteristic */
                                        {.uuid = &led_chr_uuid.u,
                                         .access_cb = led_chr_access,
                                         .flags = BLE_GATT_CHR_F_WRITE,
                                         .val_handle = &led_chr_val_handle},
                                        {0}},
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &sound_meter_svc_uuid.u,
        .characteristics = 
            (struct ble_gatt_chr_def[]){
                {.uuid = &sound_level_chr_uuid.u,
                .access_cb = sound_meter_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_INDICATE,
                .val_handle = &sound_level_chr_val_handle},
                {
                    0
                }}
    },

    {
        0, /* No more services. */
    },
};

/* Private functions */
static void clear_sub_by_conn(uint16_t conn_handle, uint16_t attr_handle){
    for(int i=0; i<CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++){
        if((s_hr_subs[i].heart_rate_used||s_hr_subs[i].sound_meter_used) && s_hr_subs[i].conn_handle == conn_handle){
            if(attr_handle==heart_rate_chr_val_handle){
                s_hr_subs[i].heart_rate_used = false;
                s_hr_subs[i].heart_rate_indicate_enabled = false;
            } else if(attr_handle==sound_level_chr_val_handle){
                s_hr_subs[i].sound_meter_used = false;
                s_hr_subs[i].sound_meter_used_indicate_enabled = false;
            }
        }
    }
}

static void set_sub_by_conn(uint16_t conn_handle, uint16_t attr_handle){
    for(int i=0; i<CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++){
        if(s_hr_subs[i].conn_handle==BLE_HS_CONN_HANDLE_NONE || s_hr_subs[i].conn_handle==conn_handle){
            s_hr_subs[i].conn_handle = conn_handle;
            if(attr_handle==heart_rate_chr_val_handle){
                s_hr_subs[i].heart_rate_used = true;
                s_hr_subs[i].heart_rate_indicate_enabled = true;
            } else if(attr_handle==sound_level_chr_val_handle){
                s_hr_subs[i].sound_meter_used = true;
                s_hr_subs[i].sound_meter_used_indicate_enabled = true;
            }
            return;
        }
    }
}

static void update_hr_subscribe(const struct ble_gap_event *event){
    uint16_t conn_handle = event->subscribe.conn_handle;
    uint16_t attr_handle = event->subscribe.attr_handle;

    if((attr_handle != heart_rate_chr_val_handle) && (attr_handle != sound_level_chr_val_handle) ){
        return;
    }
    if(!event->subscribe.cur_indicate){
        clear_sub_by_conn(conn_handle, attr_handle);
    } else {
        set_sub_by_conn(conn_handle, attr_handle);
    }
}

static int heart_rate_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    /* Local variables */
    int rc = 0;

    /* Handle access events */
    /* Note: Heart rate characteristic is read only */
    switch (ctxt->op) {

    /* Read characteristic event */
    case BLE_GATT_ACCESS_OP_READ_CHR:
        /* Verify connection handle */
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "characteristic read; conn_handle=%d attr_handle=%d",
                     conn_handle, attr_handle);
        } else {
            ESP_LOGI(TAG, "characteristic read by nimble stack; attr_handle=%d",
                     attr_handle);
        }

        /* Verify attribute handle */
        if (attr_handle == heart_rate_chr_val_handle) {
            /* Update access buffer value */
            heart_rate_chr_val[1] = get_heart_rate();
            rc = os_mbuf_append(ctxt->om, &heart_rate_chr_val,
                                sizeof(heart_rate_chr_val));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        goto error;

    /* Unknown event */
    default:
        goto error;
    }

error:
    ESP_LOGE(
        TAG,
        "unexpected access operation to heart rate characteristic, opcode: %d",
        ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}

static int led_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    /* Local variables */
    int rc = 0;

    /* Handle access events */
    /* Note: LED characteristic is write only */
    switch (ctxt->op) {

    /* Write characteristic event */
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        /* Verify connection handle */
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "characteristic write; conn_handle=%d attr_handle=%d",
                     conn_handle, attr_handle);
        } else {
            ESP_LOGI(TAG,
                     "characteristic write by nimble stack; attr_handle=%d",
                     attr_handle);
        }

        /* Verify attribute handle */
        if (attr_handle == led_chr_val_handle) {
            /* Verify access buffer length */
            if (ctxt->om->om_len == 1) {
                /* Turn the LED on or off according to the operation bit */
                if (ctxt->om->om_data[0]) {
                    led_on();
                    ESP_LOGI(TAG, "led turned on!");
                } else {
                    led_off();
                    ESP_LOGI(TAG, "led turned off!");
                }
            } else {
                goto error;
            }
            return rc;
        }
        goto error;

    /* Unknown event */
    default:
        goto error;
    }

error:
    ESP_LOGE(TAG,
             "unexpected access operation to led characteristic, opcode: %d",
             ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}


static int sound_meter_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg){
    int rc = 0;
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        if(conn_handle != BLE_HS_CONN_HANDLE_NONE){
            ESP_LOGI(TAG, "charasteristic read; conn_handle=%d attr_handle=%d",
            conn_handle, attr_handle);
        }else {
            ESP_LOGI(TAG, "charasteristic read by nimble stack; attr_handle=%d",
            attr_handle);
        }

        if(attr_handle == sound_level_chr_val_handle){
            sound_level_chr_val[1] = get_sound_level();
            rc = os_mbuf_append(ctxt->om, &sound_level_chr_val,
                                sizeof(sound_level_chr_val));
            return rc == 0 ? 0:BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        goto error;
    
    default:
        goto error;
    }

error:
    ESP_LOGI(
        TAG,
        "unexpected access operation to sound meter characteristic, opcode: %d",
        ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}

/* Public functions */
void send_heart_rate_indication(void) {
    for (int i=0; i<CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++){
        if(s_hr_subs[i].heart_rate_used && s_hr_subs[i].heart_rate_indicate_enabled){
            ble_gatts_indicate(s_hr_subs[i].conn_handle, heart_rate_chr_val_handle);
            ESP_LOGI(TAG, "heart rate indication sent!");
        }
    }
}

void send_sound_level_indication(void){
    for (int i=0; i<CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++){
        if(s_hr_subs[i].sound_meter_used && s_hr_subs[i].sound_meter_used_indicate_enabled){
            ble_gatts_indicate(s_hr_subs[i].conn_handle, sound_level_chr_val_handle);
            ESP_LOGI(TAG, "sound level indication sent!");
        }
    }
}

/*
 *  Handle GATT attribute register events
 *      - Service register event
 *      - Characteristic register event
 *      - Descriptor register event
 */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    /* Local variables */
    char buf[BLE_UUID_STR_LEN];

    /* Handle GATT attributes register events */
    switch (ctxt->op) {

    /* Service register event */
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "registered service %s with handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                 ctxt->svc.handle);
        break;

    /* Characteristic register event */
    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG,
                 "registering characteristic %s with "
                 "def_handle=%d val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;

    /* Descriptor register event */
    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "registering descriptor %s with handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                 ctxt->dsc.handle);
        break;

    /* Unknown event */
    default:
        assert(0);
        break;
    }
}

/*
 *  GATT server subscribe event callback
 *      1. Update heart rate subscription status
 */

void gatt_svr_subscribe_cb(struct ble_gap_event *event) {
    /* Check connection handle */
    if (event->subscribe.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle);
    } else {
        ESP_LOGI(TAG, "subscribe by nimble stack; attr_handle=%d",
                 event->subscribe.attr_handle);
    }

    /* Check attribute handle */
    if (event->subscribe.attr_handle == heart_rate_chr_val_handle) {
        /* Update heart rate subscription status */
        update_hr_subscribe(event);
    } else if (event->subscribe.attr_handle == sound_level_chr_val_handle){
        update_hr_subscribe(event);
    }
}

void gatt_svr_reset_heart_rate_subscription(struct ble_gap_event *event) {
    update_hr_subscribe(event);
}

void gatt_svr_reset_sound_meter_subscription(struct ble_gap_event *event){
    update_hr_subscribe(event);
}

/*
 *  GATT server initialization
 *      1. Initialize GATT service
 *      2. Update NimBLE host GATT services counter
 *      3. Add GATT services to server
 */
int gatt_svc_init(void) {
    for(int i=0; i<CONFIG_BT_NIMBLE_MAX_CONNECTIONS; i++){
        s_hr_subs[i].heart_rate_used = false;
        s_hr_subs[i].heart_rate_indicate_enabled = false;
        s_hr_subs[i].sound_meter_used = false;
        s_hr_subs[i].sound_meter_used_indicate_enabled = false;
        s_hr_subs[i].conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }

    /* Local variables */
    int rc = 0;

    /* 1. GATT service initialization */
    ble_svc_gatt_init();

    /* 2. Update GATT services counter */
    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    /* 3. Add GATT services */
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
