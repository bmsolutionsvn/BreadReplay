#include "play/player.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "events/pass.h"

namespace br {
namespace {
template <typename Point>
void upsert(std::vector<Point>& points, const Point& p) {
  const auto at = std::find_if(points.begin(), points.end(), [&](const Point& q) { return q.tUs == p.tUs; });
  if (at != points.end()) {
    *at = p;
  } else {
    points.push_back(p);
  }
  std::sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.tUs < b.tUs; });
}

template <typename Pass>
std::pair<int64_t, int64_t> passRange(const Pass& pass, int64_t toUs) {
  return {std::min(pass.fromUs, toUs), std::max(pass.fromUs, toUs)};
}
}  // namespace

Player::Player(EventStore& events, bc::Rate showRate, bool recordCuts, bool freeze,
               std::recursive_mutex* storeLock)
    : events_(events), recordCuts_(recordCuts), freeze_(freeze), lock_(storeLock) {
  periodUs_ = static_cast<int64_t>(1'000'000.0 / showRate.fps() + 0.5);
}

std::unique_lock<std::recursive_mutex> Player::lockStore() const {
  if (lock_) return std::unique_lock<std::recursive_mutex>(*lock_);
  return {};
}

const Event* Player::event() const {
  if (frozen_) return &*frozen_;
  return state_.eventId ? events_.find(*state_.eventId) : nullptr;
}

void Player::cue(int eventId) {
  auto held = lockStore();
  const Event* e = events_.find(eventId);
  if (!e) return;
  endPass();
  if (freeze_) {
    frozen_ = *e;
  } else {
    frozen_.reset();
  }
  state_.eventId = eventId;
  state_.posUs = e->inUs;
  state_.angle = angleAt(*e, e->inUs);
  state_.zoom = zoomAt(*e, e->inUs);
  state_.playing = false;
  state_.overriding = false;
  angleManual_ = false;
  zoomManual_ = false;
  ended_ = false;
}

void Player::cueCopy(const Event& e) {
  auto held = lockStore();
  endPass();
  frozen_ = e;
  state_.eventId = e.id;
  state_.posUs = e.inUs;
  state_.angle = angleAt(e, e.inUs);
  state_.zoom = zoomAt(e, e.inUs);
  state_.playing = false;
  state_.overriding = false;
  angleManual_ = false;
  zoomManual_ = false;
  ended_ = false;
}

void Player::cueAt(int64_t t, const std::string& angle) {
  auto held = lockStore();
  endPass();
  frozen_.reset();
  state_.eventId.reset();
  state_.posUs = t;
  state_.angle = angle;
  state_.zoom = ZoomRect{};
  state_.playing = false;
  state_.overriding = false;
  angleManual_ = false;
  zoomManual_ = false;
  ended_ = false;
}

void Player::beginPass() {
  passAngles_ = {};
  passSpeeds_ = {};
  passZooms_ = {};
  passHasBase_ = false;
  const Event* store = state_.eventId ? events_.find(*state_.eventId) : nullptr;
  const Event* picture = event();
  if (!recordCuts_ || !store || !picture || state_.dir != +1) return;
  baseAngles_ = store->angles;
  baseSpeeds_ = store->speeds;
  baseZooms_ = store->zooms;
  frozenBaseAngles_ = frozen_ ? frozen_->angles : store->angles;
  frozenBaseSpeeds_ = frozen_ ? frozen_->speeds : store->speeds;
  frozenBaseZooms_ = frozen_ ? frozen_->zooms : store->zooms;
  passHasBase_ = true;
}

// ponytail: a timeline edit to the same kind during a pass is overwritten by the pass's base;
// split the base per point if that matters.
void Player::commitPass(bool finish, int64_t toUs) {
  const Event* store = state_.eventId ? events_.find(*state_.eventId) : nullptr;
  if (!store) return;

  if (passAngles_.active) {
    const auto [from, to] = passRange(passAngles_, toUs);
    const auto next = finish ? finishPass(baseAngles_, passAngles_.points, from, to, store->outUs)
                             : splicePass(baseAngles_, passAngles_.points, from, to);
    events_.setAngles(store->id, next);
    if (frozen_) {
      frozen_->angles = finish ? finishPass(frozenBaseAngles_, passAngles_.points, from, to, frozen_->outUs)
                               : splicePass(frozenBaseAngles_, passAngles_.points, from, to);
    }
    if (finish) passAngles_ = {};
  }
  if (passSpeeds_.active) {
    const auto [from, to] = passRange(passSpeeds_, toUs);
    const auto next = finish ? finishPass(baseSpeeds_, passSpeeds_.points, from, to, store->outUs)
                             : splicePass(baseSpeeds_, passSpeeds_.points, from, to);
    events_.setSpeeds(store->id, next);
    if (frozen_) {
      frozen_->speeds = finish ? finishPass(frozenBaseSpeeds_, passSpeeds_.points, from, to, frozen_->outUs)
                               : splicePass(frozenBaseSpeeds_, passSpeeds_.points, from, to);
    }
    if (finish) passSpeeds_ = {};
  }
  if (passZooms_.active) {
    const auto [from, to] = passRange(passZooms_, toUs);
    const auto next = finish ? finishPass(baseZooms_, passZooms_.points, from, to, store->outUs)
                             : splicePass(baseZooms_, passZooms_.points, from, to);
    events_.setZooms(store->id, next);
    if (frozen_) {
      frozen_->zooms = finish ? finishPass(frozenBaseZooms_, passZooms_.points, from, to, frozen_->outUs)
                              : splicePass(frozenBaseZooms_, passZooms_.points, from, to);
    }
    if (finish) passZooms_ = {};
  }
}

