#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ness_muxer_v2.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using Microsoft::WRL::ComPtr;
template<class T> static T fresh() { T value{};value.size=sizeof(value);value.version=2;return value; }
static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
static void ok(int32_t code,const ness_error_v2 &error) {
  if(code<0) throw std::runtime_error(reinterpret_cast<const char *>(error.message_utf8));
}
static int64_t at(uint64_t index) { return static_cast<int64_t>(index*10000000ULL*1001/30000); }
int main(int argc,char **argv) {
  try {
    check(argc==3,"Provide nvenc or amf and a new MKV output path");
    const bool nvenc=std::string(argv[1])=="nvenc";
    check(nvenc || std::string(argv[1])=="amf","Unknown native encoder");
    const uint32_t backend=nvenc?NESS_V2_BACKEND_NVENC_D3D11:NESS_V2_BACKEND_AMF_D3D11;
    const uint32_t vendor=nvenc?0x10deu:0x1002u;
    std::filesystem::path output=std::filesystem::absolute(argv[2]);
    const auto base=output;
    for(unsigned run=2;std::filesystem::exists(output) && run<10000;++run)
      output=base.parent_path()/(base.stem().string()+"-r"+std::to_string(run)+".mkv");
    check(!std::filesystem::exists(output),"Preserve previous Native GPU encoder media");
    std::filesystem::create_directories(output.parent_path());
    ComPtr<IDXGIFactory4> factory;
    check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"DXGI enumeration failed");
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 description{};
    for(UINT index=0;;++index) {
      ComPtr<IDXGIAdapter1> candidate;
      const auto hr=factory->EnumAdapters1(index,&candidate);
      if(hr==DXGI_ERROR_NOT_FOUND) {
        std::cout<<"{\"status\":\"BLOCKED\",\"reason\":\"No adapter for selected hardware encoder\"}\n";return 77;
      }
      check(SUCCEEDED(hr),"Adapter enumeration failed");
      check(SUCCEEDED(candidate->GetDesc1(&description)),"Adapter identity unavailable");
      if(description.VendorId==vendor && !(description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)) { adapter=candidate;break; }
    }
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL level{};
    check(SUCCEEDED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,
      D3D11_CREATE_DEVICE_VIDEO_SUPPORT|D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,2,D3D11_SDK_VERSION,
      &device,&level,&context)),"D3D11 device failed");
    const auto luid=(uint64_t(static_cast<uint32_t>(description.AdapterLuid.HighPart))<<32)|description.AdapterLuid.LowPart;
    auto error=fresh<ness_error_v2>(); auto config=fresh<ness_encoder_config_v2>();
    config.backend=backend; config.policy=NESS_V2_POLICY_REQUIRE_GPU;
    config.width=640;config.height=360;config.fps_num=30000;config.fps_den=1001;
    config.bitrate_bps=2000000;config.rate_control=NESS_V2_CBR;
    config.profile=NESS_V2_PROFILE_BASELINE;config.color=NESS_V2_BT709_LIMITED;
    config.max_pending_frames=4;config.max_packet_bytes=1024*1024;
    auto target=fresh<ness_gpu_device_v2>();target.device=device.Get();target.adapter_luid=luid;
    ness_encoder_v2 *raw{};
    auto invalid_target=target;invalid_target.adapter_luid^=1;
    check(ness_encoder_create_gpu_v2(&config,&invalid_target,&raw,&error)==NESS_V2_E_UNAVAILABLE && !raw,
      "Native GPU encoder substituted an adapter after a mismatched LUID");
    const auto created=ness_encoder_create_gpu_v2(&config,&target,&raw,&error);
    if(created==NESS_V2_E_UNAVAILABLE) {
      check(!raw,"Unavailable native encoder returned ownership");
      auto counts=fresh<ness_runtime_stats_v2>();auto runtime_error=fresh<ness_error_v2>();
      ok(ness_runtime_stats_get_v2(&counts,&runtime_error),runtime_error);
      check(!counts.encoders,"Unavailable native encoder leaked ownership");
      std::cout<<"{\"status\":\"BLOCKED\",\"code\":"<<created<<",\"nativeCode\":"<<error.native_code
        <<",\"reason\":\""<<reinterpret_cast<const char *>(error.message_utf8)<<"\",\"nativeQuiescence\":true}\n";
      return 77;
    }
    ok(created,error);
    std::unique_ptr<ness_encoder_v2,decltype(&ness_encoder_release_v2)> encoder(raw,ness_encoder_release_v2);
    auto info=fresh<ness_encoder_info_v2>();ok(ness_encoder_info_get_v2(raw,&info,&error),error);
    check(info.backend==backend && !info.software && info.gpu_input,"Native GPU encoder route was substituted");
    const auto foreign=std::async(std::launch::async,[raw] {
      auto e=fresh<ness_error_v2>();auto i=fresh<ness_encoder_info_v2>();return ness_encoder_info_get_v2(raw,&i,&e);
    }).get();check(foreign==NESS_V2_E_THREAD,"Native GPU encoder accepted a foreign owner thread");
    ness_codec_v2 *codec_raw{};ok(ness_encoder_description_v2(raw,&codec_raw,&error),error);
    std::unique_ptr<ness_codec_v2,decltype(&ness_codec_release_v2)> codec(codec_raw,ness_codec_release_v2);
    check(bool(codec),"Native GPU encoder codec description unavailable");
    using Packet=std::unique_ptr<ness_packet_v2,decltype(&ness_packet_release_v2)>;
    using Lease=std::unique_ptr<ness_input_lease_v2,decltype(&ness_input_release_v2)>;
    std::vector<Packet> packets;std::vector<Lease> leases;
    auto receive=[&] {
      for(;;) {
        ness_packet_v2 *packet{};const auto status=ness_encoder_receive_v2(raw,&packet,&error);ok(status,error);
        if(status==NESS_V2_AGAIN || status==NESS_V2_END) return status;
        check(packet!=nullptr,"Native GPU encoder returned no owned packet");packets.emplace_back(packet,ness_packet_release_v2);
      }
    };
    constexpr uint32_t stride=672;
    std::vector<uint8_t> pixels(stride*360*3/2,0xa5);
    const std::array<std::array<uint8_t,3>,4> bars{{{16,128,128},{63,102,240},{173,42,26},{32,240,118}}};
    unsigned pressure{};
    for(uint64_t index=0;index<120;++index) {
      for(uint32_t y=0;y<360;++y)
        for(uint32_t x=0;x<640;++x) pixels[y*stride+x]=bars[(x/160+index/30)%4][0];
      for(uint32_t y=0;y<180;++y)
        for(uint32_t x=0;x<640;x+=2) {
          const auto &bar=bars[(x/160+index/30)%4];
          pixels[stride*360+y*stride+x]=bar[1];pixels[stride*360+y*stride+x+1]=bar[2];
        }
      D3D11_TEXTURE2D_DESC texture_desc{};
      texture_desc.Width=640;texture_desc.Height=360;texture_desc.MipLevels=texture_desc.ArraySize=1;
      texture_desc.Format=DXGI_FORMAT_NV12;texture_desc.SampleDesc.Count=1;texture_desc.Usage=D3D11_USAGE_DEFAULT;
      const D3D11_SUBRESOURCE_DATA initial{pixels.data(),stride,0};
      ComPtr<ID3D11Texture2D> texture;
      check(SUCCEEDED(device->CreateTexture2D(&texture_desc,&initial,&texture)),"Owned test texture upload failed");
      auto frame=fresh<ness_gpu_frame_v2>();frame.resource_type=NESS_V2_D3D11_TEXTURE2D;frame.format=NESS_V2_NV12;
      frame.width=640;frame.height=360;frame.texture=texture.Get();frame.device=device.Get();frame.adapter_luid=luid;
      frame.frame_id=100+index*3;frame.pts_hns=1000000+at(index);frame.duration_hns=at(index+1)-at(index);
      if(!index) {
        auto invalid=frame;invalid.subresource=1;ness_input_lease_v2 *rejected{};
        check(ness_encoder_submit_gpu_v2(raw,&invalid,&rejected,&error)==NESS_V2_E_ARGUMENT && !rejected,
          "Native GPU encoder accepted an invalid subresource");
      }
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      for(;;) {
        ness_input_lease_v2 *lease{};const auto status=ness_encoder_submit_gpu_v2(raw,&frame,&lease,&error);
        if(status==NESS_V2_E_BACKPRESSURE) {
          ++pressure;check(!lease,"Native GPU encoder backpressure accepted a caller lease");receive();
          check(std::chrono::steady_clock::now()<deadline,"Native GPU encoder input stalled");Sleep(1);continue;
        }
        ok(status,error);check(lease!=nullptr,"Native GPU encoder accepted input without completion lease");
        leases.emplace_back(lease,ness_input_release_v2);break;
      }
      texture.Reset();std::fill(pixels.begin(),pixels.end(),uint8_t{0});
      receive();
    }
    ok(ness_encoder_drain_v2(raw,&error),error);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(receive()!=NESS_V2_END) {
      check(std::chrono::steady_clock::now()<deadline,"Native GPU encoder EOS did not complete");Sleep(1);
    }
    auto stats=fresh<ness_encoder_stats_v2>();ok(ness_encoder_stats_get_v2(raw,&stats,&error),error);
    check(stats.drained && !stats.failed && stats.submitted==120 && stats.emitted==120 &&
      !stats.pending && packets.size()==120,"Native GPU encoder lost original input frames");
    encoder.reset();
    for(const auto &lease:leases) {
      auto completion=fresh<ness_completion_v2>();ok(ness_input_completion_get_v2(lease.get(),&completion,&error),error);
      check(completion.ready!=0,"Native GPU encoder copy lease was not ready after encoding completed");
    }
    const auto path=output.u8string();auto mux_config=fresh<ness_muxer_config_v2>();
    mux_config.track_id=1;mux_config.max_buffered_packets=16;mux_config.max_buffered_bytes=1024*1024;
    mux_config.output_path_utf8={reinterpret_cast<const uint8_t *>(path.data()),path.size()};
    ness_mkv_v2 *mux_raw{};ok(ness_mkv_open_v2(&mux_config,codec.get(),&mux_raw,&error),error);
    std::unique_ptr<ness_mkv_v2,decltype(&ness_mkv_release_v2)> mux(mux_raw,ness_mkv_release_v2);
    for(size_t index=0;index<packets.size();++index) {
      auto view=fresh<ness_packet_view_v2>();ok(ness_packet_view_get_v2(packets[index].get(),&view,&error),error);
      check(view.frame_id==100+index*3 && view.pts_hns==1000000+at(index) && view.dts_hns==view.pts_hns &&
        view.duration_hns==at(index+1)-at(index),"Native GPU encoder changed original identity or rational timestamps");
      ok(ness_mkv_write_packet_v2(mux_raw,packets[index].get(),&error),error);
    }
    ok(ness_mkv_finalize_v2(mux_raw,&error),error);
    mux.reset();codec.reset();packets.clear();leases.clear();
    auto runtime=fresh<ness_runtime_stats_v2>();ok(ness_runtime_stats_get_v2(&runtime,&error),error);
    auto gpu=fresh<ness_gpu_runtime_stats_v2>();ok(ness_gpu_runtime_stats_get_v2(&gpu,&error),error);
    check(!runtime.encoders && !runtime.muxers && !runtime.codecs && !runtime.packets && !runtime.packet_bytes &&
      !gpu.completion_states && !gpu.input_leases,"Native GPU encoder leaked native ownership");
    std::cout<<"{\"status\":\"PASS\",\"frames\":120,\"originalPtsPreserved\":true,"
      "\"callerTextureMayBeReleased\":true,\"gpuReadbackBytes\":0,\"backpressureCount\":"<<pressure<<","
      "\"adapterLuid\":"<<luid<<",\"nativeQuiescence\":true}\n";return 0;
  } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
