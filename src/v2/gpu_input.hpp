#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "core.hpp"
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <mutex>
#include <thread>

namespace ness2::gpu {
using Microsoft::WRL::ComPtr;
inline void checked(HRESULT result,const char *message) {
  if(FAILED(result)) fail(NESS_V2_E_ENCODER,NESS_V2_STAGE_ENCODER,message,result);
}
// A caller lease owns only the asynchronous copy, independently of the encoder.
// Retaining the source until the fence completes prevents premature pool reuse.
struct Copy final : InputCompletion {
  std::mutex mutex;
  ComPtr<ID3D11Texture2D> source;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11Fence> fence;
  uint64_t value{};
  void poll() noexcept override {
    std::lock_guard lock(mutex);
    if(ready.load(std::memory_order_relaxed) || !fence) return;
    const auto completed=fence->GetCompletedValue();
    if(completed==UINT64_MAX) {
      if(SUCCEEDED(device->GetDeviceRemovedReason())) return;
    } else if(completed<value) return;
    source.Reset(); device.Reset(); fence.Reset();
    ready.store(true,std::memory_order_release);
  }
};
class Input {
  const std::thread::id owner_=std::this_thread::get_id();
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<ID3D11DeviceContext4> timeline_;
  ComPtr<ID3D11Fence> fence_;
  uint64_t next_{};
public:
  ComPtr<ID3D11Device> device;
  uint64_t luid{};
  Input(const ness_gpu_device_v2 &target,uint32_t vendor) {
    device=static_cast<ID3D11Device *>(target.device);
    ComPtr<IDXGIDevice> dxgi;
    checked(device.As(&dxgi),"Native encoder requires a DXGI D3D11 device");
    ComPtr<IDXGIAdapter> adapter;
    checked(dxgi->GetAdapter(&adapter),"Native encoder adapter unavailable");
    DXGI_ADAPTER_DESC desc{};
    checked(adapter->GetDesc(&desc),"Native encoder adapter identity unavailable");
    luid=(uint64_t(static_cast<uint32_t>(desc.AdapterLuid.HighPart))<<32)|desc.AdapterLuid.LowPart;
    if(luid!=target.adapter_luid || desc.VendorId!=vendor)
      fail(NESS_V2_E_UNAVAILABLE,1,"Native encoder does not match selected adapter vendor/LUID");
    device->GetImmediateContext(&context_);
    ComPtr<ID3D11Multithread> protection;
    checked(context_.As(&protection),"Native encoder context protection unavailable");
    protection->SetMultithreadProtected(TRUE);
    ComPtr<ID3D11Device5> version5;
    checked(device.As(&version5),"Native encoder requires D3D11 timeline fences");
    checked(context_.As(&timeline_),"Native encoder timeline context unavailable");
    checked(version5->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence_)),
      "Native encoder input fence creation failed");
  }
  void thread() const {
    if(std::this_thread::get_id()!=owner_)
      fail(NESS_V2_E_THREAD,2,"Native GPU encoder requires its creating thread");
  }
  void alive() const {
    thread(); checked(device->GetDeviceRemovedReason(),"Native encoder device was removed");
  }
  void validate(const ness_gpu_frame_v2 &frame,const ness_encoder_config_v2 &config) const {
    envelope(&frame);
    if(frame.device!=device.Get() || frame.adapter_luid!=luid || frame.width!=config.width ||
       frame.height!=config.height || frame.resource_type!=NESS_V2_D3D11_TEXTURE2D ||
       frame.format!=NESS_V2_NV12 || frame.reserved || !frame.texture)
      fail(NESS_V2_E_ARGUMENT,2,"Native GPU input differs from negotiated device/geometry");
    auto *source=static_cast<ID3D11Texture2D *>(frame.texture);
    D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
    ComPtr<ID3D11Device> source_device; source->GetDevice(&source_device);
    if(source_device.Get()!=device.Get() || desc.Format!=DXGI_FORMAT_NV12 ||
       desc.Width<frame.width || desc.Height<frame.height || desc.MipLevels!=1 ||
       desc.SampleDesc.Count!=1 || frame.subresource>=desc.ArraySize ||
       desc.Usage!=D3D11_USAGE_DEFAULT || desc.CPUAccessFlags)
      fail(NESS_V2_E_ARGUMENT,2,"Native GPU source texture layout is invalid");
  }
  ComPtr<ID3D11Texture2D> texture(uint32_t width,uint32_t height) const {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=width; desc.Height=height; desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_NV12; desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Texture2D> result;
    checked(device->CreateTexture2D(&desc,nullptr,&result),"Native encoder NV12 texture creation failed");
    return result;
  }
  std::shared_ptr<Copy> copy(const ness_gpu_frame_v2 &frame,ID3D11Texture2D *destination) {
    if(next_>=UINT64_MAX-1) fail(NESS_V2_E_STATE,2,"Native GPU fence domain exhausted");
    auto lease=std::make_shared<Copy>();
    lease->source=static_cast<ID3D11Texture2D *>(frame.texture);
    lease->device=device; lease->fence=fence_; lease->value=++next_;
    const D3D11_BOX box{0,0,0,frame.width,frame.height,1};
    context_->CopySubresourceRegion(destination,0,0,0,0,lease->source.Get(),frame.subresource,&box);
    checked(timeline_->Signal(fence_.Get(),lease->value),"Native GPU input fence signal failed");
    context_->Flush(); return lease;
  }
};
}
