#include "store/retention.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <numeric>

namespace br {
namespace {
bool overlaps(const SegmentInfo& s, const ProtectedRange& r) {
  return s.lastTs >= r.inUs && s.firstTs <= r.outUs;
}
}  // namespace

std::vector<StoredSegment> segmentsToDelete(std::vector<StoredSegment> all, const RetentionLimit& limit,
                                            int64_t freeBytes, int64_t minFreeBytes,
                                            std::span<const ProtectedRange> keep) {
  std::sort(all.begin(), all.end(),
            [](const StoredSegment& a, const StoredSegment& b) { return a.info.firstTs < b.info.firstTs; });
  const int64_t newest = all.empty() ? 0 : std::max_element(all.begin(), all.end(), [](const auto& a, const auto& b) {
                                                 return a.info.lastTs < b.info.lastTs;
                                               })->info.lastTs;

  std::vector<bool> removed(all.size(), false);
  std::vector<StoredSegment> out;

  for (;;) {
    int64_t remainingBytes = 0;
    for (size_t i = 0; i < all.size(); ++i) {
      if (removed[i]) continue;
      remainingBytes += all[i].info.bytes;
    }

    bool wantDelete = freeBytes < minFreeBytes;
    if (!wantDelete && limit.bytes && remainingBytes > *limit.bytes) wantDelete = true;
    if (!wantDelete && limit.hours) {
      const auto cutoff = newest - static_cast<int64_t>(*limit.hours * 3'600'000'000.0);
      for (size_t i = 0; i < all.size(); ++i) {
        if (!removed[i] && all[i].info.lastTs <= cutoff) {
          wantDelete = true;
          break;
        }
      }
    }
    if (!wantDelete) break;

    size_t pick = all.size();
    for (size_t i = 0; i < all.size(); ++i) {
      if (removed[i] || all[i].open) continue;
      bool protectedSeg = false;
      for (const auto& r : keep) {
        if (overlaps(all[i].info, r)) {
          protectedSeg = true;
          break;
        }
      }
      if (protectedSeg) continue;
      pick = i;
      break;
    }
    if (pick == all.size()) break;  // nothing may be deleted

    removed[pick] = true;
    freeBytes += all[pick].info.bytes;
    out.push_back(all[pick]);
  }
  return out;
}

void WriteRateMeter::add(int64_t bytes, int64_t nowUs) {
  samples_.push_back(Sample{nowUs, bytes});
  while (!samples_.empty() && nowUs - samples_.front().tsUs > 5'000'000) samples_.erase(samples_.begin());
}

double WriteRateMeter::bytesPerSec(int64_t nowUs) const {
  int64_t sum = 0;
  for (const auto& s : samples_) {
    if (nowUs - s.tsUs <= 5'000'000) sum += s.bytes;
  }
  return static_cast<double>(sum) / 5.0;
}

std::optional<double> secondsLeft(int64_t freeBytes, double writeBytesPerSec) {
  if (writeBytesPerSec <= 0) return std::nullopt;
  return static_cast<double>(freeBytes) / writeBytesPerSec;
}

Preflight preflight(const std::filesystem::path& folder, int inputs, double mbpsPerInput) {
  Preflight p;
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  const auto probe = folder / "br-preflight.tmp";
  {
    std::ofstream f(probe, std::ios::binary | std::ios::trunc);
    if (!f) {
      p.error = "the recording folder is not writable: " + folder.string();
      return p;
    }
    const auto start = std::chrono::steady_clock::now();
    std::vector<char> block(8 * 1024 * 1024, 0x5A);
    int64_t written = 0;
    while (written < 256LL * 1024 * 1024 &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) {
      f.write(block.data(), static_cast<std::streamsize>(block.size()));
      if (!f) break;
      written += static_cast<int64_t>(block.size());
    }
    f.flush();
    if (!f) {
      p.error = "the recording folder became unwritable during the test write";
      return p;
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    p.writeMBps = secs > 0 ? static_cast<double>(written) / 1'000'000.0 / secs : 0;
  }
  std::filesystem::remove(probe, ec);

  ULARGE_INTEGER freeBytes{};
  if (GetDiskFreeSpaceExW(folder.wstring().c_str(), &freeBytes, nullptr, nullptr)) {
    const double neededMBps = std::max(0.001, mbpsPerInput * inputs);
    p.minutesFree = static_cast<double>(freeBytes.QuadPart) / (neededMBps * 1'000'000.0) / 60.0;
    if (p.minutesFree < 30.0) {
      p.warnings.push_back("Only " + std::to_string(static_cast<int>(p.minutesFree)) +
                           " min of recording space left");
    }
    if (p.writeMBps < 1.5 * neededMBps) {
      p.warnings.push_back("The disk writes at " + std::to_string(static_cast<int>(p.writeMBps)) +
                           " MB/s but " + std::to_string(static_cast<int>(neededMBps * 1.5)) +
                           " MB/s is needed");
    }
  }
  p.canRecord = true;
  return p;
}

}  // namespace br