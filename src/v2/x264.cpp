#include "core.hpp"
#include "x264_thread_budget.hpp"
#include <cstdio>
#include <filesystem>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <dlfcn.h>
#endif
#include <x264.h>

static_assert(X264_BUILD == 164, "Use the pinned matching x264 ABI164 runtime");
#define NESS_STRING_INNER(value) #value
#define NESS_STRING(value) NESS_STRING_INNER(value)
namespace ness2 {
namespace {
#ifdef _WIN32
// This fingerprint identifies the bundled, hash-verified ABI164 runtime.
// A matching exported symbol name alone is insufficient to establish compatibility.
class VerifiedLibrary final {
  HANDLE file_ = INVALID_HANDLE_VALUE;
public:
  explicit VerifiedLibrary(const std::filesystem::path &path) {
    file_=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,
      OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if (file_==INVALID_HANDLE_VALUE)
      fail(NESS_V2_E_UNAVAILABLE,1,"Pinned x264 runtime is missing or cannot be locked",GetLastError());
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    try {
      LARGE_INTEGER length{};
      if (!GetFileSizeEx(file_,&length) || length.QuadPart<=0 || length.QuadPart>64*1024*1024)
        fail(NESS_V2_E_UNAVAILABLE,1,"Pinned x264 runtime size is invalid");
      if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 ||
          BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)
        fail(NESS_V2_E_UNAVAILABLE,1,"Cannot verify pinned x264 runtime integrity");
      std::array<uint8_t,65536> buffer{};
      DWORD read{};
      for (;;) {
        if (!ReadFile(file_,buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr))
          fail(NESS_V2_E_UNAVAILABLE,1,"Cannot read pinned x264 runtime",GetLastError());
        if (!read) break;
        if (BCryptHashData(hash,buffer.data(),read,0)<0)
          fail(NESS_V2_E_UNAVAILABLE,1,"Cannot hash pinned x264 runtime");
      }
      std::array<uint8_t,32> digest{};
      constexpr std::array<uint8_t,32> expected{
        0x12,0xdc,0x03,0xbc,0xba,0x9f,0xf8,0x33,0x11,0xc6,0xb4,0x70,0xbc,0xea,0x59,0x30,
        0xfe,0x11,0x7f,0x9b,0x22,0xe8,0x94,0xba,0x45,0x41,0x34,0xdb,0xa3,0x94,0xfb,0x37};
      if (BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0 || digest!=expected)
        fail(NESS_V2_E_UNAVAILABLE,1,"Pinned x264 ABI164 runtime fingerprint mismatch");
      BCryptDestroyHash(hash); hash=nullptr;
      BCryptCloseAlgorithmProvider(algorithm,0); algorithm=nullptr;
    } catch (...) {
      if (hash) BCryptDestroyHash(hash);
      if (algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
      CloseHandle(file_); file_=INVALID_HANDLE_VALUE;
      throw;
    }
  }
  VerifiedLibrary(const VerifiedLibrary &)=delete;
  VerifiedLibrary &operator=(const VerifiedLibrary &)=delete;
  ~VerifiedLibrary() { if (file_!=INVALID_HANDLE_VALUE) CloseHandle(file_); }
};
#endif
class X264Api final {
#ifdef _WIN32
  HMODULE module_{};
#else
  void *module_{};
#endif
  template<class Function> void bind(Function &function, const char *name) {
#ifdef _WIN32
    const auto address = GetProcAddress(module_, name);
#else
    const auto address = dlsym(module_, name);
#endif
    if (!address)
      fail(NESS_V2_E_UNAVAILABLE, 1, "Pinned x264 ABI symbol unavailable");
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function,&address,sizeof(function));
  }
  void release() noexcept {
#ifdef _WIN32
    if (module_) FreeLibrary(module_);
#else
    if (module_) dlclose(module_);
#endif
    module_ = nullptr;
  }
public:
  decltype(&x264_param_default_preset) preset{};
  decltype(&x264_param_apply_profile) profile{};
  decltype(&x264_picture_init) picture{};
  decltype(&x264_encoder_open) open{};
  decltype(&x264_encoder_close) close{};
  decltype(&x264_encoder_encode) encode{};
  decltype(&x264_encoder_headers) headers{};
  decltype(&x264_encoder_delayed_frames) delayed{};
  decltype(&x264_encoder_parameters) parameters{};
  X264Api() {
    try {
#ifdef _WIN32
      static constexpr wchar_t anchor[] = L"NessMuxer.Native.X264.ABI164";
      HMODULE owner{};
      if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,anchor,&owner))
        fail(NESS_V2_E_UNAVAILABLE,1,"Cannot resolve native x264 package directory",GetLastError());
      std::array<wchar_t,32768> path{};
      const auto length = GetModuleFileNameW(owner,path.data(),static_cast<DWORD>(path.size()));
      if (!length || length >= path.size())
        fail(NESS_V2_E_UNAVAILABLE,1,"Native x264 package path unavailable",GetLastError());
      const auto library = std::filesystem::path(path.data()).parent_path()/L"libx264-164.dll";
      // Keep a read-only file lock until the verified image has been mapped.
      const VerifiedLibrary verified(library);
      module_ = LoadLibraryExW(library.c_str(),nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
      if (!module_)
        fail(NESS_V2_E_UNAVAILABLE,1,"Pinned libx264-164.dll is unavailable beside the native library",GetLastError());
#else
#ifdef __APPLE__
      module_ = dlopen("libx264.164.dylib",RTLD_NOW|RTLD_LOCAL);
#else
      module_ = dlopen("libx264.so.164",RTLD_NOW|RTLD_LOCAL);
#endif
      if (!module_) fail(NESS_V2_E_UNAVAILABLE,1,"Pinned x264 ABI164 runtime unavailable");
#endif
      bind(preset,"x264_param_default_preset");
      bind(profile,"x264_param_apply_profile");
      bind(picture,"x264_picture_init");
      bind(open,NESS_STRING(x264_encoder_open));
      bind(close,"x264_encoder_close");
      bind(encode,"x264_encoder_encode");
      bind(headers,"x264_encoder_headers");
      bind(delayed,"x264_encoder_delayed_frames");
      bind(parameters,"x264_encoder_parameters");
    } catch (...) { release(); throw; }
  }
  X264Api(const X264Api &) = delete;
  X264Api &operator=(const X264Api &) = delete;
  ~X264Api() { release(); }
};
class X264 final : public Encoder {
  struct TaggedInput { int64_t token{}; InputIdentity identity; };
  struct PendingOutput {
    const uint8_t *data{};
    size_t bytes{}, normalized_bytes{};
    int64_t token{};
    bool key{};
  };
  X264Api api_;
  ness_encoder_config_v2 config_;
  PacketCollector collector_;
  x264_t *encoder_{};
  std::optional<Codec> codec_;
  std::array<std::optional<TaggedInput>,64> identities_{};
  std::optional<PendingOutput> pending_output_;
  const std::thread::id owner_ = std::this_thread::get_id();
  int64_t next_token_{};
  int effective_threads_{}, effective_slices_{};
  bool draining_{}, drained_{}, discarded_{}, failed_{};
  void thread() const {
    if (std::this_thread::get_id() != owner_)
      fail(NESS_V2_E_THREAD,2,"x264 encoder requires its creating thread");
  }
  void usable() const {
    thread();
    if (!encoder_ || failed_ || discarded_)
      fail(NESS_V2_E_STATE,2,"x264 encoder is closed, failed or discarded");
  }
  void output(int bytes, x264_nal_t *nals, int count, const x264_picture_t &picture) {
    if (bytes < 0) {
      failed_ = true;
      fail(NESS_V2_E_ENCODER,2,"x264 rejected encoding input");
    }
    if (!bytes) return;
    if (!nals || count <= 0 || count > 4096 || !nals[0].p_payload) {
      failed_ = true;
      fail(NESS_V2_E_ENCODER,3,"x264 returned invalid output NALs");
    }
    if (picture.i_dts != picture.i_pts) {
      failed_ = true;
      fail(NESS_V2_E_ENCODER,3,"x264 changed an input identity or reordered a zero-B-frame stream");
    }
    size_t normalized{}, observed{};
    for (int index=0;index<count;++index) {
      const auto &nal=nals[index];
      if (nal.i_payload<4 || nal.p_payload!=nals[0].p_payload+observed) {
        failed_=true;
        fail(NESS_V2_E_ENCODER,3,"x264 NAL payload is truncated or noncontiguous");
      }
      const auto prefix = nal.p_payload[0]==0 && nal.p_payload[1]==0 && nal.p_payload[2]==1 ? 3u : 4u;
      normalized += static_cast<size_t>(nal.i_payload)-prefix+4;
      observed += static_cast<size_t>(nal.i_payload);
    }
    if (observed!=static_cast<size_t>(bytes) || normalized>config_.max_packet_bytes) {
      failed_=true;
      fail(NESS_V2_E_ENCODER,3,"Encoded x264 frame exceeds the declared packet budget");
    }
    pending_output_=PendingOutput{nals[0].p_payload,observed,normalized,picture.i_pts,picture.b_keyframe!=0};
    (void)commit_output();
  }
  bool commit_output() {
    if (!pending_output_) return true;
    const auto &pending=*pending_output_;
    if (!collector_.can_receive_output() ||
        pending.normalized_bytes>config_.max_packet_bytes-collector_.bytes()) return false;
    const auto found=std::find_if(identities_.begin(),identities_.end(),[&](const auto &value) {
      return value && value->token==pending.token;
    });
    if (found==identities_.end()) {
      failed_=true;
      fail(NESS_V2_E_ENCODER,3,"x264 output has no owned input identity");
    }
    // Codec timestamps are private ordinal tags. Container PTS/duration are
    // restored exactly from the original caller identity, without quantization.
    const auto input = (*found)->identity;
    collector_.accept({pending.data,pending.bytes},NESS_V2_ANNEX_B,
                      input.pts,input.pts,pending.key);
    found->reset();
    pending_output_.reset();
    return true;
  }
  void pump_drain() {
    if (!commit_output()) return;
    while (api_.delayed(encoder_) > 0 && collector_.can_receive_output() && !pending_output_) {
      x264_nal_t *nals{};
      int count{};
      x264_picture_t picture{};
      const auto bytes = api_.encode(encoder_,&nals,&count,nullptr,&picture);
      output(bytes,nals,count,picture);
      if (!bytes && api_.delayed(encoder_) > 0)
        fail(NESS_V2_E_ENCODER,2,"x264 drain made no progress");
    }
    drained_ = !pending_output_ && api_.delayed(encoder_) == 0 && collector_.pending() == 0;
  }
public:
  explicit X264(ness_encoder_config_v2 config) : config_(config),collector_(config) {
    configure(config_);
    x264_param_t parameters{};
    if (api_.preset(&parameters,"veryfast","zerolatency") < 0)
      fail(NESS_V2_E_UNAVAILABLE,1,"x264 realtime preset unavailable");
    parameters.i_threads = x264_slice_thread_budget(config_.width,config_.height,
      config_.fps_num,config_.fps_den,std::thread::hardware_concurrency());
    parameters.i_width = static_cast<int>(config_.width);
    parameters.i_height = static_cast<int>(config_.height);
    parameters.i_csp = X264_CSP_NV12;
    parameters.i_bitdepth = 8;
    parameters.i_fps_num = config_.fps_num;
    parameters.i_fps_den = config_.fps_den;
    parameters.b_vfr_input = 0;
    parameters.i_timebase_num = config_.fps_den;
    parameters.i_timebase_den = config_.fps_num;
    parameters.i_keyint_max = (std::max)(1,static_cast<int>(2ULL*config_.fps_num/config_.fps_den));
    parameters.i_bframe = 0;
    parameters.rc.i_rc_method = X264_RC_ABR;
    parameters.rc.i_bitrate = static_cast<int>((uint64_t(config_.bitrate_bps)+500)/1000);
    parameters.rc.i_vbv_max_bitrate = parameters.rc.i_bitrate;
    parameters.rc.i_vbv_buffer_size = parameters.rc.i_bitrate;
    parameters.rc.b_filler = 1;
    parameters.i_nal_hrd = X264_NAL_HRD_CBR;
    parameters.b_annexb = 1;
    parameters.b_repeat_headers = 0;
    parameters.vui.b_fullrange = 0;
    parameters.vui.i_colorprim = parameters.vui.i_transfer = parameters.vui.i_colmatrix = 1;
    if (api_.profile(&parameters,"baseline") < 0)
      fail(NESS_V2_E_UNAVAILABLE,1,"x264 baseline profile unavailable");
    encoder_ = api_.open(&parameters);
    if (!encoder_) fail(NESS_V2_E_ENCODER,2,"Cannot initialize pinned x264 software encoder");
    try {
      x264_param_t effective{};
      api_.parameters(encoder_,&effective);
      if (effective.i_bframe || effective.i_width != parameters.i_width ||
          effective.i_height != parameters.i_height || effective.i_bitdepth != 8)
        fail(NESS_V2_E_ENCODER,2,"x264 changed the qualified stream configuration");
      effective_threads_ = effective.i_threads;
      effective_slices_ = effective.i_slice_count;
      x264_nal_t *nals{};
      int count{};
      const auto bytes = api_.headers(encoder_,&nals,&count);
      if (bytes <= 0 || !nals || count <= 0)
        fail(NESS_V2_E_ENCODER,3,"x264 SPS/PPS unavailable");
      auto avcc = codec_private({nals[0].p_payload,static_cast<size_t>(bytes)});
      if (avcc[1] != config_.profile)
        fail(NESS_V2_E_ENCODER,3,"x264 profile does not match the requested AVC description");
      auto info = fresh<ness_codec_view_v2>();
      info.width=config_.width; info.height=config_.height;
      info.fps_num=config_.fps_num; info.fps_den=config_.fps_den;
      info.profile=config_.profile; info.color=config_.color;
      codec_ = Codec{info,std::move(avcc)};
    } catch (...) { api_.close(encoder_); encoder_=nullptr; throw; }
  }
  ~X264() override { if (encoder_) api_.close(encoder_); }
  void submit(const ness_cpu_frame_v2 &frame) override {
    usable();
    envelope(&frame);
    if (draining_ || drained_) fail(NESS_V2_E_STATE,2,"x264 input after EOS");
    if (!commit_output())
      fail(NESS_V2_E_BACKPRESSURE,2,"Receive the retained x264 output before more input");
    if (frame.reserved || frame.format != NESS_V2_NV12 ||
        frame.width != config_.width || frame.height != config_.height)
      fail(NESS_V2_E_ARGUMENT,2,"x264 expects the negotiated NV12 geometry");
    for (size_t plane=0;plane<2;++plane) {
      const auto &input=frame.planes[plane];
      const uint64_t rows=plane ? frame.height/2 : frame.height;
      if (!input.data || input.reserved || input.stride<frame.width || input.stride>65536 ||
          input.bytes<(rows-1)*input.stride+frame.width)
        fail(NESS_V2_E_ARGUMENT,2,"x264 input plane or stride is invalid");
    }
    collector_.validate({frame.frame_id,frame.pts_hns,frame.duration_hns});
    if (!collector_.can_receive_output())
      fail(NESS_V2_E_BACKPRESSURE,2,"Receive x264 output before submitting more input");
    const auto slot=std::find(identities_.begin(),identities_.end(),std::nullopt);
    if (slot==identities_.end()) fail(NESS_V2_E_BACKPRESSURE,2,"x264 identity budget exhausted");
    x264_picture_t input{},encoded{};
    api_.picture(&input);
    input.img.i_csp=X264_CSP_NV12; input.img.i_plane=2;
    for (size_t plane=0;plane<2;++plane) {
      input.img.plane[plane]=const_cast<uint8_t *>(frame.planes[plane].data);
      input.img.i_stride[plane]=static_cast<int>(frame.planes[plane].stride);
    }
    input.i_pts=next_token_++;
    *slot=TaggedInput{input.i_pts,{frame.frame_id,frame.pts_hns,frame.duration_hns}};
    collector_.remember((*slot)->identity);
    x264_nal_t *nals{}; int count{};
    const auto bytes=api_.encode(encoder_,&nals,&count,&input,&encoded);
    output(bytes,nals,count,encoded);
  }
  std::shared_ptr<Packet> receive() override {
    usable();
    if (auto packet=collector_.receive()) {
      (void)commit_output();
      return packet;
    }
    (void)commit_output();
    if (draining_ && !drained_) pump_drain();
    return collector_.receive();
  }
  std::optional<Codec> description() override { usable(); return codec_; }
  void drain() override { usable(); draining_=true; pump_drain(); }
  void discard() override {
    usable(); collector_.discard(); pending_output_.reset(); discarded_=true;
    for (auto &input:identities_) input.reset();
  }
  ness_encoder_info_v2 info() override {
    thread(); auto value=fresh<ness_encoder_info_v2>();
    value.backend=NESS_V2_BACKEND_X264; value.software=1;
    value.width=config_.width; value.height=config_.height;
    value.fps_num=config_.fps_num; value.fps_den=config_.fps_den;
    value.profile=config_.profile; value.color=config_.color;
    const auto length=std::snprintf(reinterpret_cast<char *>(value.implementation_utf8),
      sizeof(value.implementation_utf8),"x264 ABI164 veryfast/zerolatency t=%d s=%d",
      effective_threads_,effective_slices_);
    if (length<0 || size_t(length)>=sizeof(value.implementation_utf8))
      fail(NESS_V2_E_STATE,2,"x264 implementation description exceeded its ABI field");
    return value;
  }
  ness_encoder_stats_v2 stats() override {
    thread(); auto value=fresh<ness_encoder_stats_v2>();
    value.submitted=collector_.submitted; value.emitted=collector_.emitted;
    value.pending=collector_.pending(); value.queued_packets=collector_.queued();
    value.queued_bytes=collector_.bytes(); value.discarded=collector_.discarded;
    value.draining=draining_; value.drained=drained_; value.failed=failed_; return value;
  }
};
}
std::unique_ptr<Encoder> x264_encoder(ness_encoder_config_v2 config) {
  return std::make_unique<X264>(config);
}
}
