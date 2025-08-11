#include "calc.h"

static K_MUTEX_DEFINE(distance_estimate_buffer_mutex);

static uint8_t buffer_index;
static uint8_t buffer_num_valid;
static cs_de_dist_estimates_t distance_estimate_buffer[MAX_AP][DE_SLIDING_WINDOW_SIZE];

uint8_t cs_buffer_status() {
	return buffer_num_valid; 
}

cs_de_dist_estimates_t get_distance(uint8_t ap) {
	cs_de_dist_estimates_t averaged_result = {};
	uint8_t num_ifft = 0;
	uint8_t num_phase_slope = 0;
	uint8_t num_rtt = 0;

	int lock_state = k_mutex_lock(&distance_estimate_buffer_mutex, K_FOREVER);

	__ASSERT_NO_MSG(lock_state == 0);

	for (uint8_t i = 0; i < buffer_num_valid; i++) {
		if (isfinite(distance_estimate_buffer[ap][i].ifft)) { // isfinite i.e is a real number check 
			num_ifft++;
			averaged_result.ifft += distance_estimate_buffer[ap][i].ifft;
		}
		if (isfinite(distance_estimate_buffer[ap][i].phase_slope)) {
			num_phase_slope++;
			averaged_result.phase_slope += distance_estimate_buffer[ap][i].phase_slope;
		}
		if (isfinite(distance_estimate_buffer[ap][i].rtt)) {
			num_rtt++;
			averaged_result.rtt += distance_estimate_buffer[ap][i].rtt;
		}
	}

	k_mutex_unlock(&distance_estimate_buffer_mutex);

	if (num_ifft) {
		averaged_result.ifft /= num_ifft;
	}

	if (num_phase_slope) {
		averaged_result.phase_slope /= num_phase_slope;
	}

	if (num_rtt) {
		averaged_result.rtt /= num_rtt;
	}

	return averaged_result;
}

void store_distance_estimates(cs_de_report_t *p_report) {
	int lock_state = k_mutex_lock(&distance_estimate_buffer_mutex, K_FOREVER);

	__ASSERT_NO_MSG(lock_state == 0);

	for (uint8_t ap = 0; ap < p_report->n_ap; ap++) {
		memcpy(&distance_estimate_buffer[ap][buffer_index],
		       &p_report->distance_estimates[ap], sizeof(cs_de_dist_estimates_t));
	}

	buffer_index = (buffer_index + 1) % DE_SLIDING_WINDOW_SIZE;

	if (buffer_num_valid < DE_SLIDING_WINDOW_SIZE) {
		buffer_num_valid++;
	}

	k_mutex_unlock(&distance_estimate_buffer_mutex);
}