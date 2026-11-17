#include "gpu_input.hpp"
#include <nvEncodeAPI.h>

namespace ness2 {
namespace {
#ifdef NESS_NVENC_CONTRACT_TEST
// Compiled only into the deterministic driver-contract test executable.
void contract_open(NV_ENCODE_API_FUNCTION_LIST &,void *&,ID3D11Device *);
uint32_t contract_vendor();
#define NESS_NVENC_ADAPTER_VENDOR contract_vendor()
#else
#define NESS_NVENC_ADAPTER_VENDOR 0x10de
#endif
void nv_checked(NVENCSTATUS status,const char *message,uint32_t stage=2) {
  if(status==NV_ENC_SUCCESS) return;
  const bool unavailable=status==NV_ENC_ERR_NO_ENCODE_DEVICE || status==NV_ENC_ERR_UNSUPPORTED_DEVICE ||
    status==NV_ENC_ERR_INVALID_DEVICE || status==NV_ENC_ERR_INVALID_VERSION ||
    status==NV_ENC_ERR_UNSUPPORTED_PARAM || status==NV_ENC_ERR_UNIMPLEMENTED;
  fail(unavailable?NESS_V2_E_UNAVAILABLE:(status==NV_ENC_ERR_OUT_OF_MEMORY?NESS_V2_E_ALLOC:NESS_V2_E_ENCODER),
    stage,message,static_cast<int32_t>(status));
}
struct Runtime {
  HMODULE module{};
  NV_ENCODE_API_FUNCTION_LIST api{};
  void *session{};
  void close() noexcept {
    if(session && api.nvEncDestroyEncoder) (void)api.nvEncDestroyEncoder(session);
    session=nullptr;
  }
  ~Runtime() { close(); if(module) FreeLibrary(module); }
  void open(ID3D11Device *device) {
#ifdef NESS_NVENC_CONTRACT_TEST
    contract_open(api,session,device); return;
#else
    // The driver is optional. Never search the working directory/PATH for a DLL.
    module=LoadLibraryExW(L"nvEncodeAPI64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module) fail(NESS_V2_E_UNAVAILABLE,1,"NVENC driver runtime is not installed",static_cast<int32_t>(GetLastError()));
    using Maximum=NVENCSTATUS(NVENCAPI *)(uint32_t *);
    using Create=NVENCSTATUS(NVENCAPI *)(NV_ENCODE_API_FUNCTION_LIST *);
    const auto maximum=reinterpret_cast<Maximum>(GetProcAddress(module,"NvEncodeAPIGetMaxSupportedVersion"));
    const auto create=reinterpret_cast<Create>(GetProcAddress(module,"NvEncodeAPICreateInstance"));
    if(!maximum || !create) fail(NESS_V2_E_UNAVAILABLE,1,"NVENC driver entry points are unavailable");
    uint32_t version{};
    nv_checked(maximum(&version),"NVENC driver version query failed",1);
    constexpr uint32_t required=(NVENCAPI_MAJOR_VERSION<<4)|NVENCAPI_MINOR_VERSION;
    if(version<required) fail(NESS_V2_E_UNAVAILABLE,1,"NVENC driver API is older than the compiled SDK",static_cast<int32_t>(version));
    api.version=NV_ENCODE_API_FUNCTION_LIST_VER;
    nv_checked(create(&api),"NVENC function table creation failed",1);
    if(!api.nvEncOpenEncodeSessionEx || !api.nvEncGetEncodeGUIDCount || !api.nvEncGetEncodeGUIDs ||
       !api.nvEncGetInputFormatCount || !api.nvEncGetInputFormats || !api.nvEncGetEncodeCaps ||
       !api.nvEncGetEncodeProfileGUIDCount || !api.nvEncGetEncodeProfileGUIDs ||
       !api.nvEncGetEncodePresetConfigEx || !api.nvEncInitializeEncoder || !api.nvEncGetSequenceParams ||
       !api.nvEncRegisterResource || !api.nvEncUnregisterResource || !api.nvEncMapInputResource ||
       !api.nvEncUnmapInputResource || !api.nvEncCreateBitstreamBuffer || !api.nvEncDestroyBitstreamBuffer ||
       !api.nvEncRegisterAsyncEvent || !api.nvEncUnregisterAsyncEvent || !api.nvEncEncodePicture ||
       !api.nvEncLockBitstream || !api.nvEncUnlockBitstream || !api.nvEncDestroyEncoder)
      fail(NESS_V2_E_UNAVAILABLE,1,"NVENC driver returned an incomplete function table");
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{};
    open.version=NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    open.apiVersion=NVENCAPI_VERSION; open.device=device; open.deviceType=NV_ENC_DEVICE_TYPE_DIRECTX;
    nv_checked(api.nvEncOpenEncodeSessionEx(&open,&session),"NVENC hardware session is unavailable",1);
    if(!session) fail(NESS_V2_E_ENCODER,1,"NVENC returned an empty session");
#endif
  }
  int capability(NV_ENC_CAPS capability) {
    NV_ENC_CAPS_PARAM query{}; query.version=NV_ENC_CAPS_PARAM_VER; query.capsToQuery=capability;
    int value{};
    nv_checked(api.nvEncGetEncodeCaps(session,NV_ENC_CODEC_H264_GUID,&query,&value),"NVENC capability query failed",1);
    return value;
  }
};
struct Slot {
  gpu::ComPtr<ID3D11Texture2D> texture;
  NV_ENC_REGISTERED_PTR registered{};
  NV_ENC_INPUT_PTR mapped{};
  NV_ENC_OUTPUT_PTR output{};
  HANDLE event{};
  bool event_registered{};
  std::optional<InputIdentity> identity;
  std::shared_ptr<gpu::Copy> copy;
  ~Slot() { if(event) CloseHandle(event); }
};
class Nvenc final : public Encoder {
  ness_encoder_config_v2 config_;
  PacketCollector collector_;
  gpu::Input input_;
  std::array<Slot,4> slots_;
  HANDLE eos_event_{};
  bool eos_registered_{};
  // Destroy the driver before the textures/events even if construction throws.
  Runtime runtime_;
  std::deque<Slot *> ordered_;
  Slot *pending_{};
  std::optional<Codec> codec_;
  uint32_t ordinal_{};
  bool draining_{},eos_sent_{},drained_{},discarded_{},failed_{};

