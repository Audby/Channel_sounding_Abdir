#pragma once 

#include <stdint.h>
#include <zephyr/bluetooth/cs.h>
#include "zephyr/bluetooth/hci_types.h"
#include <math.h>
#include <zephyr/bluetooth/cs.h>
#include <bluetooth/services/ras.h>
#include "coreaiot_distance.h"
#include <bluetooth/cs_de.h>
#include <zephyr/drivers/uart.h>
#include "../global.h"

#define CS_FREQUENCY_MHZ(ch)	(2402u + 1u * (ch))
#define CS_FREQUENCY_HZ(ch)	(CS_FREQUENCY_MHZ(ch) * 1000000.0f)
#define SPEED_OF_LIGHT_M_PER_S	(299792458.0f)
#define SPEED_OF_LIGHT_NM_PER_S (SPEED_OF_LIGHT_M_PER_S / 1000000000.0f)
#define PI			3.14159265358979323846f
#define MAX_NUM_RTT_SAMPLES		256
#define MAX_NUM_IQ_SAMPLES		256 * CONFIG_BT_RAS_MAX_ANTENNA_PATHS
#define STRIDE 4 
#define CS_BANDWIDTH_HZ 80000000.0f  // 80 MHz for Bluetooth Channel Sounding
#define IFFT_SIZE 64
#define MAX_DISTANCE_BINS (IFFT_SIZE / 2)

struct iq_sample_and_channel {
	bool failed;
	uint8_t channel;
	uint8_t antenna_path;
	struct bt_le_cs_iq_sample local_iq_sample;
	struct bt_le_cs_iq_sample peer_iq_sample;
};

struct rtt_timing {
	bool failed;
	int16_t toa_tod_initiator;
	int16_t tod_toa_reflector;
};

struct processing_context {
	uint16_t rtt_timing_data_index;
	uint16_t iq_sample_channel_data_index;
	uint8_t n_ap;
	enum bt_conn_le_cs_role role;
};

struct complex_sample {
    float real;
    float imag;
};

float estimate_distance(struct net_buf_simple *local_steps, struct net_buf_simple *peer_steps,
		       uint8_t n_ap, enum bt_conn_le_cs_role role, uint16_t compensation, int tag_idx);