// This executable substitutes only the driver API. It is NOT a hardware encoder
// qualification and its synthetic payload is never exported as recorded media.
#define NESS_NVENC_CONTRACT_TEST
#include "../../src/v2/nvenc.cpp"
#include <future>
#include <iostream>
#include <unordered_set>

namespace ness2 {
namespace {
struct FakeBits {
  uint64_t pts{},duration{};
  HANDLE event{};
  bool locked{},submitted{},consumed{};
  std::array<uint8_t,8> bytes{0,0,1,0x65,0x88,0x84,0x21,0x80};
};
struct Fake {
  std::unordered_set<void *> registrations,mappings;
  std::unordered_set<FakeBits *> outputs;
  std::unordered_set<HANDLE> events;
  bool hold{},wrong_pts{};
  unsigned busy{},lock_busy{};
};
Fake *active{};
unsigned violations{},sessions{},register_failure{},register_calls{},vendor{};
bool next_hold{},next_wrong{};
unsigned next_busy{},next_lock_busy{};
void expect(bool condition,const char *message) {
  if(!condition) throw std::runtime_error(message);
}
uint32_t contract_vendor() { return vendor; }
void release_outputs() {
  active->hold=false;
  for(auto *bits:active->outputs) if(bits->submitted) SetEvent(bits->event);
}
void contract_open(NV_ENCODE_API_FUNCTION_LIST &api,void *&session,ID3D11Device *) {
  auto *state=new Fake;
  state->hold=next_hold; state->wrong_pts=next_wrong; state->busy=next_busy; state->lock_busy=next_lock_busy;
  active=state; session=state; ++sessions; register_calls=0;
  api.nvEncDestroyEncoder=[](void *value) {
    auto *s=static_cast<Fake *>(value);
    if(!s->registrations.empty() || !s->mappings.empty() || !s->outputs.empty() || !s->events.empty()) ++violations;
    delete s; active=nullptr; --sessions; return NV_ENC_SUCCESS;
  };
  api.nvEncGetEncodeGUIDCount=[](void *,uint32_t *count) { *count=1;return NV_ENC_SUCCESS; };
  api.nvEncGetEncodeGUIDs=[](void *,GUID *ids,uint32_t,uint32_t *count) { *ids=NV_ENC_CODEC_H264_GUID;*count=1;return NV_ENC_SUCCESS; };
  api.nvEncGetEncodeProfileGUIDCount=[](void *,GUID,uint32_t *count) { *count=1;return NV_ENC_SUCCESS; };
  api.nvEncGetEncodeProfileGUIDs=[](void *,GUID,GUID *ids,uint32_t,uint32_t *count) { *ids=NV_ENC_H264_PROFILE_BASELINE_GUID;*count=1;return NV_ENC_SUCCESS; };
  api.nvEncGetInputFormatCount=[](void *,GUID,uint32_t *count) { *count=1;return NV_ENC_SUCCESS; };
  api.nvEncGetInputFormats=[](void *,GUID,NV_ENC_BUFFER_FORMAT *formats,uint32_t,uint32_t *count) { *formats=NV_ENC_BUFFER_FORMAT_NV12;*count=1;return NV_ENC_SUCCESS; };
  api.nvEncGetEncodeCaps=[](void *,GUID,NV_ENC_CAPS_PARAM *query,int *result) {
    *result=query->capsToQuery==NV_ENC_CAPS_WIDTH_MAX || query->capsToQuery==NV_ENC_CAPS_HEIGHT_MAX?4096:
      query->capsToQuery==NV_ENC_CAPS_SUPPORTED_RATECONTROL_MODES?NV_ENC_PARAMS_RC_CBR:1;
    return NV_ENC_SUCCESS;
  };
  api.nvEncGetEncodePresetConfigEx=[](void *,GUID,GUID,NV_ENC_TUNING_INFO,NV_ENC_PRESET_CONFIG *preset) {
    preset->presetCfg={}; preset->presetCfg.version=NV_ENC_CONFIG_VER;return NV_ENC_SUCCESS;
  };
  api.nvEncInitializeEncoder=[](void *,NV_ENC_INITIALIZE_PARAMS *init) {
    if(!init->enableEncodeAsync || init->encodeConfig->frameIntervalP!=1 ||
       init->encodeConfig->rcParams.enableLookahead || init->encodeConfig->profileGUID!=NV_ENC_H264_PROFILE_BASELINE_GUID)
      ++violations;
    return NV_ENC_SUCCESS;
  };
  api.nvEncRegisterResource=[](void *value,NV_ENC_REGISTER_RESOURCE *resource) {
    if(++register_calls==register_failure) return NV_ENC_ERR_OUT_OF_MEMORY;
    auto &s=*static_cast<Fake *>(value);
    resource->registeredResource=resource->resourceToRegister;
    s.registrations.insert(resource->registeredResource);return NV_ENC_SUCCESS;
  };
  api.nvEncUnregisterResource=[](void *value,NV_ENC_REGISTERED_PTR resource) {
    auto &s=*static_cast<Fake *>(value);
    if(s.mappings.contains(resource) || s.registrations.erase(resource)!=1) ++violations;
    return NV_ENC_SUCCESS;
  };
  api.nvEncMapInputResource=[](void *value,NV_ENC_MAP_INPUT_RESOURCE *map) {
    auto &s=*static_cast<Fake *>(value);
    if(!s.registrations.contains(map->registeredResource) || !s.mappings.insert(map->registeredResource).second) ++violations;
    map->mappedResource=map->registeredResource; map->mappedBufferFmt=NV_ENC_BUFFER_FORMAT_NV12;return NV_ENC_SUCCESS;
  };
  api.nvEncUnmapInputResource=[](void *value,NV_ENC_INPUT_PTR resource) {
    if(static_cast<Fake *>(value)->mappings.erase(resource)!=1) ++violations;
    return NV_ENC_SUCCESS;
  };
  api.nvEncCreateBitstreamBuffer=[](void *value,NV_ENC_CREATE_BITSTREAM_BUFFER *buffer) {
    auto *bits=new FakeBits;static_cast<Fake *>(value)->outputs.insert(bits);buffer->bitstreamBuffer=bits;return NV_ENC_SUCCESS;
  };
  api.nvEncDestroyBitstreamBuffer=[](void *value,NV_ENC_OUTPUT_PTR output) {
    auto *bits=static_cast<FakeBits *>(output);
    if(bits->locked || (bits->submitted && !bits->consumed) || static_cast<Fake *>(value)->outputs.erase(bits)!=1) ++violations;
    delete bits;return NV_ENC_SUCCESS;
  };
  api.nvEncRegisterAsyncEvent=[](void *value,NV_ENC_EVENT_PARAMS *event) {
    if(!static_cast<Fake *>(value)->events.insert(event->completionEvent).second) ++violations;
    return NV_ENC_SUCCESS;
  };
  api.nvEncUnregisterAsyncEvent=[](void *value,NV_ENC_EVENT_PARAMS *event) {
    if(static_cast<Fake *>(value)->events.erase(event->completionEvent)!=1) ++violations;
    return NV_ENC_SUCCESS;
  };
  api.nvEncGetSequenceParams=[](void *,NV_ENC_SEQUENCE_PARAM_PAYLOAD *sequence) {
    constexpr std::array<uint8_t,15> headers{0,0,0,1,0x67,66,0,30,0x80,0,0,1,0x68,0x88,0x80};
    std::memcpy(sequence->spsppsBuffer,headers.data(),headers.size());
    *sequence->outSPSPPSPayloadSize=static_cast<uint32_t>(headers.size());return NV_ENC_SUCCESS;
  };
  api.nvEncEncodePicture=[](void *value,NV_ENC_PIC_PARAMS *pic) {
    auto &s=*static_cast<Fake *>(value);
    if(s.busy) { --s.busy;return NV_ENC_ERR_ENCODER_BUSY; }
    if(pic->encodePicFlags&NV_ENC_PIC_FLAG_EOS) {
      for(auto *bits:s.outputs) if(bits->submitted) SetEvent(bits->event);
      SetEvent(pic->completionEvent);return NV_ENC_SUCCESS;
    }
    auto &bits=*static_cast<FakeBits *>(pic->outputBitstream);
    if(bits.submitted && !bits.consumed) ++violations;
    bits.pts=pic->inputTimeStamp;bits.duration=pic->inputDuration;
    bits.event=pic->completionEvent;bits.submitted=true;bits.consumed=false;
    if(!s.mappings.contains(pic->inputBuffer) || !s.events.contains(bits.event)) ++violations;
    if(!s.hold) SetEvent(bits.event);
    return (pic->frameIdx&1)?NV_ENC_ERR_NEED_MORE_INPUT:NV_ENC_SUCCESS;
  };
  api.nvEncLockBitstream=[](void *value,NV_ENC_LOCK_BITSTREAM *bits) {
    auto &s=*static_cast<Fake *>(value);
    if(s.lock_busy) { --s.lock_busy;return NV_ENC_ERR_LOCK_BUSY; }
    auto &owned=*static_cast<FakeBits *>(bits->outputBitstream);
    if(WaitForSingleObject(owned.event,0)!=WAIT_OBJECT_0 || owned.locked) ++violations;
    owned.locked=true;owned.consumed=true;bits->bitstreamBufferPtr=owned.bytes.data();
    bits->bitstreamSizeInBytes=static_cast<uint32_t>(owned.bytes.size());
    bits->outputTimeStamp=owned.pts+(s.wrong_pts?1:0);bits->outputDuration=owned.duration;
    bits->pictureType=NV_ENC_PIC_TYPE_IDR;return NV_ENC_SUCCESS;
  };
  api.nvEncUnlockBitstream=[](void *,NV_ENC_OUTPUT_PTR output) {
    auto &bits=*static_cast<FakeBits *>(output);if(!bits.locked) ++violations;
    bits.locked=false;return NV_ENC_SUCCESS;
  };
}
}
}

