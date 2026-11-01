#pragma once
#include <cstdint>
#include <string>

#include "events/event_store.h"
#include "gpu/converter.h"
#include "gpu/device.h"
#include "gpu/texture_pool.h"
#include "play/frame_source.h"

namespace br {

// Uploads a decoded UYVY frame, applies the zoom crop and fits it into the show raster.
class Renderer {
 public:
  Renderer(bc::GpuDevice&, bc::TexturePool&, bc::Converter&, uint32_t outW, uint32_t outH);
  bc::TextureRef render(const DecodedFrame& f, ZoomRect z);  // linear RGBA16F, outW x outH
  bc::TextureRef mix(const bc::TextureRef& a, const bc::TextureRef& b, float t);  // t=0 -> a
  bc::TextureRef slate(const std::string& imagePath);  // black when path empty/unreadable
  bc::CpuImage toCpu(const bc::TextureRef& linear);    // BGRA8 readback at outW x outH
  uint32_t outW() const { return outW_; }
  uint32_t outH() const { return outH_; }

 private:
  bc::GpuDevice& gpu_;
  bc::TexturePool& pool_;
  bc::Converter& conv_;
  uint32_t outW_, outH_;
  Microsoft::WRL::ComPtr<ID3D11ComputeShader> mixCs_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> mixCb_;
};

}  // namespace br