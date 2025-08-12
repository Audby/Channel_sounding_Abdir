#pragma once 

#define ID 0
#define PRINT_VERBOSE 0
#define PRINT_TIME 0
#define DE_SLIDING_WINDOW_SIZE (10)
#define MAX_AP (CONFIG_BT_RAS_MAX_ANTENNA_PATHS)
#define REFLECTOR_NAME "1234"
#define INITIATOR_NAME "5678"
#define BUILD_INITIATOR CONFIG_CS_BUILD_INITIATOR
#define BUILD_REFLECTOR CONFIG_CS_BUILD_REFLECTOR
#define USING_BLACK_BOX CONFIG_CS_BLACK_BOX_CALC
#define PROCEDURE_COUNTER_NONE (-1)
#define SLOT_DURATION 500

#define LOCAL_PROCEDURE_MEM                                                                        \
	((BT_RAS_MAX_STEPS_PER_PROCEDURE * sizeof(struct bt_le_cs_subevent_step)) +                \
	 (BT_RAS_MAX_STEPS_PER_PROCEDURE * BT_RAS_MAX_STEP_DATA_LEN))

#define TRY_BASE(func, ...) \
    do { \
        int err = func; \
        if (err) { \
            __VA_ARGS__; \
        } \
    } while(0)

#define TRY(func) \
    TRY_BASE(func, printk("Failed to run %s: (error %d)\n", #func, err))

#define TRY_GOTO(func, place) \
    TRY_BASE(func, goto place)

#define TRY_RETURN(func) \
    TRY_BASE(func, \
        printk("Failed to run %s: (error %d)\n", #func, err); \
        return err)

