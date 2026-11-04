#pragma once
#include <atomic>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/vmx_codec.h"
#include "gpu/converter.h"
#include "io/d3d11va.h"
#include "io/input.h"
#include "store/input_store.h"

namespace br {

struct DecodedFrame {
  int64_t tsUs = 0;
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> uyvy;  // VMX frames: CPU UYVY
  bc::VideoFrameIn picture;   // H.264/HEVC frames: NV12/P010 on the GPU
  std::vector<float> audio;
  int channels = 2, sampleRate = 48000;
  bool inGap = false;
};

struct PreviewImage {
  int64_t tsUs = 0;
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> bgra;
};

class FrameSource {
 public:
  FrameSource(std::map<std::string, const InputStore*> stores, bc::GpuDevice* gpu = nullptr,
              bc::TexturePool* pool = nullptr, int threads = 4, size_t cacheFrames = 32);
  // lane keeps two players (Output and Preview) from resetting each other's decoder state.
  std::shared_ptr<const DecodedFrame> get(const std::string& inputId, int64_t tsUs,
                                          size_t lane = 0);  // nullptr if none
  void prefetch(const std::string& inputId, int64_t fromUs, int64_t stepUs, int count);
  std::shared_ptr<const PreviewImage> preview(const std::string& inputId, int64_t tsUs);
  uint64_t diskReads() const { return diskReads_.load(); }
  uint64_t decodes() const { return decodes_.load(); }  // pictures decoded

 private:
  struct Key {
    std::string input;
    int64_t tsUs;
    bool operator<(const Key& o) const {
      return input < o.input || (input == o.input && tsUs < o.tsUs);
    }
  };
  struct Dec {
    std::unique_ptr<VmxDecoder> decoder;
    uint32_t w = 0, h = 0;
    StoredCodec codec = StoredCodec::Vmx;
    std::unique_ptr<bc::GpuDecoder> hx;
    size_t seg = 0, fedIndex = SIZE_MAX;
    bool primed = false;
    std::unique_ptr<bc::GpuDecoder> preview;
    StoredCodec previewCodec = StoredCodec::Vmx;
    uint32_t pw = 0, ph = 0;
    int64_t previewKeyTs = INT64_MIN;
    std::shared_ptr<const PreviewImage> previewImage;
  };

  void ensureDecoder(const std::string& id, size_t lane, uint32_t w, uint32_t h);
  void ensureHx(Dec& d, StoredCodec codec, uint32_t w, uint32_t h);
  std::shared_ptr<const DecodedFrame> getCompressed(const std::string& id, const InputStore& store,
                                                    const FrameRef& ref, size_t lane);
  void cachePicture(const std::string& id, const InputStore& store, bc::VideoFrameIn pic, uint32_t w,
                    uint32_t h);
  static size_t bytesOf(const DecodedFrame& f);
  void evict();
  void touch(const Key& key);

  std::map<std::string, const InputStore*> stores_;
  bc::GpuDevice* gpu_ = nullptr;
  bc::TexturePool* pool_ = nullptr;
  std::unique_ptr<bc::Converter> conv_;
  size_t cacheFrames_;

  std::mutex m_;
  std::map<Key, std::shared_ptr<const DecodedFrame>> cache_;
  std::list<Key> lru_;  // front = most recently used
  size_t cacheBytes_ = 0;
  std::map<std::pair<std::string, size_t>, Dec> decoders_;
  std::atomic<uint64_t> diskReads_{0};
  std::atomic<uint64_t> decodes_{0};
};

}  // namespace br