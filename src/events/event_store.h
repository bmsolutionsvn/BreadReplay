#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "store/retention.h"

namespace br {

struct ZoomRect {
  float cx = 0.5f, cy = 0.5f, scale = 1.0f;  // scale 1..4
};

struct AngleCut {
  int64_t tUs = 0;
  std::string inputId;
};

struct SpeedPoint {
  int64_t tUs = 0;
  double speed = 1.0;
};

struct ZoomPoint {
  int64_t tUs = 0;
  ZoomRect rect;
};

struct Event {
  int id = 0;
  std::string listId, name;
  std::vector<std::string> tags;
  std::map<std::string, std::string> angleNotes;
  int64_t inUs = 0, outUs = 0;
  std::vector<AngleCut> angles;
  std::vector<SpeedPoint> speeds;
  std::vector<ZoomPoint> zooms;
};

struct EventList {
  std::string id, name;
  std::vector<int> order;
};

class EventStore {
 public:
  explicit EventStore(int64_t preRollUs = 0, int64_t postRollUs = 0);
  Event& markIn(int64_t t, const std::string& angle);
  Event& markOut(int64_t t, const std::string& angle, int64_t lastNUs);
  Event& lastN(int64_t nowUs, int64_t nUs, const std::string& angle);
  void trim(int id, int64_t inDeltaUs, int64_t outDeltaUs);
  void remove(int id);
  void clearCuts(int id);
  void recordAngle(int id, int64_t t, const std::string& inputId);
  void recordSpeed(int id, int64_t t, double s);
  void recordZoom(int id, int64_t t, ZoomRect r);
  const std::vector<int>& orderOf(const std::string& listId) const;
  std::vector<int> displayOrder() const;
  std::string newList(const std::string& name);
  bool renameList(const std::string& id, const std::string& name);
  void deleteList(const std::string& id);
  void selectList(const std::string& id);
  void reorder(const std::string& listId, std::vector<int> order);
  void moveEvents(const std::vector<int>& ids, const std::string& toList, std::optional<int> beforeId);
  const Event* find(int id) const;
  Event* find(int id);
  const std::vector<EventList>& lists() const { return lists_; }
  const std::string& currentList() const { return currentList_; }
  const std::vector<Event>& events() const { return events_; }
  std::vector<ProtectedRange> protectedRanges() const;
  uint64_t version() const { return version_; }
  void bumpVersion() { ++version_; }
  void setRoll(int64_t preUs, int64_t postUs) {
    preRollUs_ = preUs;
    postRollUs_ = postUs;
  }
  // Replaces everything and leaves only the version newer than both, so clients see a change.
  void restoreFrom(const EventStore& snap);
  nlohmann::json toJson() const;
  static EventStore fromJson(const nlohmann::json&);
  double defaultSpeed = 0.5;

 private:
  Event& makeEvent(int64_t in, int64_t out, const std::string& angle);
  void resetPoints(Event& e, const std::string& angle);

  std::vector<Event> events_;
  std::vector<int> rootOrder_;
  std::vector<EventList> lists_;
  std::string currentList_;
  int nextId_ = 1;
  int nextListId_ = 1;
  std::optional<int> openId_;
  int64_t preRollUs_ = 0, postRollUs_ = 0;
  uint64_t version_ = 1;
};

std::string angleAt(const Event&, int64_t t);
double speedAt(const Event&, int64_t t, double fallback);
ZoomRect zoomAt(const Event&, int64_t t);

}  // namespace br