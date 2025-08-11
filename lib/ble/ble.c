#include "ble.h"

struct ble_ctx ble_info; 
struct semaphores_ctx semaphores; 
struct ble_state_handler ble_state; 
extern atomic_t write_busy; 


#if BUILD_REFLECTOR
static struct bt_conn *active_conn = NULL; 
static bool indication_busy; 
static int turn_idx = 0; 

static const struct bt_le_conn_param ble_param = {
    .interval_min = 12, 
    .interval_max = 12, 
    .latency = 0, 
    .timeout = 10,
}; 

const static struct bt_le_scan_param search_param = {
    .type = BT_HCI_LE_SCAN_ACTIVE, 
    .options = BT_LE_SCAN_OPT_NONE,
    .interval = 20,
    .window = 6
};

static struct bt_scan_init_param scan_param = {
    .connect_if_match = true, 
    .scan_param = &search_param, 
    // .conn_param = BT_LE_CONN_PARAM_DEFAULT,
    .conn_param = &ble_param,
};

struct cs_config_item {
    void *fifo_reserved;  
    struct bt_conn *conn;
    bool enable;  // true = configure CS, false = cleanup CS
};
#endif 

#if BUILD_INITIATOR
struct indicate_handler indicate_ctx;

static struct bt_gatt_dm_cb gatt_callbacks = {
    .completed = dm_completed, 
    .service_not_found = dm_service_not_found, 
    .error_found = dm_error_found, 
};



static struct bt_gatt_exchange_params mtu_exchange_params = {.func = mtu_exchange_cb};

static const struct bt_data adv[] = {
  BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
  BT_DATA_BYTES(BT_DATA_UUID16_ALL,
                BT_UUID_16_ENCODE(BT_UUID_RANGING_SERVICE_VAL)),
  BT_DATA_BYTES(BT_DATA_UUID128_ALL, SYNC_SERVICE_UUID_VAL),
};

static const struct bt_data scan_rsp[] = {
  BT_DATA(BT_DATA_NAME_COMPLETE, INITIATOR_NAME,
          sizeof(INITIATOR_NAME) - 1),
};
#endif 

/**********************************************************************/
/*                           CALLBACKS                                */
/**********************************************************************/

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
    .le_phy_updated = phy_changed_cb, 
};


void connected(struct bt_conn *conn, uint8_t err) {
    char addr[BT_ADDR_LE_STR_LEN];

    (void)bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    printk("Connected to %s (err %d)\n", addr, err);

    if (err) {
        printk("Failed to connect to %s (err %d)\n", addr, err);

        #if BUILD_REFLECTOR 
        for (int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
            if (ble_info.connections[i] == conn) {
                bt_conn_unref(ble_info.connections[i]);
                ble_info.connections[i] = NULL;
                ble_info.conn_count--;
                printk("Connection slot %d freed\n", i);
                break;
            }
        }
        #endif 

        #if BUILD_INITIATOR 
        bt_conn_unref(conn);
        conn = NULL; 
        ble_info.connection = NULL; 
        #endif 
        return; 
    }

    #if BUILD_REFLECTOR 
    for (int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
        if (ble_info.connections[i] == NULL) {
            ble_info.connections[i] = bt_conn_ref(conn);
            ble_info.conn_count++;
            printk("Connection slot %d assigned\n", i);

            TRY(bt_conn_le_param_update(conn, &ble_param));
            ble_set_phy(conn);
            configure_cs_connection(conn);
            dk_set_led_on(DK_LED1 + i);
            break;
        }
    }

    if (ble_info.conn_count == CONFIG_BT_MAX_CONN) {
        TRY(bt_le_scan_stop()); 
        ble_update_state(INACTIVE_SCAN); 
    } else {
        TRY(bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE)); // TODO: Better check to stop starting in cases where its still on
        ble_update_state(ACTIVE_SCAN); 
    }
    #endif 

    #if BUILD_INITIATOR 
    ble_info.connection = bt_conn_ref(conn); 
    dk_set_led_on(DK_LED1);

    TRY(bt_le_adv_stop());
    ble_update_state(INACTIVE_ADV);
    #endif 
 
    k_sem_give(&semaphores.connected);
}

