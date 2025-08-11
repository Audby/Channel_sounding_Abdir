#include "sync.h"

struct sync_handler sync_ctx; 
#if BUILD_REFLECTOR 
K_FIFO_DEFINE(sheduling_fifo); 
K_THREAD_DEFINE(
    sync_thread, STACK_SIZE, scheduling_thread,
    NULL, NULL, NULL, PRIO, 0, 0); 
#endif 

int sync_init(struct sync_handler *callback) {
    if (callback) {
        sync_ctx.led_cb = callback->led_cb; 
    }
    sync_ctx.write_busy = 0; 
    sync_ctx.att_err = 0; 
    k_sem_init(&sync_ctx.sem_reflector_ack, 0, 1);
    k_sem_init(&sync_ctx.sem_write_done, 0, 1);

    k_sem_init(&sync_ctx.sem_sync_init_done, 0, 1); 
    k_sem_give(&sync_ctx.sem_sync_init_done);
    return 0; 
}

void sync_update_led(bool val) {
    if (sync_ctx.led_cb)    sync_ctx.led_cb(val); 
}

#if BUILD_INITIATOR 
int sync_request_cs(write_func_t write_fn, bool state) {
    int err;
    for(;;) {
        while ((err = write_fn(state)) == -EBUSY) k_msleep(1);      

        if (err) { k_msleep(2);    continue; }

        k_sem_take(&sync_ctx.sem_write_done, K_FOREVER);

        if (sync_ctx.att_err == BT_ATT_ERR_PREPARE_QUEUE_FULL ||
            sync_ctx.att_err == BT_ATT_ERR_UNLIKELY) {
            k_msleep(2);
            continue;
        }

        if (sync_ctx.att_err) {  
            printk("LED write failed (ATT 0x%02x)\n", sync_ctx.att_err);
            return -EIO;
        }
        break;           
    } 

    k_sem_take(&sync_ctx.sem_reflector_ack, K_FOREVER);
    return 0;
}
#endif 

void sync_reflector_ack_cb(bool state) {
    dk_set_led(USER_LED, state); 
    k_sem_give(&sync_ctx.sem_reflector_ack); 
}

bool sync_reflector_busy() {
    return !atomic_cas(&sync_ctx.write_busy, 0, 1); 
}

/**********************************************************************/
/*                              THREADS                               */
/**********************************************************************/

#if BUILD_REFLECTOR 
void scheduling_thread() {
    printk("Started scheduling thread!\n"); 
    k_sem_take(&sync_ctx.sem_sync_init_done, K_FOREVER); 

    struct fifo_container *container; 
    // int err = 0; 

    while (true) {
        container = k_fifo_get(&sheduling_fifo, K_FOREVER);
        if (!container) continue; 
        if (!container->conn) { k_free(container); continue; } 

        switch (container->val) {
            case 1: 
                container->indicate_write_func(container->conn, container->val); 
                break; 

            case 0: 
                container->indicate_write_func(container->conn, container->val); 
                break; 
            default: printk("WEIRD CASE? value %d", container->val); break; 
        }

        bt_conn_unref(container->conn); 
        k_free(container); 
    }
}

void sync_put_fifo(struct fifo_container *container) {
    k_fifo_put(&sheduling_fifo, container); 
}
#endif 

/**********************************************************************/
/*                            CALLBACKS                               */
/**********************************************************************/

void sync_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    if (value == BT_GATT_CCC_INDICATE) {
        // printk("SYNC_ID indications enabled by client.\n");
    } else if (value == BT_GATT_CCC_NOTIFY) {
        // printk("SYNC_ID notifications enabled by client.\n");
    } else if (value == 0) {
        // printk("SYNC_ID notifications/indications disabled by client.\n");
    } else {
        printk("SYNC_ID CCCD changed to unknown value: 0x%04x\n", value);
    }
}

void sync_write_cb(struct bt_conn *conn, uint8_t err,
                    struct bt_gatt_write_params *params) {
    sync_ctx.write_busy = 0; 
    sync_ctx.att_err = err; 
    k_sem_give(&sync_ctx.sem_write_done);

    if (err != 0x00 && err != BT_ATT_ERR_PREPARE_QUEUE_FULL) {
        printk("SYNC_ID write failed  (ATT 0x%02x)\n", err); 
    }
}

uint8_t sync_id_indicated(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, 
    const void *data, uint16_t length) {
    if (data == NULL) {
        printk("Unsubscribed from SYNC_ID indications\n");
        params->value_handle = 0;
        return BT_GATT_ITER_STOP;
    }

    if (length != sizeof(uint8_t)) {
        printk("Received SYNC_ID indication with invalid length: %d\n", length);
        return BT_GATT_ITER_CONTINUE;
    }

    uint8_t received_value = *((uint8_t *)data);
    // printk("Received SYNC_ID indication from server: %d\n", received_value);

    #if BUILD_INITIATOR 
        k_sem_give(&sync_ctx.sem_reflector_ack);
    #endif 
    
    /* Update local state */
    if (sync_ctx.led_cb) {
        sync_ctx.led_cb(received_value ? true : false);
    }
    
    return BT_GATT_ITER_CONTINUE;
}