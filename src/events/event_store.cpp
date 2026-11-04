#include "events/event_store.h"

#include <algorithm>
#include <stdexcept>

namespace br {
namespace {
template <typename Point>
void overwriteFrom(std::vector<Point>& points, int64_t t) {
  points.erase(std::remove_if(points.begin(), points.end(), [&](const Point& p) { return p.tUs >= t; }), points.end());
}
}  // namespace

EventStore::EventStore(int64_t preRollUs, int64_t postRollUs)
    : preRollUs_(preRollUs), postRollUs_(postRollUs) {}

void EventStore::resetPoints(Event& e, const std::string& angle) {
  e.angles = {AngleCut{e.inUs, angle}};
  e.speeds = {SpeedPoint{e.inUs, defaultSpeed}};
  e.zooms = {ZoomPoint{e.inUs, ZoomRect{}}};
}

Event& EventStore::makeEvent(int64_t in, int64_t out, const std::string& angle) {
  Event e;
  e.id = nextId_++;
  e.listId = currentList_;
  e.name = "Event " + std::to_string(e.id);
  e.inUs = in;
  e.outUs = out;
  resetPoints(e, angle);
  events_.push_back(std::move(e));
  if (currentList_.empty()) {
    rootOrder_.push_back(events_.back().id);
  } else {
    for (auto& l : lists_) {
      if (l.id == currentList_) l.order.push_back(events_.back().id);
    }
  }
  ++version_;
  return events_.back();
}

Event& EventStore::markIn(int64_t t, const std::string& angle) {
  const int64_t in = t - preRollUs_;
  if (openId_) {
    Event* e = find(*openId_);
    if (e) {
      e->inUs = in;
      if (e->outUs < e->inUs) e->outUs = e->inUs;
      resetPoints(*e, angle);
      ++version_;
      return *e;
    }
  }
  Event& e = makeEvent(in, in, angle);
  openId_ = e.id;
  return e;
}

Event& EventStore::markOut(int64_t t, const std::string& angle, int64_t lastNUs) {
  const int64_t out = t + postRollUs_;
  if (openId_) {
    Event* e = find(*openId_);
    if (e) {
      e->outUs = std::max(out, e->inUs);
      openId_.reset();
      ++version_;
      return *e;
    }
  }
  return lastN(t, lastNUs, angle);
}

Event& EventStore::lastN(int64_t nowUs, int64_t nUs, const std::string& angle) {
  Event& e = makeEvent(nowUs - nUs, nowUs, angle);
  openId_.reset();
  return e;
}

void EventStore::trim(int id, int64_t inDeltaUs, int64_t outDeltaUs) {
  Event* e = find(id);
  if (!e) return;
  e->inUs += inDeltaUs;
  e->outUs += outDeltaUs;
  if (e->outUs < e->inUs) e->outUs = e->inUs;
  ++version_;
}

void EventStore::remove(int id) {
  events_.erase(std::remove_if(events_.begin(), events_.end(), [&](const Event& e) { return e.id == id; }),
                events_.end());
  for (auto& l : lists_) l.order.erase(std::remove(l.order.begin(), l.order.end(), id), l.order.end());
  rootOrder_.erase(std::remove(rootOrder_.begin(), rootOrder_.end(), id), rootOrder_.end());
  if (openId_ == id) openId_.reset();
  ++version_;
}

void EventStore::clearCuts(int id) {
  Event* e = find(id);
  if (!e) return;
  const std::string angle = e->angles.empty() ? std::string() : e->angles.front().inputId;
  e->angles = angle.empty() ? std::vector<AngleCut>{} : std::vector<AngleCut>{AngleCut{e->inUs, angle}};
  e->speeds = {SpeedPoint{e->inUs, defaultSpeed}};
  e->zooms = {ZoomPoint{e->inUs, ZoomRect{}}};
  ++version_;
}

void EventStore::recordAngle(int id, int64_t t, const std::string& inputId) {
  Event* e = find(id);
  if (!e) return;
  overwriteFrom(e->angles, t);
  e->angles.push_back(AngleCut{t, inputId});
  std::sort(e->angles.begin(), e->angles.end(), [](const AngleCut& a, const AngleCut& b) { return a.tUs < b.tUs; });
  ++version_;
}

void EventStore::recordSpeed(int id, int64_t t, double s) {
  Event* e = find(id);
  if (!e) return;
  overwriteFrom(e->speeds, t);
  e->speeds.push_back(SpeedPoint{t, s});
  std::sort(e->speeds.begin(), e->speeds.end(), [](const SpeedPoint& a, const SpeedPoint& b) { return a.tUs < b.tUs; });
  ++version_;
}

void EventStore::recordZoom(int id, int64_t t, ZoomRect r) {
  Event* e = find(id);
  if (!e) return;
  overwriteFrom(e->zooms, t);
  e->zooms.push_back(ZoomPoint{t, r});
  std::sort(e->zooms.begin(), e->zooms.end(), [](const ZoomPoint& a, const ZoomPoint& b) { return a.tUs < b.tUs; });
  ++version_;
}

const std::vector<int>& EventStore::orderOf(const std::string& listId) const {
  static const std::vector<int> empty;
  if (listId.empty()) return rootOrder_;
  for (const auto& l : lists_) {
    if (l.id == listId) return l.order;
  }
  return empty;
}

std::vector<int> EventStore::displayOrder() const {
  std::vector<int> out = rootOrder_;
  for (const auto& l : lists_) out.insert(out.end(), l.order.begin(), l.order.end());
  return out;
}

std::string EventStore::newList(const std::string& name) {
  EventList l;
  l.id = "list-" + std::to_string(nextListId_++);
  const auto first = name.find_first_not_of(" \t\r\n");
  const auto last = name.find_last_not_of(" \t\r\n");
  l.name = first == std::string::npos ? "Game " + std::to_string(lists_.size() + 1)
                                      : name.substr(first, last - first + 1);
  lists_.push_back(l);
  currentList_ = l.id;
  ++version_;
  return l.id;
}

bool EventStore::renameList(const std::string& id, const std::string& name) {
  const auto first = name.find_first_not_of(" \t\r\n");
  const auto last = name.find_last_not_of(" \t\r\n");
  if (first == std::string::npos) return false;
  for (auto& l : lists_) {
    if (l.id == id) {
      l.name = name.substr(first, last - first + 1);
      ++version_;
      return true;
    }
  }
  return false;
}

void EventStore::deleteList(const std::string& id) {
  auto it = std::find_if(lists_.begin(), lists_.end(), [&](const EventList& l) { return l.id == id; });
  if (it == lists_.end()) return;
  for (int eventId : it->order) {
    if (Event* e = find(eventId)) e->listId.clear();
    if (std::find(rootOrder_.begin(), rootOrder_.end(), eventId) == rootOrder_.end())
      rootOrder_.push_back(eventId);
  }
  lists_.erase(it);
  if (currentList_ == id) currentList_.clear();
  ++version_;
}

void EventStore::selectList(const std::string& id) {
  if (id.empty()) {
    currentList_.clear();
    ++version_;
    return;
  }
  for (const auto& l : lists_) {
    if (l.id == id) {
      currentList_ = id;
      ++version_;
      return;
    }
  }
}

void EventStore::moveEvents(const std::vector<int>& ids, const std::string& toList,
                            std::optional<int> beforeId) {
  std::vector<int>* target = toList.empty() ? &rootOrder_ : nullptr;
  for (auto& l : lists_) {
    if (l.id == toList) target = &l.order;
  }
  if (!target) return;

  std::vector<int> moving;
  for (int id : ids) {
    if (!find(id) || std::find(moving.begin(), moving.end(), id) != moving.end()) continue;
    moving.push_back(id);
  }
  if (moving.empty()) return;

  const std::vector<int> original = *target;
  auto isMoving = [&](int id) { return std::find(moving.begin(), moving.end(), id) != moving.end(); };
  for (auto& l : lists_)
    l.order.erase(std::remove_if(l.order.begin(), l.order.end(), isMoving), l.order.end());
  rootOrder_.erase(std::remove_if(rootOrder_.begin(), rootOrder_.end(), isMoving), rootOrder_.end());

  size_t index = target->size();
  if (beforeId) {
    const auto at = std::find(original.begin(), original.end(), *beforeId);
    if (at != original.end()) {
      if (!isMoving(*beforeId)) {
        index = static_cast<size_t>(std::find(target->begin(), target->end(), *beforeId) - target->begin());
      } else {
        for (auto next = std::next(at); next != original.end(); ++next) {
          if (isMoving(*next)) continue;
          index = static_cast<size_t>(std::find(target->begin(), target->end(), *next) - target->begin());
          break;
        }
      }
    }
  }
  target->insert(target->begin() + static_cast<std::ptrdiff_t>(index), moving.begin(), moving.end());
  for (int id : moving) find(id)->listId = toList;
  ++version_;
}

void EventStore::reorder(const std::string& listId, std::vector<int> order) {
  for (auto& l : lists_) {
    if (l.id == listId) {
      l.order = std::move(order);
      ++version_;
      return;
    }
  }
}

const Event* EventStore::find(int id) const {
  for (const auto& e : events_) {
    if (e.id == id) return &e;
  }
  return nullptr;
}

Event* EventStore::find(int id) {
  for (auto& e : events_) {
    if (e.id == id) return &e;
  }
  return nullptr;
}

std::vector<ProtectedRange> EventStore::protectedRanges() const {
  std::vector<ProtectedRange> out;
  out.reserve(events_.size());
  for (const auto& e : events_) out.push_back(ProtectedRange{std::min(e.inUs, e.outUs), std::max(e.inUs, e.outUs)});
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.inUs < b.inUs; });
  return out;
}

