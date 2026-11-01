#include "play/renderer.h"

#include <algorithm>
#include <stdexcept>

#include "gpu/image_io.h"
#include "io/uyvy.h"
#include "mix_linear.h"

namespace br {
namespace {
uint32_t even(uint32_t v) { return v & ~1u; }
}  // namespace

Renderer::Renderer(bc::GpuDevice& gpu, bc::TexturePool& pool, bc::Converter& conv, uint32_t outW,
                   uint32_t outH)
    : gpu_(gpu), pool_(pool), conv_(conv), outW_(even(outW)), outH_(even(outH)) {
  const D3D11_BUFFER_DESC cb{16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
  if (FAILED(gpu_.device()->CreateBuffer(&cb, nullptr, &mixCb_))) throw std::runtime_error("mix buffer failed");
  if (FAILED(gpu_.device()->CreateComputeShader(g_mix_linear, sizeof(g_mix_linear), nullptr, &mixCs_))) {
    throw std::runtime_error("mix shader failed");
  }
}

bc::TextureRef Renderer::render(const DecodedFrame& f, ZoomRect z) {
  const uint32_t w = even(f.w), h = even(f.h);
  const auto bgra = bc::uyvyToBgra(f.uyvy.data(), static_cast<int>(w), static_cast<int>(h),
                                   static_cast<int>(w * 2), h >= 720);
  const auto uploaded = conv_.uploadBgra8(bgra.data(), w, h, w * 4);
  auto linear = conv_.toLinear(*uploaded, bc::PixelLayout::BGRA8, true);

  const float scale = std::clamp(z.scale, 1.0f, 4.0f);
  if (scale > 1.001f) {
    const float rw = 1.0f / scale;
    const float x = std::clamp(z.cx - rw / 2.0f, 0.0f, 1.0f - rw);
    const float y = std::clamp(z.cy - rw / 2.0f, 0.0f, 1.0f - rw);
    linear = conv_.crop(*linear, x, y, rw, rw);
  }
  return conv_.fit(*linear, outW_, outH_);
}

bc::TextureRef Renderer::mix(const bc::TextureRef& a, const bc::TextureRef& b, float t) {
  if (!a || !b) return a ? a : b;
  if (a->width != b->width || a->height != b->height) return t < 0.5f ? a : b;
  auto out = pool_.acquire(a->width, a->height, bc::Converter::kLinearFormat,
                           D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
  const float params[4] = {std::clamp(t, 0.0f, 1.0f), 0, 0, 0};
  auto lock = gpu_.lock();
  auto* ctx = gpu_.context();
  ctx->UpdateSubresource(mixCb_.Get(), 0, nullptr, params, 0, 0);
  ID3D11ShaderResourceView* srvs[2] = {a->srv.Get(), b->srv.Get()};
  ID3D11UnorderedAccessView* uav = out->uav.Get();
  ID3D11Buffer* cb = mixCb_.Get();
  ctx->CSSetShader(mixCs_.Get(), nullptr, 0);
  ctx->CSSetShaderResources(0, 2, srvs);
  ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
  ctx->CSSetConstantBuffers(0, 1, &cb);
  ctx->Dispatch((a->width + 7) / 8, (a->height + 7) / 8, 1);
  ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
  ID3D11UnorderedAccessView* noUav = nullptr;
  ctx->CSSetShaderResources(0, 2, none);
  ctx->CSSetUnorderedAccessViews(0, 1, &noUav, nullptr);
  ctx->CSSetShader(nullptr, nullptr, 0);
  return out;
}

bc::CpuImage Renderer::toCpu(const bc::TextureRef& linear) {
  if (!linear) return {};
  const auto bgra = conv_.toBgra8(*linear, outW_, outH_);
  return conv_.readback(*bgra);
}

bc::TextureRef Renderer::slate(const std::string& imagePath) {
  if (!imagePath.empty()) {
    try {
      const auto image = bc::loadImageBgra(imagePath);
      if (image.width && image.height) {
        const auto uploaded = conv_.uploadBgra8(image.bgra.data(), image.width, image.height, image.width * 4);
        const auto linear = conv_.toLinear(*uploaded, bc::PixelLayout::BGRA8, true);
        return conv_.fit(*linear, outW_, outH_);
      }
    } catch (const std::exception&) {
      // an unreadable slate is black, never an output failure
    }
  }
  return conv_.solid(outW_, outH_, {0.0f, 0.0f, 0.0f, 1.0f});
}

}  // namespace br