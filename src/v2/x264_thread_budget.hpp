#pragma once
#include <cstdint>

namespace ness2 {
// Two slices reduced CPU cost without delayed frames in the 720p30 qualification.
// Preserve x264's automatic selection outside that measured workload envelope.
// Zero is X264_THREADS_AUTO; an unknown hardware count is not evidence to tune it.
constexpr int x264_slice_thread_budget(uint32_t width, uint32_t height,
    uint32_t fps_num, uint32_t fps_den, unsigned logical_processors) noexcept {
  if (logical_processors < 4 || !width || !height || width < height ||
      width > 1280 || height > 720 || (width & 1) || (height & 1) ||
      !fps_num || !fps_den || uint64_t(fps_num) > uint64_t(fps_den) * 30)
    return 0;
  return 2;
}
}
