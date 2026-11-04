#include <doctest/doctest.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "export/export_queue.h"
#include "core/vmx_codec.h"
#include "gpu/converter.h"
#include "gpu/device.h"
#include "gpu/texture_pool.h"
#include "play/output_player.h"
#include "play/player.h"
#include "play/renderer.h"
#include "store/input_store.h"
#include "support/fake_output.h"
#include "support/recording_output.h"
#include "support/requires.h"
#include "support/temp_dir.h"

namespace {
constexpr int64_t kS = 1'000'000;
constexpr int64_t kBase = 500'000'000;

struct Rgb {
  double b = 0, g = 0, r = 0;
};

Rgb meanImage(const bc::CpuImage& img) {
  Rgb c;
  const size_t pixels = img.bgra.size() / 4;
  if (pixels == 0) return c;
  double b = 0, g = 0, r = 0;
  size_t n = 0;
  for (size_t i = 0; i < pixels; i += 4) {
    const size_t p = i * 4;
    b += img.bgra[p];
    g += img.bgra[p + 1];
    r += img.bgra[p + 2];
    ++n;
  }
  c.b = b / n;
  c.g = g / n;
  c.r = r / n;
  return c;
}

std::unique_ptr<br::InputStore> makeStore(const std::filesystem::path& dir, const std::string& id,
                                          bool red, int64_t fromUs, int64_t toUs,
                                          int64_t gapFromUs = -1, int64_t gapToUs = -1) {
  br::SegmentHeader h;
  h.inputId = id;
  h.width = 320;
  h.height = 180;
  h.rate = bc::Rate{30, 1};
  auto store = std::make_unique<br::InputStore>(dir / id, h);
  std::vector<uint8_t> uyvy(static_cast<size_t>(320) * 180 * 2, 128);
  for (size_t p = 0; p < uyvy.size(); p += 4) {
    if (red) {
      uyvy[p] = 90;   // U
      uyvy[p + 1] = 81;  // Y
      uyvy[p + 2] = 240;  // V
      uyvy[p + 3] = 81;
    } else {
      uyvy[p] = 240;
      uyvy[p + 1] = 41;
      uyvy[p + 2] = 110;
      uyvy[p + 3] = 41;
    }
  }
  bc::CpuVideoFrame f;
  f.data = uyvy.data();
  f.stride = 320 * 2;
  f.width = 320;
  f.height = 180;
  f.format = bc::CpuPixel::UYVY;
  br::VmxEncoder enc(320, 180, br::VmxQuality::SQ);
  const auto e = enc.encode(f);
  for (int64_t t = fromUs; t < toUs; t += 33333) {
    if (gapFromUs >= 0 && t >= gapFromUs && t < gapToUs) continue;
    store->append(t, e.bytes, e.previewLen, std::vector<float>(1600, 0.25f));
  }
  return store;
}

struct Fixture {
  bc::test::TempDir dir;
  std::unique_ptr<br::InputStore> cam1, cam2;
  std::map<std::string, const br::InputStore*> stores;
  std::unique_ptr<bc::GpuDevice> gpu = bc::GpuDevice::create(bc::GpuDevice::Pref::HardwareOrWarp);
  bc::TexturePool pool{*gpu};
  bc::Converter conv{*gpu, pool};
  br::ShowSettings settings;
  br::EventStore events;

  Fixture() {
    cam1 = makeStore(dir.path, "cam1", true, kBase, kBase + 6 * kS);
    cam2 = makeStore(dir.path, "cam2", false, kBase, kBase + 6 * kS);
    stores = {{"cam1", cam1.get()}, {"cam2", cam2.get()}};
    settings.rate = bc::Rate{30, 1};
    settings.outW = 320;
    settings.outH = 180;
    settings.transition = "fade";
    settings.fadeS = 0.2;
  }

  int makeA() {
    br::Event& made = events.markIn(kBase + 1 * kS, "cam1");
    const int id = made.id;
    events.markOut(kBase + 3 * kS, "cam1", kS);
    auto* e = events.find(id);
    e->angles = {{kBase + 1 * kS, "cam1"}, {kBase + 2 * kS, "cam2"}};
    e->speeds = {{kBase + 1 * kS, 1.0}, {kBase + 2 * kS + kS / 2, 0.5}};
    return id;
  }

  int makeB() {
    br::Event& made = events.markIn(kBase + 4 * kS, "cam2");
    const int id = made.id;
    events.markOut(kBase + 5 * kS, "cam2", kS);
    return id;
  }

