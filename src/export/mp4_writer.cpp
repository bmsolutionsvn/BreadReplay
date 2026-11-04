#include "export/mp4_writer.h"

#include <stdexcept>

#include "export/export_encoder.h"

namespace br {
namespace {
std::string averr(int rc) {
  char buf[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(rc, buf, sizeof buf);
  return buf;
}

void check(int rc, const char* what) {
  if (rc < 0) throw std::runtime_error(std::string(what) + ": " + averr(rc));
}
}  // namespace

Mp4Writer::Mp4Writer(Mp4WriterOptions options) : o_(std::move(options)) {
  try {
    open();
  } catch (...) {
    abort();
    throw;
  }
}

Mp4Writer::~Mp4Writer() {
  if (!finished_) abort();
}

void Mp4Writer::open() {
  encW_ = o_.w & ~1u;
  encH_ = o_.h & ~1u;
  if (encW_ == 0 || encH_ == 0) throw std::runtime_error("export: the raster is empty");

  video_ = openExportEncoder(o_.codec, encW_, encH_, o_.rate, o_.bitRate).ctx;

  const AVCodec* ac = avcodec_find_encoder(AV_CODEC_ID_AAC);
  if (!ac) throw std::runtime_error("export: no AAC encoder");
  audio_ = avcodec_alloc_context3(ac);
  if (!audio_) throw std::runtime_error("export: no audio codec context");
  audio_->bit_rate = 192'000;
  audio_->sample_rate = 48'000;
  audio_->sample_fmt = AV_SAMPLE_FMT_FLTP;
  audio_->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
  audio_->time_base = {1, 48'000};
  audio_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  check(avcodec_open2(audio_, ac, nullptr), "export: audio encoder");

  part_ = o_.finalPath;
  part_ += ".part";
  check(avformat_alloc_output_context2(&fmt_, nullptr, "mp4", part_.string().c_str()),
        "export: mp4 muxer");
  if (!fmt_) throw std::runtime_error("export: no mp4 muxer");

  videoStream_ = avformat_new_stream(fmt_, nullptr);
  audioStream_ = avformat_new_stream(fmt_, nullptr);
  if (!videoStream_ || !audioStream_) throw std::runtime_error("export: no stream");
  videoStream_->time_base = video_->time_base;
  audioStream_->time_base = audio_->time_base;
  check(avcodec_parameters_from_context(videoStream_->codecpar, video_), "export: video parameters");
  check(avcodec_parameters_from_context(audioStream_->codecpar, audio_), "export: audio parameters");
  videoStream_->codecpar->codec_tag = 0;
  audioStream_->codecpar->codec_tag = 0;

  check(avio_open(&fmt_->pb, part_.string().c_str(), AVIO_FLAG_WRITE), "export: open file");
  AVDictionary* opts = nullptr;
  av_dict_set(&opts, "movflags", "+faststart", 0);
  const int rc = avformat_write_header(fmt_, &opts);
  av_dict_free(&opts);
  check(rc, "export: header");

  nv12_ = av_frame_alloc();
  pcm_ = av_frame_alloc();
  pkt_ = av_packet_alloc();
  if (!nv12_ || !pcm_ || !pkt_) throw std::runtime_error("export: no frame");
  nv12_->format = AV_PIX_FMT_NV12;
  nv12_->width = static_cast<int>(encW_);
  nv12_->height = static_cast<int>(encH_);
  check(av_frame_get_buffer(nv12_, 0), "export: video buffer");
  pcm_->format = audio_->sample_fmt;
  pcm_->sample_rate = audio_->sample_rate;
  pcm_->ch_layout = audio_->ch_layout;
  pcm_->nb_samples = audio_->frame_size;
  check(av_frame_get_buffer(pcm_, 0), "export: audio buffer");
}

void Mp4Writer::submit(bc::OutFrame frame) {
  if (error_ || finished_ || closed_) return;
  try {
    if (frame.bgra && frame.bgra->width && frame.bgra->height) writeVideo(frame);
    if (frame.audio) writeAudio(frame);
  } catch (const std::exception& e) {
    fail(e.what());
  }
}

void Mp4Writer::writeVideo(const bc::OutFrame& frame) {
  const auto& img = *frame.bgra;
  sws_ = sws_getCachedContext(sws_, static_cast<int>(img.width), static_cast<int>(img.height),
                              AV_PIX_FMT_BGRA, static_cast<int>(encW_), static_cast<int>(encH_),
                              AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (!sws_) throw std::runtime_error("export: cannot convert the picture");
  check(av_frame_make_writable(nv12_), "export: video frame");
  const uint8_t* src[1] = {img.bgra.data()};
  const int stride[1] = {static_cast<int>(img.width * 4)};
  sws_scale(sws_, src, stride, 0, static_cast<int>(img.height), nv12_->data, nv12_->linesize);
  nv12_->pts = videoIndex_++;
  send(video_, nv12_, videoStream_);
}

void Mp4Writer::writeAudio(const bc::OutFrame& frame) {
  if (frame.channels != 2 || frame.sampleRate != 48'000)
    throw std::runtime_error("export: audio is not 48 kHz stereo");
  pending_.insert(pending_.end(), frame.audio->begin(), frame.audio->end());
  const size_t frameSize = static_cast<size_t>(audio_->frame_size);
  while (pending_.size() >= frameSize * 2) {
    check(av_frame_make_writable(pcm_), "export: audio frame");
    auto* left = reinterpret_cast<float*>(pcm_->data[0]);
    auto* right = reinterpret_cast<float*>(pcm_->data[1]);
    for (size_t i = 0; i < frameSize; ++i) {
      left[i] = pending_[i * 2];
      right[i] = pending_[i * 2 + 1];
    }
    pcm_->pts = audioPts_;
    audioPts_ += static_cast<int64_t>(frameSize);
    send(audio_, pcm_, audioStream_);
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(frameSize * 2));
  }
}

void Mp4Writer::send(AVCodecContext* ctx, AVFrame* frame, AVStream* stream) {
  const int rc = avcodec_send_frame(ctx, frame);
  if (rc < 0 && rc != AVERROR_EOF) check(rc, "export: encode");
  drain(ctx, stream);
}

void Mp4Writer::drain(AVCodecContext* ctx, AVStream* stream) {
  while (true) {
    const int rc = avcodec_receive_packet(ctx, pkt_);
    if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) return;
    check(rc, "export: packet");
    av_packet_rescale_ts(pkt_, ctx->time_base, stream->time_base);
    pkt_->stream_index = stream->index;
    check(av_interleaved_write_frame(fmt_, pkt_), "export: write packet");
    av_packet_unref(pkt_);
  }
}

void Mp4Writer::finish() {
  if (finished_) return;
  if (error_) {
    abort();
    return;
  }
  try {
    send(video_, nullptr, videoStream_);
    send(audio_, nullptr, audioStream_);
    check(av_write_trailer(fmt_), "export: trailer");
    close();
    std::error_code ec;
    std::filesystem::rename(part_, o_.finalPath, ec);
    if (ec) throw std::runtime_error("export: cannot name the file: " + ec.message());
    finished_ = true;
  } catch (const std::exception& e) {
    fail(e.what());
  }
}

void Mp4Writer::abort() {
  close();
  std::error_code ec;
  std::filesystem::remove(part_, ec);
}

void Mp4Writer::fail(const std::string& why) {
  if (!error_) error_ = why;
  abort();
}

void Mp4Writer::close() {
  if (closed_) return;
  closed_ = true;
  if (pkt_) av_packet_free(&pkt_);
  if (pcm_) av_frame_free(&pcm_);
  if (nv12_) av_frame_free(&nv12_);
  if (sws_) {
    sws_freeContext(sws_);
    sws_ = nullptr;
  }
  if (audio_) avcodec_free_context(&audio_);
  if (video_) avcodec_free_context(&video_);
  if (fmt_) {
    if (fmt_->pb) avio_closep(&fmt_->pb);
    avformat_free_context(fmt_);
    fmt_ = nullptr;
  }
}

}  // namespace br