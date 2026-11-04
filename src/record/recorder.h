#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

#include "core/vmx_codec.h"
#include "io/input.h"
#include "store/input_store.h"

namespace br {

struct RecorderStats {
  uint64_t framesIn = 0, framesWritten = 0, dropped = 0;
  int queued = 0;
  double encodeMsAvg = 0;
  double encodeMsP99 = 0;
};

// Encode on its own workers and write in arrival order; pushVideo never blocks capture.
class Recorder {
 public:
  Recorder(InputStore& store, VmxQuality q, int queueDepth = 8, int threads = 2);
  ~Recorder();
  Recorder(const Recorder&) = delete;
  Recorder& operator=(const Recorder&) = delete;
  void pushVideo(const bc::CpuVideoFrame& f, int64_t tsUs);
  void pushCoded(int64_t tsUs, std::vector<uint8_t> vmx);  // OMT pass-through
  // One H.264/HEVC access unit as it arrived (Annex-B); stored without decoding or encoding.
  void pushCodedAu(int64_t tsUs, StoredCodec codec, uint32_t w, uint32_t h, bool keyframe,
                   std::vector<uint8_t> au);
  void pushAudio(std::span<const float> samples, int channels, int sampleRate);
  RecorderStats stats() const;
  void stop();

 private:
  struct Job {
    uint64_t seq = 0;
    int64_t tsUs = 0;
    bool coded = false;  // VMX as received
    bool codedAu = false;  // H.264/HEVC as received
    StoredCodec codec = StoredCodec::Vmx;
    bool keyframe = true;
    uint32_t w = 0, h = 0;
    int stride = 0;
    bc::CpuPixel format = bc::CpuPixel::UYVY;
    std::vector<uint8_t> video;
    std::vector<float> audio;
  };
  struct Done {
    EncodedFrame frame;
    bool coded = false;
    bool codedAu = false;
    StoredCodec codec = StoredCodec::Vmx;
    bool keyframe = true;
    std::vector<uint8_t> raw;
    int64_t tsUs = 0;
    std::vector<float> audio;
    uint32_t w = 0, h = 0;
  };

  void worker();
  void writerLoop();

  InputStore& store_;
  VmxQuality quality_;
  int queueDepth_;
  int threadCount_;

  mutable std::mutex m_;
  std::deque<Job> queue_;
  std::vector<float> pendingAudio_;
  bool stopping_ = false;
  uint64_t nextSeq_ = 0;
  uint64_t nextWrite_ = 0;
  uint64_t framesIn_ = 0, framesWritten_ = 0, dropped_ = 0;
  bool awaitKeyframe_ = false;  // a queued AU was dropped: wait for a keyframe to restart the GOP
  double encodeMsSum_ = 0;
  uint64_t encodeCount_ = 0;
  mutable std::vector<double> encodeTimes_;
  int inflight_ = 0;
  std::condition_variable cv_;
  std::condition_variable writeCv_;
  std::map<uint64_t, Done> done_;

  std::vector<std::thread> workers_;
  std::thread writer_;
};

}  // namespace br