  void usable() const {
    input_.alive();
    if(!runtime_.session || discarded_ || failed_)
      fail(NESS_V2_E_STATE,2,"Direct NVENC encoder is closed, failed or discarded");
  }
  void capabilities() {
    auto &r=runtime_;
    uint32_t count{},actual{};
    nv_checked(r.api.nvEncGetEncodeGUIDCount(r.session,&count),"NVENC codec count unavailable",1);
    if(!count || count>64) fail(NESS_V2_E_UNAVAILABLE,1,"NVENC has no bounded codec list");
    std::vector<GUID> supported_codecs(count);
    nv_checked(r.api.nvEncGetEncodeGUIDs(r.session,supported_codecs.data(),count,&actual),"NVENC codec list unavailable",1);
    if(actual>count || std::none_of(supported_codecs.begin(),supported_codecs.begin()+actual,[](const GUID &id){return id==NV_ENC_CODEC_H264_GUID;}))
      fail(NESS_V2_E_UNAVAILABLE,1,"Selected NVENC hardware does not support H.264");
    nv_checked(r.api.nvEncGetEncodeProfileGUIDCount(r.session,NV_ENC_CODEC_H264_GUID,&count),"NVENC profile count unavailable",1);
    if(!count || count>64) fail(NESS_V2_E_UNAVAILABLE,1,"NVENC has no bounded H.264 profile list");
    std::vector<GUID> profiles(count);
    nv_checked(r.api.nvEncGetEncodeProfileGUIDs(r.session,NV_ENC_CODEC_H264_GUID,profiles.data(),count,&actual),"NVENC profile list unavailable",1);
    if(actual>count || std::none_of(profiles.begin(),profiles.begin()+actual,[](const GUID &id){return id==NV_ENC_H264_PROFILE_BASELINE_GUID;}))
      fail(NESS_V2_E_UNAVAILABLE,1,"Selected NVENC hardware does not support H.264 Baseline");
    nv_checked(r.api.nvEncGetInputFormatCount(r.session,NV_ENC_CODEC_H264_GUID,&count),"NVENC input format count unavailable",1);
    if(!count || count>128) fail(NESS_V2_E_UNAVAILABLE,1,"NVENC has no bounded input format list");
    std::vector<NV_ENC_BUFFER_FORMAT> formats(count);
    nv_checked(r.api.nvEncGetInputFormats(r.session,NV_ENC_CODEC_H264_GUID,formats.data(),count,&actual),"NVENC input formats unavailable",1);
    if(actual>count || std::find(formats.begin(),formats.begin()+actual,NV_ENC_BUFFER_FORMAT_NV12)==formats.begin()+actual)
      fail(NESS_V2_E_UNAVAILABLE,1,"Selected NVENC hardware does not support NV12 input");
    const auto width=r.capability(NV_ENC_CAPS_WIDTH_MAX),height=r.capability(NV_ENC_CAPS_HEIGHT_MAX);
    if(width<=0 || height<=0 || config_.width>static_cast<uint32_t>(width) || config_.height>static_cast<uint32_t>(height) ||
       !r.capability(NV_ENC_CAPS_ASYNC_ENCODE_SUPPORT) ||
       !(r.capability(NV_ENC_CAPS_SUPPORTED_RATECONTROL_MODES)&NV_ENC_PARAMS_RC_CBR))
      fail(NESS_V2_E_UNAVAILABLE,1,"NVENC hardware cannot satisfy dimensions, asynchronous operation or CBR");
  }
  void initialize() {
    auto &r=runtime_;
    NV_ENC_PRESET_CONFIG preset{};
    preset.version=NV_ENC_PRESET_CONFIG_VER; preset.presetCfg.version=NV_ENC_CONFIG_VER;
    nv_checked(r.api.nvEncGetEncodePresetConfigEx(r.session,NV_ENC_CODEC_H264_GUID,NV_ENC_PRESET_P4_GUID,
      NV_ENC_TUNING_INFO_LOW_LATENCY,&preset),"NVENC P4 low-latency preset unavailable",1);
    auto configuration=preset.presetCfg;
    configuration.profileGUID=NV_ENC_H264_PROFILE_BASELINE_GUID;
    configuration.gopLength=(std::max)(1u,static_cast<uint32_t>(uint64_t(config_.fps_num)*2/config_.fps_den));
    configuration.frameIntervalP=1; configuration.frameFieldMode=NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
    auto &rc=configuration.rcParams;
    rc.rateControlMode=NV_ENC_PARAMS_RC_CBR; rc.averageBitRate=rc.maxBitRate=config_.bitrate_bps;
    rc.vbvBufferSize=rc.vbvInitialDelay=config_.bitrate_bps;
    rc.enableLookahead=0; rc.lookaheadDepth=0; rc.enableAQ=0; rc.enableTemporalAQ=0;
    rc.multiPass=NV_ENC_MULTI_PASS_DISABLED;
    auto &h264=configuration.encodeCodecConfig.h264Config;
    h264.idrPeriod=configuration.gopLength; h264.repeatSPSPPS=1; h264.disableSPSPPS=0;
    h264.maxNumRefFrames=1; h264.chromaFormatIDC=1;
    h264.entropyCodingMode=NV_ENC_H264_ENTROPY_CODING_MODE_CAVLC;
    h264.enableFillerDataInsertion=1;
    auto &vui=h264.h264VUIParameters;
    vui.videoSignalTypePresentFlag=1; vui.videoFormat=NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED; vui.videoFullRangeFlag=0;
    vui.colourDescriptionPresentFlag=1; vui.colourPrimaries=NV_ENC_VUI_COLOR_PRIMARIES_BT709;
    vui.transferCharacteristics=NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709; vui.colourMatrix=NV_ENC_VUI_MATRIX_COEFFS_BT709;
    NV_ENC_INITIALIZE_PARAMS init{};
    init.version=NV_ENC_INITIALIZE_PARAMS_VER; init.encodeGUID=NV_ENC_CODEC_H264_GUID;
    init.presetGUID=NV_ENC_PRESET_P4_GUID; init.tuningInfo=NV_ENC_TUNING_INFO_LOW_LATENCY;
    init.encodeWidth=init.darWidth=init.maxEncodeWidth=config_.width;
    init.encodeHeight=init.darHeight=init.maxEncodeHeight=config_.height;
    init.frameRateNum=config_.fps_num; init.frameRateDen=config_.fps_den;
    init.enableEncodeAsync=1; init.enablePTD=1; init.encodeConfig=&configuration;
    init.bufferFormat=NV_ENC_BUFFER_FORMAT_NV12;
    nv_checked(r.api.nvEncInitializeEncoder(r.session,&init),"NVENC initialization rejected the stream contract",1);
    for(auto &slot:slots_) {
      slot.texture=input_.texture(config_.width,config_.height);
      NV_ENC_REGISTER_RESOURCE resource{}; resource.version=NV_ENC_REGISTER_RESOURCE_VER;
      resource.resourceType=NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX; resource.resourceToRegister=slot.texture.Get();
      resource.width=config_.width; resource.height=config_.height;
      resource.bufferFormat=NV_ENC_BUFFER_FORMAT_NV12; resource.bufferUsage=NV_ENC_INPUT_IMAGE;
      nv_checked(r.api.nvEncRegisterResource(r.session,&resource),"NVENC texture registration failed",1);
      slot.registered=resource.registeredResource;
      NV_ENC_CREATE_BITSTREAM_BUFFER bits{}; bits.version=NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
      nv_checked(r.api.nvEncCreateBitstreamBuffer(r.session,&bits),"NVENC bitstream allocation failed",1);
      slot.output=bits.bitstreamBuffer;
      slot.event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
      if(!slot.event) fail(NESS_V2_E_ALLOC,1,"NVENC completion event creation failed",static_cast<int32_t>(GetLastError()));
      NV_ENC_EVENT_PARAMS event{}; event.version=NV_ENC_EVENT_PARAMS_VER; event.completionEvent=slot.event;
      nv_checked(r.api.nvEncRegisterAsyncEvent(r.session,&event),"NVENC event registration failed",1);
      slot.event_registered=true;
    }
    eos_event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!eos_event_) fail(NESS_V2_E_ALLOC,1,"NVENC EOS event creation failed",static_cast<int32_t>(GetLastError()));
    NV_ENC_EVENT_PARAMS event{}; event.version=NV_ENC_EVENT_PARAMS_VER; event.completionEvent=eos_event_;
    nv_checked(r.api.nvEncRegisterAsyncEvent(r.session,&event),"NVENC EOS event registration failed",1);
    eos_registered_=true;
    std::array<uint8_t,16384> headers{}; uint32_t bytes{};
    NV_ENC_SEQUENCE_PARAM_PAYLOAD sequence{}; sequence.version=NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;
    sequence.spsppsBuffer=headers.data(); sequence.inBufferSize=static_cast<uint32_t>(headers.size());
    sequence.outSPSPPSPayloadSize=&bytes;
    nv_checked(r.api.nvEncGetSequenceParams(r.session,&sequence),"NVENC parameter sets unavailable",1);
    if(!bytes || bytes>headers.size()) fail(NESS_V2_E_ENCODER,1,"NVENC parameter-set size is invalid");
    auto avcc=codec_private({headers.data(),bytes});
    if(avcc[1]!=config_.profile) fail(NESS_V2_E_ENCODER,1,"NVENC changed the requested AVC profile");
    auto info=fresh<ness_codec_view_v2>();
    info.width=config_.width; info.height=config_.height; info.fps_num=config_.fps_num; info.fps_den=config_.fps_den;
    info.profile=config_.profile; info.color=config_.color; codec_=Codec{info,std::move(avcc)};
  }
  void send_pending() {
    if(!pending_) return;
    auto &slot=*pending_; slot.copy->poll();
    if(!slot.copy->ready.load(std::memory_order_acquire)) return;
    auto &r=runtime_;
    if(!slot.mapped) {
      NV_ENC_MAP_INPUT_RESOURCE map{}; map.version=NV_ENC_MAP_INPUT_RESOURCE_VER;
      map.registeredResource=slot.registered;
      nv_checked(r.api.nvEncMapInputResource(r.session,&map),"NVENC input mapping failed");
      slot.mapped=map.mappedResource;
      if(!slot.mapped || map.mappedBufferFmt!=NV_ENC_BUFFER_FORMAT_NV12)
        fail(NESS_V2_E_ENCODER,2,"NVENC mapped an unexpected input format");
    }
    NV_ENC_PIC_PARAMS pic{}; pic.version=NV_ENC_PIC_PARAMS_VER;
    pic.inputBuffer=slot.mapped; pic.bufferFmt=NV_ENC_BUFFER_FORMAT_NV12;
    pic.inputWidth=config_.width; pic.inputHeight=config_.height; pic.inputPitch=config_.width;
    pic.outputBitstream=slot.output; pic.completionEvent=slot.event;
    pic.pictureStruct=NV_ENC_PIC_STRUCT_FRAME; pic.frameIdx=ordinal_;
    pic.inputTimeStamp=static_cast<uint64_t>(slot.identity->pts);
    pic.inputDuration=static_cast<uint64_t>(slot.identity->duration);
    const auto result=r.api.nvEncEncodePicture(r.session,&pic);
    if(result==NV_ENC_ERR_ENCODER_BUSY) return;
    if(result!=NV_ENC_ERR_NEED_MORE_INPUT) nv_checked(result,"NVENC input encoding failed");
    ++ordinal_; ordered_.push_back(&slot); pending_=nullptr;
  }
  void collect() {
    auto &r=runtime_;
    while(!ordered_.empty() && collector_.can_receive_output()) {
      auto &slot=*ordered_.front();
      const auto wait=WaitForSingleObject(slot.event,0);
      if(wait==WAIT_TIMEOUT) return;
      if(wait!=WAIT_OBJECT_0) fail(NESS_V2_E_ENCODER,2,"NVENC completion event failed",static_cast<int32_t>(GetLastError()));
      NV_ENC_LOCK_BITSTREAM bits{}; bits.version=NV_ENC_LOCK_BITSTREAM_VER;
      bits.outputBitstream=slot.output; bits.doNotWait=1;
      const auto locked=r.api.nvEncLockBitstream(r.session,&bits);
      if(locked==NV_ENC_ERR_LOCK_BUSY) return;
      nv_checked(locked,"NVENC bitstream lock failed");
      struct Unlock {
        Runtime &runtime; void *buffer;
        ~Unlock() { if(buffer) (void)runtime.api.nvEncUnlockBitstream(runtime.session,buffer); }
      } unlock{r,slot.output};
      const auto identity=*slot.identity;
      if(!bits.bitstreamBufferPtr || !bits.bitstreamSizeInBytes || bits.bitstreamSizeInBytes>config_.max_packet_bytes ||
         bits.outputTimeStamp!=static_cast<uint64_t>(identity.pts) || bits.pictureType==NV_ENC_PIC_TYPE_B)
        fail(NESS_V2_E_ENCODER,3,"NVENC returned oversized, reordered or unidentified output");
      auto normalized=normalize({static_cast<const uint8_t *>(bits.bitstreamBufferPtr),bits.bitstreamSizeInBytes},NESS_V2_ANNEX_B,true);
      if(normalized.size()>config_.max_packet_bytes)
        fail(NESS_V2_E_ENCODER,3,"NVENC normalized frame exceeds the declared packet budget");
      try {
        collector_.accept_normalized(std::move(normalized),identity.pts,identity.pts,bits.pictureType==NV_ENC_PIC_TYPE_IDR);
      } catch(const Failure &failure) {
        if(failure.code==NESS_V2_E_BACKPRESSURE) return;
        throw;
      }
      nv_checked(r.api.nvEncUnlockBitstream(r.session,slot.output),"NVENC bitstream unlock failed"); unlock.buffer=nullptr;
      nv_checked(r.api.nvEncUnmapInputResource(r.session,slot.mapped),"NVENC input unmapping failed");
      slot.mapped=nullptr; slot.identity.reset(); slot.copy.reset();
      if(!ResetEvent(slot.event)) fail(NESS_V2_E_ENCODER,2,"NVENC completion reset failed",static_cast<int32_t>(GetLastError()));
      ordered_.pop_front();
    }
  }
  void pump() {
    try {
      collect(); send_pending();
      if(draining_ && !pending_ && !eos_sent_) {
        NV_ENC_PIC_PARAMS eos{}; eos.version=NV_ENC_PIC_PARAMS_VER;
        eos.encodePicFlags=NV_ENC_PIC_FLAG_EOS; eos.completionEvent=eos_event_;
        const auto result=runtime_.api.nvEncEncodePicture(runtime_.session,&eos);
        if(result!=NV_ENC_ERR_ENCODER_BUSY) {
          nv_checked(result,"NVENC end-of-stream submission failed"); eos_sent_=true;
        }
      }
      collect();
      if(eos_sent_ && ordered_.empty()) {
        const auto wait=WaitForSingleObject(eos_event_,0);
        if(wait==WAIT_OBJECT_0) {
          if(collector_.pending()) fail(NESS_V2_E_ENCODER,3,"NVENC reached EOS with missing frames");
          drained_=true;
        } else if(wait!=WAIT_TIMEOUT) fail(NESS_V2_E_ENCODER,2,"NVENC EOS event failed",static_cast<int32_t>(GetLastError()));
      }
    } catch(...) { failed_=true; throw; }
  }
  void close() noexcept {
    // Release is not a successful Drain, but NVENC still requires a flush before
    // unregistering resources. Discard output, retain caller copy leases, and
    // make a bounded best effort if the driver/device has already failed.
    auto &r=runtime_;
    if(r.session) {
      if(ordinal_ && !drained_ && eos_event_ && eos_registered_) {
        const auto deadline=GetTickCount64()+5000;
        while(!eos_sent_ && GetTickCount64()<deadline) {
          NV_ENC_PIC_PARAMS eos{}; eos.version=NV_ENC_PIC_PARAMS_VER;
          eos.encodePicFlags=NV_ENC_PIC_FLAG_EOS; eos.completionEvent=eos_event_;
          const auto result=r.api.nvEncEncodePicture(r.session,&eos);
          if(result==NV_ENC_SUCCESS) eos_sent_=true;
          else if(result!=NV_ENC_ERR_ENCODER_BUSY || FAILED(input_.device->GetDeviceRemovedReason())) break;
          else Sleep(1);
        }
        const auto now=GetTickCount64();
        if(eos_sent_ && now<deadline) (void)WaitForSingleObject(eos_event_,static_cast<DWORD>(deadline-now));
        for(auto *slot:ordered_) {
          if(WaitForSingleObject(slot->event,0)==WAIT_OBJECT_0) {
            NV_ENC_LOCK_BITSTREAM bits{}; bits.version=NV_ENC_LOCK_BITSTREAM_VER;
            bits.outputBitstream=slot->output; bits.doNotWait=1;
            if(r.api.nvEncLockBitstream(r.session,&bits)==NV_ENC_SUCCESS)
              (void)r.api.nvEncUnlockBitstream(r.session,slot->output);
          }
        }
      }
      for(auto &slot:slots_) {
        if(slot.mapped) (void)r.api.nvEncUnmapInputResource(r.session,slot.mapped);
        if(slot.registered) (void)r.api.nvEncUnregisterResource(r.session,slot.registered);
        if(slot.output) (void)r.api.nvEncDestroyBitstreamBuffer(r.session,slot.output);
        if(slot.event_registered) {
          NV_ENC_EVENT_PARAMS event{}; event.version=NV_ENC_EVENT_PARAMS_VER; event.completionEvent=slot.event;
          (void)r.api.nvEncUnregisterAsyncEvent(r.session,&event);
        }
      }
      if(eos_registered_) {
        NV_ENC_EVENT_PARAMS event{}; event.version=NV_ENC_EVENT_PARAMS_VER; event.completionEvent=eos_event_;
        (void)r.api.nvEncUnregisterAsyncEvent(r.session,&event);
      }
    }
    r.close();
    if(eos_event_) CloseHandle(eos_event_);
    eos_event_=nullptr;
    for(auto &slot:slots_) if(slot.copy) slot.copy->poll();
  }
public:
  Nvenc(ness_encoder_config_v2 config,const ness_gpu_device_v2 &target)
    :config_(config),collector_(config),input_(target,NESS_NVENC_ADAPTER_VENDOR) {
    try { runtime_.open(input_.device.Get()); capabilities(); initialize(); }
    catch(...) { close(); throw; }
  }
  ~Nvenc() override { close(); }
  void submit(const ness_cpu_frame_v2 &) override {
    usable(); fail(NESS_V2_E_UNAVAILABLE,2,"Direct NVENC accepts GPU NV12 input only");
  }
  std::shared_ptr<InputCompletion> submit_gpu(const ness_gpu_frame_v2 &frame) override {
    usable();
    if(draining_) fail(NESS_V2_E_STATE,2,"Direct NVENC input after EOS");
    input_.validate(frame,config_); collector_.validate({frame.frame_id,frame.pts_hns,frame.duration_hns});
    pump();
    auto slot=std::find_if(slots_.begin(),slots_.end(),[](const Slot &item){return !item.identity;});
    if(pending_ || slot==slots_.end() || !collector_.can_receive_output())
      fail(NESS_V2_E_BACKPRESSURE,2,"Receive NVENC output before submitting more input");
    auto lease=input_.copy(frame,slot->texture.Get());
    slot->identity=InputIdentity{frame.frame_id,frame.pts_hns,frame.duration_hns}; slot->copy=lease;
    collector_.remember(*slot->identity); pending_=&*slot;
    pump(); return lease;
  }
  ness_gpu_info_v2 gpu_info() override {
    input_.thread(); auto result=fresh<ness_gpu_info_v2>(); result.adapter_luid=input_.luid;
    result.asynchronous=result.d3d11_aware=1;
    constexpr char name[]="NVIDIA NVENC / D3D11 native NV12 / asynchronous events";
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
    result.backend=NESS_V2_BACKEND_NVENC_D3D11; result.gpu_input=1;
    result.width=config_.width; result.height=config_.height; result.fps_num=config_.fps_num; result.fps_den=config_.fps_den;
    result.profile=config_.profile; result.color=config_.color;
    constexpr char name[]="NVIDIA NVENC direct D3D11";
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
std::unique_ptr<Encoder> nvenc_encoder(ness_encoder_config_v2 config,const ness_gpu_device_v2 &target) {
  return std::make_unique<Nvenc>(config,target);
}
}
