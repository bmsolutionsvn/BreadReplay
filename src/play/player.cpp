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

std::unique_lock<std::recursive_mutex> Player::tryStore() const {
  if (lock_) return std::unique_lock<std::recursive_mutex>(*lock_, std::try_to_lock);
  return {};
}

const Event* Player::event() const { return copy_ ? &*copy_ : nullptr; }

void Player::refreshCopy() {
  if (!freeze_ && copy_ && !state_.eventId) copy_.reset();
  if (freeze_ || !state_.eventId) return;
  if (copy_ && events_.version() == copyVersion_) return;
  if (const Event* e = events_.find(*state_.eventId)) {
    copy_ = *e;
    copyVersion_ = events_.version();
  } else {
    copy_.reset();
  }
}

void Player::flushPending() {
  if (!storeDirty_) return;
  if (passEventId_ != 0) {
    if (pending_.angles) events_.setAngles(passEventId_, std::move(pending_.angleValues));
    if (pending_.speeds) events_.setSpeeds(passEventId_, std::move(pending_.speedValues));
    if (pending_.zooms) events_.setZooms(passEventId_, std::move(pending_.zoomValues));
  }
  pending_ = {};
  storeDirty_ = false;
}

void Player::cue(int eventId) {
  auto held = lockStore();
  std::lock_guard pl(m_);
  flushPending();
  const Event* e = events_.find(eventId);
  if (!e) return;
  endPass(true);
  copy_ = *e;
  copyVersion_ = events_.version();
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
  std::lock_guard pl(m_);
  flushPending();
  endPass(true);
  copy_ = e;
  copyVersion_ = events_.version();
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
  std::lock_guard pl(m_);
  flushPending();
  endPass(true);
  copy_.reset();
  copyVersion_ = 0;
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
  passEventId_ = 0;
  const Event* store = state_.eventId ? events_.find(*state_.eventId) : nullptr;
  const Event* picture = event();
  if (!recordCuts_ || !store || !picture || state_.dir != +1) return;
  baseAngles_ = store->angles;
  baseSpeeds_ = store->speeds;
  baseZooms_ = store->zooms;
  frozenBaseAngles_ = picture->angles;
  frozenBaseSpeeds_ = picture->speeds;
  frozenBaseZooms_ = picture->zooms;
  passEventId_ = store->id;
  passStoreOutUs_ = store->outUs;
  passHasBase_ = true;
}

// ponytail: a timeline edit to the same kind during a pass is overwritten by the pass's base;
// split the base per point if that matters.
void Player::commitPass(bool finish, int64_t toUs, bool storeOwned) {
  if (passEventId_ == 0) return;

  if (passAngles_.active) {
    const auto [from, to] = passRange(passAngles_, toUs);
    pending_.angleValues = finish ? finishPass(baseAngles_, passAngles_.points, from, to, passStoreOutUs_)
                                  : splicePass(baseAngles_, passAngles_.points, from, to);
    pending_.angles = true;
    if (copy_) {
      copy_->angles = finish ? finishPass(frozenBaseAngles_, passAngles_.points, from, to, copy_->outUs)
                             : splicePass(frozenBaseAngles_, passAngles_.points, from, to);
    }
    if (finish) passAngles_ = {};
  }
  if (passSpeeds_.active) {
    const auto [from, to] = passRange(passSpeeds_, toUs);
    pending_.speedValues = finish ? finishPass(baseSpeeds_, passSpeeds_.points, from, to, passStoreOutUs_)
                                  : splicePass(baseSpeeds_, passSpeeds_.points, from, to);
    pending_.speeds = true;
    if (copy_) {
      copy_->speeds = finish ? finishPass(frozenBaseSpeeds_, passSpeeds_.points, from, to, copy_->outUs)
                             : splicePass(frozenBaseSpeeds_, passSpeeds_.points, from, to);
    }
    if (finish) passSpeeds_ = {};
  }
  if (passZooms_.active) {
    const auto [from, to] = passRange(passZooms_, toUs);
    pending_.zoomValues = finish ? finishPass(baseZooms_, passZooms_.points, from, to, passStoreOutUs_)
                                 : splicePass(baseZooms_, passZooms_.points, from, to);
    pending_.zooms = true;
    if (copy_) {
      copy_->zooms = finish ? finishPass(frozenBaseZooms_, passZooms_.points, from, to, copy_->outUs)
                            : splicePass(frozenBaseZooms_, passZooms_.points, from, to);
    }
    if (finish) passZooms_ = {};
  }
  storeDirty_ = storeDirty_ || pending_.angles || pending_.speeds || pending_.zooms;
  if (storeOwned) flushPending();
}

