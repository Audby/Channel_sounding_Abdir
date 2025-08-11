#ifndef COREAOIT_DISTANCE_H_
#define COREAOIT_DISTANCE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

struct bt_le_cs_iq_sample_replica {
	int16_t i;
	int16_t q;
};
struct iq_sample_and_channel_replica {
	bool failed;
	uint8_t channel;
	uint8_t antenna_path;
	struct bt_le_cs_iq_sample_replica local_iq_sample;
	struct bt_le_cs_iq_sample_replica peer_iq_sample;
};
/**
 * @brief Estimate the distance based on IQ samples and channel information using CoreAIOT algorithm.
 *
 * This function calculates the distance between devices by analyzing the IQ samples,
 * and other parameters such as antenna path and frequency compensation. It implements a multi-step 
 * signal processing pipeline to improve accuracy and robustness in various propagation conditions.
 *
 * @param data Pointer to an array of IQ sample data and channel/antenna path.
 * @param len Length of the `data` array (number of IQ samples).
 * @param ant_path Antenna path used for the measurement (start from 0).
 * @param samples_cnt Pointer to a variable where the number of valid IQ samples will be stored.
 * @param most_recent_frequency_compensation Frequency compensation value from the latest calibration. 
 * ref to frequency_compensation in the header of bt_conn_le_cs_subevent_result
 * @param tag_idx Index of the tag for which distance is being estimated (used for internal state tracking, start from 0).
 *
 * @return Estimated distance in centimeters. Returns zero if the hardware is not supported.
 */
float estimate_distance_coreaiot(struct iq_sample_and_channel_replica *data, 
	uint8_t len, 
	uint8_t ant_path,
	uint8_t *samples_cnt,
	uint16_t most_recent_frequency_compensation, 
	int tag_idx);

#endif /* COREAOIT_DISTANCE_H_ */