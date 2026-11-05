#include <doctest/doctest.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/tick_schedule.h"
#include "export/export_encoder.h"
#include "export/mp4_writer.h"
#include "support/requires.h"
#include "support/temp_dir.h"

namespace {
constexpr int64_t kS = 1'000'000;

struct Probe {
  int videoFrames = 0;
  std::string videoCodec;
  int width = 0, height = 0;
  double durationS = 0;
  int audioRate = 0, audioChannels = 0;
};

Probe probeFile(const std::filesystem::path& path) {
  AVFormatContext* fmt = nullptr;
  REQUIRE(avformat_open_input(&fmt, path.string().c_str(), nullptr, nullptr) >= 0);
  REQUIRE(avformat_find_stream_info(fmt, nullptr) >= 0);
  Probe r;
  for (unsigned i = 0; i < fmt->nb_streams; ++i) {
    const AVCodecParameters* p = fmt->streams[i]->codecpar;
    if (p->codec_type == AVMEDIA_TYPE_VIDEO) {
      r.videoCodec = avcodec_get_name(p->codec_id);
      r.width = p->width;
      r.height = p->height;
    } else if (p->codec_type == AVMEDIA_TYPE_AUDIO) {
      r.audioRate = p->sample_rate;
      r.audioChannels = p->ch_layout.nb_channels;
    }
  }
  AVPacket* pkt = av_packet_alloc();
  while (av_read_frame(fmt, pkt) >= 0) {
    if (fmt->streams[pkt->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) ++r.videoFrames;
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  if (fmt->duration != AV_NOPTS_VALUE) r.durationS = static_cast<double>(fmt->duration) / AV_TIME_BASE;
  avformat_close_input(&fmt);
  return r;
}

std::shared_ptr<const bc::CpuImage> solidImage() {
  static const auto image = [] {
    auto img = std::make_shared<bc::CpuImage>();
    img->width = 1366;
    img->height = 768;
    img->bgra.assign(static_cast<size_t>(1366) * 768 * 4, 0x40);
    return img;
  }();
  return image;
}

void feed(br::Mp4Writer& w, int frames, bc::Rate rate) {
  const auto image = solidImage();
  const bc::TickSchedule schedule(rate);
  for (int i = 0; i < frames; ++i) {
    bc::OutFrame f;
    f.tick = static_cast<uint64_t>(i);
    f.rate = rate;
    f.bgra = image;
    f.audio = std::make_shared<const std::vector<float>>(
        static_cast<size_t>(schedule.audioFramesForTick(static_cast<uint64_t>(i), 48'000)) * 2, 0.1f);
    f.sampleRate = 48'000;
    f.channels = 2;
    w.submit(std::move(f));
  }
}

br::Mp4WriterOptions options(const std::filesystem::path& final, br::ExportCodec codec,
                             int64_t bitRate, bc::Rate rate) {
  return {final, codec, bitRate, 1366, 768, rate};
}
}  // namespace

TEST_CASE("Mp4Writer: writes H.264 + AAC and renames on finish" BC_REQUIRES(bc::test::Req::H264Encoder)) {
  bc::test::TempDir dir;
  const auto final = dir.path / "out.mp4";
  br::Mp4Writer w(options(final, br::ExportCodec::H264, 4'000'000, {30, 1}));
  feed(w, 60, {30, 1});
  w.finish();
  REQUIRE_FALSE(w.error());
  CHECK_FALSE(std::filesystem::exists(final.string() + ".part"));
  REQUIRE(std::filesystem::exists(final));
  const auto p = probeFile(final);
  CHECK(p.videoFrames == 60);
  CHECK(p.videoCodec == "h264");
  CHECK(p.width == 1366);
  CHECK(p.height == 768);
  CHECK(p.durationS == doctest::Approx(2.0).epsilon(0.025));
  CHECK(p.audioRate == 48'000);
  CHECK(p.audioChannels == 2);
}

TEST_CASE("Mp4Writer: abort leaves nothing" BC_REQUIRES(bc::test::Req::H264Encoder)) {
  bc::test::TempDir dir;
  const auto final = dir.path / "out.mp4";
  br::Mp4Writer w(options(final, br::ExportCodec::H264, 4'000'000, {30, 1}));
  feed(w, 10, {30, 1});
  w.abort();
  CHECK_FALSE(std::filesystem::exists(final));
  CHECK_FALSE(std::filesystem::exists(final.string() + ".part"));
}

TEST_CASE("Mp4Writer: 59.94 fps keeps exact timestamps" BC_REQUIRES(bc::test::Req::H264Encoder)) {
  bc::test::TempDir dir;
  const auto final = dir.path / "out.mp4";
  const bc::Rate rate{60000, 1001};
  br::Mp4Writer w(options(final, br::ExportCodec::H264, 20'000'000, rate));
  feed(w, 3600, rate);
  w.finish();
  REQUIRE_FALSE(w.error());
  const auto p = probeFile(final);
  CHECK(p.videoFrames == 3600);
  const double expected = 3600.0 * 1001.0 / 60000.0;
  CHECK(std::abs(p.durationS - expected) <= 1001.0 / 60000.0 + 0.001);
}

TEST_CASE("Mp4Writer: H.265 when available" BC_REQUIRES(bc::test::Req::H264Encoder)) {
  const auto& codecs = br::availableExportCodecs();
  if (std::find(codecs.begin(), codecs.end(), br::ExportCodec::Hevc) == codecs.end()) {
    MESSAGE("no H.265 encoder on this PC");
    return;
  }
  bc::test::TempDir dir;
  const auto final = dir.path / "out.mp4";
  br::Mp4Writer w(options(final, br::ExportCodec::Hevc, 6'000'000, {30, 1}));
  feed(w, 30, {30, 1});
  w.finish();
  REQUIRE_FALSE(w.error());
  const auto p = probeFile(final);
  CHECK(p.videoFrames == 30);
  CHECK(p.videoCodec == "hevc");
}