nlohmann::json EventStore::toJson() const {
  nlohmann::json j;
  j["version"] = version_;
  j["nextId"] = nextId_;
  j["nextListId"] = nextListId_;
  j["currentList"] = currentList_;
  j["rootOrder"] = rootOrder_;
  j["preRollUs"] = preRollUs_;
  j["postRollUs"] = postRollUs_;
  j["defaultSpeed"] = defaultSpeed;
  auto& lists = j["lists"] = nlohmann::json::array();
  for (const auto& l : lists_) lists.push_back({{"id", l.id}, {"name", l.name}, {"order", l.order}});
  auto& events = j["events"] = nlohmann::json::array();
  for (const auto& e : events_) {
    nlohmann::json je;
    je["id"] = e.id;
    je["listId"] = e.listId;
    je["name"] = e.name;
    je["tags"] = e.tags;
    je["angleNotes"] = e.angleNotes;
    je["inUs"] = e.inUs;
    je["outUs"] = e.outUs;
    je["angles"] = nlohmann::json::array();
    for (const auto& a : e.angles) je["angles"].push_back({{"tUs", a.tUs}, {"inputId", a.inputId}});
    je["speeds"] = nlohmann::json::array();
    for (const auto& s : e.speeds) je["speeds"].push_back({{"tUs", s.tUs}, {"speed", s.speed}});
    je["zooms"] = nlohmann::json::array();
    for (const auto& z : e.zooms)
      je["zooms"].push_back({{"tUs", z.tUs},
                             {"rect", {{"cx", z.rect.cx}, {"cy", z.rect.cy}, {"scale", z.rect.scale}}}});
    events.push_back(std::move(je));
  }
  return j;
}

