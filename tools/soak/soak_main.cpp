#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "core/timecode.h"
#include "core/vmx_codec.h"
#include "play/frame_source.h"
#include "record/recorder.h"
#include "store/input_store.h"

namespace {
struct Options {
  int inputs = 8;
  int minutes = 10;
  uint32_t w = 1920, h = 1080;
  int rate = 60;
  br::VmxQuality quality = br::VmxQuality::SQ;
  std::filesystem::path folder = "D:\\BrSoak";
};

// A moving UYVY pattern: a bright bar whose position follows the frame number.
void makePattern(std::vector<uint8_t>& out, uint32_t w, uint32_t h, uint64_t frame) {
  out.resize(static_cast<size_t>(w) * h * 2);
  const uint32_t bar = static_cast<uint32_t>(frame * 7) % w;
  for (uint32_t y = 0; y < h; ++y) {
    uint8_t* row = out.data() + static_cast<size_t>(y) * w * 2;
    for (uint32_t x = 0; x < w; x += 2) {
      const uint8_t luma = static_cast<uint8_t>(32 + ((x > bar && x < bar + 200) ? 180 : (x * 200) / w));
      row[x * 2 + 1] = luma;
      row[x * 2 + 3] = luma;
      row[x * 2 + 0] = static_cast<uint8_t>(128 + (y * 31) % 60);
      row[x * 2 + 2] = 128;
    }
  }
}

std::string human(double mbps) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.1f MB/s, %.0f GB/h", mbps, mbps * 3.6);
  return buf;
}
}  // namespace

