#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include "core/rate.h"
#include "export/export_rules.h"
#include "io/output.h"

namespace br {

struct Mp4WriterOptions {
  std::filesystem::path finalPath;
  ExportCodec codec;
  int64_t bitRate;
  uint32_t w, h;
  bc::Rate rate;
};

// BGRA in, H.264/HEVC + AAC in an MP4 out. Writes <finalPath>.part until finish() renames it.
class Mp4Writer final : public bc::Output {
 public:
  explicit Mp4Writer(Mp4WriterOptions options);
  ~Mp4Writer() override;
  Mp4Writer(const Mp4Writer&) = delete;
  Mp4Writer& operator=(const Mp4Writer&) = delete;

  bool needsCpu() const override { return true; }
  bool wantsGpuRgba8() const override { return false; }
  void start() override {}
  void submit(bc::OutFrame frame) override;
  void stop() override {}
  bc::OutputStatus status() const override { return {}; }

  // Flushes both encoders, writes the trailer and renames .part to the final name.
  void finish();
  // Closes and deletes .part.
  void abort();
  const std::optional<std::string>& error() const { return error_; }

 private:
  void open();
  void close();
  void fail(const std::string& why);
  void writeVideo(const bc::OutFrame& frame);
  void writeAudio(const bc::OutFrame& frame);
  void send(AVCodecContext* ctx, AVFrame* frame, AVStream* stream);
  void drain(AVCodecContext* ctx, AVStream* stream);

  Mp4WriterOptions o_;
  std::filesystem::path part_;
  AVFormatContext* fmt_ = nullptr;
  AVCodecContext* video_ = nullptr;
  AVCodecContext* audio_ = nullptr;
  AVStream* videoStream_ = nullptr;
  AVStream* audioStream_ = nullptr;
  AVFrame* nv12_ = nullptr;
  AVFrame* pcm_ = nullptr;
  SwsContext* sws_ = nullptr;
  AVPacket* pkt_ = nullptr;
  uint32_t encW_ = 0, encH_ = 0;
  int64_t videoIndex_ = 0;
  int64_t audioPts_ = 0;
  std::vector<float> pending_;
  bool finished_ = false;
  bool closed_ = false;
  std::optional<std::string> error_;
};

}  // namespace br