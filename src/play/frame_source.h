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
#include "store/input_store.h"

namespace br {

struct DecodedFrame {
  int64_t tsUs = 0;
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> uyvy;
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
  FrameSource(std::map<std::string, const InputStore*> stores, int threads = 4, size_t cacheFrames = 32);
  std::shared_ptr<const DecodedFrame> get(const std::string& inputId, int64_t tsUs);  // nullptr if none
  void prefetch(const std::string& inputId, int64_t fromUs, int64_t stepUs, int count);
  std::shared_ptr<const PreviewImage> preview(const std::string& inputId, int64_t tsUs);
  uint64_t diskReads() const { return diskReads_.load(); }

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
  };

  void ensureDecoder(const std::string& id, uint32_t w, uint32_t h);
  void touch(const Key& key);

  std::map<std::string, const InputStore*> stores_;
  size_t cacheFrames_;

  std::mutex m_;
  std::map<Key, std::shared_ptr<const DecodedFrame>> cache_;
  std::list<Key> lru_;  // front = most recently used
  std::map<std::string, Dec> decoders_;
  std::atomic<uint64_t> diskReads_{0};
};

}  // namespace br