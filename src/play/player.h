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
  // waitForStore=false is for the ticker: the cue/play still happens from the copy, and any
  // pending pass write lands on the next tick that gets the command lock.
  void cueCopy(const Event& e, bool waitForStore = true);
  void cueAt(int64_t t, const std::string& angle);
  void play(bool waitForStore = true);
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
  const PlayerState& state() const { return state_; }  // single-threaded tests only
  PlayerState snapshot() const;                        // a copy under the player mutex
  void restore(const PlayerState&);
  void endRide();  // finish a live-riding pass without changing play state
  // The event this player plays: the same copy tick() reads.
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
  // The pass write a tick computed while the command lock was busy.
  struct PendingWrites {
    int eventId = 0;
    bool angles = false, speeds = false, zooms = false;
    std::vector<AngleCut> angleValues;
    std::vector<SpeedPoint> speedValues;
    std::vector<ZoomPoint> zoomValues;
  };

  double currentSpeed(const Event* e) const;
  std::unique_lock<std::recursive_mutex> lockStore(bool wait = true) const;
  std::unique_lock<std::recursive_mutex> tryStore() const;
  void refreshCopy();   // requires the store lock
  void flushPending();  // requires the store lock
  PendingWrites& pendingFor(int eventId);
  void beginPass(bool storeOwned);
  void endPass(bool storeOwned = true);
  void commitPass(bool finish, int64_t toUs, bool storeOwned);
  void recordAnglePass(const std::string& id);
  void recordSpeedPass(double s);
  void recordZoomPass(ZoomRect r);

  EventStore& events_;
  int64_t periodUs_ = 0;
  bool recordCuts_ = true;
  bool freeze_ = false;
  std::recursive_mutex* lock_ = nullptr;
  mutable std::mutex m_;  // state_, copy_, pass state; never held while blocking on lock_
  PlayerState state_;
  bool ended_ = false;
  std::optional<Event> copy_;
  uint64_t copyVersion_ = 0;
  bool storeDirty_ = false;
  std::vector<PendingWrites> pending_;
  int passEventId_ = 0;
  int64_t passStoreOutUs_ = 0;
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