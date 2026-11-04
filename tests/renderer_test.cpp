#include <doctest/doctest.h>

#include <cstdlib>
#include <memory>

#include "gpu/converter.h"
#include "gpu/device.h"
#include "gpu/texture_pool.h"
#include "gpu/upload.h"
#include "io/uyvy.h"
#include "play/renderer.h"

namespace {
struct Gpu {
  std::unique_ptr<bc::GpuDevice> dev = bc::GpuDevice::create(bc::GpuDevice::Pref::HardwareOrWarp);
  bc::TexturePool pool{*dev};
  bc::Converter conv{*dev, pool};
};

br::DecodedFrame frameFromBgra(const std::vector<uint8_t>& bgra, uint32_t w, uint32_t h) {
  br::DecodedFrame f;
  f.w = w;
  f.h = h;
  f.uyvy.resize(static_cast<size_t>(w) * h * 2);
  bc::bgraToUyvy(bgra.data(), static_cast<int>(w), static_cast<int>(h), f.uyvy.data(),
                 static_cast<int>(w * 2), h >= 720);
  return f;
}

br::DecodedFrame solid(uint32_t w, uint32_t h, uint8_t b, uint8_t g, uint8_t r) {
  std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4);
  for (size_t i = 0; i < bgra.size(); i += 4) {
    bgra[i] = b;
    bgra[i + 1] = g;
    bgra[i + 2] = r;
    bgra[i + 3] = 255;
  }
  return frameFromBgra(bgra, w, h);
}

std::vector<uint8_t> readPixel(bc::Converter& conv, const bc::TextureRef& linear, uint32_t w, uint32_t h,
                               uint32_t x, uint32_t y) {
  const auto bgra = conv.toBgra8(*linear, w, h);
  const auto img = conv.readback(*bgra);
  std::vector<uint8_t> px(4);
  const size_t i = (static_cast<size_t>(y) * img.width + x) * 4;
  for (int c = 0; c < 4; ++c) px[static_cast<size_t>(c)] = img.bgra[i + static_cast<size_t>(c)];
  return px;
}

int channel(const std::vector<uint8_t>& px, int c) { return px[static_cast<size_t>(c)]; }
}  // namespace

TEST_CASE("Renderer: a grey frame renders grey") {
  Gpu g;
  br::Renderer r(*g.dev, g.pool, g.conv, 1920, 1080);
  const auto f = solid(1920, 1080, 128, 128, 128);
  const auto out = r.render(f, br::ZoomRect{});
  const auto px = readPixel(g.conv, out, 1920, 1080, 960, 540);
  for (int c = 0; c < 3; ++c) CHECK(std::abs(channel(px, c) - 128) <= 6);
}

TEST_CASE("Renderer: zoom 2x on the top-left shows the top-left colour") {
  Gpu g;
  br::Renderer r(*g.dev, g.pool, g.conv, 1920, 1080);
  const uint32_t w = 1920, h = 1080;
  std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      const bool left = x < w / 2, top = y < h / 2;
      uint8_t b = 0, gg = 0, rr = 0;
      if (top && left) rr = 220;          // top-left: red
      else if (top) gg = 220;             // top-right: green
      else if (left) b = 220;             // bottom-left: blue
      else { rr = 220; gg = 220; b = 220; }  // bottom-right: white
      const size_t i = (static_cast<size_t>(y) * w + x) * 4;
      bgra[i] = b;
      bgra[i + 1] = gg;
      bgra[i + 2] = rr;
      bgra[i + 3] = 255;
    }
  }
  const auto f = frameFromBgra(bgra, w, h);
  const auto out = r.render(f, br::ZoomRect{0.25f, 0.25f, 2.0f});
  const auto px = readPixel(g.conv, out, 1920, 1080, 960, 540);
  CHECK(channel(px, 2) > 170);  // red
  CHECK(channel(px, 1) < 80);
  CHECK(channel(px, 0) < 80);
}

TEST_CASE("Renderer: a 4:3 input is letterboxed") {
  Gpu g;
  br::Renderer r(*g.dev, g.pool, g.conv, 1920, 1080);
  const auto f = solid(640, 480, 40, 60, 200);
  const auto out = r.render(f, br::ZoomRect{});
  const auto left = readPixel(g.conv, out, 1920, 1080, 60, 540);
  const auto centre = readPixel(g.conv, out, 1920, 1080, 960, 540);
  for (int c = 0; c < 3; ++c) CHECK(channel(left, c) <= 8);
  CHECK(channel(centre, 2) > 150);  // red channel of the coloured picture
  CHECK(channel(centre, 0) < 90);
}

TEST_CASE("Renderer: an NV12 picture renders through the GPU converter") {
  Gpu g;
  br::Renderer r(*g.dev, g.pool, g.conv, 1920, 1080);
  const uint32_t w = 640, h = 360;
  // Limited-range BT.601 red (Y=81, U=90, V=240): a wrong matrix or range shows immediately.
  const std::vector<uint8_t> y(static_cast<size_t>(w) * h, 81);
  std::vector<uint8_t> uv(static_cast<size_t>(w) * h / 2);
  for (size_t i = 0; i < uv.size(); i += 2) {
    uv[i] = 90;    // U
    uv[i + 1] = 240;  // V
  }
  bc::TextureRef tex;
  {
    auto lock = g.dev->lock();
    tex = bc::uploadNv12Planes(*g.dev, g.pool, y.data(), uv.data(), w, h);
  }
  br::DecodedFrame f;
  f.w = w;
  f.h = h;
  f.picture.tex = tex;
  f.picture.layout = bc::PixelLayout::NV12;
  const auto out = r.render(f, br::ZoomRect{});
  const auto px = readPixel(g.conv, out, 1920, 1080, 960, 540);
  CHECK(channel(px, 2) > 200);  // red
  CHECK(channel(px, 1) < 70);
  CHECK(channel(px, 0) < 70);
}

TEST_CASE("Renderer: mix at 0.5 averages") {
  Gpu g;
  br::Renderer r(*g.dev, g.pool, g.conv, 1920, 1080);
  const auto a = g.conv.solid(640, 360, {0.2f, 0.4f, 0.6f, 1.0f});
  const auto b = g.conv.solid(640, 360, {0.8f, 0.4f, 0.2f, 1.0f});
  const auto mixed = r.mix(a, b, 0.5f);
  // The mix happens in linear light, so compare against a solid of the linear average (comparing
  // averaged 8-bit gamma pixels would be a different operation).
  const auto expected = g.conv.solid(640, 360, {0.5f, 0.4f, 0.4f, 1.0f});
  const auto pe = readPixel(g.conv, expected, 640, 360, 320, 180);
  const auto pm = readPixel(g.conv, mixed, 640, 360, 320, 180);
  for (int c = 0; c < 3; ++c) CHECK(std::abs(channel(pm, c) - channel(pe, c)) <= 2);
  // t=0 is a, t=1 is b.
  const auto p0 = readPixel(g.conv, r.mix(a, b, 0.0f), 640, 360, 320, 180);
  const auto pa = readPixel(g.conv, a, 640, 360, 320, 180);
  for (int c = 0; c < 3; ++c) CHECK(p0[c] == pa[c]);
}