void disconnected(struct bt_conn *conn, uint8_t reason) {
    char addr[BT_ADDR_LE_STR_LEN];

    (void)bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    printk("Disconnected from %s (reason 0x%02x)\n", addr, reason);

    #if BUILD_REFLECTOR
    if (conn == active_conn) {
        active_conn = NULL; 
        indication_busy = false; 
    }

    for(int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
        if (ble_info.connections[i] == conn) {
            bt_conn_unref(ble_info.connections[i]);
            ble_info.connections[i] = NULL;
            dk_set_led_off(DK_LED1 + i); 
            ble_info.conn_count--;
            printk("Connection slot %d freed\n", i);
            break;
        }
    }

    if (ble_info.conn_count < CONFIG_BT_MAX_CONN) {
        TRY(bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE)); // TODO: Better check to stop starting in cases where its still on
        ble_update_state(ACTIVE_SCAN); 
    } 
    #endif 

    #if BUILD_INITIATOR 
    bt_conn_unref(conn);
    ble_info.connection = NULL; 

    cs_reset_state(); 
    TRY(ble_adv_start());
    ble_update_state(ACTIVE_ADV); 
    #endif
}

void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err) {
    char addr[BT_ADDR_LE_STR_LEN];

    (void)bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    printk("Security changed for %s to level %d (err %d)\n", addr, level, err);

    if (err) {
        printk("Failed to change security for %s (err %d)\n", addr, err);
        return;
    } 

    k_sem_give(&semaphores.security);
}

#if BUILD_INITIATOR && CONFIG_BT_CHANNEL_SOUNDING
void dm_completed(struct bt_gatt_dm *dm, void *context) {
    int err;

    printk("The discovery procedure succeeded\n");

    struct bt_conn *conn = bt_gatt_dm_conn_get(dm);

    bt_gatt_dm_data_print(dm);

    /* Subscribing to SYNC_SERIVICE */
    if (context != NULL) {
        static struct bt_uuid_128 sync_id_uuid_inst = BT_UUID_INIT_128(SYNC_ID_UUID_VAL);
        
        const struct bt_gatt_dm_attr *sync_id_chrc_attr = bt_gatt_dm_char_by_uuid(dm, &sync_id_uuid_inst.uuid);
        if (!sync_id_chrc_attr) {
            printk("SYNC_ID_UUID characteristic not found!\n");
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            goto release_dm_data;
        }
        indicate_ctx.char_handle = sync_id_chrc_attr->handle + 1;

        const struct bt_gatt_dm_attr *sync_id_ccc_attr = bt_gatt_dm_desc_by_uuid(dm, sync_id_chrc_attr, BT_UUID_GATT_CCC);
        if (!sync_id_ccc_attr) {
            printk("CCCD for SYNC_ID_UUID not found!\n");
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            goto release_dm_data;
        }
        indicate_ctx.ccc_handle = sync_id_ccc_attr->handle;
        
        // Set up subscription parameters
        indicate_ctx.sub_params.ccc_handle = indicate_ctx.ccc_handle;
        indicate_ctx.sub_params.value_handle = indicate_ctx.char_handle;
        indicate_ctx.sub_params.value = BT_GATT_CCC_INDICATE;
        indicate_ctx.sub_params.notify = sync_id_indicated;
        indicate_ctx.sub_params.subscribe = NULL; 

        err = bt_gatt_subscribe(conn, &indicate_ctx.sub_params);
        if (err && err != -EALREADY) {
            printk("Failed to subscribe to SYNC_ID_UUID (err %d)\n", err);
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            goto release_dm_data;
        }

        printk("Successfully subscribed to SYNC_ID indications\n");

    } else {
        // This is the ranging service discovery
        TRY(bt_ras_rreq_alloc_and_assign_handles(dm, conn));
    } 

release_dm_data:
    bt_gatt_dm_data_release(dm);
    k_sem_give(&semaphores.discovery);
}


