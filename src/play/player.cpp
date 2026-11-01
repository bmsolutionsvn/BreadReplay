#include "play/player.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace br {

Player::Player(EventStore& events, bc::Rate showRate, bool recordCuts)
    : events_(events), recordCuts_(recordCuts) {
  periodUs_ = static_cast<int64_t>(1'000'000.0 / showRate.fps() + 0.5);
}

void Player::cue(int eventId) {
  const Event* e = events_.find(eventId);
  if (!e) return;
  state_.eventId = eventId;
  state_.posUs = e->inUs;
  state_.angle = angleAt(*e, e->inUs);
  state_.zoom = zoomAt(*e, e->inUs);
  state_.playing = false;
  state_.overriding = false;
  ended_ = false;
}

void Player::cueAt(int64_t t, const std::string& angle) {
  state_.eventId.reset();
  state_.posUs = t;
  state_.angle = angle;
  state_.zoom = ZoomRect{};
  state_.playing = false;
  state_.overriding = false;
  ended_ = false;
}

void Player::play() {
  state_.playing = true;
  ended_ = false;
}

void Player::pause() { state_.playing = false; }

void Player::setDirection(int dir) { state_.dir = dir < 0 ? -1 : +1; }

void Player::step(int frames) {
  state_.playing = false;
  ended_ = false;
  state_.posUs += static_cast<int64_t>(frames) * periodUs_;
  if (const Event* e = state_.eventId ? events_.find(*state_.eventId) : nullptr) {
    state_.posUs = std::clamp(state_.posUs, e->inUs, e->outUs);
  }
}

void Player::setLever(double s) {
  if (state_.leverLocked) return;
  state_.lever = s;
  state_.overriding = true;
  if (recordCuts_ && state_.eventId) events_.recordSpeed(*state_.eventId, state_.posUs, s);
}

void Player::setSpeedPreset(double s) {
  state_.lever = s;
  state_.overriding = true;
  if (recordCuts_ && state_.eventId) events_.recordSpeed(*state_.eventId, state_.posUs, s);
}

void Player::lockLever(bool locked) { state_.leverLocked = locked; }

void Player::setAngle(const std::string& id) {
  state_.angle = id;
  if (recordCuts_ && state_.eventId) events_.recordAngle(*state_.eventId, state_.posUs, id);
}

void Player::setZoom(ZoomRect r) {
  state_.zoom = r;
  if (recordCuts_ && state_.eventId) events_.recordZoom(*state_.eventId, state_.posUs, r);
}

double Player::currentSpeed(const Event* e) const {
  if (e && !state_.overriding) return speedAt(*e, state_.posUs, state_.lever);
  return state_.lever;
}

TickResult Player::tick() {
  TickResult r;
  const Event* e = state_.eventId ? events_.find(*state_.eventId) : nullptr;
  if (e) {
    r.inputId = angleAt(*e, state_.posUs);
    r.zoom = zoomAt(*e, state_.posUs);
  } else {
    r.zoom = state_.zoom;
  }
  r.angleChanged = !r.inputId.empty() && r.inputId != state_.angle;
  if (r.angleChanged) state_.angle = r.inputId;
  if (r.inputId.empty()) r.inputId = state_.angle;

  r.speed = currentSpeed(e);
  r.sourceUs = state_.posUs;

  if (state_.playing) {
    state_.posUs += state_.dir * static_cast<int64_t>(std::llround(static_cast<double>(periodUs_) * r.speed));
    if (e) {
      if (state_.dir > 0 && state_.posUs >= e->outUs) {
        state_.posUs = e->outUs;
        state_.playing = false;
        ended_ = true;
      } else if (state_.dir < 0 && state_.posUs <= e->inUs) {
        state_.posUs = e->inUs;
        state_.playing = false;
        ended_ = true;
      }
    }
  }
  r.ended = ended_;
  return r;
}

double Player::remainingSeconds() const {
  const Event* e = state_.eventId ? events_.find(*state_.eventId) : nullptr;
  if (!e) return std::numeric_limits<double>::infinity();
  if (state_.posUs >= e->outUs) return 0.0;

  auto safe = [](double speed) { return speed > 0.0 ? speed : 0.0; };
  double total = 0;
  if (state_.overriding) {
    const double s = safe(state_.lever);
    if (s == 0.0) return std::numeric_limits<double>::infinity();
    return static_cast<double>(e->outUs - state_.posUs) / 1'000'000.0 / s;
  }

  int64_t t = state_.posUs;
  double speed = safe(speedAt(*e, t, 1.0));
  for (const auto& point : e->speeds) {
    if (point.tUs <= t) continue;
    if (point.tUs >= e->outUs) break;
    const int64_t dt = point.tUs - t;
    if (speed == 0.0) return std::numeric_limits<double>::infinity();
    total += static_cast<double>(dt) / 1'000'000.0 / speed;
    t = point.tUs;
    speed = safe(point.speed);
  }
  const int64_t dt = e->outUs - t;
  if (speed == 0.0) return std::numeric_limits<double>::infinity();
  total += static_cast<double>(dt) / 1'000'000.0 / speed;
  return total;
}

void Player::restore(const PlayerState& s) { state_ = s; }

}  // namespace br