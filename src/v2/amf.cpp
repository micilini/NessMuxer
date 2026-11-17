#include "gpu_input.hpp"
#pragma warning(push)
#pragma warning(disable: 4201)
#include <core/Factory.h>
#include <core/Version.h>
#include <core/Buffer.h>
#include <components/VideoEncoderVCE.h>
#include <components/ColorSpace.h>
#pragma warning(pop)

namespace ness2 {
namespace {
void amf_checked(AMF_RESULT status,const char *message,uint32_t stage=2) {
  if(status==AMF_OK) return;
  const bool unavailable=status==AMF_NOT_SUPPORTED || status==AMF_NOT_IMPLEMENTED ||
    status==AMF_NO_DEVICE || status==AMF_CODEC_NOT_SUPPORTED || status==AMF_ENCODER_NOT_PRESENT ||
    status==AMF_SURFACE_FORMAT_NOT_SUPPORTED || status==AMF_NO_INTERFACE ||
    (stage==NESS_V2_STAGE_CONFIG && (status==AMF_NOT_FOUND || status==AMF_INVALID_FORMAT || status==AMF_INVALID_RESOLUTION));
  fail(unavailable?NESS_V2_E_UNAVAILABLE:(status==AMF_OUT_OF_MEMORY?NESS_V2_E_ALLOC:NESS_V2_E_ENCODER),
    stage,message,static_cast<int32_t>(status));
}
struct Runtime {
  HMODULE module{};
  amf::AMFFactory *factory{};
  amf::AMFContextPtr context;
  amf::AMFComponentPtr encoder;
  void stop() noexcept { if(encoder) { (void)encoder->Terminate(); encoder.Release(); } }
  ~Runtime() {
    stop();
    if(context) { (void)context->Terminate(); context.Release(); }
    if(module) FreeLibrary(module);
  }
  void open(ID3D11Device *device) {
    module=LoadLibraryExW(AMF_DLL_NAME,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module) fail(NESS_V2_E_UNAVAILABLE,1,"AMD AMF driver runtime is not installed",static_cast<int32_t>(GetLastError()));
    const auto init=reinterpret_cast<AMFInit_Fn>(GetProcAddress(module,AMF_INIT_FUNCTION_NAME));
    const auto query=reinterpret_cast<AMFQueryVersion_Fn>(GetProcAddress(module,AMF_QUERY_VERSION_FUNCTION_NAME));
    if(!init || !query) fail(NESS_V2_E_UNAVAILABLE,1,"AMD AMF driver entry points are unavailable");
    amf_uint64 version{};
    amf_checked(query(&version),"AMD AMF runtime version query failed",1);
    if(!version) fail(NESS_V2_E_UNAVAILABLE,1,"AMD AMF runtime returned no API version");
    amf_checked(init(AMF_FULL_VERSION,&factory),"AMD AMF factory initialization failed",1);
    if(!factory) fail(NESS_V2_E_ENCODER,1,"AMD AMF returned an empty factory");
    amf_checked(factory->CreateContext(&context),"AMD AMF context creation failed",1);
    if(!context) fail(NESS_V2_E_ENCODER,1,"AMD AMF returned an empty context");
    amf_checked(context->InitDX11(device),"AMD AMF rejected the selected D3D11 device",1);
    if(context->GetDX11Device()!=device) fail(NESS_V2_E_UNAVAILABLE,1,"AMD AMF substituted the selected device");
    amf_checked(factory->CreateComponent(context,AMFVideoEncoderVCE_AVC,&encoder),"AMD AMF H.264 component unavailable",1);
    if(!encoder) fail(NESS_V2_E_ENCODER,1,"AMD AMF returned an empty encoder");
  }
};
struct Slot final : amf::AMFSurfaceObserver {
  gpu::ComPtr<ID3D11Texture2D> texture;
  amf::AMFSurfacePtr surface;
  std::atomic<bool> released{true};
  std::optional<InputIdentity> identity;
  std::shared_ptr<gpu::Copy> copy;
  void AMF_STD_CALL OnSurfaceDataRelease(amf::AMFSurface *) override {
    released.store(true,std::memory_order_release);
  }
};
class Amf final : public Encoder {
  ness_encoder_config_v2 config_;
  PacketCollector collector_;
  gpu::Input input_;
  // The runtime is destroyed after every AMF interface and surface observer.
  Runtime runtime_;
  std::array<Slot,4> slots_;
  amf::AMFDataPtr output_;
  std::deque<Slot *> ordered_;
  Slot *pending_{};
  std::optional<Codec> codec_;
  bool draining_{},eos_sent_{},drained_{},discarded_{},failed_{};

