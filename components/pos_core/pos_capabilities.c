#include "pos_capabilities.h"
bool pos_feature_allowed(pos_feature_t feature, bool cap, bool gps,
                         bool enabled) {
  switch (feature) {
  case POS_FEATURE_RADIO:
  case POS_FEATURE_RANGE_TEST:
    return cap && enabled;
  case POS_FEATURE_MAPS:
    return gps;
  default:
    return false;
  }
}
