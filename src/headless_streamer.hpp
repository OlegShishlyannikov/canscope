#pragma once

#include "can_data.hpp"

#include <vector>

class HeadlessStreamer {
public:
  void onBatch(const std::vector<can_frame_update_s> &batch);
};
