#include "core/timecode.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace br {
namespace {
struct ClockBase {
  LARGE_INTEGER freq{};
  LARGE_INTEGER start{};
  int64_t epochUs = 0;
  ClockBase() {
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    epochUs = std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
  }
};
}  // namespace

int64_t nowUs() {
  static const ClockBase base;
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  const int64_t us =
      base.epochUs + (c.QuadPart - base.start.QuadPart) * 1'000'000 / base.freq.QuadPart;
  // One shared clock: a QPC read that steps back (a different core's counter) must never make
  // timestamps go backwards, or a mark would land before the frame it followed.
  static std::atomic<int64_t> last{0};
  int64_t prev = last.load(std::memory_order_relaxed);
  while (us > prev && !last.compare_exchange_weak(prev, us, std::memory_order_relaxed)) {
  }
  return last.load(std::memory_order_relaxed);
}

std::string formatTimecode(int64_t epochUs, bc::Rate rate) {
  int64_t secs = epochUs / 1'000'000;
  int64_t rem = epochUs % 1'000'000;
  if (rem < 0) {
    rem += 1'000'000;
    --secs;
  }
  const std::time_t t = static_cast<std::time_t>(secs);
  std::tm local{};
  localtime_s(&local, &t);
  const int fps = static_cast<int>(std::llround(rate.fps()));
  int frame = fps > 0 ? static_cast<int>(rem * fps / 1'000'000) : 0;
  if (frame >= fps) frame = fps - 1;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec, frame);
  return buf;
}

}  // namespace br