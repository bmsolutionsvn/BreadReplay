#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

#include "core/vmx_codec.h"
#include "engine/preview_encoder.h"
#include "gpu/device.h"
#include "gpu/texture_pool.h"
#include "play/frame_source.h"
#include "record/recorder.h"
#include "support/requires.h"
#include "support/temp_dir.h"

namespace {
constexpr int64_t kStep = 16683;
constexpr int64_t kBase = 100'000'000;

struct Store {
  bc::test::TempDir dir;
  std::unique_ptr<br::InputStore> store;

  Store(uint32_t w, uint32_t h, const std::vector<int64_t>& times) {
    br::SegmentHeader header;
    header.inputId = "cam1";
    header.width = w;
    header.height = h;
    header.rate = bc::Rate{60000, 1001};
    store = std::make_unique<br::InputStore>(dir.path, header);
    std::vector<uint8_t> frame(static_cast<size_t>(w) * h * 2);
    for (size_t i = 0; i < frame.size(); i += 2) {
      frame[i] = 128;
      frame[i + 1] = static_cast<uint8_t>(40 + (i / 2) % 100);
    }
    bc::CpuVideoFrame f;
    f.data = frame.data();
    f.stride = static_cast<int>(w * 2);
    f.width = w;
    f.height = h;
    f.format = bc::CpuPixel::UYVY;
    br::VmxEncoder enc(w, h, br::VmxQuality::SQ);
    for (int64_t t : times) {
      const auto e = enc.encode(f);
      store->append(t, e.bytes, e.previewLen, std::vector<float>{0.1f, 0.2f});
    }
  }

  std::map<std::string, const br::InputStore*> map() const { return {{"cam1", store.get()}}; }
};

// ---- a short H.264 recording with a long GOP and B-frames ----

bool hasNalType(std::span<const uint8_t> d, int type) {
  for (size_t i = 0; i + 3 < d.size(); ++i) {
    if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1 && (d[i + 3] & 0x1F) == type) return true;
  }
  return false;
}

bool isAnnexB(std::span<const uint8_t> d) {
  return d.size() >= 4 && d[0] == 0 && d[1] == 0 && (d[2] == 1 || (d[2] == 0 && d[3] == 1));
}

struct Encoded {
  std::string encoder;
  std::vector<std::vector<uint8_t>> aus;
  std::vector<int64_t> pts;
  std::vector<bool> key;
  uint32_t w = 0, h = 0;
  int fps = 30;
};

