#include "x264_thread_budget.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

int main() {
  struct Case { uint32_t width, height, num, den; unsigned processors; int threads; };
  constexpr auto maximum=std::numeric_limits<uint32_t>::max();
  constexpr std::array cases{
    Case{1280,720,30,1,12,2}, Case{1280,720,30000,1001,12,2},
    Case{640,360,30,1,4,2}, Case{1280,720,24,1,4,2},
    Case{1280,720,30,1,0,0}, Case{1280,720,30,1,1,0},
    Case{1280,720,30,1,2,0}, Case{1280,720,30,1,3,0},
    Case{1280,720,31,1,12,0}, Case{1280,720,30001,1000,12,0},
    Case{1280,720,60000,1001,12,0}, Case{1920,1080,30,1,12,0},
    Case{1282,720,30,1,12,0}, Case{1280,722,30,1,12,0},
    Case{360,640,30,1,12,0}, Case{1279,720,30,1,12,0},
    Case{1280,719,30,1,12,0}, Case{0,0,30,1,12,0},
    Case{1280,720,0,1,12,0}, Case{1280,720,30,0,12,0},
    Case{1280,720,maximum,maximum,12,2},
    Case{1280,720,maximum,1,12,0}
  };
  for (const auto &test:cases) {
    if (ness2::x264_slice_thread_budget(test.width,test.height,test.num,test.den,
        test.processors)!=test.threads) {
      std::cerr << "x264 slice policy escaped its qualified workload boundary\n";
      return 1;
    }
  }
  std::cout << "PASS: " << cases.size() << " workload/rate/topology boundaries\n";
  return 0;
}