int main(int argc, char** argv) {
  Options o;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string k = argv[i], v = argv[i + 1];
    if (k == "--inputs") o.inputs = std::stoi(v);
    else if (k == "--minutes") o.minutes = std::stoi(v);
    else if (k == "--size") {
      const auto x = v.find('x');
      o.w = static_cast<uint32_t>(std::stoi(v.substr(0, x)));
      o.h = static_cast<uint32_t>(std::stoi(v.substr(x + 1)));
    } else if (k == "--rate") o.rate = std::stoi(v);
    else if (k == "--quality") o.quality = v == "LQ" ? br::VmxQuality::LQ : (v == "HQ" ? br::VmxQuality::HQ : br::VmxQuality::SQ);
    else if (k == "--folder") o.folder = v;
    else {
      std::fprintf(stderr, "unknown argument %s\n", k.c_str());
      return 2;
    }
  }
  if (o.inputs < 1 || o.inputs > 16 || o.minutes < 1 || o.w < 16 || o.h < 16 || o.rate < 1) {
    std::fprintf(stderr, "usage: br_soak --inputs N --minutes M --size WxH --rate R --quality LQ|SQ|HQ --folder D\n");
    return 2;
  }

  const auto runDir = o.folder / ("soak-" + std::to_string(std::time(nullptr)));
  std::filesystem::create_directories(runDir);

  std::vector<std::unique_ptr<br::InputStore>> stores;
  std::vector<std::unique_ptr<br::Recorder>> recorders;
  std::vector<std::vector<uint8_t>> frames(static_cast<size_t>(o.inputs));
  for (int i = 0; i < o.inputs; ++i) {
    br::SegmentHeader h;
    h.inputId = "in" + std::to_string(i);
    h.width = o.w;
    h.height = o.h;
    h.rate = bc::Rate{o.rate, 1};
    stores.push_back(std::make_unique<br::InputStore>(runDir / h.inputId, h));
    recorders.push_back(std::make_unique<br::Recorder>(*stores.back(), o.quality, 8, 2));
  }

  std::atomic<bool> run{true};
  const int64_t periodUs = static_cast<int64_t>(1'000'000.0 / o.rate + 0.5);
  const int64_t startUs = br::nowUs();
  const int64_t endUs = startUs + static_cast<int64_t>(o.minutes) * 60 * 1'000'000;
  const int64_t bytesAtStart = 0;
  (void)bytesAtStart;

  std::vector<std::thread> writers;
  for (int i = 0; i < o.inputs; ++i) {
    writers.emplace_back([&, i] {
      uint64_t frame = 0;
      auto next = std::chrono::steady_clock::now();
      while (run && br::nowUs() < endUs) {
        makePattern(frames[static_cast<size_t>(i)], o.w, o.h, frame + static_cast<uint64_t>(i) * 101);
        bc::CpuVideoFrame f;
        f.data = frames[static_cast<size_t>(i)].data();
        f.stride = static_cast<int>(o.w * 2);
        f.width = o.w;
        f.height = o.h;
        f.format = bc::CpuPixel::UYVY;
        recorders[static_cast<size_t>(i)]->pushVideo(f, br::nowUs());
        ++frame;
        next += std::chrono::microseconds(periodUs);
        std::this_thread::sleep_until(next);
      }
    });
  }

  std::map<std::string, const br::InputStore*> sourceMap;
  for (int i = 0; i < o.inputs; ++i) sourceMap["in" + std::to_string(i)] = stores[static_cast<size_t>(i)].get();
  br::FrameSource reader(std::move(sourceMap));
  std::atomic<uint64_t> readerMisses{0}, readerReads{0};

  auto readerLoop = [&](double speed) {
    std::mt19937 rng(static_cast<uint32_t>(speed * 1000));
    while (run && br::nowUs() < endUs) {
      // A random 12 s range *speed across the inputs, older than 12 s so it is on disk.
      const int64_t newest = [&] {
        int64_t n = 0;
        for (auto& s : stores) n = std::max(n, s->newestTs());
        return n;
      }();
      if (newest - startUs > 13'000'000) {
        std::uniform_int_distribution<int64_t> pick(0, newest - startUs - 12'000'000);
        const int64_t from = startUs + pick(rng);
        const int input = static_cast<int>(rng() % static_cast<size_t>(o.inputs));
        const int64_t step = static_cast<int64_t>(periodUs * speed);
        for (int k = 0; k < 24 && run; ++k) {
          const auto f = reader.get("in" + std::to_string(input), from + k * step);
          ++readerReads;
          if (!f) ++readerMisses;
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  };
  std::thread r100(readerLoop, 1.0), r50(readerLoop, 0.5);

  const auto t0 = std::chrono::steady_clock::now();
  while (br::nowUs() < endUs && run) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    const int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
                                             std::chrono::steady_clock::now() - t0)
                                             .count());
    std::printf("\r%02d:%02d", elapsed / 60, elapsed % 60);
    std::fflush(stdout);
  }
  run = false;
  for (auto& t : writers) t.join();
  r100.join();
  r50.join();

  int64_t totalBytes = 0;
  uint64_t totalDropped = 0;
  for (int i = 0; i < o.inputs; ++i) {
    recorders[static_cast<size_t>(i)]->stop();
    const auto s = recorders[static_cast<size_t>(i)]->stats();
    totalBytes += stores[static_cast<size_t>(i)]->bytesOnDisk();
    totalDropped += s.dropped;
    std::printf("\nin%d: frames %llu written %llu dropped %llu queued %d encode %.1f ms avg, %.1f ms p99\n", i,
                static_cast<unsigned long long>(s.framesIn), static_cast<unsigned long long>(s.framesWritten),
                static_cast<unsigned long long>(s.dropped), s.queued, s.encodeMsAvg, s.encodeMsP99);
  }
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  const double mbps = secs > 0 ? static_cast<double>(totalBytes) / 1'000'000.0 / secs : 0;
  std::printf("\ntotal %s; reader reads %llu, misses %llu\n", human(mbps).c_str(),
              static_cast<unsigned long long>(readerReads.load()),
              static_cast<unsigned long long>(readerMisses.load()));
  std::printf("folder %s\n", runDir.string().c_str());
  return totalDropped == 0 ? 0 : 1;
}