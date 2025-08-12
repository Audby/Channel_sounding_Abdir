#include <ble.h>

struct bt_conn *conn; 
K_SEM_DEFINE(init_done_sem, 0, 1);

static struct sync_handler cb = {
    .led_cb = sync_reflector_ack_cb,
};

void acquisition_thread() {
    float distance = 0;
    struct bt_conn *c = NULL;
    int32_t loop_start = 0, loop_end = 0;

	k_sem_take(&init_done_sem, K_FOREVER);

    c = conn; 
	if (!c) {
		printk("Connection is NULL even after init. Aborting.\n");
		return;
	}

	printk("Connection ready. Starting measurement loop.\n");

    while (true) {
        if (PRINT_TIME) loop_start = k_uptime_get();
        if(!c)  { k_msleep(50); continue;}

        if(sync_reflector_busy()) { continue; }

        TRY(sync_request_cs(ble_write, true));

	    cs_reset_state(); 
        TRY(cs_start_ranging(c)); 
        distance = cs_calc(c);
        // TRY(cs_stop_ranging(c)); 
        // TRY(cs_wait_disabled());
        TRY(sync_request_cs(ble_write, false));
        if (PRINT_TIME) {
            loop_end = k_uptime_get();
            printk("[INIT][TIME] loop_total=%lld ms\n", (long long)(loop_end - loop_start));
        }
	}

}

int main(void) {
    printk("Starting Nordic CS Initiator...\n");

    conn = ble_init(); 
    TRY(cs_init(conn));
    TRY_RETURN(sync_init(&cb)); 

	printk("Setup complete. Handing over to acquisition thread.\n");
	k_sem_give(&init_done_sem);
    return 0;
}

K_THREAD_DEFINE(
    acquisition, 16384, acquisition_thread, 
    NULL, NULL, NULL, 7, 0, 0
);