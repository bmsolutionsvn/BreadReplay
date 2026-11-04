#include "play/output_player.h"

#include <algorithm>
#include <chrono>

#include "core/tick_schedule.h"

namespace br {
namespace {
int64_t periodUs(bc::Rate rate) {
  return static_cast<int64_t>(1'000'000.0 / rate.fps() + 0.5);
}
}  // namespace

OutputPlayer::OutputPlayer(Player& p, FrameSource& src, Renderer& r, EventStore& events,
                           const ShowSettings& s)
    : player_(p), src_(src), renderer_(r), events_(events), settings_(s) {
  fading_ = false;
}

void OutputPlayer::setOutputs(std::vector<std::unique_ptr<bc::Output>> outs) {
  outs_ = std::move(outs);
  for (auto& o : outs_) o->start();
}

void OutputPlayer::playHighlight(const std::string& listId) { playEvents(events_.orderOf(listId)); }

void OutputPlayer::playEvents(std::vector<int> ids) {
  const auto display = events_.displayOrder();
  ids.erase(std::remove_if(ids.begin(), ids.end(), [&](int id) { return !events_.find(id); }), ids.end());
  std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) {
    return std::find(display.begin(), display.end(), a) < std::find(display.begin(), display.end(), b);
  });
  highlight_ = std::move(ids);
  if (highlight_.empty()) return;
  highlightIndex_ = 0;
  playingHighlight_ = true;
  fading_ = false;
  fadeElapsedUs_ = 0;
  fadePlayer_ = std::make_unique<Player>(events_, settings_.rate, false);
  player_.cue(highlight_.front());
  player_.play();
}

bc::TextureRef OutputPlayer::renderAt(const TickResult& tr) {
  const auto frame = src_.get(tr.inputId, tr.sourceUs);
  if (!frame) {
    std::lock_guard lock(heldMutex_);
    return held_;
  }
  const auto pic = renderer_.render(*frame, tr.zoom);
  {
    std::lock_guard lock(heldMutex_);
    held_ = pic;
  }
  return pic;
}

void OutputPlayer::advanceHighlight() {
  fading_ = false;
  fadeElapsedUs_ = 0;
  ++highlightIndex_;
  if (highlightIndex_ < highlight_.size()) {
    player_.cue(highlight_[highlightIndex_]);
    player_.play();
  } else {
    playingHighlight_ = false;
  }
}

void OutputPlayer::submit(bc::TextureRef pic, const TickResult& tr, bool angleChanged) {
  bc::OutFrame frame;
  frame.tick = tick_++;
  frame.rate = settings_.rate;
  frame.linear = pic;
  bool needCpu = false;
  bool needGpu = false;
  for (const auto& o : outs_) {
    needCpu = needCpu || o->needsCpu();
    needGpu = needGpu || o->wantsGpuRgba8();
  }
  if (needCpu && pic) frame.bgra = std::make_shared<const bc::CpuImage>(renderer_.toCpu(pic));
  // The GPU frame, made once per tick and only when an output pulls it (the Output contract).
  if (needGpu && pic) frame.rgba8 = renderer_.toRgba8(pic);

  const bc::TickSchedule schedule(settings_.rate);
  const uint32_t samples = schedule.audioFramesForTick(frame.tick, 48000);
  const auto decoded = src_.get(tr.inputId, tr.sourceUs);
  frame.audio = std::make_shared<const std::vector<float>>(
      audio_.forTick(decoded.get(), tr.speed, angleChanged, samples));
  frame.sampleRate = 48000;
  frame.channels = 2;

  // Each output gets only the picture it asked for, as BreadCamera's Camera::endTick does.
  for (auto& o : outs_) {
    bc::OutFrame forOut = frame;
    if (!o->needsCpu()) forOut.bgra.reset();
    if (!o->wantsGpuRgba8()) forOut.rgba8 = nullptr;
    o->submit(std::move(forOut));
  }
}

void OutputPlayer::tickOnce() {
  const TickResult tr = player_.tick();
  bc::TextureRef pic = renderAt(tr);

  if (playingHighlight_ && settings_.transition == "fade" && settings_.fadeS > 0 && fadePlayer_ &&
      highlightIndex_ + 1 < highlight_.size() && player_.state().playing) {
    const bool inFade = player_.remainingSeconds() <= settings_.fadeS;
    if (inFade) {
      if (!fading_) {
        fading_ = true;
        fadeElapsedUs_ = 0;
        fadePlayer_->cue(highlight_[highlightIndex_ + 1]);
        fadePlayer_->play();
      }
      const TickResult next = fadePlayer_->tick();
      const auto nextFrame = src_.get(next.inputId, next.sourceUs);
      if (nextFrame && pic) {
        const auto nextPic = renderer_.render(*nextFrame, next.zoom);
        const float t = std::clamp(static_cast<float>(static_cast<double>(fadeElapsedUs_) /
                                                      (settings_.fadeS * 1'000'000.0)),
                                   0.0f, 1.0f);
        pic = renderer_.mix(pic, nextPic, t);
      }
      fadeElapsedUs_ += periodUs(settings_.rate);
    }
  }

  submit(pic, tr, tr.angleChanged);

  if (tr.ended && playingHighlight_) advanceHighlight();
}

void OutputPlayer::start() {
  if (running_) return;
  running_ = true;
  for (auto& o : outs_) o->start();
  thread_ = std::thread([this] {
    const bc::TickSchedule schedule(settings_.rate);
    const auto startTime = std::chrono::steady_clock::now();
    uint64_t index = 0;
    while (running_) {
      const auto deadline = startTime + schedule.offset(index);
      std::this_thread::sleep_until(deadline);
      if (!running_) break;
      tickOnce();
      ++index;
    }
  });
}

void OutputPlayer::stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
  for (auto& o : outs_) o->stop();
}

bool OutputPlayer::idle() const { return !player_.state().playing && !playingHighlight_; }

}  // namespace br