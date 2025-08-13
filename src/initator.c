#include <ble.h>
#include <sync.h> // Ensure sync.h is included

struct bt_conn *conn;
K_SEM_DEFINE(init_done_sem, 0, 1);
K_SEM_DEFINE(start_ranging_sem, 0, 1); // NEW: Semaphore to wait for start signal

// NEW: Callback implementation to handle the start signal (Indication)
void initiator_sync_cb(bool start_signal) {
    // Update LED (USER_LED defined in sync.h)
    dk_set_led(USER_LED, start_signal);

    if (start_signal) {
        // printk("Received start signal from reflector. Starting ranging...\n");
        // Signal the acquisition thread to start ranging
        k_sem_give(&start_ranging_sem);
    }
}

static struct sync_handler cb = {
    // .led_cb = sync_reflector_ack_cb, // OLD
    .led_cb = initiator_sync_cb, // NEW
};

void acquisition_thread() {
    float distance = 0;
    struct bt_conn *c = NULL;
    int32_t loop_start = 0, loop_end = 0;
    int sample_count = 0; // Add a counter for samples

	k_sem_take(&init_done_sem, K_FOREVER);

    c = conn; 
	if (!c) {
		printk("Connection is NULL even after init. Aborting.\n");
		return;
	}

    // printk("Connection ready. Starting measurement loop.\n"); // OLD
    printk("Connection ready. Waiting for reflector signal...\n"); // NEW

    while (true) {
        /* --- REMOVED (Old contention-based logic) ---
        if(sync_reflector_busy()) { continue; }
        TRY(sync_request_cs(ble_write, true));
        */

        // NEW: Wait for the start signal from the reflector
        k_sem_take(&start_ranging_sem, K_FOREVER);

        if (PRINT_TIME) loop_start = k_uptime_get();
        if(!c)  { k_msleep(50); continue;}


        // Ranging sequence
	    cs_reset_state();
        TRY(cs_start_ranging(c));
        distance = cs_calc(c);
        
        // Signal completion (Write 0x00)
        TRY(sync_signal_completion(ble_write));

#if USE_PSEUDO
        if (sample_count > 10) {
            for (int i = 0; i < PSEUDO_INJECTIONS_COUNT; i++) {
                k_msleep(100); // Wait for half the time between real measurements
                float pseudo_distance = cs_calc_pseudo(c);
                // printk("Distance: %.2f m (pseudo)\n", pseudo_distance);
            }
        } else {
            sample_count++;
        }
#endif
        // TRY(cs_stop_ranging(c));
        // TRY(cs_wait_disabled());

        /*
        TRY(sync_request_cs(ble_write, false));
        */

        // Signal completion (Write 0x00)
        // TRY(sync_signal_completion(ble_write));

        if (PRINT_TIME) {
            loop_end = k_uptime_get();
            // This time now represents the actual measurement duration + minimal sync overhead.
            printk("[INIT][TIME] loop_total=%lld ms\n", (long long)(loop_end - loop_start));
        }
	}

}

int main(void) {
    printk("Starting Nordic CS Initiator...\n");

    // Initialize sync early to ensure callbacks are ready before BLE connection process starts
    TRY_RETURN(sync_init(&cb));

    conn = ble_init();
    TRY(cs_init(conn));
    // TRY_RETURN(sync_init(&cb)); // MOVED UP

	printk("Setup complete. Handing over to acquisition thread.\n");
	k_sem_give(&init_done_sem);
	
	// Keep main thread alive to let acquisition thread run
	while (true) {
	    k_msleep(100);
	}
	
    return 0;
}

K_THREAD_DEFINE(
    acquisition, 16384, acquisition_thread, 
    NULL, NULL, NULL, 7, 0, 0
);