#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include "core/rate.h"
#include "io/input.h"

namespace br {

enum class VmxQuality { LQ, SQ, HQ };

struct EncodedFrame {
  std::vector<uint8_t> bytes;
  uint32_t previewLen = 0;
};

struct Size {
  uint32_t w = 0, h = 0;
  bool operator==(const Size&) const = default;
};

// One VMX instance per object: libvmx is not thread-safe across a shared instance.
class VmxEncoder {
 public:
  VmxEncoder(uint32_t w, uint32_t h, VmxQuality q);
  ~VmxEncoder();
  VmxEncoder(const VmxEncoder&) = delete;
  VmxEncoder& operator=(const VmxEncoder&) = delete;
  EncodedFrame encode(const bc::CpuVideoFrame& f);

 private:
  struct Impl;
  Impl* impl_;
};

class VmxDecoder {
 public:
  VmxDecoder(uint32_t w, uint32_t h);
  ~VmxDecoder();
  VmxDecoder(const VmxDecoder&) = delete;
  VmxDecoder& operator=(const VmxDecoder&) = delete;
  void decodeUYVY(std::span<const uint8_t> frame, uint8_t* dst, int stride);
  void decodePreviewBGRA(std::span<const uint8_t> prefix, uint8_t* dst, int stride);
  static Size previewSize(uint32_t w, uint32_t h);

 private:
  struct Impl;
  Impl* impl_;
};

// Length of the prefix of an encoded frame that decodes its 1/8 preview (throws when invalid).
uint32_t vmxPreviewLength(std::span<const uint8_t> frame, uint32_t w, uint32_t h);

}  // namespace br