EventStore EventStore::fromJson(const nlohmann::json& j) {
  EventStore s;
  s.events_.clear();
  s.lists_.clear();
  s.version_ = j.value("version", 1u);
  s.nextId_ = j.value("nextId", 1);
  s.nextListId_ = j.value("nextListId", 1);
  s.currentList_ = j.value("currentList", std::string{});
  s.preRollUs_ = j.value("preRollUs", int64_t{0});
  s.postRollUs_ = j.value("postRollUs", int64_t{0});
  s.defaultSpeed = j.value("defaultSpeed", 0.5);
  for (const auto& l : j.value("lists", nlohmann::json::array())) {
    EventList list;
    list.id = l.value("id", std::string{});
    list.name = l.value("name", std::string{});
    list.order = l.value("order", std::vector<int>{});
    s.lists_.push_back(std::move(list));
  }
  s.rootOrder_ = j.value("rootOrder", std::vector<int>{});
  for (const auto& je : j.value("events", nlohmann::json::array())) {
    Event e;
    e.id = je.value("id", 0);
    e.listId = je.value("listId", std::string{});
    e.name = je.value("name", std::string{});
    e.tags = je.value("tags", std::vector<std::string>{});
    e.angleNotes = je.value("angleNotes", std::map<std::string, std::string>{});
    e.inUs = je.value("inUs", int64_t{0});
    e.outUs = je.value("outUs", int64_t{0});
    for (const auto& a : je.value("angles", nlohmann::json::array()))
      e.angles.push_back(AngleCut{a.value("tUs", int64_t{0}), a.value("inputId", std::string{})});
    for (const auto& sp : je.value("speeds", nlohmann::json::array()))
      e.speeds.push_back(SpeedPoint{sp.value("tUs", int64_t{0}), sp.value("speed", 1.0)});
    for (const auto& z : je.value("zooms", nlohmann::json::array())) {
      const auto& r = z.at("rect");
      e.zooms.push_back(ZoomPoint{z.value("tUs", int64_t{0}),
                                  ZoomRect{r.value("cx", 0.5f), r.value("cy", 0.5f), r.value("scale", 1.0f)}});
    }
    // Next id must stay above every loaded id even when the file was written by an older build.
    if (e.id >= s.nextId_) s.nextId_ = e.id + 1;
    s.events_.push_back(std::move(e));
  }

  // An old show has no rootOrder: its lists stay intact. An event whose group vanished goes
  // to the root, an event missing from its order is appended, and no id sits in two orders.
  auto dedupe = [&](std::vector<int>& order) {
    std::vector<int> out;
    for (int id : order) {
      if (!s.find(id) || std::find(out.begin(), out.end(), id) != out.end()) continue;
      out.push_back(id);
    }
    order = std::move(out);
  };
  dedupe(s.rootOrder_);
  for (auto& l : s.lists_) dedupe(l.order);
  for (auto& e : s.events_) {
    std::vector<int>* keep = &s.rootOrder_;
    for (auto& l : s.lists_) {
      if (l.id == e.listId) keep = &l.order;
    }
    if (keep == &s.rootOrder_ && !e.listId.empty()) e.listId.clear();
    auto removeElsewhere = [&](std::vector<int>& order) {
      if (&order == keep) return;
      order.erase(std::remove(order.begin(), order.end(), e.id), order.end());
    };
    removeElsewhere(s.rootOrder_);
    for (auto& l : s.lists_) removeElsewhere(l.order);
    if (std::find(keep->begin(), keep->end(), e.id) == keep->end()) keep->push_back(e.id);
  }
  const auto known = [&](const std::string& id) {
    return id.empty() || std::any_of(s.lists_.begin(), s.lists_.end(), [&](const EventList& l) { return l.id == id; });
  };
  if (!known(s.currentList_)) s.currentList_.clear();
  return s;
}

std::string angleAt(const Event& e, int64_t t) {
  std::string out = e.angles.empty() ? std::string() : e.angles.front().inputId;
  for (const auto& a : e.angles) {
    if (a.tUs <= t) out = a.inputId;
    else break;
  }
  return out;
}

double speedAt(const Event& e, int64_t t, double fallback) {
  double out = fallback;
  for (const auto& s : e.speeds) {
    if (s.tUs <= t) out = s.speed;
    else break;
  }
  return out;
}

ZoomRect zoomAt(const Event& e, int64_t t) {
  ZoomRect out;
  for (const auto& z : e.zooms) {
    if (z.tUs <= t) out = z.rect;
    else break;
  }
  return out;
}

}  // namespace br