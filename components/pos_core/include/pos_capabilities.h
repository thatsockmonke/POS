#pragma once
#include <stdbool.h>
typedef enum {POS_FEATURE_RADIO,POS_FEATURE_RANGE_TEST,POS_FEATURE_MAPS} pos_feature_t;
bool pos_feature_allowed(pos_feature_t feature,bool cap_detected,bool gps_detected,bool lora_enabled);
