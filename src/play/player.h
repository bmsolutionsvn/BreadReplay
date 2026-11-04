#pragma once
#include <cstdint>
#include <mutex>
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
  Player(EventStore& events, bc::Rate showRate, bool recordCuts, bool freeze = false,
         std::recursive_mutex* storeLock = nullptr);
  void cue(int eventId);
  void cueCopy(const Event& e);
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
  void endRide();  // finish a live-riding pass without changing play state
  // The event this player reads: a copy when frozen or cued from one, else the store.
  const Event* event() const;

 private:
  struct AnglePass {
    bool active = false;
    int64_t fromUs = 0;
    std::vector<AngleCut> points;
  };
  struct SpeedPass {
    bool active = false;
    int64_t fromUs = 0;
    std::vector<SpeedPoint> points;
  };
  struct ZoomPass {
    bool active = false;
    int64_t fromUs = 0;
    std::vector<ZoomPoint> points;
  };

  double currentSpeed(const Event* e) const;
  std::unique_lock<std::recursive_mutex> lockStore() const;
  void beginPass();
  void endPass();
  void commitPass(bool finish, int64_t toUs);
  void recordAnglePass(const std::string& id);
  void recordSpeedPass(double s);
  void recordZoomPass(ZoomRect r);

  EventStore& events_;
  int64_t periodUs_ = 0;
  bool recordCuts_ = true;
  bool freeze_ = false;
  std::recursive_mutex* lock_ = nullptr;
  PlayerState state_;
  bool ended_ = false;
  std::optional<Event> frozen_;
  bool angleManual_ = false;
  bool zoomManual_ = false;
  bool passHasBase_ = false;
  AnglePass passAngles_;
  SpeedPass passSpeeds_;
  ZoomPass passZooms_;
  std::vector<AngleCut> baseAngles_, frozenBaseAngles_;
  std::vector<SpeedPoint> baseSpeeds_, frozenBaseSpeeds_;
  std::vector<ZoomPoint> baseZooms_, frozenBaseZooms_;
};

}  // namespace br