void Player::endPass(bool storeOwned) {
  if (!passHasBase_) {
    passAngles_ = {};
    passSpeeds_ = {};
    passZooms_ = {};
    if (storeOwned) flushPending();
    return;
  }
  commitPass(true, state_.posUs, storeOwned);
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
  commitPass(false, state_.posUs, true);
}

void Player::recordSpeedPass(double s) {
  if (!passSpeeds_.active) {
    passSpeeds_.active = true;
    passSpeeds_.fromUs = state_.posUs;
    passSpeeds_.points.clear();
  }
  upsert(passSpeeds_.points, SpeedPoint{state_.posUs, s});
  commitPass(false, state_.posUs, true);
}

void Player::recordZoomPass(ZoomRect r) {
  if (!passZooms_.active) {
    passZooms_.active = true;
    passZooms_.fromUs = state_.posUs;
    passZooms_.points.clear();
  }
  upsert(passZooms_.points, ZoomPoint{state_.posUs, r});
  commitPass(false, state_.posUs, true);
}

void Player::play() {
  auto held = lockStore();
  std::lock_guard pl(m_);
  flushPending();
  refreshCopy();
  state_.playing = true;
  ended_ = false;
  angleManual_ = false;
  zoomManual_ = false;
  beginPass();
}

void Player::pause() {
  auto held = lockStore();
  std::lock_guard pl(m_);
  flushPending();
  refreshCopy();
  endPass(true);
  state_.playing = false;
}

void Player::setDirection(int dir) {
  auto held = lockStore();
  std::lock_guard pl(m_);
  flushPending();
  refreshCopy();
  endPass(true);
  state_.dir = dir < 0 ? -1 : +1;
}

void Player::step(int frames) {
  auto held = lockStore();
  std::lock_guard pl(m_);
  flushPending();
  refreshCopy();
  endPass(true);
  state_.playing = false;
  ended_ = false;
  state_.posUs += static_cast<int64_t>(frames) * periodUs_;
  if (const Event* e = event()) {
    state_.posUs = std::clamp(state_.posUs, e->inUs, e->outUs);
  }
}

void Player::endRide() {
  auto held = lockStore();
  std::lock_guard pl(m_);
  flushPending();
  endPass(true);
}

void Player::setLever(double s) {
  auto held = lockStore();
  std::lock_guard pl(m_);
  flushPending();
  refreshCopy();
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
  std::lock_guard pl(m_);
  flushPending();
  refreshCopy();
  state_.lever = s;
  state_.overriding = true;
  if (!recordCuts_ || !state_.eventId) return;
  if (state_.playing) {
    if (state_.dir > 0) recordSpeedPass(s);
  } else {
    events_.recordSpeed(*state_.eventId, state_.posUs, s);
  }
}

void Player::lockLever(bool locked) {
  std::lock_guard pl(m_);
  state_.leverLocked = locked;
}

void Player::setAngle(const std::string& id) {
  auto held = lockStore();
  std::lock_guard pl(m_);
  flushPending();
  refreshCopy();
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
  std::lock_guard pl(m_);
  flushPending();
  refreshCopy();
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

PlayerState Player::snapshot() const {
  std::lock_guard pl(m_);
  return state_;
}

double Player::currentSpeed(const Event* e) const {
  if (e && !state_.overriding) return speedAt(*e, state_.posUs, state_.lever);
  return state_.lever;
}

TickResult Player::tick() {
  std::lock_guard pl(m_);
  std::unique_lock<std::recursive_mutex> store = tryStore();
  const bool storeOwned = !lock_ || store.owns_lock();
  if (storeOwned) {
    flushPending();
    refreshCopy();
  }

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
    if (state_.posUs != before && passHasBase_) commitPass(false, state_.posUs, storeOwned);
  }
  if (ended_ && passHasBase_) endPass(storeOwned);
  r.ended = ended_;
  return r;
}

double Player::remainingSeconds() const {
  std::lock_guard pl(m_);
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
  std::lock_guard pl(m_);
  flushPending();
  endPass(true);
  state_ = s;
  angleManual_ = false;
  zoomManual_ = false;
  copy_.reset();
  copyVersion_ = 0;
  if (s.eventId) {
    if (const Event* e = events_.find(*s.eventId)) {
      copy_ = *e;
      copyVersion_ = events_.version();
    }
  }
}

}  // namespace br