  int makeLong(int64_t seconds = 30) {
    br::Event& made = events.markIn(kBase + 1 * kS, "cam1");
    const int id = made.id;
    events.markOut(kBase + seconds * kS, "cam1", kS);
    return id;
  }
};

std::vector<Rgb> referenceColours(Fixture& f, std::vector<int> ids) {
  br::FrameSource src(f.stores);
  br::Renderer renderer(*f.gpu, f.pool, f.conv, f.settings.outW, f.settings.outH);
  br::Player player(f.events, f.settings.rate, true);
  br::OutputPlayer op(player, src, renderer, f.events, f.settings);
  auto out = std::make_unique<br::test::RecordingOutput>(true, false);
  auto* raw = out.get();
  std::vector<std::unique_ptr<bc::Output>> outs;
  outs.push_back(std::move(out));
  op.setOutputs(std::move(outs));
  op.playEvents(ids);
  for (int i = 0; i < 10000 && !op.idle(); ++i) op.tickOnce();
  std::vector<Rgb> colours;
  for (const auto& frame : raw->frames) {
    colours.push_back(frame.bgra ? meanImage(*frame.bgra) : Rgb{});
  }
  return colours;
}

std::vector<Rgb> exportColours(const std::filesystem::path& path) {
  AVFormatContext* fmt = nullptr;
  REQUIRE(avformat_open_input(&fmt, path.string().c_str(), nullptr, nullptr) >= 0);
  REQUIRE(avformat_find_stream_info(fmt, nullptr) >= 0);
  const int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  REQUIRE(vs >= 0);
  const AVCodec* dec = avcodec_find_decoder(fmt->streams[vs]->codecpar->codec_id);
  REQUIRE(dec != nullptr);
  AVCodecContext* ctx = avcodec_alloc_context3(dec);
  REQUIRE(avcodec_parameters_to_context(ctx, fmt->streams[vs]->codecpar) >= 0);
  REQUIRE(avcodec_open2(ctx, dec, nullptr) >= 0);

  SwsContext* sws = nullptr;
  AVFrame* frame = av_frame_alloc();
  AVPacket* pkt = av_packet_alloc();
  std::vector<Rgb> colours;
  auto take = [&] {
    sws = sws_getCachedContext(sws, ctx->width, ctx->height, ctx->pix_fmt, ctx->width, ctx->height,
                               AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    REQUIRE(sws != nullptr);
    bc::CpuImage img;
    img.width = static_cast<uint32_t>(ctx->width);
    img.height = static_cast<uint32_t>(ctx->height);
    img.bgra.resize(static_cast<size_t>(img.width) * img.height * 4);
    uint8_t* dst[1] = {img.bgra.data()};
    const int stride[1] = {static_cast<int>(img.width * 4)};
    sws_scale(sws, frame->data, frame->linesize, 0, ctx->height, dst, stride);
    colours.push_back(meanImage(img));
  };
  while (av_read_frame(fmt, pkt) >= 0) {
    if (pkt->stream_index == vs) {
      avcodec_send_packet(ctx, pkt);
      while (avcodec_receive_frame(ctx, frame) >= 0) {
        take();
        av_frame_unref(frame);
      }
    }
    av_packet_unref(pkt);
  }
  avcodec_send_packet(ctx, nullptr);
  while (avcodec_receive_frame(ctx, frame) >= 0) {
    take();
    av_frame_unref(frame);
  }
  av_packet_free(&pkt);
  av_frame_free(&frame);
  if (sws) sws_freeContext(sws);
  avcodec_free_context(&ctx);
  avformat_close_input(&fmt);
  return colours;
}

br::ExportState waitFor(br::ExportQueue& q, int id, std::chrono::seconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for (;;) {
    for (const auto& s : q.states()) {
      if (s.id != id) continue;
      if (s.state == "done" || s.state == "failed" || s.state == "cancelled") return s;
    }
    if (std::chrono::steady_clock::now() > deadline) return {};
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

br::ExportJob jobFor(Fixture& f, const std::vector<int>& ids, const std::filesystem::path& path) {
  br::ExportJob job;
  job.name = "Export";
  job.codec = br::ExportCodec::H264;
  job.bitRate = 4'000'000;
  job.path = path;
  for (int id : ids) job.events.push_back(*f.events.find(id));
  return job;
}
}  // namespace

TEST_CASE("Export: a replay with cuts, speed and a fade exports frame-accurate"
          BC_REQUIRES(bc::test::Req::H264Encoder)) {
  Fixture f;
  const int a = f.makeA();
  const int b = f.makeB();
  const auto reference = referenceColours(f, {a, b});
  REQUIRE(reference.size() > 20);

  const auto final = f.dir.path / "a.mp4";
  br::ExportQueue q(f.stores, f.settings);
  const int id = q.add(jobFor(f, {a, b}, final));
  const auto state = waitFor(q, id, std::chrono::seconds(60));
  REQUIRE(state.state == "done");
  CHECK(state.error.empty());
  CHECK_FALSE(std::filesystem::exists(final.string() + ".part"));

  const auto got = exportColours(final);
  CHECK(got.size() == reference.size());
  REQUIRE(got.size() > 10);
  const size_t early = 5;
  const size_t late = got.size() - 5;
  CHECK(std::abs(got[early].r - reference[early].r) <= 16);
  CHECK(std::abs(got[early].b - reference[early].b) <= 16);
  CHECK(std::abs(got[late].b - reference[late].b) <= 16);
  CHECK(std::abs(got[late].r - reference[late].r) <= 16);
}

TEST_CASE("Export: the output player keeps its pace while exporting"
          BC_REQUIRES(bc::test::Req::H264Encoder)) {
  Fixture f;
  f.settings.rate = bc::Rate{60, 1};
  const int id = f.makeLong(10);

  br::FrameSource src(f.stores);
  br::Renderer renderer(*f.gpu, f.pool, f.conv, f.settings.outW, f.settings.outH);
  br::Player player(f.events, f.settings.rate, true);
  br::OutputPlayer op(player, src, renderer, f.events, f.settings);
  auto fake = std::make_unique<bc::test::FakeOutput>(false);
  std::vector<std::unique_ptr<bc::Output>> outs;
  outs.push_back(std::move(fake));
  op.setOutputs(std::move(outs));
  op.start();
  player.cue(id);
  player.play();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const uint64_t before = op.ticks();

  br::ExportQueue q(f.stores, f.settings);
  q.add(jobFor(f, {id}, f.dir.path / "pace.mp4"));
  std::this_thread::sleep_for(std::chrono::seconds(2));
  const uint64_t ticks = op.ticks() - before;
  op.stop();
  CHECK(ticks >= 114);
}

TEST_CASE("Export: a recording gap keeps the frame count"
          BC_REQUIRES(bc::test::Req::H264Encoder)) {
  Fixture f;
  f.cam1 = makeStore(f.dir.path, "cam1", true, kBase, kBase + 6 * kS, kBase + 2 * kS + kS / 2,
                     kBase + 3 * kS + kS / 2);
  f.stores["cam1"] = f.cam1.get();
  br::Event& made = f.events.markIn(kBase + 1 * kS, "cam1");
  const int id = made.id;
  f.events.markOut(kBase + 4 * kS, "cam1", kS);

  const auto reference = referenceColours(f, {id});
  REQUIRE(reference.size() > 10);

  const auto final = f.dir.path / "gap.mp4";
  br::ExportQueue q(f.stores, f.settings);
  const auto state = waitFor(q, q.add(jobFor(f, {id}, final)), std::chrono::seconds(60));
  REQUIRE(state.state == "done");
  CHECK(exportColours(final).size() == reference.size());
}

TEST_CASE("Export: cancel leaves no file" BC_REQUIRES(bc::test::Req::H264Encoder)) {
  Fixture f;
  const int id = f.makeLong(30);
  const auto final = f.dir.path / "cancel.mp4";
  br::ExportQueue q(f.stores, f.settings);
  const int job = q.add(jobFor(f, {id}, final));

  bool started = false;
  for (int i = 0; i < 2000 && !started; ++i) {
    for (const auto& s : q.states()) {
      if (s.id == job && s.state == "running" && s.progress > 0) started = true;
    }
    if (!started) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(started);
  q.cancel(job);
  const auto state = waitFor(q, job, std::chrono::seconds(30));
  CHECK(state.state == "cancelled");
  CHECK_FALSE(std::filesystem::exists(final));
  CHECK_FALSE(std::filesystem::exists(final.string() + ".part"));
}

TEST_CASE("Export: an unwritable folder fails with a reason"
          BC_REQUIRES(bc::test::Req::H264Encoder)) {
  Fixture f;
  const int id = f.makeB();
  const auto blocked = f.dir.path / "blocked";
  std::ofstream(blocked) << "not a folder";
  br::ExportQueue q(f.stores, f.settings);
  const auto final = blocked / "x.mp4";
  const auto state = waitFor(q, q.add(jobFor(f, {id}, final)), std::chrono::seconds(30));
  CHECK(state.state == "failed");
  CHECK_FALSE(state.error.empty());
  CHECK_FALSE(std::filesystem::exists(final.string() + ".part"));
}

TEST_CASE("Export: stop during a job deletes its part" BC_REQUIRES(bc::test::Req::H264Encoder)) {
  Fixture f;
  const int id = f.makeLong(30);
  const auto final = f.dir.path / "stop.mp4";
  auto q = std::make_unique<br::ExportQueue>(f.stores, f.settings);
  q->add(jobFor(f, {id}, final));

  const auto part = final.string() + ".part";
  bool created = false;
  for (int i = 0; i < 4000 && !created; ++i) {
    created = std::filesystem::exists(part);
    if (!created) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(created);
  q.reset();
  CHECK_FALSE(std::filesystem::exists(part));
  CHECK_FALSE(std::filesystem::exists(final));
}