Encoded encodeH264(int frames, int gop, int bframes) {
  Encoded out;
  // libx264 is not linked into the product build; AMF honours B-frames, MF's camera record does not.
  const char* names[] = {"libx264", "h264_amf", "h264_mf"};
  AVCodecContext* ctx = nullptr;
  for (const char* name : names) {
    const AVCodec* codec = avcodec_find_encoder_by_name(name);
    if (!codec) continue;
    ctx = avcodec_alloc_context3(codec);
    if (!ctx) continue;
    ctx->width = 640;
    ctx->height = 360;
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx->time_base = {1, 30};
    ctx->framerate = {30, 1};
    ctx->gop_size = gop;
    ctx->max_b_frames = bframes;
    ctx->profile = AV_PROFILE_H264_HIGH;  // baseline has no B-frames
    ctx->bit_rate = 2'000'000;
    ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* opts = nullptr;
    if (std::string(name) == "h264_mf") av_dict_set(&opts, "scenario", "camera_record", 0);
    if (std::string(name) == "h264_amf") {
      av_dict_set(&opts, "bf", "2", 0);
      av_dict_set(&opts, "max_b_frames", "2", 0);
    }
    const int rc = avcodec_open2(ctx, codec, &opts);
    av_dict_free(&opts);
    if (rc >= 0) {
      out.encoder = name;
      break;
    }
    avcodec_free_context(&ctx);
  }
  if (!ctx) return out;
  out.w = static_cast<uint32_t>(ctx->width);
  out.h = static_cast<uint32_t>(ctx->height);

  std::vector<std::pair<std::vector<uint8_t>, int64_t>> raw;
  std::vector<bool> key;
  AVPacket* pkt = av_packet_alloc();
  const auto drain = [&] {
    for (;;) {
      const int rc = avcodec_receive_packet(ctx, pkt);
      if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) break;
      if (rc < 0) break;
      raw.emplace_back(std::vector<uint8_t>(pkt->data, pkt->data + pkt->size), pkt->pts);
      key.push_back((pkt->flags & AV_PKT_FLAG_KEY) != 0);
      av_packet_unref(pkt);
    }
  };

  AVFrame* frame = av_frame_alloc();
  frame->format = ctx->pix_fmt;
  frame->width = ctx->width;
  frame->height = ctx->height;
  if (av_frame_get_buffer(frame, 0) < 0) return out;
  for (int i = 0; i < frames; ++i) {
    av_frame_make_writable(frame);
    for (int y = 0; y < ctx->height; ++y) {
      for (int x = 0; x < ctx->width; ++x) {
        frame->data[0][y * frame->linesize[0] + x] = static_cast<uint8_t>((x + y + i * 7) & 0xFF);
      }
    }
    for (int y = 0; y < ctx->height / 2; ++y) {
      std::memset(frame->data[1] + y * frame->linesize[1], 128, static_cast<size_t>(ctx->width / 2));
      std::memset(frame->data[2] + y * frame->linesize[2], 128, static_cast<size_t>(ctx->width / 2));
    }
    frame->pts = i;
    frame->duration = 1;
    if (avcodec_send_frame(ctx, frame) < 0) break;
    drain();
  }
  av_frame_free(&frame);
  avcodec_send_frame(ctx, nullptr);
  drain();
  av_packet_free(&pkt);

  // A keyframe carries its parameter sets in band (as the recorder does for SRT).
  std::vector<uint8_t> params;
  if (ctx->extradata && ctx->extradata_size > 0) {
    const std::span<const uint8_t> ex(ctx->extradata, static_cast<size_t>(ctx->extradata_size));
    if (isAnnexB(ex)) {
      params.assign(ex.begin(), ex.end());
    } else if (ex[0] == 1) {
      try {
        params = bc::avccToAnnexB(ex);
      } catch (const std::exception&) {
      }
    }
  }
  for (size_t i = 0; i < raw.size(); ++i) {
    auto& au = raw[i].first;
    if (key[i] && !params.empty() && !hasNalType(au, 7)) {
      std::vector<uint8_t> withParams = params;
      withParams.insert(withParams.end(), au.begin(), au.end());
      au = std::move(withParams);
    }
    out.aus.push_back(std::move(au));
    out.pts.push_back(raw[i].second);
    out.key.push_back(key[i]);
  }
  avcodec_free_context(&ctx);
  return out;
}

// A software decode of the same access units, keyed by packet pts.
std::map<int64_t, std::vector<uint8_t>> referenceY(const Encoded& e) {
  std::map<int64_t, std::vector<uint8_t>> out;
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!codec) return out;
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  if (avcodec_open2(ctx, codec, nullptr) < 0) {
    avcodec_free_context(&ctx);
    return out;
  }
  ctx->pkt_timebase = {1, 1'000'000};
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  auto drain = [&] {
    for (;;) {
      const int rc = avcodec_receive_frame(ctx, frame);
      if (rc < 0) break;
      std::vector<uint8_t> y(static_cast<size_t>(e.w) * e.h);
      for (uint32_t row = 0; row < e.h; ++row) {
        std::memcpy(y.data() + static_cast<size_t>(row) * e.w, frame->data[0] + row * frame->linesize[0], e.w);
      }
      out[frame->pts] = std::move(y);
      av_frame_unref(frame);
    }
  };
  for (size_t i = 0; i < e.aus.size(); ++i) {
    av_new_packet(pkt, static_cast<int>(e.aus[i].size()));
    std::memcpy(pkt->data, e.aus[i].data(), e.aus[i].size());
    pkt->pts = pkt->dts = e.pts[i];
    if (avcodec_send_packet(ctx, pkt) >= 0) drain();
    av_packet_unref(pkt);
  }
  avcodec_send_packet(ctx, nullptr);
  drain();
  av_packet_free(&pkt);
  av_frame_free(&frame);
  avcodec_free_context(&ctx);
  return out;
}