  void usable() const {
    input_.alive();
    if(!runtime_.encoder || discarded_ || failed_)
      fail(NESS_V2_E_STATE,2,"Direct AMD AMF encoder is closed, failed or discarded");
  }
  template<class T> void set(const wchar_t *name,const T &value) {
    amf_checked(runtime_.encoder->SetProperty(name,value),"AMD AMF rejected a required stream property",1);
  }
  void capabilities() {
    amf::AMFCapsPtr caps;
    amf_checked(runtime_.encoder->GetCaps(&caps),"AMD AMF capabilities unavailable",1);
    if(!caps || caps->GetAccelerationType()!=amf::AMF_ACCEL_HARDWARE)
      fail(NESS_V2_E_UNAVAILABLE,1,"AMD AMF selected component is not a hardware encoder");
    amf::AMFIOCapsPtr input;
    amf_checked(caps->GetInputCaps(&input),"AMD AMF input capabilities unavailable",1);
    if(!input) fail(NESS_V2_E_UNAVAILABLE,1,"AMD AMF returned no input capabilities");
    amf_int32 min_width{},max_width{},min_height{},max_height{};
    input->GetWidthRange(&min_width,&max_width); input->GetHeightRange(&min_height,&max_height);
    if(min_width<0 || min_height<0 || max_width<=0 || max_height<=0 ||
       config_.width<static_cast<uint32_t>(min_width) || config_.width>static_cast<uint32_t>(max_width) ||
       config_.height<static_cast<uint32_t>(min_height) || config_.height>static_cast<uint32_t>(max_height))
      fail(NESS_V2_E_UNAVAILABLE,1,"AMD AMF hardware cannot satisfy the requested dimensions");
    const auto formats=input->GetNumOfFormats(),memories=input->GetNumOfMemoryTypes();
    if(formats<1 || formats>128 || memories<1 || memories>64)
      fail(NESS_V2_E_UNAVAILABLE,1,"AMD AMF returned an invalid capability list");
    bool nv12{},dx11{};
    for(amf_int32 index=0;index<formats;++index) {
      amf::AMF_SURFACE_FORMAT format{}; amf_bool native{};
      amf_checked(input->GetFormatAt(index,&format,&native),"AMD AMF format query failed",1);
      nv12|=native && format==amf::AMF_SURFACE_NV12;
    }
    for(amf_int32 index=0;index<memories;++index) {
      amf::AMF_MEMORY_TYPE memory{}; amf_bool native{};
      amf_checked(input->GetMemoryTypeAt(index,&memory,&native),"AMD AMF memory query failed",1);
      dx11|=native && memory==amf::AMF_MEMORY_DX11;
    }
    if(!nv12 || !dx11) fail(NESS_V2_E_UNAVAILABLE,1,"AMD AMF does not support native D3D11 NV12 input");
  }
  void initialize() {
    set(AMF_VIDEO_ENCODER_USAGE,static_cast<amf_int64>(AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY));
    set(AMF_VIDEO_ENCODER_PROFILE,static_cast<amf_int64>(AMF_VIDEO_ENCODER_PROFILE_BASELINE));
    set(AMF_VIDEO_ENCODER_QUALITY_PRESET,static_cast<amf_int64>(AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED));
    set(AMF_VIDEO_ENCODER_FRAMESIZE,AMFConstructSize(static_cast<amf_int32>(config_.width),static_cast<amf_int32>(config_.height)));
    set(AMF_VIDEO_ENCODER_FRAMERATE,AMFConstructRate(config_.fps_num,config_.fps_den));
    set(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD,static_cast<amf_int64>(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR));
    set(AMF_VIDEO_ENCODER_TARGET_BITRATE,static_cast<amf_int64>(config_.bitrate_bps));
    set(AMF_VIDEO_ENCODER_PEAK_BITRATE,static_cast<amf_int64>(config_.bitrate_bps));
    set(AMF_VIDEO_ENCODER_VBV_BUFFER_SIZE,static_cast<amf_int64>(config_.bitrate_bps));
    set(AMF_VIDEO_ENCODER_B_PIC_PATTERN,amf_int64{0});
    set(AMF_VIDEO_ENCODER_IDR_PERIOD,static_cast<amf_int64>((std::max)(uint64_t{1},uint64_t(config_.fps_num)*2/config_.fps_den)));
    set(AMF_VIDEO_ENCODER_RATE_CONTROL_SKIP_FRAME_ENABLE,false);
    set(AMF_VIDEO_ENCODER_FILLER_DATA_ENABLE,true);
    set(AMF_VIDEO_ENCODER_FULL_RANGE_COLOR,false);
    set(AMF_VIDEO_ENCODER_INPUT_COLOR_PROFILE,static_cast<amf_int64>(AMF_VIDEO_CONVERTER_COLOR_PROFILE_709));
    set(AMF_VIDEO_ENCODER_OUTPUT_COLOR_PROFILE,static_cast<amf_int64>(AMF_VIDEO_CONVERTER_COLOR_PROFILE_709));
    set(AMF_VIDEO_ENCODER_INPUT_COLOR_PRIMARIES,static_cast<amf_int64>(AMF_COLOR_PRIMARIES_BT709));
    set(AMF_VIDEO_ENCODER_OUTPUT_COLOR_PRIMARIES,static_cast<amf_int64>(AMF_COLOR_PRIMARIES_BT709));
    set(AMF_VIDEO_ENCODER_INPUT_TRANSFER_CHARACTERISTIC,static_cast<amf_int64>(AMF_COLOR_TRANSFER_CHARACTERISTIC_BT709));
    set(AMF_VIDEO_ENCODER_OUTPUT_TRANSFER_CHARACTERISTIC,static_cast<amf_int64>(AMF_COLOR_TRANSFER_CHARACTERISTIC_BT709));
    amf_checked(runtime_.encoder->Init(amf::AMF_SURFACE_NV12,static_cast<amf_int32>(config_.width),
      static_cast<amf_int32>(config_.height)),"AMD AMF initialization rejected the stream contract",1);
    // Confirm required properties instead of assuming the driver honored SetProperty.
    amf_int64 b_frames{-1},profile{-1},rate{-1}; AMFSize size{}; AMFRate fps{};
    amf_checked(runtime_.encoder->GetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN,&b_frames),"AMD AMF B-frame confirmation failed",1);
    amf_checked(runtime_.encoder->GetProperty(AMF_VIDEO_ENCODER_PROFILE,&profile),"AMD AMF profile confirmation failed",1);
    amf_checked(runtime_.encoder->GetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD,&rate),"AMD AMF rate-control confirmation failed",1);
    amf_checked(runtime_.encoder->GetProperty(AMF_VIDEO_ENCODER_FRAMESIZE,&size),"AMD AMF geometry confirmation failed",1);
    amf_checked(runtime_.encoder->GetProperty(AMF_VIDEO_ENCODER_FRAMERATE,&fps),"AMD AMF frame-rate confirmation failed",1);
    if(b_frames || profile!=config_.profile || rate!=AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR ||
       size.width!=static_cast<amf_int32>(config_.width) || size.height!=static_cast<amf_int32>(config_.height) ||
       fps.num!=config_.fps_num || fps.den!=config_.fps_den)
      fail(NESS_V2_E_UNAVAILABLE,1,"AMD AMF changed the negotiated stream contract");
    amf::AMFVariant extra;
    amf_checked(runtime_.encoder->GetProperty(AMF_VIDEO_ENCODER_EXTRADATA,&extra),"AMD AMF parameter sets unavailable",1);
    if(extra.type!=amf::AMF_VARIANT_INTERFACE || !extra.pInterface)
      fail(NESS_V2_E_ENCODER,1,"AMD AMF returned invalid parameter-set metadata");
    amf::AMFBufferPtr headers(extra.pInterface);
    if(!headers || headers->GetMemoryType()!=amf::AMF_MEMORY_HOST || !headers->GetNative() ||
       !headers->GetSize() || headers->GetSize()>16384)
      fail(NESS_V2_E_ENCODER,1,"AMD AMF returned invalid parameter-set storage");
    auto avcc=codec_private({static_cast<const uint8_t *>(headers->GetNative()),headers->GetSize()});
    if(avcc[1]!=config_.profile) fail(NESS_V2_E_ENCODER,1,"AMD AMF changed the requested AVC profile");
    auto info=fresh<ness_codec_view_v2>();
    info.width=config_.width; info.height=config_.height; info.fps_num=config_.fps_num; info.fps_den=config_.fps_den;
    info.profile=config_.profile; info.color=config_.color; codec_=Codec{info,std::move(avcc)};
    for(auto &slot:slots_) slot.texture=input_.texture(config_.width,config_.height);
  }
  void send_pending() {
    if(!pending_) return;
    auto &slot=*pending_; slot.copy->poll();
    if(!slot.copy->ready.load(std::memory_order_acquire)) return;
    if(!slot.surface) {
      slot.released.store(false,std::memory_order_release);
      amf_checked(runtime_.context->CreateSurfaceFromDX11Native(slot.texture.Get(),&slot.surface,&slot),
        "AMD AMF input texture wrapping failed");
      if(!slot.surface || slot.surface->GetFormat()!=amf::AMF_SURFACE_NV12 ||
         slot.surface->GetMemoryType()!=amf::AMF_MEMORY_DX11)
        fail(NESS_V2_E_ENCODER,2,"AMD AMF wrapped an unexpected input format");
      slot.surface->SetPts(slot.identity->pts); slot.surface->SetDuration(slot.identity->duration);
      slot.surface->SetFrameType(amf::AMF_FRAME_PROGRESSIVE);
    }
    const auto result=runtime_.encoder->SubmitInput(slot.surface);
    if(result==AMF_INPUT_FULL || result==AMF_REPEAT) return;
    if(result!=AMF_NEED_MORE_INPUT) amf_checked(result,"AMD AMF input encoding failed");
    ordered_.push_back(&slot); slot.surface.Release(); pending_=nullptr;
  }
  void collect() {
    while(collector_.can_receive_output() && !drained_) {
      if(!output_) {
        const auto status=runtime_.encoder->QueryOutput(&output_);
        if(status==AMF_EOF) {
          if(!eos_sent_ || pending_ || !ordered_.empty() || collector_.pending())
            fail(NESS_V2_E_ENCODER,3,"AMD AMF reached EOS with missing original frames");
          drained_=true; return;
        }
        if(status==AMF_REPEAT || (status==AMF_OK && !output_)) return;
        amf_checked(status,"AMD AMF output query failed");
      }
      if(!output_ || ordered_.empty()) fail(NESS_V2_E_ENCODER,3,"AMD AMF returned output without an original input");
      auto &slot=*ordered_.front(); const auto identity=*slot.identity;
      amf::AMFBufferPtr buffer(output_);
      amf_int64 type{};
      amf_checked(output_->GetProperty(AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE,&type),"AMD AMF output picture type unavailable");
      if(!buffer || buffer->GetMemoryType()!=amf::AMF_MEMORY_HOST || !buffer->GetNative() || !buffer->GetSize() ||
         buffer->GetSize()>config_.max_packet_bytes || output_->GetPts()!=identity.pts ||
         type==AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_B)
        fail(NESS_V2_E_ENCODER,3,"AMD AMF returned oversized, reordered or unidentified output");
      auto normalized=normalize({static_cast<const uint8_t *>(buffer->GetNative()),buffer->GetSize()},NESS_V2_ANNEX_B,true);
      if(normalized.size()>config_.max_packet_bytes)
        fail(NESS_V2_E_ENCODER,3,"AMD AMF normalized frame exceeds the declared packet budget");
      try {
        collector_.accept_normalized(std::move(normalized),identity.pts,identity.pts,type==AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR);
      } catch(const Failure &failure) {
        if(failure.code==NESS_V2_E_BACKPRESSURE) return;
        throw;
      }
      slot.identity.reset(); slot.copy.reset(); ordered_.pop_front(); output_.Release();
    }
  }
  void pump() {
    try {
      collect(); send_pending();
      if(draining_ && !pending_ && !eos_sent_) {
        const auto result=runtime_.encoder->Drain();
        if(result!=AMF_INPUT_FULL && result!=AMF_REPEAT) {
          amf_checked(result,"AMD AMF end-of-stream submission failed"); eos_sent_=true;
        }
      }
      collect();
    } catch(...) { failed_=true; throw; }
  }
  void close() noexcept {
    runtime_.stop(); output_.Release();
    for(auto &slot:slots_) {
      slot.surface.Release();
      if(slot.copy) slot.copy->poll();
    }
    if(runtime_.context) { (void)runtime_.context->Terminate(); runtime_.context.Release(); }
  }
public:
  Amf(ness_encoder_config_v2 config,const ness_gpu_device_v2 &target)
    :config_(config),collector_(config),input_(target,0x1002) {
    try { runtime_.open(input_.device.Get()); capabilities(); initialize(); }
    catch(...) { close(); throw; }
  }
  ~Amf() override { close(); }
  void submit(const ness_cpu_frame_v2 &) override {
    usable(); fail(NESS_V2_E_UNAVAILABLE,2,"Direct AMD AMF accepts GPU NV12 input only");
  }
  std::shared_ptr<InputCompletion> submit_gpu(const ness_gpu_frame_v2 &frame) override {
    usable();
    if(draining_) fail(NESS_V2_E_STATE,2,"Direct AMD AMF input after EOS");
    input_.validate(frame,config_); collector_.validate({frame.frame_id,frame.pts_hns,frame.duration_hns});
    pump();
    auto slot=std::find_if(slots_.begin(),slots_.end(),[](const Slot &item) {
      return !item.identity && item.released.load(std::memory_order_acquire);
    });
    if(pending_ || slot==slots_.end() || !collector_.can_receive_output())
      fail(NESS_V2_E_BACKPRESSURE,2,"Receive AMD AMF output before submitting more input");
    auto lease=input_.copy(frame,slot->texture.Get());
    slot->identity=InputIdentity{frame.frame_id,frame.pts_hns,frame.duration_hns}; slot->copy=lease;
    collector_.remember(*slot->identity); pending_=&*slot;
    pump(); return lease;
  }
  ness_gpu_info_v2 gpu_info() override {
    input_.thread(); auto result=fresh<ness_gpu_info_v2>(); result.adapter_luid=input_.luid;
    result.asynchronous=result.d3d11_aware=1;
    constexpr char name[]="AMD AMF / D3D11 native NV12 / asynchronous output";
    std::memcpy(result.implementation_utf8,name,sizeof(name)); return result;
  }
  std::shared_ptr<Packet> receive() override {
    usable(); if(auto packet=collector_.receive()) { pump(); return packet; }
    pump(); return collector_.receive();
  }
  std::optional<Codec> description() override { usable(); return codec_; }
  void drain() override { usable(); draining_=true; pump(); }
  void discard() override { usable(); close(); collector_.discard(); discarded_=true; }
  ness_encoder_info_v2 info() override {
    input_.thread(); auto result=fresh<ness_encoder_info_v2>();
    result.backend=NESS_V2_BACKEND_AMF_D3D11; result.gpu_input=1;
    result.width=config_.width; result.height=config_.height; result.fps_num=config_.fps_num; result.fps_den=config_.fps_den;
    result.profile=config_.profile; result.color=config_.color;
    constexpr char name[]="AMD AMF direct D3D11";
    std::memcpy(result.implementation_utf8,name,sizeof(name)); return result;
  }
  ness_encoder_stats_v2 stats() override {
    input_.thread(); auto result=fresh<ness_encoder_stats_v2>();
    result.submitted=collector_.submitted; result.emitted=collector_.emitted; result.pending=collector_.pending();
    result.queued_packets=collector_.queued(); result.queued_bytes=collector_.bytes(); result.discarded=collector_.discarded;
    result.draining=draining_; result.drained=drained_; result.failed=failed_; return result;
  }
};
}
std::unique_ptr<Encoder> amf_encoder(ness_encoder_config_v2 config,const ness_gpu_device_v2 &target) {
  return std::make_unique<Amf>(config,target);
}
}
