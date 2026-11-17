#include "core.hpp"
namespace ness2 {
std::unique_ptr<Encoder> mf_encoder(ness_encoder_config_v2) {
  fail(NESS_V2_E_UNAVAILABLE,2,"Media Foundation encoder is not compiled for this build/platform");
}
std::unique_ptr<Encoder> mf_gpu_encoder(ness_encoder_config_v2,const ness_gpu_device_v2 &) {
  fail(NESS_V2_E_UNAVAILABLE,2,"Media Foundation D3D11 encoder is not compiled for this build/platform");
}
}
