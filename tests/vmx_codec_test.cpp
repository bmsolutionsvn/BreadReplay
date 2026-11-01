#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "core/vmx_codec.h"

namespace {
// A frame plus the storage its data pointer refers to.
struct Frame {
  std::vector<uint8_t> storage;
  bc::CpuVideoFrame frame;

  Frame(uint32_t w, uint32_t h, bc::CpuPixel format) {
    storage.resize(static_cast<size_t>(w) * h * 4);
    frame.data = storage.data();
    frame.stride = static_cast<int>(w * (format == bc::CpuPixel::NV12 ? 1 : 4));
    frame.width = w;
    frame.height = h;
    frame.format = format;
    frame.fullRange = false;
    frame.ptsUs = 0;
    frame.rate = bc::Rate{60, 1};
  }
};

Frame makeUyvyGradient(uint32_t w, uint32_t h) {
  Frame f(w, h, bc::CpuPixel::UYVY);
  f.frame.stride = static_cast<int>(w * 2);
  f.storage.resize(static_cast<size_t>(f.frame.stride) * h);
  f.frame.data = f.storage.data();
  for (uint32_t y = 0; y < h; ++y) {
    uint8_t* row = f.storage.data() + static_cast<size_t>(y) * f.frame.stride;
    for (uint32_t x = 0; x < w; x += 2) {
      const uint8_t luma = static_cast<uint8_t>(16 + (219 * x) / (w - 1));
      row[x * 2 + 1] = luma;
      row[x * 2 + 3] = luma;
      row[x * 2 + 0] = 128;
      row[x * 2 + 2] = 128;
    }
  }
  return f;
}

Frame makeBgraBlocks(uint32_t w, uint32_t h) {
  Frame f(w, h, bc::CpuPixel::BGRA);
  for (uint32_t y = 0; y < h; ++y) {
    uint8_t* row = f.storage.data() + static_cast<size_t>(y) * f.frame.stride;
    for (uint32_t x = 0; x < w; ++x) {
      const uint32_t bx = x / 16, by = y / 16;
      row[x * 4 + 0] = static_cast<uint8_t>((bx * 37 + by * 11) & 0xFF);
      row[x * 4 + 1] = static_cast<uint8_t>((bx * 13 + by * 41) & 0xFF);
      row[x * 4 + 2] = static_cast<uint8_t>((bx * 29 + by * 7) & 0xFF);
      row[x * 4 + 3] = 255;
    }
  }
  return f;
}

Frame makeBgraGradient(uint32_t w, uint32_t h) {
  Frame f(w, h, bc::CpuPixel::BGRA);
  for (uint32_t y = 0; y < h; ++y) {
    uint8_t* row = f.storage.data() + static_cast<size_t>(y) * f.frame.stride;
    for (uint32_t x = 0; x < w; ++x) {
      row[x * 4 + 0] = static_cast<uint8_t>((255 * x) / (w - 1));
      row[x * 4 + 1] = static_cast<uint8_t>((255 * y) / (h - 1));
      row[x * 4 + 2] = 96;
      row[x * 4 + 3] = 255;
    }
  }
  return f;
}

Frame makeNv12Gradient(uint32_t w, uint32_t h) {
  Frame f(w, h, bc::CpuPixel::NV12);
  f.frame.stride = static_cast<int>(w);
  const size_t ySize = static_cast<size_t>(w) * h;
  f.storage.resize(ySize + ySize / 2);
  f.frame.data = f.storage.data();
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) f.storage[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(16 + (219 * x) / (w - 1));
  }
  std::memset(f.storage.data() + ySize, 128, ySize / 2);
  return f;
}

double psnrLuma(std::span<const uint8_t> a, std::span<const uint8_t> b, uint32_t w, uint32_t h) {
  double se = 0;
  size_t n = 0;
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; x += 2) {
      const size_t i = static_cast<size_t>(y) * w * 2 + x * 2 + 1;
      const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
      se += d * d;
      ++n;
    }
  }
  const double mse = se / static_cast<double>(n);
  CHECK(mse > 0);
  return 10.0 * std::log10(255.0 * 255.0 / mse);
}
}  // namespace

TEST_CASE("VmxCodec: UYVY round trip keeps the picture") {
  auto src = makeUyvyGradient(1920, 1080);
  br::VmxEncoder enc(1920, 1080, br::VmxQuality::SQ);
  const br::EncodedFrame e = enc.encode(src.frame);
  REQUIRE_FALSE(e.bytes.empty());

  br::VmxDecoder dec(1920, 1080);
  std::vector<uint8_t> out(static_cast<size_t>(1920) * 1080 * 2);
  dec.decodeUYVY(e.bytes, out.data(), 1920 * 2);

  CHECK(psnrLuma(src.storage, out, 1920, 1080) >= 38.0);
}