std::vector<uint8_t> readLuma(bc::GpuDevice& gpu, const br::DecodedFrame& f) {
  REQUIRE(f.picture.tex);
  REQUIRE(f.picture.layout == bc::PixelLayout::NV12);
  D3D11_TEXTURE2D_DESC d{};
  f.picture.tex->tex->GetDesc(&d);
  d.Usage = D3D11_USAGE_STAGING;
  d.BindFlags = 0;
  d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  d.MiscFlags = 0;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  if (FAILED(gpu.device()->CreateTexture2D(&d, nullptr, &staging))) return {};
  {
    auto lock = gpu.lock();
    gpu.context()->CopyResource(staging.Get(), f.picture.tex->tex.Get());
  }
  D3D11_MAPPED_SUBRESOURCE m{};
  if (FAILED(gpu.context()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return {};
  std::vector<uint8_t> y(static_cast<size_t>(f.w) * f.h);
  for (uint32_t row = 0; row < f.h; ++row) {
    std::memcpy(y.data() + static_cast<size_t>(row) * f.w, static_cast<uint8_t*>(m.pData) + row * m.RowPitch, f.w);
  }
  gpu.context()->Unmap(staging.Get(), 0);
  return y;
}
}  // namespace

TEST_CASE("FrameSource: get returns the frame at or before") {
  Store s(320, 180, {kBase, kBase + kStep, kBase + 2 * kStep});
  br::FrameSource src(s.map());
  const auto f = src.get("cam1", kBase + kStep + 500);
  REQUIRE(f);
  CHECK(f->tsUs == kBase + kStep);
  CHECK(f->uyvy.size() == 320u * 180u * 2u);
  CHECK(f->w == 320);
  CHECK_FALSE(src.get("nope", kBase));
}

TEST_CASE("FrameSource: a cached frame is not read twice") {
  Store s(320, 180, {kBase, kBase + kStep});
  br::FrameSource src(s.map());
  REQUIRE(src.get("cam1", kBase));
  const auto before = src.diskReads();
  REQUIRE(src.get("cam1", kBase + 10));
  CHECK(src.diskReads() == before);
  CHECK(before == 1);
}

TEST_CASE("FrameSource: a gap returns the last frame before it, flagged") {
  Store s(320, 180, {kBase, kBase + kStep, kBase + 180 * kStep, kBase + 181 * kStep});
  br::FrameSource src(s.map());
  const auto f = src.get("cam1", kBase + 90 * kStep);
  REQUIRE(f);
  CHECK(f->tsUs == kBase + kStep);
  CHECK(f->inGap);
  const auto live = src.get("cam1", kBase + 181 * kStep);
  REQUIRE(live);
  CHECK_FALSE(live->inGap);
}

TEST_CASE("FrameSource: preview is 240x135 and reads only the prefix") {
  Store s(1920, 1080, {kBase});
  br::FrameSource src(s.map());
  const auto p = src.preview("cam1", kBase);
  REQUIRE(p);
  CHECK(p->w == 240);
  CHECK(p->h == 135);
  CHECK(p->bgra.size() == 240u * 135u * 4u);
  CHECK(src.diskReads() == 1);
}

TEST_CASE("FrameSource: prefetch makes the next get a cache hit") {
  Store s(320, 180, {kBase, kBase + kStep, kBase + 2 * kStep, kBase + 3 * kStep});
  br::FrameSource src(s.map());
  src.prefetch("cam1", kBase, kStep, 4);
  const auto before = src.diskReads();
  CHECK(before == 4);
  REQUIRE(src.get("cam1", kBase + 3 * kStep));
  CHECK(src.diskReads() == before);
}

TEST_CASE("FrameSource: the frame cache is per input") {
  Store a(320, 180, {kBase, kBase + kStep, kBase + 2 * kStep});
  Store b(320, 180, {kBase, kBase + kStep, kBase + 2 * kStep});
  br::FrameSource src({{"a", a.store.get()}, {"b", b.store.get()}}, nullptr, nullptr, 1, 2);
  REQUIRE(src.get("a", kBase));
  REQUIRE(src.get("a", kBase + kStep));
  REQUIRE(src.get("b", kBase));
  REQUIRE(src.get("b", kBase + kStep));
  const auto before = src.diskReads();
  REQUIRE(src.get("a", kBase));
  CHECK(src.diskReads() == before);  // b's frames must not evict a's
}

TEST_CASE("FrameSource: a long-GOP H.264 recording plays in display order"
          BC_REQUIRES(bc::test::Req::HardwareGpu)) {
  auto gpu = bc::GpuDevice::create(bc::GpuDevice::Pref::Hardware);
  if (!gpu) return;
  bc::TexturePool pool{*gpu};

  const Encoded enc = encodeH264(90, 60, 2);
  REQUIRE_FALSE(enc.encoder.empty());
  bool reordered = false;
  for (size_t i = 1; i < enc.pts.size(); ++i) reordered = reordered || enc.pts[i] < enc.pts[i - 1];
  MESSAGE("encoder: " << enc.encoder << ", packets: " << enc.aus.size() << ", B-frames: " << reordered);
  REQUIRE(enc.aus.size() == 90);
  if (enc.encoder == "h264_amf") REQUIRE(reordered);  // the AMF path is what exercises reordering

  const auto ref = referenceY(enc);
  REQUIRE(ref.size() == 90);

  const auto record = [&](bc::test::TempDir& dir, bool keepSourcePts) {
    br::SegmentHeader header;
    header.inputId = "cam1";
    header.width = enc.w;
    header.height = enc.h;
    header.rate = bc::Rate{30, 1};
    auto store = std::make_unique<br::InputStore>(dir.path, header);
    br::Recorder rec(*store, br::VmxQuality::SQ, 512, 1);
    for (size_t i = 0; i < enc.aus.size(); ++i) {
      const int64_t ts = kBase + static_cast<int64_t>(i) * 33333;
      const int64_t src = keepSourcePts ? kBase + enc.pts[i] * 33333 : ts;
      rec.pushCodedAu(ts, src, br::StoredCodec::H264, enc.w, enc.h, enc.key[i], enc.aus[i]);
    }
    for (int i = 0; i < 400 && rec.stats().framesWritten < enc.aus.size(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(rec.stats().framesWritten == enc.aus.size());
    rec.stop();
    return store;
  };

  // The worst luma difference for a slot, or a value above the 2 bound when it is not that picture.
  const auto slotWorst = [&](br::FrameSource& s, int slot, int64_t display) {
    const int64_t ts = kBase + static_cast<int64_t>(slot) * 33333;
    const auto f = s.get("cam1", ts);
    if (!f || !f->picture.tex || f->tsUs != ts) return 1000;
    const auto y = readLuma(*gpu, *f);
    const auto& want = ref.at(display);
    if (y.size() != want.size()) return 1001;
    int worst = 0;
    for (size_t i = 0; i < y.size(); ++i) {
      worst = std::max(worst, std::abs(static_cast<int>(y[i]) - want[i]));
    }
    return worst;
  };

  // Review focus 1: every slot shows the picture whose display pts is that slot, in forward playback.
  bc::test::TempDir dir;
  auto store = record(dir, true);
  br::FrameSource src({{"cam1", store.get()}}, gpu.get(), &pool, 1, 64);
  for (int i = 0; i < 90; ++i) CHECK(slotWorst(src, i, i) <= 2);

  // A cold cue at 47, then stepping backwards, also shows display order.
  br::FrameSource cold({{"cam1", store.get()}}, gpu.get(), &pool, 1, 64);
  CHECK(slotWorst(cold, 47, 47) <= 2);
  const auto beforeForward = cold.decodes();
  for (int i = 48; i <= 52; ++i) CHECK(slotWorst(cold, i, i) <= 2);
  CHECK(cold.decodes() - beforeForward <= 20);
  CHECK(slotWorst(cold, 59, 59) <= 2);
  const auto before = cold.decodes();
  for (int i = 58; i >= 31; --i) CHECK(slotWorst(cold, i, i) <= 2);
  CHECK(cold.decodes() - before <= 60);

  // Review focus 2: a recording whose source pts are its arrival times keeps decode order.
  bc::test::TempDir flatDir;
  auto flat = record(flatDir, false);
  br::FrameSource plain({{"cam1", flat.get()}}, gpu.get(), &pool, 1, 64);
  for (int i = 0; i < 90; ++i) {
    CHECK(slotWorst(plain, i, enc.pts[static_cast<size_t>(i)]) <= 2);
  }

  // Review focus 3: a preview is the keyframe's picture at the VMX preview size.
  const int64_t ts47 = kBase + 47 * 33333;
  const auto p = src.preview("cam1", ts47);
  REQUIRE(p);
  const auto ps = br::VmxDecoder::previewSize(enc.w, enc.h);
  CHECK(p->w == ps.w);
  CHECK(p->h == ps.h);
  double avgPreview = 0;
  for (size_t i = 0; i < p->bgra.size(); i += 4) {
    avgPreview += 0.114 * p->bgra[i] + 0.587 * p->bgra[i + 1] + 0.299 * p->bgra[i + 2];
  }
  avgPreview /= static_cast<double>(p->bgra.size() / 4);
  const auto& key = ref.at(enc.pts[0]);
  double avgKey = 0;
  for (uint8_t v : key) avgKey += v;
  avgKey /= static_cast<double>(key.size());
  CHECK(std::abs(avgPreview - avgKey) <= 12.0);
}