void dm_service_not_found(struct bt_conn *conn, void *context) {
    printk("The service could not be found during the discovery, disconnecting\n");
    bt_conn_disconnect(ble_info.connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

void dm_error_found(struct bt_conn *conn, int err, void *context) {
    printk("The discovery procedure failed (err %d)\n", err);
    bt_conn_disconnect(ble_info.connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
			    struct bt_gatt_exchange_params *params) {
	if (err) {
		printk("MTU exchange failed (err %d)\n", err);
		return;
	}

	printk("MTU exchange success (%u)\n", bt_gatt_get_mtu(conn));
    k_sem_give(&semaphores.mtu_exchange);
}
#endif 

void phy_changed_cb(struct bt_conn *conn, struct bt_conn_le_phy_info *param) {
    switch (param->tx_phy) {
        case BT_CONN_LE_TX_POWER_PHY_1M: 
            printk("Phy updated to 1M\n");
            break; 
        
        case BT_CONN_LE_TX_POWER_PHY_2M: 
            printk("Phy updated to 2M\n");
            break; 

        default: break; 
    }
}

/**********************************************************************/
/*                        SERVER / CLIENT                             */
/**********************************************************************/

BT_GATT_SERVICE_DEFINE(ble_sync_service,
    BT_GATT_PRIMARY_SERVICE(SYNC_SERVICE_UUID),
    BT_GATT_CHARACTERISTIC(SYNC_ID_UUID, 
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_INDICATE, 
        BT_GATT_PERM_WRITE_ENCRYPT, 
        NULL, sync_write_id, NULL),
    BT_GATT_CCC(sync_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT)
);

enum ble_state_ctx ble_get_currrent_state() {
    return ble_state.state; 
}

#if BUILD_INITIATOR 
int ble_write(bool state) {
    static struct bt_gatt_write_params write_params_led;
    struct bt_conn *c = ble_info.connection; 
    if (!c) {
        printk("LED write skipped – not connected\n");
        sys_reboot(SYS_REBOOT_COLD);
        return -ENOTCONN;
    }

    if (indicate_ctx.char_handle == 0x0000) {
        printk("Invalid characteristic handle. Discovery might have failed.\n");
        return -EINVAL;
    }

    static uint8_t state_val; 
    state_val = state ? 0x01 : 0x00;

    write_params_led.func = sync_write_cb; 
    write_params_led.handle = indicate_ctx.char_handle;
    write_params_led.offset = 0;
    write_params_led.data = &state_val;
    write_params_led.length = sizeof(state_val);

    return bt_gatt_write(c, &write_params_led);
}
#endif 

#if BUILD_REFLECTOR 
void indicate_done(struct bt_conn *conn,
					struct bt_gatt_indicate_params *params,
					uint8_t err) {

    ARG_UNUSED(conn);
    ARG_UNUSED(err);
    ARG_UNUSED(params);
    indication_busy = false; 
}
#endif 

ssize_t sync_write_id(struct bt_conn *conn, const struct bt_gatt_attr *attr, 
	const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {

    if(len != 1U || offset != 0) {
        printk("Illegal length or offset on SYNC_ID write.\n");
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    uint8_t val = *((uint8_t *)buf); 

    if(val != 0x00 && val != 0x01) {  // Fix the printk error
        printk("Incorrect write value: %d \n", val); 
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED); 
    }

    sync_update_led(val ? true : false); 

    #if BUILD_REFLECTOR
    static uint8_t indicate_value;
    indicate_value = val;

    if (ble_info.conn_count)
        turn_idx %= ble_info.conn_count;
    
    // if (conn != ble_info.connections[turn_idx] ||
    //     ble_info.conn_count != CONFIG_BT_MAX_CONN) {
    //     return BT_GATT_ERR(BT_ATT_ERR_PREPARE_QUEUE_FULL);
    // }
    if (conn != ble_info.connections[turn_idx]) {
        return BT_GATT_ERR(BT_ATT_ERR_PREPARE_QUEUE_FULL);
    }

    if (indicate_value == 0x01) {
        
        if (active_conn && active_conn != conn) {
            return BT_GATT_ERR(BT_ATT_ERR_PREPARE_QUEUE_FULL);
        }
        if (indication_busy) {
            return BT_GATT_ERR(BT_ATT_ERR_PREPARE_QUEUE_FULL);
        }

        active_conn = conn;
        FIFO_CONTAINER_DEFINE(
            c, configure_cs_connection, ble_indicate_write, conn, indicate_value
        );
        if (!c) { 
            printk("CONTAINER WAS NULL :( \n"); 
            return BT_GATT_ERR(BT_ATT_ERR_PREPARE_QUEUE_FULL); }
        sync_put_fifo(c);
    }

    if (indicate_value == 0x00) {
        if (conn != active_conn) {
            return BT_GATT_ERR(BT_ATT_ERR_PREPARE_QUEUE_FULL);
        }
        
        active_conn = NULL; 
        turn_idx = (turn_idx + 1) % ble_info.conn_count; 
        FIFO_CONTAINER_DEFINE(
            c, configure_cs_connection, ble_indicate_write, conn, indicate_value
        );
        if (!c) { 
            printk("CONTAINER WAS NULL :( \n"); 
            return BT_GATT_ERR(BT_ATT_ERR_PREPARE_QUEUE_FULL); }
        sync_put_fifo(c); 
    }
    #endif

    return len;
}

#if BUILD_REFLECTOR 
void ble_indicate_write(struct bt_conn *conn, uint8_t val) {
    indication_busy = true; 

    static uint8_t p_val;
    p_val = val;  
    static struct bt_gatt_indicate_params p = {
        .attr = &ble_sync_service.attrs[1],
        .data = &p_val,
        .len = sizeof(p_val),
        .func = indicate_done,
    };
    
    int ret = bt_gatt_indicate(conn, &p); 

    if (ret) {
        printk("Failed to send LED state indication to connection %d (err %d)\n", 
            bt_conn_index(conn), ret);
        indication_busy = false;
    } else {
        printk("Wrote to conn %d state: %d\n", bt_conn_index(conn), p_val); 
    }
}
#endif 

/**********************************************************************/
/*                             GENERAL                                */
/**********************************************************************/

struct bt_conn *ble_init() {
    ble_setup_struct_and_types(); 
    cs_setup_struct_and_types();

    dk_leds_init();

    TRY(bt_enable(NULL)); 

    #if BUILD_INITIATOR 
        TRY(ble_adv_start());

        printk("Initator started, advertising as %s\n", INITIATOR_NAME);

        ble_update_state(ACTIVE_ADV); 

        k_sem_take(&semaphores.connected, K_FOREVER); // wait for connection

        TRY(bt_conn_set_security(ble_info.connection, BT_SECURITY_L2));

        k_sem_take(&semaphores.security, K_FOREVER); // wait for security to be established

        bt_gatt_exchange_mtu(ble_info.connection, &mtu_exchange_params);

        k_sem_take(&semaphores.mtu_exchange, K_FOREVER); // wait for maximum transition unit (MTU) exchange

        TRY(bt_gatt_dm_start(ble_info.connection, BT_UUID_RANGING_SERVICE, &gatt_callbacks, NULL));
        
        k_sem_take(&semaphores.discovery, K_FOREVER);
        
        TRY(bt_gatt_dm_start(ble_info.connection, SYNC_SERVICE_UUID, &gatt_callbacks, &indicate_ctx.char_handle));

        k_sem_take(&semaphores.discovery, K_FOREVER);
   
        printk("GATT discovery started successfully\n");

        return ble_info.connection; 
    #endif 

    #if BUILD_REFLECTOR
        TRY(ble_scan_init()); 
        
        TRY(bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE));

        ble_update_state(ACTIVE_ADV); 

        return NULL; 
    #endif 
}

void ble_update_state(enum ble_state_ctx state) {
    k_mutex_lock(&ble_state.mutex, K_FOREVER); 
    ble_state.state = state; 
    k_mutex_unlock(&ble_state.mutex); 
}

#if BUILD_REFLECTOR 
int ble_scan_init() {
    bt_scan_init(&scan_param);
    TRY(bt_scan_filter_add(BT_SCAN_FILTER_TYPE_NAME, INITIATOR_NAME));
    TRY(bt_scan_filter_enable(BT_SCAN_NAME_FILTER, true));
    printk("BLE init done, scanning for %s\n", INITIATOR_NAME);
    return 0;
}
#endif 

int ble_setup_struct_and_types() {
    k_sem_init(&semaphores.connected, 0, 1); 
    k_sem_init(&semaphores.security, 0, 1); 

    #if BUILD_REFLECTOR 
    memset(ble_info.connections, 0, sizeof(ble_info.connections)); 
    ble_info.conn_count = 0; 
    #endif 

    #if BUILD_INITIATOR 
    ble_info.connection = NULL; 
    k_sem_init(&semaphores.mtu_exchange, 0, 1); 
    k_sem_init(&semaphores.discovery, 0, 1); 
    #endif 

    k_mutex_init(&ble_state.mutex);
    ble_state.state = SETUP; 

    return 0; 
}

void ble_set_phy(struct bt_conn *conn) {
    const struct bt_conn_le_phy_param phy = {
       .options = BT_CONN_LE_PHY_OPT_NONE,
       .pref_tx_phy = BT_GAP_LE_PHY_2M, 
       .pref_rx_phy = BT_GAP_LE_PHY_2M,
    };

    TRY(bt_conn_le_phy_update(conn, &phy)); 
}

#if BUILD_INITIATOR 
int ble_adv_start() {
    return bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, adv, ARRAY_SIZE(adv), scan_rsp, ARRAY_SIZE(scan_rsp)); 
}

#endif 