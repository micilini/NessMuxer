#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "helpers.hpp"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <iostream>
int main() {
  try {
    auto c=config();auto error=fresh<ness_error_v2>();ness_encoder_v2 *encoder{};
    check(ness_encoder_create_v2(&c,&encoder,&error)==NESS_V2_E_UNAVAILABLE && !encoder,
      "Disabled MF software backend was opened");
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
      &device,nullptr,nullptr)),"WARP test device unavailable");
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC description{};
    check(SUCCEEDED(device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&description)),
      "WARP adapter identity unavailable");
    auto gpu=fresh<ness_gpu_device_v2>();gpu.device=device.Get();
    gpu.adapter_luid=(uint64_t(static_cast<uint32_t>(description.AdapterLuid.HighPart))<<32)|description.AdapterLuid.LowPart;
    c.backend=NESS_V2_BACKEND_MF_D3D11;c.policy=NESS_V2_POLICY_REQUIRE_GPU;
    check(ness_encoder_create_gpu_v2(&c,&gpu,&encoder,&error)==NESS_V2_E_UNAVAILABLE && !encoder,
      "Disabled MF hardware backend was opened");
    const auto path=std::filesystem::temp_directory_path()/
      ("ness-mf-disabled-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".mkv");
    check(!std::filesystem::exists(path),"Test output must be new");const auto text=path.string();
    NessMuxerConfig legacy{};legacy.output_path=text.c_str();legacy.width=320;legacy.height=240;
    legacy.fps=30;legacy.bitrate_kbps=800;legacy.encoder_type=NESS_ENCODER_MEDIA_FOUNDATION;legacy.codec_type=NESS_CODEC_AVC;
    NessMuxer *muxer{};
    check(ness_muxer_open(&muxer,&legacy)==NESS_ERROR_ENCODER && !muxer && !std::filesystem::exists(path),
      "Disabled legacy MF encoder created an output");
    quiet();std::cout<<"{\"status\":\"PASS\",\"disabledRoutes\":[\"MF-software-v2\",\"MF-D3D11-v2\",\"MF-legacy\"]}\n";return 0;
  } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
