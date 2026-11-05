#include "export/export_encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace br {
namespace {
std::string averr(int rc) {
  char buf[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(rc, buf, sizeof buf);
  return buf;
}

const std::vector<std::string>& hevcOrder() {
  static const std::vector<std::string> order{"hevc_nvenc", "hevc_amf", "hevc_qsv", "hevc_mf"};
  return order;
}

// The same per-encoder options BreadCamera uses for H.264, on the HEVC encoder equivalents.
bc::OpenedEncoder openHevc(int w, int h, bc::Rate rate, int64_t bitRate, int gop, bool globalHeader) {
  std::string tried;
  for (const auto& name : hevcOrder()) {
    const AVCodec* codec = avcodec_find_encoder_by_name(name.c_str());
    if (!codec) {
      tried += name + " (not built); ";
      continue;
    }
    AVCodecContext* c = avcodec_alloc_context3(codec);
    if (!c) throw std::runtime_error("hevc: no codec context");
    c->width = w;
    c->height = h;
    c->time_base = {rate.den, rate.num};
    c->framerate = {rate.num, rate.den};
    c->pix_fmt = AV_PIX_FMT_NV12;
    c->gop_size = gop;
    c->max_b_frames = 0;
    c->bit_rate = bitRate;
    c->flags |= AV_CODEC_FLAG_LOW_DELAY;
    if (globalHeader) c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* o = nullptr;
    if (name == "hevc_nvenc") {
      av_dict_set(&o, "preset", "p1", 0);
      av_dict_set(&o, "tune", "ull", 0);
      av_dict_set(&o, "zerolatency", "1", 0);
      av_dict_set(&o, "forced-idr", "1", 0);
    } else if (name == "hevc_amf") {
      av_dict_set(&o, "usage", "ultralowlatency", 0);
      av_dict_set(&o, "forced_idr", "1", 0);
    } else if (name == "hevc_qsv") {
      av_dict_set(&o, "preset", "veryfast", 0);
      av_dict_set(&o, "async_depth", "1", 0);
    } else if (name == "hevc_mf") {
      av_dict_set(&o, "rate_control", "cbr", 0);
      av_dict_set(&o, "scenario", "display_remoting", 0);
    }
    const int rc = avcodec_open2(c, codec, &o);
    av_dict_free(&o);
    if (rc < 0) {
      tried += name + " (" + averr(rc) + "); ";
      avcodec_free_context(&c);
      continue;
    }
    return {c, name};
  }
  throw std::runtime_error("no H.265 encoder could be opened: " + tried);
}
}  // namespace

bc::OpenedEncoder openExportEncoder(ExportCodec codec, uint32_t w, uint32_t h, bc::Rate rate,
                                    int64_t bitRate) {
  const int gop = std::max(1, static_cast<int>(std::lround(rate.fps() * 2.0)));
  if (codec == ExportCodec::H264)
    return bc::openH264Encoder(static_cast<int>(w), static_cast<int>(h), rate.num, rate.den, bitRate,
                               gop, true);
  return openHevc(static_cast<int>(w), static_cast<int>(h), rate, bitRate, gop, true);
}

const std::vector<ExportCodec>& availableExportCodecs() {
  static const std::vector<ExportCodec> codecs = [] {
    std::vector<ExportCodec> found;
    for (const ExportCodec codec : {ExportCodec::H264, ExportCodec::Hevc}) {
      try {
        auto opened = openExportEncoder(codec, 640, 360, bc::Rate{30, 1}, 1'000'000);
        avcodec_free_context(&opened.ctx);
        found.push_back(codec);
      } catch (const std::exception&) {
        // Not offered.
      }
    }
    return found;
  }();
  return codecs;
}

}  // namespace br