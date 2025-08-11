#pragma once

#include <zephyr/kernel.h>
#include <math.h>
#include <zephyr/bluetooth/cs.h>
#include <bluetooth/services/ras.h>
#include <bluetooth/cs_de.h>
#include "../global.h"

void store_distance_estimates(cs_de_report_t *p_report);
uint8_t cs_buffer_status();
cs_de_dist_estimates_t get_distance(uint8_t ap);
