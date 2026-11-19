#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "core.hpp"
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#pragma warning(push)
#pragma warning(disable : 4201)
#include <vpl/mfxdispatcher.h>
#include <vpl/mfxvideo.h>
#include <vpl/mfxmemory.h>
#pragma warning(pop)
#include <thread>
#include <mutex>

namespace ness2 {
namespace {
using Microsoft::WRL::ComPtr;
void checked_hr(HRESULT result,const char *message) {
  if (FAILED(result)) fail(NESS_V2_E_ENCODER,2,message,result);
}
void checked_mfx(mfxStatus result,const char *message,uint32_t stage=NESS_V2_STAGE_ENCODER) {
  if(result>=0) return;
  // Unsupported capabilities discovered before Start may select another route.
  // A device failure or an unsupported operation after Start must remain fatal.
  const bool unavailable=stage==NESS_V2_STAGE_CONFIG &&
    (result==MFX_ERR_UNSUPPORTED || result==MFX_ERR_NOT_FOUND || result==MFX_ERR_NOT_IMPLEMENTED);
  fail(unavailable?NESS_V2_E_UNAVAILABLE:result==MFX_ERR_MEMORY_ALLOC?NESS_V2_E_ALLOC:NESS_V2_E_ENCODER,
    stage,message,result);
}
struct Loader {
  mfxLoader value=MFXLoad();
  ~Loader() { if(value) MFXUnload(value); }
  void filter(const char *name,mfxU32 data) {
    if(!value) fail(NESS_V2_E_UNAVAILABLE,1,"oneVPL dispatcher unavailable");
    const auto config=MFXCreateConfig(value);
    if(!config) fail(NESS_V2_E_ALLOC,1,"oneVPL filter allocation failed");
    mfxVariant variant{}; variant.Version.Version=MFX_VARIANT_VERSION;
    variant.Type=MFX_VARIANT_TYPE_U32; variant.Data.U32=data;
    checked_mfx(MFXSetConfigFilterProperty(config,reinterpret_cast<const mfxU8 *>(name),variant),
      "oneVPL hardware filter rejected",NESS_V2_STAGE_CONFIG);
  }
};
struct Driver {
  mfxSession value{};
  bool encoder_open{};
  void close() noexcept {
    if(value && encoder_open) (void)MFXVideoENCODE_Close(value);
    if(value) (void)MFXClose(value);
    value=nullptr; encoder_open=false;
  }
  ~Driver() { close(); }
};
struct Surface {
  mfxFrameSurface1 *value{};
  void reset() noexcept { if(value) value->FrameInterface->Release(value); value=nullptr; }
  ~Surface() { reset(); }
};
// The completion handle remains useful after encoder destruction. Querying a
// timeline fence does not touch the immediate context or require its owner.
struct CopyCompletion final : InputCompletion {
  std::mutex mutex;
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11Fence> fence;
  uint64_t signal{};
  void poll() noexcept override {
    std::lock_guard lock(mutex);
    if(ready.load(std::memory_order_relaxed) || !fence) return;
    const auto completed=fence->GetCompletedValue();
    if(completed==UINT64_MAX) {
      if(SUCCEEDED(device->GetDeviceRemovedReason())) return;
    } else if(completed<signal) return;
    texture.Reset(); device.Reset(); fence.Reset();
    ready.store(true,std::memory_order_release);
  }
};
class Qsv final : public Encoder {
  struct TaggedInput {
    uint64_t tag{};
    InputIdentity identity;
    std::shared_ptr<CopyCompletion> copy;
  };
  struct Job {
    Bytes storage;
    mfxBitstream bits{};
    mfxSyncPoint sync{};
    bool completed{};
    explicit Job(size_t bytes):storage(bytes+31) {
      const auto address=reinterpret_cast<uintptr_t>(storage.data());
      bits.Data=reinterpret_cast<mfxU8 *>((address+31)&~uintptr_t{31});
      bits.MaxLength=static_cast<mfxU32>(bytes);
    }
    void reset() {
      auto *data=bits.Data; const auto bytes=bits.MaxLength;
      bits={}; bits.Data=data; bits.MaxLength=bytes;
      bits.TimeStamp=static_cast<mfxU64>(MFX_TIMESTAMP_UNKNOWN); bits.DecodeTimeStamp=MFX_TIMESTAMP_UNKNOWN;
      sync=nullptr; completed=false;
    }
  };
  ness_encoder_config_v2 config_;
  PacketCollector collector_;
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<ID3D11DeviceContext4> timeline_;
  ComPtr<ID3D11Fence> fence_;
  Loader loader_;
  Driver driver_;
  std::array<std::unique_ptr<Job>,2> jobs_;
  std::deque<Job *> submitted_;
  Surface pending_surface_;
  Job *pending_job_{};
  std::optional<size_t> pending_input_;
  std::array<std::optional<TaggedInput>,64> identities_{};
  std::optional<Codec> codec_;
  ness_gpu_info_v2 gpu_=fresh<ness_gpu_info_v2>();
  const std::thread::id owner_=std::this_thread::get_id();
  uint64_t next_signal_{},ordinal_{};
  bool draining_{},driver_drained_{},drained_{},discarded_{},failed_{};
  void thread() const {
    if(std::this_thread::get_id()!=owner_)
      fail(NESS_V2_E_THREAD,2,"Direct QSV encoder requires its creating thread");
  }
  void usable() const {
    thread();
    if(!driver_.value || discarded_ || failed_)
      fail(NESS_V2_E_STATE,2,"Direct QSV encoder is closed, failed or discarded");
    checked_hr(device_->GetDeviceRemovedReason(),"Direct QSV device was removed");
  }
  Job *available_job() const {
    for(const auto &job:jobs_)
      if(job.get()!=pending_job_ && !job->sync) return job.get();
    return nullptr;
  }
  void bind_device(const ness_gpu_device_v2 &target) {
    device_=static_cast<ID3D11Device *>(target.device);
    ComPtr<IDXGIDevice> dxgi;
    checked_hr(device_.As(&dxgi),"QSV requires a DXGI D3D11 device");
    ComPtr<IDXGIAdapter> adapter;
    checked_hr(dxgi->GetAdapter(&adapter),"QSV adapter unavailable");
    DXGI_ADAPTER_DESC description{};
    checked_hr(adapter->GetDesc(&description),"QSV adapter identity unavailable");
    const auto actual=(uint64_t(static_cast<uint32_t>(description.AdapterLuid.HighPart))<<32)|
      description.AdapterLuid.LowPart;
    if(actual!=target.adapter_luid || description.VendorId!=0x8086)
      fail(NESS_V2_E_UNAVAILABLE,1,"Direct QSV requires the selected Intel adapter, without fallback");
    gpu_.adapter_luid=actual; gpu_.asynchronous=1; gpu_.d3d11_aware=1;
    device_->GetImmediateContext(&context_);
    ComPtr<ID3D11Multithread> protection;
    checked_hr(context_.As(&protection),"QSV context protection unavailable");
    protection->SetMultithreadProtected(TRUE);
    ComPtr<ID3D11Device5> version5;
    checked_hr(device_.As(&version5),"Direct QSV requires D3D11 timeline fences");
    checked_hr(context_.As(&timeline_),"Direct QSV timeline context unavailable");
    checked_hr(version5->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence_)),
      "Direct QSV input fence creation failed");
    loader_.filter("mfxImplDescription.Impl",MFX_IMPL_TYPE_HARDWARE);
    loader_.filter("mfxImplDescription.AccelerationMode",MFX_ACCEL_MODE_VIA_D3D11);
    loader_.filter("mfxImplDescription.VendorID",0x8086);
    // Internal, reference-counted encode surfaces are a 2.0 API. The dispatcher
    // header release (2.17) is deliberately not a minimum driver requirement.
    loader_.filter("mfxImplDescription.ApiVersion.Version",2u<<16);
    loader_.filter("mfxImplDescription.mfxEncoderDescription.encoder.CodecID",MFX_CODEC_AVC);
    for(mfxU32 index=0;index<64;++index) {
      mfxHDL handle{};
      const auto status=MFXEnumImplementations(loader_.value,index,MFX_IMPLCAPS_DEVICE_ID_EXTENDED,&handle);
      if(status==MFX_ERR_NOT_FOUND) break;
      if(status<0 || !handle) continue;
      const auto *description_ext=static_cast<const mfxExtendedDeviceId *>(handle);
      uint64_t implementation_luid{};
      std::memcpy(&implementation_luid,description_ext->DeviceLUID,sizeof(implementation_luid));
      const bool matches=description_ext->LUIDValid && implementation_luid==actual &&
        description_ext->VendorID==0x8086;
      MFXDispReleaseImplDescription(loader_.value,handle);
      if(!matches) continue;
      checked_mfx(MFXCreateSession(loader_.value,index,&driver_.value),"Direct QSV session creation failed",NESS_V2_STAGE_CONFIG);
      if(driver_.value) break;
    }
    if(!driver_.value) fail(NESS_V2_E_UNAVAILABLE,1,"No direct QSV runtime matches the selected adapter LUID");
    checked_mfx(MFXVideoCORE_SetHandle(driver_.value,MFX_HANDLE_D3D11_DEVICE,device_.Get()),
      "Direct QSV rejected the caller-owned D3D11 device");
    mfxHDL returned{};
    checked_mfx(MFXVideoCORE_GetHandle(driver_.value,MFX_HANDLE_D3D11_DEVICE,&returned),
      "Direct QSV device confirmation failed");
    if(returned!=device_.Get()) fail(NESS_V2_E_ENCODER,1,"Direct QSV substituted the caller device");
    constexpr char name[]="Intel Quick Sync / oneVPL / D3D11 native NV12";
    std::memcpy(gpu_.implementation_utf8,name,sizeof(name));
  }
  void configure_driver() {
    mfxVideoParam parameters{};
    // The driver's input latency must fit the caller's accepted-frame budget.
    // Otherwise a depth-two driver can wait for the next input while our
    // one-frame collector correctly refuses it: neither side can progress.
    parameters.AsyncDepth=static_cast<mfxU16>((std::min)(2u,config_.max_pending_frames));
    parameters.IOPattern=MFX_IOPATTERN_IN_VIDEO_MEMORY;
    auto &codec=parameters.mfx;
    codec.CodecId=MFX_CODEC_AVC; codec.CodecProfile=MFX_PROFILE_AVC_BASELINE;
    codec.TargetUsage=MFX_TARGETUSAGE_BALANCED; codec.RateControlMethod=MFX_RATECONTROL_CBR;
    codec.TargetKbps=static_cast<mfxU16>((uint64_t(config_.bitrate_bps)+500)/1000);
    // mfxU16 bitrate fields use a shared scale above 65535 Kbps.
    if(config_.bitrate_bps>65535000) {
      codec.BRCParamMultiplier=2;
      codec.TargetKbps=static_cast<mfxU16>((uint64_t(config_.bitrate_bps)+1000)/2000);
    }
    codec.GopPicSize=static_cast<mfxU16>((std::max)(1u,2*config_.fps_num/config_.fps_den));
    codec.GopRefDist=1; codec.NumRefFrame=1;
    auto &frame=codec.FrameInfo;
    frame.FourCC=MFX_FOURCC_NV12; frame.ChromaFormat=MFX_CHROMAFORMAT_YUV420;
    frame.PicStruct=MFX_PICSTRUCT_PROGRESSIVE;
    frame.Width=static_cast<mfxU16>((config_.width+15)&~15u);
    frame.Height=static_cast<mfxU16>((config_.height+15)&~15u);
    frame.CropW=static_cast<mfxU16>(config_.width); frame.CropH=static_cast<mfxU16>(config_.height);
    frame.FrameRateExtN=config_.fps_num; frame.FrameRateExtD=config_.fps_den;
    mfxExtVideoSignalInfo color{};
    color.Header={MFX_EXTBUFF_VIDEO_SIGNAL_INFO,sizeof(color)};
    color.VideoFormat=5; color.VideoFullRange=0; color.ColourDescriptionPresent=1;
    color.ColourPrimaries=color.TransferCharacteristics=color.MatrixCoefficients=1;
    mfxExtBuffer *extensions[]{&color.Header}; parameters.ExtParam=extensions; parameters.NumExtParam=1;
    // Prefer the public fixed-function low-power path only when Query confirms
    // this exact device/configuration supports it. Older hardware retains the
    // normal direct QSV path; strict GPU selection never falls back to MF/CPU.
    auto low_power=parameters;
    low_power.mfx.LowPower=MFX_CODINGOPTION_ON;
    auto low_power_checked=low_power;
    const auto low_power_status=MFXVideoENCODE_Query(driver_.value,&low_power,&low_power_checked);
    if(low_power_status>=0 && low_power_status!=MFX_WRN_PARTIAL_ACCELERATION &&
       low_power_checked.mfx.LowPower==MFX_CODINGOPTION_ON)
      parameters.mfx.LowPower=MFX_CODINGOPTION_ON;
    auto queried=parameters;
    const auto query=MFXVideoENCODE_Query(driver_.value,&parameters,&queried);
    if(query==MFX_WRN_PARTIAL_ACCELERATION)
      fail(NESS_V2_E_UNAVAILABLE,1,"Direct QSV configuration requires partial software acceleration");
    checked_mfx(query,"Direct QSV configuration unsupported",NESS_V2_STAGE_CONFIG);
    const auto initialized=MFXVideoENCODE_Init(driver_.value,&parameters);
    if(initialized==MFX_WRN_PARTIAL_ACCELERATION)
      fail(NESS_V2_E_UNAVAILABLE,1,"Direct QSV initialization requires partial software acceleration");
    checked_mfx(initialized,"Direct QSV encoder initialization failed",NESS_V2_STAGE_CONFIG);
    driver_.encoder_open=true;
    auto effective=parameters;
    checked_mfx(MFXVideoENCODE_GetVideoParam(driver_.value,&effective),"Direct QSV effective parameters unavailable");
    mfxVersion api_version{};
    checked_mfx(MFXQueryVersion(driver_.value,&api_version),"Direct QSV runtime version unavailable");
    const auto identity=std::string("Intel QSV / oneVPL ")+std::to_string(api_version.Major)+"."+
      std::to_string(api_version.Minor)+" / D3D11 / low-power:"+
      (effective.mfx.LowPower==MFX_CODINGOPTION_ON ? "on" : "off")+
      " / async:"+std::to_string(effective.AsyncDepth);
    std::memset(gpu_.implementation_utf8,0,sizeof(gpu_.implementation_utf8));
    std::memcpy(gpu_.implementation_utf8,identity.data(),(std::min)(identity.size(),sizeof(gpu_.implementation_utf8)-1));
    if(!effective.AsyncDepth || effective.AsyncDepth>parameters.AsyncDepth ||
       effective.mfx.CodecProfile!=MFX_PROFILE_AVC_BASELINE || effective.mfx.GopRefDist!=1 ||
       effective.mfx.FrameInfo.CropW!=config_.width || effective.mfx.FrameInfo.CropH!=config_.height ||
       effective.mfx.FrameInfo.FrameRateExtN!=config_.fps_num || effective.mfx.FrameInfo.FrameRateExtD!=config_.fps_den ||
       effective.mfx.RateControlMethod!=MFX_RATECONTROL_CBR)
      fail(NESS_V2_E_UNAVAILABLE,1,"Direct QSV changed the negotiated stream contract");
    const auto scale=(std::max)(uint64_t{1},uint64_t(effective.mfx.BRCParamMultiplier));
    const auto capacity=(std::max)(uint64_t{1024*1024},uint64_t(effective.mfx.BufferSizeInKB)*scale*1000);
    if(capacity>16*1024*1024) fail(NESS_V2_E_UNAVAILABLE,1,"Direct QSV bitstream requirement exceeds bounded capacity");
    for(auto &job:jobs_) job=std::make_unique<Job>(static_cast<size_t>(capacity));
    std::array<mfxU8,16384> sps{},pps{};
    mfxExtCodingOptionSPSPPS headers{};
    headers.Header={MFX_EXTBUFF_CODING_OPTION_SPSPPS,sizeof(headers)};
    headers.SPSBuffer=sps.data(); headers.SPSBufSize=static_cast<mfxU16>(sps.size());
    headers.PPSBuffer=pps.data(); headers.PPSBufSize=static_cast<mfxU16>(pps.size());
    mfxExtBuffer *header_extensions[]{&headers.Header};
    mfxVideoParam description{}; description.ExtParam=header_extensions; description.NumExtParam=1;
    checked_mfx(MFXVideoENCODE_GetVideoParam(driver_.value,&description),"Direct QSV SPS/PPS unavailable");
    if(!headers.SPSBufSize || !headers.PPSBufSize || headers.SPSBufSize>sps.size() || headers.PPSBufSize>pps.size())
      fail(NESS_V2_E_ENCODER,3,"Direct QSV returned invalid parameter-set sizes");
    Bytes sequence(sps.begin(),sps.begin()+headers.SPSBufSize);
    sequence.insert(sequence.end(),pps.begin(),pps.begin()+headers.PPSBufSize);
    auto avcc=codec_private(sequence);
    if(avcc[1]!=config_.profile) fail(NESS_V2_E_ENCODER,3,"Direct QSV AVC profile mismatch");
    auto info=fresh<ness_codec_view_v2>();
    info.width=config_.width; info.height=config_.height; info.fps_num=config_.fps_num; info.fps_den=config_.fps_den;
    info.profile=config_.profile; info.color=config_.color;
    codec_=Codec{info,std::move(avcc)};
  }
  void submit_pending() {
    if(!pending_input_) return;
    auto &input=*identities_[*pending_input_];
    input.copy->poll();
    if(!input.copy->ready.load(std::memory_order_acquire)) return;
    auto &job=*pending_job_;
    const auto status=MFXVideoENCODE_EncodeFrameAsync(driver_.value,nullptr,pending_surface_.value,
      &job.bits,&job.sync);
    if(status==MFX_WRN_DEVICE_BUSY) return;
    if(status!=MFX_ERR_MORE_DATA) checked_mfx(status,"Direct QSV input encoding failed");
    if(job.sync) submitted_.push_back(&job);
    else if(status!=MFX_ERR_MORE_DATA)
      fail(NESS_V2_E_ENCODER,2,"Direct QSV accepted input without a completion token");
    pending_surface_.reset(); pending_input_.reset(); pending_job_=nullptr;
  }
  void receive_completed() {
    while(!submitted_.empty() && collector_.can_receive_output()) {
      auto &job=*submitted_.front();
      if(!job.completed) {
        const auto status=MFXVideoCORE_SyncOperation(driver_.value,job.sync,0);
        if(status==MFX_WRN_IN_EXECUTION) return;
        checked_mfx(status,"Direct QSV asynchronous encoding failed");
        job.completed=true;
      }
      if(!job.bits.DataLength || job.bits.DataOffset>job.bits.MaxLength ||
         job.bits.DataLength>job.bits.MaxLength-job.bits.DataOffset ||
         (job.bits.FrameType&MFX_FRAMETYPE_B))
        fail(NESS_V2_E_ENCODER,3,"Direct QSV returned invalid or reordered bitstream output");
      const auto found=std::find_if(identities_.begin(),identities_.end(),[&](const auto &input) {
        return input && input->tag==job.bits.TimeStamp;
      });
      if(found==identities_.end()) fail(NESS_V2_E_ENCODER,3,"Direct QSV lost original input identity");
      const auto identity=(*found)->identity;
      // Preserve the hardware's CBR trailing-zero payload. Canonical removal
      // changes emitted bitrate/I/O despite an unchanged encoder target.
      // Normalize once and transfer owned bytes into the collector.
      auto normalized=normalize({job.bits.Data+job.bits.DataOffset,job.bits.DataLength},NESS_V2_ANNEX_B,true);
      if(normalized.size()>config_.max_packet_bytes)
        fail(NESS_V2_E_ENCODER,3,"Direct QSV encoded frame exceeds declared packet budget");
      try {
        collector_.accept_normalized(std::move(normalized),identity.pts,identity.pts,
          (job.bits.FrameType&MFX_FRAMETYPE_IDR)!=0);
      } catch(const Failure &error) {
        if(error.code==NESS_V2_E_BACKPRESSURE) return;
        throw;
      }
      found->reset(); job.reset(); submitted_.pop_front();
    }
  }
  void pump() {
    try {
      receive_completed(); submit_pending();
      if(draining_ && !pending_input_ && !driver_drained_) {
        if(auto *job=available_job()) {
          job->reset();
          const auto status=MFXVideoENCODE_EncodeFrameAsync(driver_.value,nullptr,nullptr,&job->bits,&job->sync);
          if(status==MFX_ERR_MORE_DATA) driver_drained_=true;
          else if(status!=MFX_WRN_DEVICE_BUSY) {
            checked_mfx(status,"Direct QSV EOS drain failed");
            if(!job->sync) fail(NESS_V2_E_ENCODER,2,"Direct QSV drain returned no completion");
            submitted_.push_back(job);
          }
        }
      }
      receive_completed();
      if(driver_drained_ && submitted_.empty() && !pending_input_ && collector_.pending())
        fail(NESS_V2_E_ENCODER,3,"Direct QSV reached EOS with missing original input frames");
      drained_=driver_drained_ && submitted_.empty() && !pending_input_ && !collector_.pending();
    } catch(...) { failed_=true; throw; }
  }
public:
  Qsv(ness_encoder_config_v2 config,const ness_gpu_device_v2 &target):config_(config),collector_(config) {
    bind_device(target); configure_driver();
  }
  ~Qsv() override {
    // Destroy the component/session while its bitstream and surfaces still live.
    // Outstanding caller leases own their copy fence independently.
    pending_surface_.reset(); driver_.close();
    for(auto &input:identities_) if(input) input->copy->poll();
  }
  void submit(const ness_cpu_frame_v2 &) override {
    usable(); fail(NESS_V2_E_UNAVAILABLE,2,"Direct QSV accepts negotiated GPU NV12 input only");
  }
  std::shared_ptr<InputCompletion> submit_gpu(const ness_gpu_frame_v2 &frame) override {
    usable(); envelope(&frame);
    if(draining_) fail(NESS_V2_E_STATE,2,"Direct QSV input after EOS");
    pump();
    auto *job=available_job();
    if(pending_input_ || !job || !collector_.can_receive_output())
      fail(NESS_V2_E_BACKPRESSURE,2,"Receive direct QSV output before more input");
    if(frame.device!=device_.Get() || frame.adapter_luid!=gpu_.adapter_luid ||
       frame.width!=config_.width || frame.height!=config_.height ||
       frame.resource_type!=NESS_V2_D3D11_TEXTURE2D || frame.format!=NESS_V2_NV12 || frame.reserved || !frame.texture)
      fail(NESS_V2_E_ARGUMENT,2,"Direct QSV input differs from negotiated device or geometry");
    collector_.validate({frame.frame_id,frame.pts_hns,frame.duration_hns});
    auto *source=static_cast<ID3D11Texture2D *>(frame.texture);
    D3D11_TEXTURE2D_DESC description{}; source->GetDesc(&description);
    ComPtr<ID3D11Device> source_device; source->GetDevice(&source_device);
    if(source_device.Get()!=device_.Get() || description.Format!=DXGI_FORMAT_NV12 ||
       description.Width<frame.width || description.Height<frame.height || description.MipLevels!=1 ||
       description.SampleDesc.Count!=1 || frame.subresource>=description.ArraySize ||
       description.Usage!=D3D11_USAGE_DEFAULT || description.CPUAccessFlags)
      fail(NESS_V2_E_ARGUMENT,2,"Direct QSV source texture layout is invalid");
    const auto slot=std::find(identities_.begin(),identities_.end(),std::nullopt);
    if(slot==identities_.end()) fail(NESS_V2_E_BACKPRESSURE,2,"Direct QSV identity budget exhausted");
    Surface surface;
    checked_mfx(MFXMemory_GetSurfaceForEncode(driver_.value,&surface.value),"Direct QSV surface allocation failed");
    if(!surface.value || !surface.value->FrameInterface)
      fail(NESS_V2_E_ENCODER,2,"Direct QSV returned no managed input surface");
    mfxHDL handle{}; mfxResourceType type{};
    checked_mfx(surface.value->FrameInterface->GetNativeHandle(surface.value,&handle,&type),
      "Direct QSV input surface native handle unavailable");
    if(type!=MFX_RESOURCE_DX11_TEXTURE || !handle)
      fail(NESS_V2_E_UNAVAILABLE,2,"Direct QSV input surface is not a D3D11 texture");
    auto *destination=static_cast<ID3D11Texture2D *>(handle);
    D3D11_TEXTURE2D_DESC destination_desc{}; destination->GetDesc(&destination_desc);
    ComPtr<ID3D11Device> destination_device; destination->GetDevice(&destination_device);
    if(destination_device.Get()!=device_.Get() || destination_desc.ArraySize!=1 ||
       destination_desc.MipLevels!=1 || destination_desc.Format!=DXGI_FORMAT_NV12 ||
       destination_desc.Width<frame.width || destination_desc.Height<frame.height)
      fail(NESS_V2_E_UNAVAILABLE,2,"Direct QSV internal texture layout is unsupported");
    if(next_signal_>=UINT64_MAX-1 || ordinal_>uint64_t(max_time)*120/second)
      fail(NESS_V2_E_STATE,2,"Direct QSV ordinal or fence domain exhausted");
    auto completion=std::make_shared<CopyCompletion>();
    completion->device=device_; completion->texture=source; completion->fence=fence_;
    completion->signal=++next_signal_;
    const auto tag=ordinal_++*90000ULL*config_.fps_den/config_.fps_num;
    surface.value->Data.TimeStamp=tag;
    // Copy only the visible even-sized NV12 rectangle. The encoder's CropW/H
    // excludes its macroblock-alignment padding; caller memory is never mapped.
    const D3D11_BOX box{0,0,0,frame.width,frame.height,1};
    context_->CopySubresourceRegion(destination,0,0,0,0,source,frame.subresource,&box);
    checked_hr(timeline_->Signal(fence_.Get(),completion->signal),"Direct QSV input fence signal failed");
    context_->Flush();
    *slot=TaggedInput{tag,{frame.frame_id,frame.pts_hns,frame.duration_hns},completion};
    collector_.remember((*slot)->identity);
    pending_input_=static_cast<size_t>(slot-identities_.begin());
    pending_surface_.value=surface.value; surface.value=nullptr;
    pending_job_=job; job->reset();
    pump(); return completion;
  }
  ness_gpu_info_v2 gpu_info() override { thread(); return gpu_; }
  std::shared_ptr<Packet> receive() override {
    usable();
    if(auto packet=collector_.receive()) { pump(); return packet; }
    pump(); return collector_.receive();
  }
  std::optional<Codec> description() override { usable(); return codec_; }
  void drain() override { usable(); draining_=true; pump(); }
  void discard() override {
    usable(); pending_surface_.reset(); driver_.close(); collector_.discard();
    for(auto &input:identities_) { if(input) input->copy->poll(); input.reset(); }
    pending_input_.reset(); pending_job_=nullptr; submitted_.clear(); discarded_=true;
  }
  ness_encoder_info_v2 info() override {
    thread(); auto result=fresh<ness_encoder_info_v2>();
    result.backend=NESS_V2_BACKEND_QSV_D3D11; result.gpu_input=1;
    result.width=config_.width; result.height=config_.height;
    result.fps_num=config_.fps_num; result.fps_den=config_.fps_den;
    result.profile=config_.profile; result.color=config_.color;
    constexpr char name[]="Intel Quick Sync direct oneVPL D3D11";
    std::memcpy(result.implementation_utf8,name,sizeof(name)); return result;
  }
  ness_encoder_stats_v2 stats() override {
    thread(); auto result=fresh<ness_encoder_stats_v2>();
    result.submitted=collector_.submitted; result.emitted=collector_.emitted;
    result.pending=collector_.pending(); result.queued_packets=collector_.queued();
    result.queued_bytes=collector_.bytes(); result.discarded=collector_.discarded;
    result.draining=draining_; result.drained=drained_; result.failed=failed_; return result;
  }
};
}
std::unique_ptr<Encoder> qsv_encoder(ness_encoder_config_v2 config,const ness_gpu_device_v2 &target) {
  return std::make_unique<Qsv>(config,target);
}
}