int main() {
  try {
    using namespace ness2;
    gpu::ComPtr<ID3D11Device> device;gpu::ComPtr<ID3D11DeviceContext> context;
    gpu::checked(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
      &device,nullptr,&context),"WARP device creation failed");
    gpu::ComPtr<IDXGIDevice> dxgi;gpu::checked(device.As(&dxgi),"DXGI device unavailable");
    gpu::ComPtr<IDXGIAdapter> adapter;gpu::checked(dxgi->GetAdapter(&adapter),"DXGI adapter unavailable");
    DXGI_ADAPTER_DESC desc{};gpu::checked(adapter->GetDesc(&desc),"DXGI descriptor unavailable");vendor=desc.VendorId;
    auto target=fresh<ness_gpu_device_v2>();target.device=device.Get();
    target.adapter_luid=(uint64_t(static_cast<uint32_t>(desc.AdapterLuid.HighPart))<<32)|desc.AdapterLuid.LowPart;
    auto config=fresh<ness_encoder_config_v2>();config.backend=NESS_V2_BACKEND_NVENC_D3D11;
    config.policy=NESS_V2_POLICY_REQUIRE_GPU;config.width=64;config.height=64;
    config.fps_num=30000;config.fps_den=1001;config.bitrate_bps=1000000;
    config.profile=66;config.color=1;config.max_pending_frames=4;config.max_packet_bytes=64;
    auto make_frame=[&](uint64_t index,gpu::ComPtr<ID3D11Texture2D> &texture) {
      D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=64;td.MipLevels=td.ArraySize=1;
      td.Format=DXGI_FORMAT_NV12;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;
      std::array<uint8_t,64*64*3/2> pixels{};pixels.fill(static_cast<uint8_t>(index));
      D3D11_SUBRESOURCE_DATA initial{pixels.data(),64,0};
      gpu::checked(device->CreateTexture2D(&td,&initial,&texture),"WARP NV12 texture creation failed");
      auto f=fresh<ness_gpu_frame_v2>();f.width=f.height=64;f.resource_type=NESS_V2_D3D11_TEXTURE2D;f.format=NESS_V2_NV12;
      f.device=device.Get();f.texture=texture.Get();f.adapter_luid=target.adapter_luid;
      f.frame_id=100+index*3;f.pts_hns=1000000+static_cast<int64_t>(index*10000000*1001/30000);
      f.duration_hns=static_cast<int64_t>((index+1)*10000000*1001/30000-index*10000000*1001/30000);return f;
    };
    unsigned pressure{},verified{};
    {
      next_hold=true;next_busy=3;next_lock_busy=2;
      auto enc=std::make_unique<Nvenc>(config,target);
      expect(std::async(std::launch::async,[&] {try {enc->info();return false;}catch(const Failure &e){return e.code==NESS_V2_E_THREAD;}}).get(),"Foreign thread accepted");
      std::vector<std::shared_ptr<InputCompletion>> leases;
      auto receive=[&] {
        while(auto packet=enc->receive()) {
          const auto at=uint64_t{verified}*10000000*1001/30000;
          expect(packet->info.frame_id==100+uint64_t{verified}*3 && packet->info.pts_hns==1000000+static_cast<int64_t>(at),"Input identity/PTS changed");
          ++verified;
        }
      };
      for(uint64_t index=0;index<120;++index) {
        gpu::ComPtr<ID3D11Texture2D> texture;auto f=make_frame(index,texture);
        if(!index) {
          auto invalid=f;invalid.subresource=1;
          try {enc->submit_gpu(invalid);expect(false,"Invalid subresource accepted");}
          catch(const Failure &e){expect(e.code==NESS_V2_E_ARGUMENT,"Wrong validation error");}
        }
        const auto deadline=GetTickCount64()+3000;
        for(;;) {
          try {leases.push_back(enc->submit_gpu(f));break;}
          catch(const Failure &e) {
            expect(e.code==NESS_V2_E_BACKPRESSURE,"Unexpected input error");++pressure;
            if(index>=4) release_outputs();receive();Sleep(1);
            expect(GetTickCount64()<deadline,"Input stalled under pressure");
          }
        }
        texture.Reset();
        if(index>=4) receive();
      }
      enc->drain();const auto deadline=GetTickCount64()+3000;
      while(!enc->stats().drained) {receive();Sleep(1);expect(GetTickCount64()<deadline,"EOS stalled");}
      receive();expect(verified==120 && pressure>0,"Frames lost or backpressure not exercised");
      enc.reset();
      for(auto &lease:leases) {lease->poll();expect(lease->ready.load(),"Copy lease unusable after encoder destruction");}
    }
    next_hold=false;next_busy=next_lock_busy=0;
    register_failure=3;
    try {Nvenc enc(config,target);expect(false,"Injected initialization failure accepted");}
    catch(const Failure &e){expect(e.code==NESS_V2_E_ALLOC,"Initialization cause lost");}
    register_failure=0;
    for(unsigned scenario=0;scenario<3;++scenario) {
      next_hold=scenario==0;next_wrong=scenario==1;
      auto c=config;if(scenario==2)c.max_packet_bytes=8;
      auto enc=std::make_unique<Nvenc>(c,target);
      gpu::ComPtr<ID3D11Texture2D> texture;auto f=make_frame(0,texture);
      std::shared_ptr<InputCompletion> lease;
      bool failed{};const auto deadline=GetTickCount64()+3000;
      try {
        lease=enc->submit_gpu(f);texture.Reset();
        while(!failed && GetTickCount64()<deadline) {
          (void)enc->receive();
          if(scenario==0 && std::any_of(active->outputs.begin(),active->outputs.end(),[](const FakeBits *bits){return bits->submitted;})) break;
          Sleep(1);
        }
      } catch(const Failure &e) {expect(e.code==NESS_V2_E_ENCODER,"Output failure cause lost");failed=true;}
      if(scenario) expect(failed && enc->stats().failed,"Bad timestamp/oversized packet accepted");
      else enc->discard();
      enc.reset();
      if(lease) {lease->poll();expect(lease->ready.load(),"Discard invalidated a caller copy lease");}
    }
    expect(!sessions && !violations && !gpu_completion_states && !packet_bytes && !packets,"Native driver resources leaked or teardown ordering violated");
    std::cout<<"{\"status\":\"PASS\",\"type\":\"mock-driver-contract-only\",\"hardwareQualified\":false,\"frames\":"<<verified
      <<",\"backpressureCount\":"<<pressure<<",\"partialInitCleanup\":true,\"discardCleanup\":true,\"badTimestampRejected\":true,\"normalizedPacketBudget\":true}\n";
    return 0;
  } catch(const ness2::Failure &e) {std::cerr<<e.message<<" native="<<e.native<<'\n';return 1;}
    catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
