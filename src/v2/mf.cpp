#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "core.hpp"
#include <algorithm>
#include <atomic>
#include <codecapi.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <windows.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>

namespace ness2 {
using Microsoft::WRL::ComPtr;
static void hrcheck(HRESULT hr, const char *text) {
  if (FAILED(hr))
    fail(NESS_V2_E_ENCODER, 2, text, hr);
}
struct Foundation {
  bool com{}, mf{};
  Foundation() {
    const auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (result != RPC_E_CHANGED_MODE)
      hrcheck(result, "COM initialization failed");
    com = SUCCEEDED(result);
    const auto start = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(start)) {
      if (com)
        CoUninitialize();
      com = false;
      hrcheck(start, "Media Foundation initialization failed");
    }
    mf = true;
  }
  ~Foundation() {
    if (mf)
      MFShutdown();
    if (com)
      CoUninitialize();
  }
};

struct GpuCompletion final : InputCompletion {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11Device> device;
};
// The sample owns this callback until every downstream sample reference is
// released. No caller-owned descriptor/sample memory is captured and no
// immediate-context operation runs here.
class SampleReturned final : public IMFAsyncCallback {
  std::atomic<ULONG> refs_{1};
  std::shared_ptr<GpuCompletion> state_;

public:
  explicit SampleReturned(std::shared_ptr<GpuCompletion> state)
      : state_(std::move(state)) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (id != __uuidof(IUnknown) && id != __uuidof(IMFAsyncCallback))
      return E_NOINTERFACE;
    *out = static_cast<IMFAsyncCallback *>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto n = --refs_;
    if (!n)
      delete this;
    return n;
  }
  HRESULT STDMETHODCALLTYPE GetParameters(DWORD *, DWORD *) override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE Invoke(IMFAsyncResult *) override {
    // MF may cache its async result/callback after Invoke has returned. Our
    // texture/device references must end at sample return, before publishing
    // readiness, rather than at eventual callback destruction.
    state_->texture.Reset();
    state_->device.Reset();
    state_->ready.store(true, std::memory_order_release);
    return S_OK;
  }
};
static std::string string_attribute(IMFAttributes *attrs, REFGUID key) {
  wchar_t *value{};
  UINT32 length{};
  hrcheck(attrs->GetAllocatedString(key, &value, &length),
          "Hardware identity attribute missing");
  struct Free {
    wchar_t *p;
    ~Free() { CoTaskMemFree(p); }
  } free{value};
  const int n = WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(length),
                                    nullptr, 0, nullptr, nullptr);
  std::string result(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(length),
                      result.data(), n, nullptr, nullptr);
  return result;
}
template <size_t N>
static void copy_text(uint8_t (&out)[N], const std::string &text) {
  std::memcpy(out, text.data(), (std::min)(N - 1, text.size()));
}
class Mf final : public Encoder {
  Foundation foundation_;
  ness_encoder_config_v2 config_;
  PacketCollector collector_;
  ComPtr<IMFTransform> transform_;
  ComPtr<IMFActivate> activation_;
  ComPtr<IMFDXGIDeviceManager> manager_;
  ComPtr<ID3D11Device> device_;
  ComPtr<IMFMediaEventGenerator> events_;
  ness_gpu_info_v2 gpu_info_ = fresh<ness_gpu_info_v2>();
  uint32_t input_tokens_{}, output_tokens_{};
  bool hardware_{}, drain_event_{};
  void close_transform() noexcept {
    if (transform_) {
      (void)transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
      if (manager_)
        (void)transform_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, 0);
      ComPtr<IMFShutdown> shutdown;
      if (SUCCEEDED(transform_.As(&shutdown)))
        (void)shutdown->Shutdown();
    }
    events_.Reset();
    codec_api_.Reset();
    transform_.Reset();
    if (activation_)
      (void)activation_->ShutdownObject();
    activation_.Reset();
    manager_.Reset();
    device_.Reset();
  }
  void activate_gpu(const ness_gpu_device_v2 &target) {
    hardware_ = true;
    // Tracked-sample notifications can run on MF workers after client release.
    // Keep callback code resident; resource references still release normally.
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&hrcheck), &pinned))
      fail(-11, 2, "Cannot retain hardware completion callback code",
           HRESULT_FROM_WIN32(GetLastError()));
    device_ = static_cast<ID3D11Device *>(target.device);
    ComPtr<IDXGIDevice> dxgi;
    hrcheck(device_.As(&dxgi), "GPU device has no DXGI identity");
    ComPtr<IDXGIAdapter> adapter;
    hrcheck(dxgi->GetAdapter(&adapter), "GPU adapter unavailable");
    DXGI_ADAPTER_DESC desc{};
    hrcheck(adapter->GetDesc(&desc), "GPU adapter description unavailable");
    const uint64_t luid =
        (uint64_t(uint32_t(desc.AdapterLuid.HighPart)) << 32) |
        desc.AdapterLuid.LowPart;
    if (luid != target.adapter_luid)
      fail(-3, 2, "GPU device/LUID mismatch");
    ComPtr<IMFAttributes> criteria;
    hrcheck(MFCreateAttributes(&criteria, 1), "MFT criteria allocation failed");
    hrcheck(
        criteria->SetBlob(MFT_ENUM_ADAPTER_LUID,
                          reinterpret_cast<const UINT8 *>(&desc.AdapterLuid),
                          sizeof(LUID)),
        "MFT LUID criteria failed");
    MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, MFVideoFormat_NV12},
        output{MFMediaType_Video, MFVideoFormat_H264};
    IMFActivate **list{};
    UINT32 count{};
    hrcheck(MFTEnum2(MFT_CATEGORY_VIDEO_ENCODER,
                     MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                     &input, &output, criteria.Get(), &list, &count),
            "Hardware encoder enumeration failed");
    struct List {
      IMFActivate **p;
      UINT32 n;
      ~List() {
        for (UINT32 i = 0; i < n; ++i)
          p[i]->Release();
        CoTaskMemFree(p);
      }
    } owned{list, count};
    if (!count)
      fail(-11, 2,
           "No H264 hardware encoder on the capture adapter; fallback "
           "forbidden");
    // Enumeration still selects the explicitly requested adapter. The Intel
    // activation proxy retains registry-notification handles per activation,
    // including after IMFShutdown/ShutdownObject. Its registered COM transform
    // does not. Limit direct creation to the qualified Intel implementation;
    // bind the capture device below and retain every negotiation/lease check.
    ComPtr<IMFActivate> selected = list[0];
    GUID cls{};
    hrcheck(selected->GetGUID(MFT_TRANSFORM_CLSID_Attribute, &cls),
            "Hardware CLSID missing");
    constexpr GUID intel_h264{0x4be8d3c0, 0x0515, 0x4a37,
                              {0xad, 0x55, 0xe4, 0xba, 0xe1, 0x9a, 0xf4, 0x71}};
    if (desc.VendorId == 0x8086 && cls == intel_h264) {
      hrcheck(CoCreateInstance(cls, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(&transform_)),
              "Selected Intel hardware transform creation failed");
    } else {
      activation_ = selected;
      hrcheck(activation_->ActivateObject(IID_PPV_ARGS(&transform_)),
              "Hardware encoder activation failed");
    }
    ComPtr<IMFAttributes> attrs;
    hrcheck(transform_->GetAttributes(&attrs),
            "Hardware attributes unavailable");
    hrcheck(attrs->GetUINT32(MF_TRANSFORM_ASYNC, &gpu_info_.asynchronous),
            "Hardware async declaration missing");
    hrcheck(attrs->GetUINT32(MF_SA_D3D11_AWARE, &gpu_info_.d3d11_aware),
            "D3D11 awareness missing");
    if (!gpu_info_.asynchronous || !gpu_info_.d3d11_aware)
      fail(-11, 2, "Hardware transform lacks async D3D11 texture support");
    hrcheck(attrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE),
            "Async transform unlock failed");
    gpu_info_.adapter_luid = luid;
    copy_text(gpu_info_.implementation_utf8,
              string_attribute(selected.Get(), MFT_FRIENDLY_NAME_Attribute));
    copy_text(
        gpu_info_.hardware_url_utf8,
        string_attribute(selected.Get(), MFT_ENUM_HARDWARE_URL_Attribute));
    wchar_t text[64]{};
    StringFromGUID2(cls, text, 64);
    char narrow[64]{};
    WideCharToMultiByte(CP_UTF8, 0, text, -1, narrow, 64, nullptr, nullptr);
    copy_text(gpu_info_.clsid_utf8, std::string(narrow));
    ComPtr<ID3D11DeviceContext> context;
    device_->GetImmediateContext(&context);
    ComPtr<ID3D11Multithread> multithread;
    hrcheck(context.As(&multithread), "D3D multithread protection unavailable");
    (void)multithread->SetMultithreadProtected(TRUE);
    UINT reset{};
    hrcheck(MFCreateDXGIDeviceManager(&reset, &manager_),
            "DXGI device manager creation failed");
    hrcheck(manager_->ResetDevice(device_.Get(), reset),
            "DXGI device manager binding failed");
    hrcheck(
        transform_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                                   reinterpret_cast<ULONG_PTR>(manager_.Get())),
        "Hardware D3D manager rejected");
    hrcheck(transform_.As(&events_), "Async event generator unavailable");
  }
  void read_events() {
    if (!hardware_)
      return;
    for (unsigned i = 0; i < 256; ++i) {
      ComPtr<IMFMediaEvent> event;
      auto hr = events_->GetEvent(MF_EVENT_FLAG_NO_WAIT, &event);
      if (hr == MF_E_NO_EVENTS_AVAILABLE)
        return;
      hrcheck(hr, "Hardware event retrieval failed");
      HRESULT status{};
      hrcheck(event->GetStatus(&status), "Hardware event status missing");
      hrcheck(status, "Hardware encoder asynchronous error");
      MediaEventType type{};
      hrcheck(event->GetType(&type), "Hardware event type missing");
      if (type == METransformNeedInput && !draining_) {
        if (input_tokens_ >= 64)
          fail(-5, 2, "Unbounded hardware input tokens");
        ++input_tokens_;
      } else if (type == METransformHaveOutput) {
        if (output_tokens_ >= 64)
          fail(-5, 2, "Unbounded hardware output tokens");
        ++output_tokens_;
      } else if (type == METransformDrainComplete) {
        drain_event_ = true;
      }
    }
  }
  ComPtr<ICodecAPI> codec_api_;
  DWORD owner_ = GetCurrentThreadId(), input_id_{}, output_id_{};
  MFT_OUTPUT_STREAM_INFO output_{};
  bool draining_{}, drained_{}, discarded_{}, failed_{};
  std::optional<Codec> codec_;
  void thread() const {
    if (GetCurrentThreadId() != owner_)
      fail(NESS_V2_E_THREAD, 2, "Encoder calls must use its creating thread");
  }
  void usable() const {
    thread();
    if (failed_ || discarded_)
      fail(-4, 2, "Encoder is failed/discarded");
  }
  void property(REFGUID id, ULONG value) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_UI4;
    v.ulVal = value;
    hrcheck(codec_api_->SetValue(&id, &v),
            "Required codec property is unsupported");
  }
  void media(IMFMediaType *type, REFGUID subtype) {
    hrcheck(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video),
            "Video type failed");
    hrcheck(type->SetGUID(MF_MT_SUBTYPE, subtype), "Video subtype failed");
    hrcheck(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive),
            "Interlace mode failed");
    hrcheck(MFSetAttributeSize(type, MF_MT_FRAME_SIZE, config_.width,
                               config_.height),
            "Frame size failed");
    hrcheck(MFSetAttributeRatio(type, MF_MT_FRAME_RATE, config_.fps_num,
                                config_.fps_den),
            "Frame rate failed");
    hrcheck(MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1),
            "Pixel ratio failed");
    hrcheck(type->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709),
            "Color primaries failed");
    hrcheck(type->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709),
            "Transfer function failed");
    hrcheck(type->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709),
            "Color matrix failed");
    hrcheck(type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235),
            "Nominal range failed");
  }
  void description_from(std::span<const uint8_t> bytes) {
    try {
      auto data = codec_private(bytes);
      if (data[1] != config_.profile)
        fail(-5, 2, "Encoder returned a different H264 profile");
      if (codec_) {
        if (codec_->avcc != data)
          fail(-5, 2, "Encoder codec description changed midstream");
        return;
      }
      Codec result;
      result.info = fresh<ness_codec_view_v2>();
      result.info.width = config_.width;
      result.info.height = config_.height;
      result.info.fps_num = config_.fps_num;
      result.info.fps_den = config_.fps_den;
      result.info.profile = config_.profile;
      result.info.color = config_.color;
      result.avcc = std::move(data);
      codec_ = std::move(result);
    } catch (const Failure &f) {
      if (f.code != NESS_V2_E_ARGUMENT)
        throw; /* A normal delta AU need not contain SPS/PPS. */
    }
  }
  void current_description() {
    ComPtr<IMFMediaType> type;
    if (SUCCEEDED(transform_->GetOutputCurrentType(output_id_, &type))) {
      UINT32 bytes{};
      if (SUCCEEDED(type->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &bytes)) &&
          bytes && bytes <= 65536) {
        Bytes sequence(bytes);
        hrcheck(type->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, sequence.data(),
                              bytes, &bytes),
                "Codec header retrieval failed");
        description_from(sequence);
      }
    }
  }
  bool one() {
    if (!collector_.can_receive_output())
      return false;
    ComPtr<IMFSample> allocated;
    MFT_OUTPUT_DATA_BUFFER output{};
    output.dwStreamID = output_id_;
    if (!(output_.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
      ComPtr<IMFMediaBuffer> memory;
      hrcheck(MFCreateSample(&allocated), "Output sample allocation failed");
      DWORD capacity =
          output_.cbSize ? output_.cbSize : config_.width * config_.height * 2;
      hrcheck(
          MFCreateAlignedMemoryBuffer(capacity, MF_16_BYTE_ALIGNMENT, &memory),
          "Output buffer allocation failed");
      hrcheck(allocated->AddBuffer(memory.Get()),
              "Output buffer attachment failed");
      output.pSample = allocated.Get();
    }
    DWORD status{};
    const auto result = transform_->ProcessOutput(0, 1, &output, &status);
    if (output.pEvents)
      output.pEvents->Release();
    ComPtr<IMFSample> provided;
    if (!allocated && output.pSample)
      provided.Attach(output.pSample);
    if (result == MF_E_TRANSFORM_NEED_MORE_INPUT) {
      if (draining_ && !hardware_) {
        if (collector_.pending())
          fail(-5, 2, "EOS ended before all accepted frames were emitted");
        drained_ = true;
      }
      return false;
    }
    if (result == MF_E_TRANSFORM_STREAM_CHANGE) {
      if (collector_.emitted)
        fail(-5, 2, "Unsupported encoder stream change after output");
      ComPtr<IMFMediaType> type;
      hrcheck(transform_->GetOutputAvailableType(output_id_, 0, &type),
              "Output renegotiation failed");
      hrcheck(transform_->SetOutputType(output_id_, type.Get(), 0),
              "Output renegotiation rejected");
      hrcheck(transform_->GetOutputStreamInfo(output_id_, &output_),
              "Output stream info failed");
      current_description();
      return true;
    }
    hrcheck(result, "Encoder ProcessOutput failed");
    auto *sample = allocated ? allocated.Get() : provided.Get();
    if (!sample)
      fail(-5, 2, "Encoder returned no output sample");
    LONGLONG pts{}, duration{};
    hrcheck(sample->GetSampleTime(&pts), "Encoder output timestamp is missing");
    // Duration is associated with the accepted input, not guessed from receive
    // time.
    (void)sample->GetSampleDuration(&duration);
    ComPtr<IMFMediaBuffer> memory;
    hrcheck(sample->ConvertToContiguousBuffer(&memory),
            "Encoded buffer conversion failed");
    BYTE *bytes{};
    DWORD size{};
    hrcheck(memory->Lock(&bytes, nullptr, &size), "Encoded buffer lock failed");
    struct unlock {
      IMFMediaBuffer *buffer;
      ~unlock() { buffer->Unlock(); }
    } unlocked{memory.Get()};
    UINT32 key{};
    (void)sample->GetUINT32(MFSampleExtension_CleanPoint, &key);
    const std::span<const uint8_t> payload(bytes, size);
    const auto format =
        size >= 4 && bytes[0] == 0 && bytes[1] == 0 &&
                (bytes[2] == 1 || (bytes[2] == 0 && bytes[3] == 1))
            ? NESS_V2_ANNEX_B
            : NESS_V2_AVCC4;
    if (!codec_)
      current_description();
    if (!codec_ && format == NESS_V2_ANNEX_B)
      description_from(payload);
    // Baseline + explicit zero B-pictures makes decode and presentation order
    // equal.
    collector_.accept(payload, format, pts, pts, key != 0);
    return true;
  }
  void pump() {
    try {
      read_events();
      if (hardware_) {
        while (output_tokens_ && collector_.can_receive_output()) {
          --output_tokens_;
          (void)one();
          read_events();
        }
        if (drain_event_ && !output_tokens_) {
          if (collector_.pending())
            fail(-5, 2, "Hardware drain lost accepted input");
          drained_ = true;
        }
      } else
        while (collector_.can_receive_output() && one()) {
        }
    } catch (...) {
      failed_ = true;
      throw;
    }
  }

public:
  explicit Mf(ness_encoder_config_v2 config,
              const ness_gpu_device_v2 *target = nullptr)
      : config_(config), collector_(config) {
    try {
      if (target)
        activate_gpu(*target);
      else {
        hrcheck(CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr,
                                 CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&transform_)),
                "Explicit Microsoft software H264 transform is unavailable");
      }
      ComPtr<IMFAttributes> attributes;
      if (SUCCEEDED(transform_->GetAttributes(&attributes))) {
        UINT32 async{};
        if (SUCCEEDED(attributes->GetUINT32(MF_TRANSFORM_ASYNC, &async)) &&
            async && !hardware_)
          fail(-11, 2, "Only the synchronous software transform is qualified");
        (void)attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
      }
      hrcheck(transform_.As(&codec_api_), "Codec control is unavailable");
      property(CODECAPI_AVEncCommonRateControlMode,
               eAVEncCommonRateControlMode_CBR);
      property(CODECAPI_AVEncCommonMeanBitRate, config.bitrate_bps);
      property(CODECAPI_AVEncMPVDefaultBPictureCount, 0);
      property(CODECAPI_AVEncMPVGOPSize,
               static_cast<ULONG>(
                   (uint64_t(config.fps_num) * 2 + config.fps_den - 1) /
                   config.fps_den));
      const auto stream =
          transform_->GetStreamIDs(1, &input_id_, 1, &output_id_);
      if (stream != E_NOTIMPL)
        hrcheck(stream, "Stream identifiers failed");
      ComPtr<IMFMediaType> output, input;
      hrcheck(MFCreateMediaType(&output),
              "Output media type allocation failed");
      media(output.Get(), MFVideoFormat_H264);
      hrcheck(output->SetUINT32(MF_MT_AVG_BITRATE, config.bitrate_bps),
              "Bitrate type failed");
      hrcheck(output->SetUINT32(MF_MT_MPEG2_PROFILE, config.profile),
              "Profile type failed");
      hrcheck(transform_->SetOutputType(output_id_, output.Get(), 0),
              "H264 output negotiation failed");
      hrcheck(MFCreateMediaType(&input), "Input media type allocation failed");
      media(input.Get(), MFVideoFormat_NV12);
      hrcheck(transform_->SetInputType(input_id_, input.Get(), 0),
              "CPU NV12 negotiation failed");
      ComPtr<IMFMediaType> actual_output, actual_input;
      hrcheck(transform_->GetOutputCurrentType(output_id_, &actual_output),
              "Negotiated output type unavailable");
      hrcheck(transform_->GetInputCurrentType(input_id_, &actual_input),
              "Negotiated input type unavailable");
      for (auto *type : {actual_output.Get(), actual_input.Get()}) {
        UINT32 width{}, height{}, num{}, den{};
        hrcheck(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height),
                "Negotiated geometry unavailable");
        hrcheck(MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den),
                "Negotiated cadence unavailable");
        if (width != config.width || height != config.height ||
            num != config.fps_num || den != config.fps_den)
          fail(-11, 2,
               "Encoder changed the requested geometry or rational cadence");
      }
      UINT32 actual_profile{}, actual_bitrate{};
      hrcheck(actual_output->GetUINT32(MF_MT_MPEG2_PROFILE, &actual_profile),
              "Negotiated profile unavailable");
      hrcheck(actual_output->GetUINT32(MF_MT_AVG_BITRATE, &actual_bitrate),
              "Negotiated bitrate unavailable");
      if (actual_profile != config.profile ||
          actual_bitrate != config.bitrate_bps)
        fail(-11, 2, "Encoder changed the requested profile or bitrate");
      hrcheck(transform_->GetOutputStreamInfo(output_id_, &output_),
              "Output stream info failed");
      hrcheck(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0),
              "Encoder begin-streaming failed");
      hrcheck(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0),
              "Encoder start-of-stream failed");
      current_description();
    } catch (...) {
      close_transform();
      throw;
    }
  }
  ~Mf() override { close_transform(); }
  std::shared_ptr<InputCompletion>
  submit_gpu(const ness_gpu_frame_v2 &frame) override {
    usable();
    if (!hardware_)
      return Encoder::submit_gpu(frame);
    if (draining_ || drained_)
      fail(-4, 2, "GPU submit after EOS/drain");
    if (frame.device != device_.Get() ||
        frame.adapter_luid != gpu_info_.adapter_luid ||
        frame.width != config_.width || frame.height != config_.height ||
        frame.subresource)
      fail(-3, 2, "GPU frame does not match the negotiated device/geometry");
    auto *texture = static_cast<ID3D11Texture2D *>(frame.texture);
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    ComPtr<ID3D11Device> actual;
    texture->GetDevice(&actual);
    if (actual.Get() != device_.Get() || desc.Format != DXGI_FORMAT_NV12 ||
        desc.Width != frame.width || desc.Height != frame.height ||
        desc.Usage != D3D11_USAGE_DEFAULT || desc.CPUAccessFlags ||
        desc.ArraySize != 1 || desc.MipLevels != 1 ||
        desc.SampleDesc.Count != 1)
      fail(-3, 2,
           "GPU texture must be native NV12 without CPU access on the encoder "
           "device");
    collector_.validate({frame.frame_id, frame.pts_hns, frame.duration_hns});
    pump();
    if (!input_tokens_ || !collector_.can_submit())
      fail(-12, 2, "Hardware encoder input backpressure");
    auto state = std::make_shared<GpuCompletion>();
    state->texture = texture;
    state->device = device_;
    ComPtr<IMFMediaBuffer> buffer;
    hrcheck(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), texture, 0,
                                      FALSE, &buffer),
            "DXGI sample buffer failed");
    hrcheck(buffer->SetCurrentLength(config_.width * config_.height * 3 / 2),
            "DXGI buffer length failed");
    ComPtr<IMFTrackedSample> tracked;
    hrcheck(MFCreateTrackedSample(&tracked),
            "Tracked GPU sample creation failed");
    ComPtr<IMFSample> sample;
    hrcheck(tracked.As(&sample), "Tracked sample interface missing");
    hrcheck(sample->AddBuffer(buffer.Get()),
            "Tracked GPU sample attachment failed");
    hrcheck(sample->SetSampleTime(frame.pts_hns), "GPU input timestamp failed");
    hrcheck(sample->SetSampleDuration(frame.duration_hns),
            "GPU input duration failed");
    ComPtr<IMFAsyncCallback> callback;
    callback.Attach(new SampleReturned(state));
    hrcheck(tracked->SetAllocator(callback.Get(), nullptr),
            "Sample completion registration failed");
    hrcheck(transform_->ProcessInput(input_id_, sample.Get(), 0),
            "Hardware texture ProcessInput failed");
    --input_tokens_;
    collector_.remember({frame.frame_id, frame.pts_hns, frame.duration_hns});
    return state;
  }
  ness_gpu_info_v2 gpu_info() override {
    thread();
    if (!hardware_)
      return Encoder::gpu_info();
    return gpu_info_;
  }
  void submit(const ness_cpu_frame_v2 &frame) override {
    usable();
    envelope(&frame);
    if (hardware_)
      fail(-11, 2,
           "Hardware texture encoder rejects CPU input; fallback forbidden");
    if (draining_ || drained_)
      fail(-4, 2, "Submit after EOS/drain");
    if (frame.reserved || frame.format != NESS_V2_NV12 ||
        frame.width != config_.width || frame.height != config_.height)
      fail(-3, 2, "CPU frame format/dimensions mismatch");
    timestamp(frame.pts_hns, frame.duration_hns);
    for (size_t plane = 0; plane < 2; ++plane) {
      const auto &p = frame.planes[plane];
      const uint64_t rows = plane ? frame.height / 2 : frame.height;
      if (!p.data || p.reserved || p.stride < frame.width || p.stride > 65536 ||
          p.bytes < (rows - 1) * p.stride + frame.width)
        fail(-3, 2, "CPU plane bytes/stride are invalid");
    }
    if (!collector_.can_submit())
      fail(NESS_V2_E_BACKPRESSURE, 2,
           "Receive output before submitting more input");
    collector_.validate({frame.frame_id, frame.pts_hns, frame.duration_hns});
    ComPtr<IMFMediaBuffer> memory;
    const DWORD size = frame.width * frame.height * 3 / 2;
    hrcheck(MFCreateMemoryBuffer(size, &memory), "Input allocation failed");
    BYTE *destination{};
    hrcheck(memory->Lock(&destination, nullptr, nullptr),
            "Input buffer lock failed");
    for (uint32_t y = 0; y < frame.height; ++y)
      std::memcpy(destination + size_t(y) * frame.width,
                  frame.planes[0].data + size_t(y) * frame.planes[0].stride,
                  frame.width);
    const size_t chroma = size_t(frame.width) * frame.height;
    for (uint32_t y = 0; y < frame.height / 2; ++y)
      std::memcpy(destination + chroma + size_t(y) * frame.width,
                  frame.planes[1].data + size_t(y) * frame.planes[1].stride,
                  frame.width);
    hrcheck(memory->Unlock(), "Input buffer unlock failed");
    hrcheck(memory->SetCurrentLength(size), "Input size failed");
    ComPtr<IMFSample> sample;
    hrcheck(MFCreateSample(&sample), "Input sample allocation failed");
    hrcheck(sample->AddBuffer(memory.Get()), "Input attachment failed");
    hrcheck(sample->SetSampleTime(frame.pts_hns), "Input timestamp failed");
    hrcheck(sample->SetSampleDuration(frame.duration_hns),
            "Input duration failed");
    auto result = transform_->ProcessInput(input_id_, sample.Get(), 0);
    if (result == MF_E_NOTACCEPTING) {
      pump();
      result = transform_->ProcessInput(input_id_, sample.Get(), 0);
    }
    if (result == MF_E_NOTACCEPTING)
      fail(NESS_V2_E_BACKPRESSURE, 2, "Encoder needs output consumed");
    hrcheck(result, "Encoder ProcessInput failed");
    collector_.remember({frame.frame_id, frame.pts_hns, frame.duration_hns});
    pump();
  }
  std::shared_ptr<Packet> receive() override {
    usable();
    if (auto value = collector_.receive())
      return value;
    pump();
    return collector_.receive();
  }
  std::optional<Codec> description() override {
    usable();
    return codec_;
  }
  void drain() override {
    usable();
    if (draining_)
      return;
    hrcheck(
        transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, input_id_),
        "Encoder EOS failed");
    hrcheck(transform_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0),
            "Encoder drain failed");
    draining_ = true;
    input_tokens_ = 0;
    pump();
  }
  void discard() override {
    usable();
    hrcheck(transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0),
            "Encoder discard failed");
    collector_.discard();
    discarded_ = true;
  }
  ness_encoder_info_v2 info() override {
    thread();
    auto value = fresh<ness_encoder_info_v2>();
    value.backend =
        hardware_ ? NESS_V2_BACKEND_MF_D3D11 : NESS_V2_BACKEND_MF_SOFTWARE;
    value.software = !hardware_;
    value.gpu_input = hardware_;
    value.width = config_.width;
    value.height = config_.height;
    value.fps_num = config_.fps_num;
    value.fps_den = config_.fps_den;
    value.profile = config_.profile;
    value.color = config_.color;
    if (hardware_)
      std::memcpy(value.implementation_utf8, gpu_info_.implementation_utf8, 63);
    else
      std::memcpy(value.implementation_utf8,
                  "Microsoft CMSH264EncoderMFT software", 36);
    return value;
  }
  ness_encoder_stats_v2 stats() override {
    thread();
    auto value = fresh<ness_encoder_stats_v2>();
    value.submitted = collector_.submitted;
    value.emitted = collector_.emitted;
    value.pending = collector_.pending();
    value.queued_packets = collector_.queued();
    value.queued_bytes = collector_.bytes();
    value.discarded = collector_.discarded;
    value.draining = draining_;
    value.drained = drained_;
    value.failed = failed_;
    return value;
  }
};
std::unique_ptr<Encoder> mf_encoder(ness_encoder_config_v2 config) {
  return std::make_unique<Mf>(config);
}
std::unique_ptr<Encoder> mf_gpu_encoder(ness_encoder_config_v2 config, const ness_gpu_device_v2 &target) {
  return std::make_unique<Mf>(config, &target);
}
} // namespace ness2
