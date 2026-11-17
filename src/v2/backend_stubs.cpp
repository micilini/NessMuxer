#include "core.hpp"
namespace ness2 {
#ifndef NESS_HAVE_X264_V2
std::unique_ptr<Encoder> x264_encoder(ness_encoder_config_v2) {
  fail(NESS_V2_E_UNAVAILABLE, NESS_V2_STAGE_CONFIG, "x264 V2 backend was not compiled");
}
#endif
#ifndef NESS_HAVE_QSV_V2
std::unique_ptr<Encoder> qsv_encoder(ness_encoder_config_v2, const ness_gpu_device_v2 &) {
  fail(NESS_V2_E_UNAVAILABLE, NESS_V2_STAGE_CONFIG, "Direct Quick Sync V2 backend was not compiled");
}
#endif
#ifndef NESS_HAVE_NVENC_V2
std::unique_ptr<Encoder> nvenc_encoder(ness_encoder_config_v2, const ness_gpu_device_v2 &) {
  fail(NESS_V2_E_UNAVAILABLE, NESS_V2_STAGE_CONFIG, "Direct NVENC V2 backend was not compiled");
}
#endif
#ifndef NESS_HAVE_AMF_V2
std::unique_ptr<Encoder> amf_encoder(ness_encoder_config_v2, const ness_gpu_device_v2 &) {
  fail(NESS_V2_E_UNAVAILABLE, NESS_V2_STAGE_CONFIG, "Direct AMF V2 backend was not compiled");
}
#endif
}
