#include "core/vmx_codec.h"

#include <vmxcodec.h>

#include <cstring>

namespace br {
namespace {
VMX_PROFILE toProfile(VmxQuality q) {
  switch (q) {
    case VmxQuality::LQ: return VMX_PROFILE_LQ;
    case VmxQuality::HQ: return VMX_PROFILE_HQ;
    default: return VMX_PROFILE_SQ;
  }
}

void check(VMX_ERR e, const char* what) {
  if (e != VMX_ERR_OK) throw std::runtime_error(std::string("VMX ") + what + " failed: " + std::to_string(e));
}

VMX_INSTANCE* create(uint32_t w, uint32_t h, VmxQuality q) {
  auto* inst = VMX_Create(VMX_SIZE{static_cast<int>(w), static_cast<int>(h)}, toProfile(q), VMX_COLORSPACE_BT709);
  if (!inst) throw std::runtime_error("VMX_Create failed for " + std::to_string(w) + "x" + std::to_string(h));
  return inst;
}

// libvmx takes mutable pointers but never writes the source picture.
BYTE* mut(const uint8_t* p) { return const_cast<BYTE*>(p); }
}  // namespace

struct VmxEncoder::Impl {
  VMX_INSTANCE* inst = nullptr;
  uint32_t w = 0, h = 0;
  ~Impl() {
    if (inst) VMX_Destroy(inst);
  }
};

VmxEncoder::VmxEncoder(uint32_t w, uint32_t h, VmxQuality q) : impl_(new Impl) {
  impl_->w = w;
  impl_->h = h;
  impl_->inst = create(w, h, q);
}

VmxEncoder::~VmxEncoder() { delete impl_; }

EncodedFrame VmxEncoder::encode(const bc::CpuVideoFrame& f) {
  VMX_INSTANCE* in = impl_->inst;
  if (f.width != impl_->w || f.height != impl_->h) throw std::runtime_error("VMX frame size changed without a new encoder");
  switch (f.format) {
    case bc::CpuPixel::UYVY:
      check(VMX_EncodeUYVY(in, mut(f.data), f.stride, 0), "UYVY encode");
      break;
    case bc::CpuPixel::NV12:
      check(VMX_EncodeNV12(in, mut(f.data), f.stride, mut(f.data + static_cast<size_t>(f.stride) * f.height),
                           f.stride, 0),
            "NV12 encode");
      break;
    case bc::CpuPixel::BGRA:
      check(VMX_EncodeBGRA(in, mut(f.data), f.stride, 0), "BGRA encode");
      break;
  }
  EncodedFrame out;
  out.bytes.resize(static_cast<size_t>(impl_->w) * impl_->h * 4 + 4096);
  const int n = VMX_SaveTo(in, out.bytes.data(), static_cast<int>(out.bytes.size()));
  if (n <= 0) throw std::runtime_error("VMX_SaveTo failed");
  out.bytes.resize(static_cast<size_t>(n));
  const int preview = VMX_GetEncodedPreviewLength(in);
  if (preview <= 0 || preview > n) throw std::runtime_error("VMX preview length is invalid");
  out.previewLen = static_cast<uint32_t>(preview);
  return out;
}

struct VmxDecoder::Impl {
  VMX_INSTANCE* inst = nullptr;
  ~Impl() {
    if (inst) VMX_Destroy(inst);
  }
};

VmxDecoder::VmxDecoder(uint32_t w, uint32_t h) : impl_(new Impl) { impl_->inst = create(w, h, VmxQuality::SQ); }

VmxDecoder::~VmxDecoder() { delete impl_; }

void VmxDecoder::decodeUYVY(std::span<const uint8_t> frame, uint8_t* dst, int stride) {
  if (frame.size() > static_cast<size_t>(INT32_MAX)) throw std::runtime_error("VMX frame is too large");
  check(VMX_LoadFrom(impl_->inst, mut(frame.data()), static_cast<int>(frame.size())), "load");
  check(VMX_DecodeUYVY(impl_->inst, dst, stride), "UYVY decode");
}

void VmxDecoder::decodePreviewBGRA(std::span<const uint8_t> prefix, uint8_t* dst, int stride) {
  check(VMX_LoadFrom(impl_->inst, mut(prefix.data()), static_cast<int>(prefix.size())), "preview load");
  check(VMX_DecodePreviewBGRA(impl_->inst, dst, stride), "preview decode");
}

Size VmxDecoder::previewSize(uint32_t w, uint32_t h) {
  uint32_t pw = (w / 8) & ~1u;
  if (pw < 2) pw = 2;
  return Size{pw, h / 8};
}

uint32_t vmxPreviewLength(std::span<const uint8_t> frame, uint32_t w, uint32_t h) {
  (void)w;
  (void)h;
  // The frame is [header][4-byte length + DC per slice]...[AC per slice]; the preview needs only
  // the slices' DC part, which is exactly what VMX_GetEncodedPreviewLength reports after encoding.
  // (libvmx's own accessor reads state LoadFrom does not restore, so parse the bytes here.)
  if (frame.size() < 3) throw std::runtime_error("VMX frame is too short");
  size_t pos = 0;
  int slices = 0;
  if (frame[0] == VMX_CODEC_FORMAT_EXTENDED) {
    if (frame.size() < 5) throw std::runtime_error("VMX frame is too short");
    slices = frame[4];
    pos = 5;
  } else if (frame[0] == VMX_CODEC_FORMAT_PROGRESSIVE || frame[0] == VMX_CODEC_FORMAT_INTERLACED) {
    slices = frame[2];
    pos = 3;
  } else {
    throw std::runtime_error("not a VMX frame");
  }
  for (int i = 0; i < slices; ++i) {
    if (pos + 4 > frame.size()) throw std::runtime_error("truncated VMX frame");
    uint32_t len = 0;
    std::memcpy(&len, frame.data() + pos, 4);
    pos += 4;
    if (pos + len > frame.size()) throw std::runtime_error("truncated VMX DC slice");
    pos += len;
  }
  if (pos == 0) throw std::runtime_error("VMX preview length is invalid");
  return static_cast<uint32_t>(pos);
}

}  // namespace br