void Player::endPass() {
  if (!passHasBase_) {
    passAngles_ = {};
    passSpeeds_ = {};
    passZooms_ = {};
    return;
  }
  commitPass(true, state_.posUs);
  passAngles_ = {};
  passSpeeds_ = {};
  passZooms_ = {};
  passHasBase_ = false;
}

void Player::recordAnglePass(const std::string& id) {
  if (!passAngles_.active) {
    passAngles_.active = true;
    passAngles_.fromUs = state_.posUs;
    passAngles_.points.clear();
  }
  upsert(passAngles_.points, AngleCut{state_.posUs, id});
  commitPass(false, state_.posUs);
}

void Player::recordSpeedPass(double s) {
  if (!passSpeeds_.active) {
    passSpeeds_.active = true;
    passSpeeds_.fromUs = state_.posUs;
    passSpeeds_.points.clear();
  }
  upsert(passSpeeds_.points, SpeedPoint{state_.posUs, s});
  commitPass(false, state_.posUs);
}

void Player::recordZoomPass(ZoomRect r) {
  if (!passZooms_.active) {
    passZooms_.active = true;
    passZooms_.fromUs = state_.posUs;
    passZooms_.points.clear();
  }
  upsert(passZooms_.points, ZoomPoint{state_.posUs, r});
  commitPass(false, state_.posUs);
}

void Player::play() {
  auto held = lockStore();
  state_.playing = true;
  ended_ = false;
  angleManual_ = false;
  zoomManual_ = false;
  beginPass();
}

void Player::pause() {
  auto held = lockStore();
  endPass();
  state_.playing = false;
}

void Player::setDirection(int dir) {
  auto held = lockStore();
  endPass();
  state_.dir = dir < 0 ? -1 : +1;
}

void Player::step(int frames) {
  auto held = lockStore();
  endPass();
  state_.playing = false;
  ended_ = false;
  state_.posUs += static_cast<int64_t>(frames) * periodUs_;
  if (const Event* e = event()) {
    state_.posUs = std::clamp(state_.posUs, e->inUs, e->outUs);
  }
}

void Player::endRide() {
  auto held = lockStore();
  endPass();
}

void Player::setLever(double s) {
  auto held = lockStore();
  if (state_.leverLocked) return;
  state_.lever = s;
  state_.overriding = true;
  if (!recordCuts_ || !state_.eventId) return;
  if (state_.playing) {
    if (state_.dir > 0) recordSpeedPass(s);
  } else {
    events_.recordSpeed(*state_.eventId, state_.posUs, s);
  }
}

void Player::setSpeedPreset(double s) {
  auto held = lockStore();
  state_.lever = s;
  state_.overriding = true;
  if (!recordCuts_ || !state_.eventId) return;
  if (state_.playing) {
    if (state_.dir > 0) recordSpeedPass(s);
  } else {
    events_.recordSpeed(*state_.eventId, state_.posUs, s);
  }
}

void Player::lockLever(bool locked) { state_.leverLocked = locked; }

void Player::setAngle(const std::string& id) {
  auto held = lockStore();
  state_.angle = id;
  if (!recordCuts_ || !state_.eventId) {
    angleManual_ = true;
    return;
  }
  if (state_.playing && state_.dir > 0) {
    recordAnglePass(id);
  } else {
    angleManual_ = true;
  }
}

void Player::setZoom(ZoomRect r) {
  auto held = lockStore();
  state_.zoom = r;
  if (!recordCuts_ || !state_.eventId) {
    zoomManual_ = true;
    return;
  }
  if (state_.playing && state_.dir > 0) {
    recordZoomPass(r);
  } else {
    zoomManual_ = true;
  }
}

double Player::currentSpeed(const Event* e) const {
  if (e && !state_.overriding) return speedAt(*e, state_.posUs, state_.lever);
  return state_.lever;
}

TickResult Player::tick() {
  auto held = lockStore();
  TickResult r;
  const Event* e = event();
  if (e) {
    r.inputId = angleManual_ ? state_.angle : angleAt(*e, state_.posUs);
    r.zoom = zoomManual_ ? state_.zoom : zoomAt(*e, state_.posUs);
  } else {
    r.inputId = state_.angle;
    r.zoom = state_.zoom;
  }
  r.angleChanged = !r.inputId.empty() && r.inputId != state_.angle;
  if (r.angleChanged) state_.angle = r.inputId;
  if (r.inputId.empty()) r.inputId = state_.angle;

  r.speed = currentSpeed(e);
  r.sourceUs = state_.posUs;

  if (state_.playing) {
    const int64_t before = state_.posUs;
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
    if (state_.posUs != before && passHasBase_) commitPass(false, state_.posUs);
  }
  if (ended_ && passHasBase_) endPass();
  r.ended = ended_;
  return r;
}

double Player::remainingSeconds() const {
  auto held = lockStore();
  const Event* e = event();
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

void Player::restore(const PlayerState& s) {
  auto held = lockStore();
  endPass();
  state_ = s;
  angleManual_ = false;
  zoomManual_ = false;
  if (freeze_ && s.eventId) {
    if (const Event* e = events_.find(*s.eventId)) frozen_ = *e;
  } else {
    frozen_.reset();
  }
}

}  // namespace br