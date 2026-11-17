#include "core.hpp"

extern "C" {
int32_t ness_encoder_create_gpu_v2(const ness_encoder_config_v2 *c,
                                   const ness_gpu_device_v2 *target,
                                   ness_encoder_v2 **out, ness_error_v2 *e) {
  if (out)
    *out = nullptr;
  return ness2::boundary(e, [&] {
    ness2::envelope(c);
    ness2::envelope(target);
    if (!out || !target->device || !target->adapter_luid)
      ness2::fail(-3, 1, "Missing GPU encoder device/output");
    if ((c->backend != NESS_V2_BACKEND_MF_D3D11 && c->backend != NESS_V2_BACKEND_QSV_D3D11 &&
         c->backend != NESS_V2_BACKEND_NVENC_D3D11 && c->backend != NESS_V2_BACKEND_AMF_D3D11) ||
        c->policy != NESS_V2_POLICY_REQUIRE_GPU)
      ness2::fail(
          -11, 1,
          "GPU encoder requires an explicit native backend and strict GPU policy");
    auto config = *c;
    if (!config.max_pending_frames)
      config.max_pending_frames = 16;
    if (!config.max_packet_bytes)
      config.max_packet_bytes = 16 * 1024 * 1024;
    // Shared geometry/color/rate validation without weakening software policy
    // validation.
    auto common = config;
    common.backend = NESS_V2_BACKEND_MF_SOFTWARE;
    common.policy = NESS_V2_POLICY_SOFTWARE;
    ness2::configure(common);
    std::unique_ptr<ness2::Encoder> encoder;
    switch (config.backend) {
    case NESS_V2_BACKEND_QSV_D3D11: encoder=ness2::qsv_encoder(config,*target); break;
    case NESS_V2_BACKEND_NVENC_D3D11: encoder=ness2::nvenc_encoder(config,*target); break;
    case NESS_V2_BACKEND_AMF_D3D11: encoder=ness2::amf_encoder(config,*target); break;
    default: encoder=ness2::mf_gpu_encoder(config,*target); break;
    }
    *out = new ness_encoder_v2(std::move(encoder));
    return 0;
  });
}
int32_t ness_encoder_gpu_info_get_v2(ness_encoder_v2 *c, ness_gpu_info_v2 *out,
                                     ness_error_v2 *e) {
  return ness2::boundary(e, [&] {
    ness2::envelope(out);
    if (!c)
      ness2::fail(-3, 2, "Missing encoder");
    *out = c->value->gpu_info();
    return 0;
  });
}

int32_t ness_encoder_create_v2(const ness_encoder_config_v2 *c,
                               ness_encoder_v2 **out, ness_error_v2 *e) {
  if (out)
    *out = nullptr;
  return ness2::boundary(e, [&] {
    ness2::envelope(c);
    if (!out)
      ness2::fail(-3, 1, "Missing encoder output");
    auto config = *c;
    if (!config.max_pending_frames)
      config.max_pending_frames = 16;
    if (!config.max_packet_bytes)
      config.max_packet_bytes = 16 * 1024 * 1024;
    ness2::configure(config);
    *out = new ness_encoder_v2(config.backend == NESS_V2_BACKEND_X264
        ? ness2::x264_encoder(config) : ness2::mf_encoder(config));
    return 0;
  });
}
int32_t ness_encoder_info_get_v2(ness_encoder_v2 *c, ness_encoder_info_v2 *out,
                                 ness_error_v2 *e) {
  return ness2::boundary(e, [&] {
    ness2::envelope(out);
    if (!c)
      ness2::fail(-3, 2, "Missing encoder");
    *out = c->value->info();
    return 0;
  });
}
int32_t ness_encoder_submit_cpu_v2(ness_encoder_v2 *c,
                                   const ness_cpu_frame_v2 *frame,
                                   ness_error_v2 *e) {
  return ness2::boundary(e, [&] {
    ness2::envelope(frame);
    if (!c)
      ness2::fail(-3, 2, "Missing encoder");
    c->value->submit(*frame);
    return 0;
  });
}
int32_t ness_encoder_receive_v2(ness_encoder_v2 *c, ness_packet_v2 **out,
                                ness_error_v2 *e) {
  if (out)
    *out = nullptr;
  return ness2::boundary(e, [&] {
    if (!c || !out)
      ness2::fail(-3, 3, "Missing encoder/packet output");
    if (auto packet = c->value->receive()) {
      *out = new ness_packet_v2{std::move(packet)};
      return 0;
    }
    return c->value->stats().drained ? NESS_V2_END : NESS_V2_AGAIN;
  });
}
int32_t ness_encoder_description_v2(ness_encoder_v2 *c, ness_codec_v2 **out,
                                    ness_error_v2 *e) {
  if (out)
    *out = nullptr;
  return ness2::boundary(e, [&] {
    if (!c || !out)
      ness2::fail(-3, 3, "Missing encoder/codec output");
    if (auto codec = c->value->description()) {
      *out = new ness_codec_v2(std::move(*codec));
      return 0;
    }
    return NESS_V2_AGAIN;
  });
}
int32_t ness_encoder_drain_v2(ness_encoder_v2 *c, ness_error_v2 *e) {
  return ness2::boundary(e, [&] {
    if (!c)
      ness2::fail(-3, 2, "Missing encoder");
    c->value->drain();
    return 0;
  });
}
int32_t ness_encoder_discard_v2(ness_encoder_v2 *c, ness_error_v2 *e) {
  return ness2::boundary(e, [&] {
    if (!c)
      ness2::fail(-3, 2, "Missing encoder");
    c->value->discard();
    return 0;
  });
}
int32_t ness_encoder_stats_get_v2(ness_encoder_v2 *c,
                                  ness_encoder_stats_v2 *out,
                                  ness_error_v2 *e) {
  return ness2::boundary(e, [&] {
    ness2::envelope(out);
    if (!c)
      ness2::fail(-3, 2, "Missing encoder");
    *out = c->value->stats();
    return 0;
  });
}
void ness_encoder_release_v2(ness_encoder_v2 *c) {
  if (!c)
    return;
  try {
    (void)c->value->info();
    delete c;
  } catch (...) { /* Owner thread must release; no exception crosses C. */
  }
}
int32_t ness_encoder_submit_gpu_v2(ness_encoder_v2 *c,
                                   const ness_gpu_frame_v2 *frame,
                                   ness_input_lease_v2 **out,
                                   ness_error_v2 *e) {
  if (out)
    *out = nullptr;
  return ness2::boundary(e, [&]() -> int32_t {
    ness2::envelope(frame);
    if (c)
      (void)c->value->info();
    if (!c || !out || !frame->device || !frame->texture ||
        frame->resource_type != NESS_V2_D3D11_TEXTURE2D ||
        frame->format != NESS_V2_NV12 || !frame->width || !frame->height ||
        frame->reserved)
      ness2::fail(-3, 2, "Invalid negotiated GPU descriptor");
    ness2::timestamp(frame->pts_hns, frame->duration_hns);
    auto owned = std::make_unique<ness_input_lease_v2>();
    owned->value = c->value->submit_gpu(*frame);
    *out = owned.release();
    return 0;
  });
}
int32_t ness_input_completion_get_v2(ness_input_lease_v2 *lease,
                                     ness_completion_v2 *out,
                                     ness_error_v2 *e) {
  return ness2::boundary(e, [&]() -> int32_t {
    ness2::envelope(out);
    if (!lease)
      ness2::fail(-3, 2, "Missing input completion lease");
    lease->value->poll();
    out->ready = lease->value->ready.load(std::memory_order_acquire);
    out->reserved = 0;
    return 0;
  });
}
void ness_input_release_v2(ness_input_lease_v2 *lease) { delete lease; }
int32_t ness_encoder_probe_v2(const ness_encoder_config_v2 *config,
                              ness_encoder_info_v2 *out, ness_error_v2 *e) {
  return ness2::boundary(e, [&] {
    ness2::envelope(config);
    ness2::envelope(out);
    ness_encoder_v2 *raw{};
    auto error = ness2::fresh<ness_error_v2>();
    auto result = ness_encoder_create_v2(config, &raw, &error);
    if (result < 0)
      ness2::fail(result, error.stage,
                  reinterpret_cast<const char *>(error.message_utf8),
                  error.native_code);
    std::unique_ptr<ness_encoder_v2, decltype(&ness_encoder_release_v2)> owned(
        raw, ness_encoder_release_v2);
    *out = raw->value->info();
    std::vector<uint8_t> pixels(size_t(config->width) * config->height * 3 / 2,
                                128);
    auto frame = ness2::fresh<ness_cpu_frame_v2>();
    frame.format = 1;
    frame.width = config->width;
    frame.height = config->height;
    frame.duration_hns = 10000000LL * config->fps_den / config->fps_num;
    frame.planes[0] = {pixels.data(), uint64_t(config->width) * config->height,
                       config->width, 0};
    frame.planes[1] = {pixels.data() + size_t(config->width) * config->height,
                       uint64_t(config->width) * config->height / 2,
                       config->width, 0};
    raw->value->submit(frame);
    raw->value->drain();
    uint64_t packets{};
    for (;;) {
      if (raw->value->receive()) {
        ++packets;
        continue;
      }
      if (raw->value->stats().drained)
        break;
    }
    if (packets != 1 || !raw->value->description())
      ness2::fail(-5, 2,
                  "Software probe did not produce one complete packet and "
                  "codec description");
    return 0;
  });
}
}
