#include "ness_muxer_v2.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
template<class T> static T fresh() {
  T value{}; value.size=sizeof(value); value.version=2; return value;
}
static void check(bool value,const char *message) {
  if (!value) throw std::runtime_error(message);
}
static void ok(int32_t value,const ness_error_v2 &error) {
  if (value<0) throw std::runtime_error(reinterpret_cast<const char *>(error.message_utf8));
}
static int64_t at(uint64_t index) { return static_cast<int64_t>(index*10000000ULL*1001/30000); }
int main(int argc,char **argv) {
  try {
    if(argc==2 && std::string_view(argv[1])=="--expect-unavailable") {
      auto config=fresh<ness_encoder_config_v2>();auto error=fresh<ness_error_v2>();
      config.backend=NESS_V2_BACKEND_X264;config.policy=NESS_V2_POLICY_SOFTWARE;
      config.width=640;config.height=360;config.fps_num=30;config.fps_den=1;config.bitrate_bps=2000000;
      config.profile=NESS_V2_PROFILE_BASELINE;config.color=NESS_V2_BT709_LIMITED;
      ness_encoder_v2 *encoder{};
      const auto code=ness_encoder_create_v2(&config,&encoder,&error);
      check(code==NESS_V2_E_UNAVAILABLE && !encoder,"Unavailable or incompatible x264 was accepted");
      const auto rejection=error;
      auto runtime=fresh<ness_runtime_stats_v2>();ok(ness_runtime_stats_get_v2(&runtime,&error),error);
      check(!runtime.encoders,"Rejected x264 initialization leaked ownership");
      std::cout<<"{\"status\":\"PASS\",\"unavailableCode\":"<<code<<",\"detail\":\""
        <<reinterpret_cast<const char *>(rejection.message_utf8)<<"\"}\n";return 0;
    }
    check(argc==2,"Provide a new MKV output path");
    std::filesystem::path output(argv[1]);
    for(unsigned run=2;std::filesystem::exists(output) && run<10000;++run)
      output=std::filesystem::path(argv[1]).parent_path()/
        (std::filesystem::path(argv[1]).stem().string()+"-r"+std::to_string(run)+".mkv");
    check(!std::filesystem::exists(output),"Preserve existing x264 test media");
    std::filesystem::create_directories(output.parent_path());
    auto error=fresh<ness_error_v2>();
    auto config=fresh<ness_encoder_config_v2>();
    config.backend=NESS_V2_BACKEND_X264; config.policy=NESS_V2_POLICY_SOFTWARE;
    config.width=640; config.height=360; config.fps_num=30000; config.fps_den=1001;
    config.bitrate_bps=2000000; config.rate_control=NESS_V2_CBR;
    config.profile=NESS_V2_PROFILE_BASELINE; config.color=NESS_V2_BT709_LIMITED;
    config.max_pending_frames=4; config.max_packet_bytes=1024*1024;
    ness_encoder_v2 *raw{};
    ok(ness_encoder_create_v2(&config,&raw,&error),error);
    std::unique_ptr<ness_encoder_v2,decltype(&ness_encoder_release_v2)> encoder(raw,ness_encoder_release_v2);
    auto info=fresh<ness_encoder_info_v2>();
    ok(ness_encoder_info_get_v2(raw,&info,&error),error);
    check(info.backend==NESS_V2_BACKEND_X264 && info.software && !info.gpu_input,
      "x264 software route was substituted");
    const std::string implementation(reinterpret_cast<const char *>(info.implementation_utf8));
    check(implementation.find("veryfast/zerolatency t=")!=std::string::npos &&
      implementation.find(" s=")!=std::string::npos,"x264 effective slice/thread metadata missing");
    if (std::thread::hardware_concurrency()>=4)
      check(implementation.ends_with("t=2 s=2"),"x264 did not apply the small realtime slice budget");
    const auto wrong_thread=std::async(std::launch::async,[raw] {
      auto e=fresh<ness_error_v2>(); auto i=fresh<ness_encoder_info_v2>();
      return ness_encoder_info_get_v2(raw,&i,&e);
    }).get();
    check(wrong_thread==NESS_V2_E_THREAD,"x264 accepted a foreign owner thread");
    ness_codec_v2 *codec_raw{};
    ok(ness_encoder_description_v2(raw,&codec_raw,&error),error);
    std::unique_ptr<ness_codec_v2,decltype(&ness_codec_release_v2)> codec(codec_raw,ness_codec_release_v2);
    check(bool(codec),"x264 codec description missing");
    using Packet=std::unique_ptr<ness_packet_v2,decltype(&ness_packet_release_v2)>;
    std::vector<Packet> packets;
    auto receive=[&] {
      for (;;) {
        ness_packet_v2 *packet{};
        const auto code=ness_encoder_receive_v2(raw,&packet,&error); ok(code,error);
        if (code==NESS_V2_AGAIN || code==NESS_V2_END) break;
        check(packet!=nullptr,"Missing x264 packet handle");
        packets.emplace_back(packet,ness_packet_release_v2);
      }
    };
    constexpr uint32_t stride=672;
    std::vector<uint8_t> luma(stride*360,0xa5),chroma(stride*180,0xa5);
    const std::array<std::array<uint8_t,3>,4> bars{{{16,128,128},{63,102,240},{173,42,26},{32,240,118}}};
    unsigned pressure{};
    for (uint64_t index=0;index<120;++index) {
      for (uint32_t y=0;y<360;++y)
        for (uint32_t x=0;x<640;++x) luma[y*stride+x]=bars[(x/160+index/30)%4][0];
      for (uint32_t y=0;y<180;++y)
        for (uint32_t x=0;x<640;x+=2) {
          const auto &bar=bars[(x/160+index/30)%4];
          chroma[y*stride+x]=bar[1]; chroma[y*stride+x+1]=bar[2];
        }
      auto frame=fresh<ness_cpu_frame_v2>();
      frame.format=NESS_V2_NV12; frame.width=640; frame.height=360;
      frame.frame_id=100+index*3; frame.pts_hns=1000000+at(index);
      frame.duration_hns=at(index+1)-at(index);
      frame.planes[0]={luma.data(),luma.size(),stride,0};
      frame.planes[1]={chroma.data(),chroma.size(),stride,0};
      if (!index) {
        auto invalid=frame; invalid.planes[0].bytes=1;
        check(ness_encoder_submit_cpu_v2(raw,&invalid,&error)==NESS_V2_E_ARGUMENT,
              "x264 accepted an undersized caller plane");
      }
      auto code=ness_encoder_submit_cpu_v2(raw,&frame,&error);
      if(code==NESS_V2_E_BACKPRESSURE) {
        ++pressure;receive();code=ness_encoder_submit_cpu_v2(raw,&frame,&error);
      }
      ok(code,error);
      // The caller can overwrite borrowed input as soon as Submit returns.
      std::fill(luma.begin(),luma.end(),uint8_t{0});
      std::fill(chroma.begin(),chroma.end(),uint8_t{0});
      if (index%40==39) receive();
    }
    ok(ness_encoder_drain_v2(raw,&error),error); receive();
    auto stats=fresh<ness_encoder_stats_v2>();
    ok(ness_encoder_stats_get_v2(raw,&stats,&error),error);
    check(stats.drained && !stats.failed && stats.pending==0 && packets.size()==120 && pressure>0,
          "x264 EOS did not drain all120 original inputs");
    encoder.reset();
    const auto path=output.u8string();
    auto mux_config=fresh<ness_muxer_config_v2>();
    mux_config.track_id=1; mux_config.max_buffered_packets=16; mux_config.max_buffered_bytes=1024*1024;
    mux_config.output_path_utf8={reinterpret_cast<const uint8_t *>(path.data()),path.size()};
    ness_mkv_v2 *mux_raw{};
    ok(ness_mkv_open_v2(&mux_config,nullptr,&mux_raw,&error),error);
    std::unique_ptr<ness_mkv_v2,decltype(&ness_mkv_release_v2)> mux(mux_raw,ness_mkv_release_v2);
    ok(ness_mkv_set_codec_v2(mux_raw,codec.get(),&error),error);
    for (size_t index=0;index<packets.size();++index) {
      auto view=fresh<ness_packet_view_v2>();
      ok(ness_packet_view_get_v2(packets[index].get(),&view,&error),error);
      check(view.frame_id==100+index*3 && view.pts_hns==1000000+at(index) &&
        view.dts_hns==view.pts_hns && view.duration_hns==at(index+1)-at(index),
        "x264 changed original caller identity or rational PTS/duration");
      check(view.payload.bytes && view.payload.data,"Packet did not outlive encoder");
      ok(ness_mkv_write_packet_v2(mux_raw,packets[index].get(),&error),error);
    }
    ok(ness_mkv_finalize_v2(mux_raw,&error),error);
    mux.reset(); codec.reset(); packets.clear();
    auto runtime=fresh<ness_runtime_stats_v2>();
    ok(ness_runtime_stats_get_v2(&runtime,&error),error);
    check(!runtime.encoders && !runtime.muxers && !runtime.packets && !runtime.codecs && !runtime.packet_bytes,
      "x264 test leaked native ownership");
    std::cout<<"{\"status\":\"PASS\",\"implementation\":\""<<implementation<<"\",\"frames\":120,\"callerInputMayBeOverwritten\":true,"
      "\"packetsAndCodecOutliveEncoder\":true,\"originalPtsPreserved\":true,\"backpressureExercised\":true,\"nativeQuiescence\":true}\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr<<error.what()<<'\n';return 1;
  }
}