TEST_CASE("VmxCodec: preview decodes from the prefix alone") {
  auto src = makeBgraBlocks(1920, 1080);
  br::VmxEncoder enc(1920, 1080, br::VmxQuality::SQ);
  const br::EncodedFrame e = enc.encode(src.frame);
  REQUIRE(e.previewLen > 0);
  REQUIRE(e.previewLen < e.bytes.size());
  CHECK(br::VmxDecoder::previewSize(1920, 1080) == br::Size{240, 135});

  br::VmxDecoder dec(1920, 1080);
  std::vector<uint8_t> preview(240 * 135 * 4);
  dec.decodePreviewBGRA(std::span(e.bytes.data(), e.previewLen), preview.data(), 240 * 4);

  // The source's 8x8 blocks are uniform, so an 8x8 box downscale is the block colour itself.
  // VMX's preview is a compressed 1/8 decode, so compare the mean error (chroma edges bleed).
  double sum[3] = {0, 0, 0};
  for (uint32_t by = 0; by < 135; ++by) {
    for (uint32_t bx = 0; bx < 240; ++bx) {
      const uint8_t* p = &preview[(static_cast<size_t>(by) * 240 + bx) * 4];
      const uint8_t* s = &src.storage[(static_cast<size_t>(by * 8) * 1920 + bx * 8) * 4];
      for (int c = 0; c < 3; ++c) sum[c] += std::abs(static_cast<int>(p[c]) - static_cast<int>(s[c]));
    }
  }
  const double count = 240.0 * 135.0;
  for (int c = 0; c < 3; ++c) CHECK(sum[c] / count < 8.0);
}

TEST_CASE("VmxCodec: preview orientation") {
  const uint32_t w = 1920, h = 1080;
  Frame src(w, h, bc::CpuPixel::BGRA);
  for (uint32_t y = 0; y < h; ++y) {
    uint8_t* row = src.storage.data() + static_cast<size_t>(y) * src.frame.stride;
    for (uint32_t x = 0; x < w; ++x) {
      row[x * 4 + 0] = 0;
      row[x * 4 + 1] = static_cast<uint8_t>(y / 8);  // G grows down the picture (0..134)
      row[x * 4 + 2] = static_cast<uint8_t>(x / 8);  // R grows to the right (0..239)
      row[x * 4 + 3] = 255;
    }
  }
  br::VmxEncoder enc(w, h, br::VmxQuality::SQ);
  const auto e = enc.encode(src.frame);
  br::VmxDecoder dec(w, h);
  std::vector<uint8_t> preview(240 * 135 * 4);
  dec.decodePreviewBGRA(std::span(e.bytes.data(), e.previewLen), preview.data(), 240 * 4);
  const auto px = [&](uint32_t x, uint32_t y, int c) { return preview[(static_cast<size_t>(y) * 240 + x) * 4 + c]; };
  CHECK(std::abs(int(px(0, 0, 2)) - 0) < 12);
  CHECK(std::abs(int(px(0, 0, 1)) - 0) < 12);
  CHECK(std::abs(int(px(239, 134, 2)) - 239) < 12);
  CHECK(std::abs(int(px(239, 134, 1)) - 134) < 12);
}

TEST_CASE("VmxCodec: NV12 and BGRA inputs encode") {
  SUBCASE("NV12") {
    auto src = makeNv12Gradient(1920, 1080);
    br::VmxEncoder enc(1920, 1080, br::VmxQuality::SQ);
    const br::EncodedFrame e = enc.encode(src.frame);
    br::VmxDecoder dec(1920, 1080);
    std::vector<uint8_t> out(static_cast<size_t>(1920) * 1080 * 2);
    dec.decodeUYVY(e.bytes, out.data(), 1920 * 2);
    // The source's luma plane is limited-range Y; the decoded UYVY carries the same values.
    std::vector<uint8_t> ref(static_cast<size_t>(1920) * 1080 * 2, 128);
    for (uint32_t y = 0; y < 1080; ++y)
      for (uint32_t x = 0; x < 1920; x += 2) ref[static_cast<size_t>(y) * 3840 + x * 2 + 1] = src.storage[static_cast<size_t>(y) * 1920 + x];
    CHECK(psnrLuma(ref, out, 1920, 1080) >= 35.0);
  }
  SUBCASE("BGRA") {
    auto src = makeBgraGradient(1920, 1080);
    br::VmxEncoder enc(1920, 1080, br::VmxQuality::SQ);
    const br::EncodedFrame e = enc.encode(src.frame);
    br::VmxDecoder dec(1920, 1080);
    std::vector<uint8_t> out(static_cast<size_t>(1920) * 1080 * 2);
    dec.decodeUYVY(e.bytes, out.data(), 1920 * 2);
    // BT.709 limited-range luma from the source BGRA.
    std::vector<uint8_t> ref(static_cast<size_t>(1920) * 1080 * 2, 128);
    for (uint32_t y = 0; y < 1080; ++y) {
      for (uint32_t x = 0; x < 1920; x += 2) {
        const uint8_t* s = &src.storage[(static_cast<size_t>(y) * 1920 + x) * 4];
        ref[static_cast<size_t>(y) * 3840 + x * 2 + 1] = static_cast<uint8_t>(
            16.0 + 0.183 * s[2] + 0.614 * s[1] + 0.062 * s[0] + 0.5);
      }
    }
    CHECK(psnrLuma(ref, out, 1920, 1080) >= 35.0);
  }
}