#pragma once
#include <cstdint>
#include <optional>
#include <string>

#include "core/rate.h"
#include "events/event_store.h"

namespace br {

struct PlayerState {
  int64_t posUs = 0;
  std::optional<int> eventId;
  std::string angle;
  ZoomRect zoom;
  double lever = 1.0;
  bool leverLocked = true;
  bool overriding = false;
  bool playing = false;
  int dir = +1;
};

struct TickResult {
  std::string inputId;
  int64_t sourceUs = 0;
  ZoomRect zoom;
  double speed = 1.0;
  bool ended = false;
  bool angleChanged = false;
};

class Player {
 public:
  Player(EventStore& events, bc::Rate showRate, bool recordCuts);
  void cue(int eventId);
  void cueAt(int64_t t, const std::string& angle);
  void play();
  void pause();
  void setDirection(int dir);
  void step(int frames);  // ± show periods, pauses
  void setLever(double s);  // ignored while locked
  void setSpeedPreset(double s);  // keys 1-4: works while locked
  void lockLever(bool locked);
  void setAngle(const std::string& id);
  void setZoom(ZoomRect r);
  TickResult tick();  // advance by one show period
  double remainingSeconds() const;  // to Out; +inf at speed 0
  const PlayerState& state() const { return state_; }
  void restore(const PlayerState&);

 private:
  double currentSpeed(const Event* e) const;

  EventStore& events_;
  int64_t periodUs_ = 0;
  bool recordCuts_ = true;
  PlayerState state_;
  bool ended_ = false;
};

}  // namespace br