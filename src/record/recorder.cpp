#include "record/recorder.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "core/vmx_codec.h"

namespace br {

Recorder::Recorder(InputStore& store, VmxQuality q, int queueDepth, int threads)
    : store_(store), quality_(q), queueDepth_(queueDepth < 1 ? 1 : queueDepth),
      threadCount_(threads < 1 ? 1 : threads) {
  for (int i = 0; i < threadCount_; ++i) workers_.emplace_back([this] { worker(); });
  writer_ = std::thread([this] { writerLoop(); });
}

Recorder::~Recorder() { stop(); }

void Recorder::pushVideo(const bc::CpuVideoFrame& f, int64_t tsUs) {
  std::unique_lock lock(m_);
  ++framesIn_;
  if (static_cast<int>(queue_.size()) >= queueDepth_) {
    ++dropped_;  // capture must never wait on the encoder
    return;
  }
  Job j;
  j.seq = nextSeq_++;
  j.tsUs = tsUs;
  j.w = f.width;
  j.h = f.height;
  j.stride = f.stride;
  j.format = f.format;
  size_t bytes = static_cast<size_t>(f.stride) * f.height;
  if (f.format == bc::CpuPixel::NV12) bytes = bytes * 3 / 2;
  j.video.assign(f.data, f.data + bytes);
  j.audio = std::move(pendingAudio_);
  pendingAudio_.clear();
  queue_.push_back(std::move(j));
  lock.unlock();
  cv_.notify_one();
}

void Recorder::pushCoded(int64_t tsUs, std::vector<uint8_t> vmx) {
  std::unique_lock lock(m_);
  ++framesIn_;
  if (static_cast<int>(queue_.size()) >= queueDepth_) {
    ++dropped_;
    return;
  }
  Job j;
  j.seq = nextSeq_++;
  j.tsUs = tsUs;
  j.coded = true;
  j.w = store_.header().width;
  j.h = store_.header().height;
  j.video = std::move(vmx);
  j.audio = std::move(pendingAudio_);
  pendingAudio_.clear();
  queue_.push_back(std::move(j));
  lock.unlock();
  cv_.notify_one();
}

void Recorder::pushCodedAu(int64_t tsUs, int64_t srcPtsUs, StoredCodec codec, uint32_t w, uint32_t h,
                           bool keyframe, std::vector<uint8_t> au) {
  std::unique_lock lock(m_);
  if (awaitKeyframe_) {
    if (!keyframe) {
      ++dropped_;
      return;
    }
    awaitKeyframe_ = false;
  }
  ++framesIn_;
  if (static_cast<int>(queue_.size()) >= queueDepth_) {
    ++dropped_;
    awaitKeyframe_ = true;  // the rest of this GOP references a picture that is not stored
    return;
  }
  Job j;
  j.seq = nextSeq_++;
  j.tsUs = tsUs;
  j.srcPtsUs = srcPtsUs;
  j.codedAu = true;
  j.codec = codec;
  j.keyframe = keyframe;
  j.w = w;
  j.h = h;
  j.video = std::move(au);
  j.audio = std::move(pendingAudio_);
  pendingAudio_.clear();
  queue_.push_back(std::move(j));
  lock.unlock();
  cv_.notify_one();
}

void Recorder::pushAudio(std::span<const float> samples, int channels, int sampleRate) {
  if (channels < 1 || sampleRate != store_.header().sampleRate) return;
  std::lock_guard lock(m_);
  const size_t frames = samples.size() / static_cast<size_t>(channels);
  const int outChannels = store_.header().audioChannels;
  for (size_t i = 0; i < frames; ++i) {
    for (int c = 0; c < outChannels; ++c) {
      const int src = c < channels ? c : channels - 1;
      pendingAudio_.push_back(samples[i * static_cast<size_t>(channels) + static_cast<size_t>(src)]);
    }
  }
}

RecorderStats Recorder::stats() const {
  std::lock_guard lock(m_);
  RecorderStats s;
  s.framesIn = framesIn_;
  s.framesWritten = framesWritten_;
  s.dropped = dropped_ + store_.droppedBeforeKeyframe();
  s.queued = static_cast<int>(queue_.size());
  s.encodeMsAvg = encodeCount_ ? encodeMsSum_ / static_cast<double>(encodeCount_) : 0;
  if (!encodeTimes_.empty()) {
    auto sorted = encodeTimes_;
    std::sort(sorted.begin(), sorted.end());
    s.encodeMsP99 = sorted[std::min(sorted.size() - 1, static_cast<size_t>(sorted.size() * 0.99))];
  }
  return s;
}

void Recorder::worker() {
  std::unique_ptr<VmxEncoder> enc;
  uint32_t ew = 0, eh = 0;
  for (;;) {
    Job job;
    {
      std::unique_lock lock(m_);
      cv_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
      if (queue_.empty()) {
        if (stopping_) return;
        continue;
      }
      job = std::move(queue_.front());
      queue_.pop_front();
      ++inflight_;
    }

    const auto t0 = std::chrono::steady_clock::now();
    Done d;
    d.tsUs = job.tsUs;
    d.srcPtsUs = job.srcPtsUs;
    d.audio = std::move(job.audio);
    d.w = job.w;
    d.h = job.h;
    if (job.coded) {
      d.coded = true;
      d.raw = std::move(job.video);
      d.frame.previewLen = vmxPreviewLength(d.raw, job.w, job.h);
    } else if (job.codedAu) {
      d.codedAu = true;
      d.codec = job.codec;
      d.keyframe = job.keyframe;
      d.raw = std::move(job.video);
      d.frame.previewLen = 0;
    } else {
      // One encoder per worker; a size change makes a new one (and the writer closes the segment).
      if (!enc || ew != job.w || eh != job.h) {
        enc = std::make_unique<VmxEncoder>(job.w, job.h, quality_);
        ew = job.w;
        eh = job.h;
      }
      bc::CpuVideoFrame f;
      f.data = job.video.data();
      f.stride = job.stride;
      f.width = job.w;
      f.height = job.h;
      f.format = job.format;
      d.frame = enc->encode(f);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    {
      std::lock_guard lock(m_);
      if (!job.coded && !job.codedAu) {
        encodeMsSum_ += ms;
        ++encodeCount_;
        if (encodeTimes_.size() < 200000) encodeTimes_.push_back(ms);
      }
      done_.emplace(job.seq, std::move(d));
      --inflight_;
    }
    writeCv_.notify_one();
  }
}

void Recorder::writerLoop() {
  for (;;) {
    Done d;
    {
      std::unique_lock lock(m_);
      writeCv_.wait(lock, [&] {
        return (stopping_ && nextWrite_ == nextSeq_ && inflight_ == 0) || done_.count(nextWrite_) != 0;
      });
      auto it = done_.find(nextWrite_);
      if (it == done_.end()) {
        if (stopping_ && nextWrite_ == nextSeq_ && inflight_ == 0) return;
        continue;
      }
      d = std::move(it->second);
      done_.erase(it);
      ++nextWrite_;
    }
    // Compare with the store's header, not the previous frame: an input whose first frame is not
    // the show size (a 720p observer in a 1080p show) must start a segment with its own size.
    const StoredCodec codec = d.codedAu ? d.codec : StoredCodec::Vmx;
    if (d.w && d.h && (d.w != store_.header().width || d.h != store_.header().height ||
                       codec != store_.header().codec)) {
      store_.changeVideoFormat(d.w, d.h, store_.header().rate, codec);
    }
    if (d.codedAu) {
      store_.append(d.tsUs, d.raw, 0, d.audio, d.keyframe, d.srcPtsUs);
    } else {
      const std::span<const uint8_t> bytes =
          d.coded ? std::span<const uint8_t>(d.raw) : std::span<const uint8_t>(d.frame.bytes);
      store_.append(d.tsUs, bytes, d.frame.previewLen, d.audio);
    }
    std::lock_guard lock(m_);
    ++framesWritten_;
  }
}

void Recorder::stop() {
  {
    std::lock_guard lock(m_);
    if (stopping_) return;
    stopping_ = true;
  }
  cv_.notify_all();
  writeCv_.notify_all();
  for (auto& t : workers_) {
    if (t.joinable()) t.join();
  }
  if (writer_.joinable()) writer_.join();
}

}  // namespace br