#include "engine/command_router.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <stdexcept>

#include "core/timecode.h"
#include "engine/engine.h"

namespace br {
namespace {
ShowInput inputFromJson(const nlohmann::json& j) {
  ShowInput in;
  in.id = j.value("id", std::string{});
  in.name = j.value("name", std::string{});
  if (j.contains("input")) {
    const auto& i = j["input"];
    in.input.kind = i.value("kind", std::string("srt"));
    in.input.params = i.value("params", nlohmann::json::object());
  } else {
    in.input.kind = j.value("kind", std::string("srt"));
    in.input.params = j.value("params", nlohmann::json::object());
  }
  return in;
}

ZoomRect zoomFromJson(const nlohmann::json& j) {
  ZoomRect z;
  z.cx = j.value("cx", 0.5f);
  z.cy = j.value("cy", 0.5f);
  z.scale = j.value("scale", 1.0f);
  return z;
}
}  // namespace

int64_t CommandRouter::markTime(bool lastN) const {
  if (lastN) return engine_.newestTs();
  Player& preview = engine_.playerFor("preview");
  const int64_t newest = engine_.newestTs();
  // At the live edge a mark means now; while reviewing it means Preview's position.
  if (newest > 0 && std::llabs(preview.state().posUs - newest) <= 1'000'000) return newest;
  return preview.state().posUs;
}

std::string CommandRouter::markAngle() const {
  const auto angle = engine_.playerFor("preview").state().angle;
  if (!angle.empty()) return angle;
  const auto& inputs = engine_.show().inputs;
  return inputs.empty() ? std::string("cam1") : inputs.front().id;
}

bool CommandRouter::isEdit(const std::string& name, const nlohmann::json& cmd) const {
  if (name == "mark.in" || name == "mark.out" || name == "mark.lastN" || name == "event.update" ||
      name == "event.trim" || name == "event.delete" || name == "event.clearCuts" ||
      name == "event.move" || name == "list.new" || name == "list.rename" ||
      name == "list.delete" || name == "list.reorder") {
    return true;
  }
  // Riding while a replay plays is part of that pass, not an edit of its own.
  if (name == "player.lever" || name == "player.preset" || name == "player.zoom") {
    return !engine_.playerFor(cmd.value("player", std::string("preview"))).state().playing;
  }
  return false;
}

bool CommandRouter::isRide(const std::string& name, const nlohmann::json&, const std::string& who) const {
  if (name == "take" || name == "highlight.play" || name == "player.play") return true;
  if (name == "player.angle" || name == "player.lever" || name == "player.preset" ||
      name == "player.zoom") {
    return engine_.playerFor(who).state().playing;
  }
  return false;
}

void CommandRouter::settlePass() {
  if (!passBefore_) return;
  if (!passOwner_.empty()) engine_.playerFor(passOwner_).endRide();
  if (engine_.events().version() != passBefore_->version()) engine_.history().record(*passBefore_);
  passBefore_.reset();
  passOwner_.clear();
}

bool CommandRouter::hasPendingPass(const EventStore& store) const {
  return passBefore_ && store.version() != passBefore_->version();
}

nlohmann::json CommandRouter::handle(const nlohmann::json& cmd) {
  try {
    const std::string name = cmd.value("cmd", std::string{});
    const std::string who = cmd.value("player", std::string("preview"));
    const bool edit = isEdit(name, cmd);
    // Anything that touches the store ends the ride first, so the pass is its own step.
    if (edit || name == "edit.undo" || name == "edit.redo" || name == "show.new" ||
        name == "show.open" || name == "take" || name == "highlight.play") {
      settlePass();
    } else if ((name == "player.cue" || name == "player.cueAt" || name == "player.live" ||
                name == "player.pause") &&
               passBefore_ && passOwner_ == who) {
      settlePass();
    }
    if (name == "player.play" && passBefore_) settlePass();

    const std::string owner = (name == "take" || name == "highlight.play") ? "output" : who;
    const bool ride = isRide(name, cmd, who);
    std::optional<EventStore> rideBefore;
    if (ride) {
      if (passBefore_ && passOwner_ != owner) settlePass();
      if (!passBefore_) {
        rideBefore = engine_.events();
        passOwner_ = owner;
      }
    }

    std::optional<EventStore> before;
    if (edit) before = engine_.events();
    auto reply = handleImpl(cmd);
    if (rideBefore) passBefore_ = *rideBefore;
    if (before && engine_.events().version() != before->version()) engine_.history().record(*before);
    return reply;
  } catch (const std::exception& e) {
    return {{"ok", false}, {"error", e.what()}};
  }
}

nlohmann::json CommandRouter::handleImpl(const nlohmann::json& cmd) {
  try {
    const std::string name = cmd.value("cmd", std::string{});
    if (name.empty()) return {{"ok", false}, {"error", "cmd is required"}};

    if (name == "ping") return {{"ok", true}};

    if (name == "show.new") {
      Show s;
      s.name = cmd.value("name", std::string("Show"));
      s.settings.rate = engine_.show().settings.rate;
      const auto root = std::filesystem::path(cmd.value("folder", std::string(".")));
      engine_.openShow(s, root / s.name);
      return {{"ok", true}};
    }
    if (name == "show.open") {
      const auto path = std::filesystem::path(cmd.value("path", std::string{}));
      Show s = loadShow(path);
      engine_.openShow(s, path.parent_path());
      return {{"ok", true}};
    }
    if (name == "show.save") {
      engine_.saveShow();
      return {{"ok", true}};
    }
    if (name == "edit.undo") return {{"ok", true}, {"done", engine_.history().undo(engine_.events())}};
    if (name == "edit.redo") return {{"ok", true}, {"done", engine_.history().redo(engine_.events())}};

    if (name == "input.add") {
      engine_.addInput(inputFromJson(cmd));
      return {{"ok", true}};
    }
    if (name == "input.update") {
      engine_.updateInput(inputFromJson(cmd));
      return {{"ok", true}};
    }
    if (name == "input.remove") {
      engine_.removeInput(cmd.value("id", std::string{}));
      return {{"ok", true}};
    }
    if (name == "output.add") {
      auto o = cmd;
      o.erase("cmd");
      engine_.addOutput(bc::outputFromJson(o));
      return {{"ok", true}};
    }
    if (name == "output.update") {
      auto o = cmd;
      o.erase("cmd");
      engine_.updateOutput(bc::outputFromJson(o));
      return {{"ok", true}};
    }
    if (name == "output.remove") {
      engine_.removeOutput(cmd.value("id", std::string{}));
      return {{"ok", true}};
    }
    if (name == "settings.set") {
      auto s = cmd;
      s.erase("cmd");
      engine_.applySettings(s);
      return {{"ok", true}};
    }
    if (name == "record.preflight") {
      const auto p = engine_.preflight();
      return {{"ok", true},
              {"canRecord", p.canRecord},
              {"minutesFree", p.minutesFree},
              {"writeMBps", p.writeMBps},
              {"warnings", p.warnings},
              {"error", p.error ? nlohmann::json(*p.error) : nlohmann::json(nullptr)}};
    }
    if (name == "record.start") {
      engine_.recordStart();
      return {{"ok", true}};
    }
    if (name == "record.stop") {
      engine_.recordStop();
      return {{"ok", true}};
    }
    if (name == "take") {
      engine_.take();
      return {{"ok", true}};
    }

    if (name == "mark.in" || name == "mark.out" || name == "mark.lastN") {
      const auto& settings = engine_.show().settings;
      const int64_t lastNUs = static_cast<int64_t>(
          cmd.value("n", settings.lastNSeconds) * 1'000'000.0);
      const std::string inputId = markAngle();
      if (name == "mark.in") {
        Event& e = engine_.events().markIn(markTime(false), inputId);
        return {{"ok", true}, {"id", e.id}, {"edge", "in"}, {"tUs", e.inUs}, {"inputId", inputId}};
      }
      if (name == "mark.out") {
        Event& e = engine_.events().markOut(markTime(false), inputId, lastNUs);
        return {{"ok", true}, {"id", e.id}, {"edge", "out"}, {"tUs", e.outUs}, {"inputId", inputId}};
      }
      Event& e = engine_.events().lastN(engine_.newestTs(), lastNUs, inputId);
      return {{"ok", true}, {"id", e.id}, {"edge", "out"}, {"tUs", e.outUs}, {"inputId", inputId}};
    }

    if (name == "event.update") {
      Event* e = engine_.events().find(cmd.value("id", 0));
      if (!e) return {{"ok", false}, {"error", "no such event"}};
      if (cmd.contains("name")) e->name = cmd.value("name", e->name);
      if (cmd.contains("tags")) e->tags = cmd.value("tags", e->tags);
      if (cmd.contains("angleNotes")) e->angleNotes = cmd.value("angleNotes", e->angleNotes);
      if (cmd.contains("inUs")) e->inUs = cmd.value("inUs", e->inUs);
      if (cmd.contains("outUs")) e->outUs = cmd.value("outUs", e->outUs);
      if (cmd.contains("angles")) {
        e->angles.clear();
        for (const auto& a : cmd["angles"]) {
          if (!a.is_object() || !a.contains("tUs")) continue;
          e->angles.push_back(AngleCut{a.value("tUs", int64_t{0}), a.value("inputId", std::string{})});
        }
        std::sort(e->angles.begin(), e->angles.end(),
                  [](const AngleCut& x, const AngleCut& y) { return x.tUs < y.tUs; });
      }
      if (cmd.contains("speeds")) {
        e->speeds.clear();
        for (const auto& s : cmd["speeds"]) {
          if (!s.is_object() || !s.contains("tUs")) continue;
          e->speeds.push_back(SpeedPoint{s.value("tUs", int64_t{0}), s.value("speed", 1.0)});
        }
        std::sort(e->speeds.begin(), e->speeds.end(),
                  [](const SpeedPoint& x, const SpeedPoint& y) { return x.tUs < y.tUs; });
      }
      if (cmd.contains("zooms")) {
        e->zooms.clear();
        for (const auto& z : cmd["zooms"]) {
          if (!z.is_object() || !z.contains("tUs")) continue;
          e->zooms.push_back(ZoomPoint{z.value("tUs", int64_t{0}), zoomFromJson(z.value("rect", nlohmann::json::object()))});
        }
        std::sort(e->zooms.begin(), e->zooms.end(),
                  [](const ZoomPoint& x, const ZoomPoint& y) { return x.tUs < y.tUs; });
      }
      if (e->outUs < e->inUs) e->outUs = e->inUs;
      engine_.events().bumpVersion();
      return {{"ok", true}};
    }
    if (name == "event.trim") {
      const auto period = static_cast<int64_t>(1'000'000.0 / engine_.show().settings.rate.fps() + 0.5);
      engine_.events().trim(cmd.value("id", 0), cmd.value("inFrames", 0) * period,
                            cmd.value("outFrames", 0) * period);
      return {{"ok", true}};
    }
    if (name == "event.delete") {
      if (cmd.contains("ids")) {
        for (int id : cmd.value("ids", std::vector<int>{})) engine_.events().remove(id);
      } else {
        engine_.events().remove(cmd.value("id", 0));
      }
      return {{"ok", true}};
    }
    if (name == "event.clearCuts") {
      engine_.events().clearCuts(cmd.value("id", 0));
      return {{"ok", true}};
    }
    if (name == "list.new") {
      return {{"ok", true}, {"id", engine_.events().newList(cmd.value("name", std::string{}))}};
    }
    if (name == "list.rename") {
      const auto name2 = cmd.value("name", std::string{});
      if (name2.find_first_not_of(" \t\r\n") == std::string::npos)
        return {{"ok", false}, {"error", "name is empty"}};
      if (!engine_.events().renameList(cmd.value("id", std::string{}), name2))
        return {{"ok", false}, {"error", "no such group"}};
      return {{"ok", true}};
    }
    if (name == "list.delete") {
      engine_.events().deleteList(cmd.value("id", std::string{}));
      return {{"ok", true}};
    }
    if (name == "list.select") {
      engine_.events().selectList(cmd.value("id", std::string{}));
      return {{"ok", true}};
    }
    if (name == "list.reorder") {
      engine_.events().reorder(cmd.value("id", std::string{}), cmd.value("order", std::vector<int>{}));
      return {{"ok", true}};
    }
    if (name == "event.move") {
      const auto ids = cmd.value("ids", std::vector<int>{});
      std::optional<int> beforeId;
      if (cmd.contains("beforeId") && !cmd["beforeId"].is_null()) beforeId = cmd["beforeId"].get<int>();
      engine_.events().moveEvents(ids, cmd.value("listId", std::string{}), beforeId);
      return {{"ok", true}};
    }
    if (name == "export.start") {
      ExportQueue* queue = engine_.exportQueue();
      if (!queue) return {{"ok", false}, {"error", "export is not available"}};
      const auto& settings = engine_.show().settings;
      std::vector<int> ids;
      std::string defaultName;
      if (cmd.contains("ids")) {
        ids = cmd.value("ids", std::vector<int>{});
        defaultName = "Selection";
      } else if (cmd.contains("listId")) {
        const auto listId = cmd.value("listId", std::string{});
        ids = engine_.events().orderOf(listId);
        defaultName = "Replays";
        for (const auto& l : engine_.events().lists())
          if (l.id == listId) defaultName = l.name;
      } else if (cmd.contains("id")) {
        const Event* e = engine_.events().find(cmd.value("id", 0));
        if (e) {
          ids = {e->id};
          defaultName = e->name.empty() ? "Replay" : e->name;
        }
      }
      const auto display = engine_.events().displayOrder();
      ids.erase(std::remove_if(ids.begin(), ids.end(),
                               [&](int id) {
                                 return !engine_.events().find(id) ||
                                        std::find(display.begin(), display.end(), id) == display.end();
                               }),
                ids.end());
      std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) {
        return std::find(display.begin(), display.end(), a) < std::find(display.begin(), display.end(), b);
      });
      if (ids.empty()) return {{"ok", false}, {"error", "nothing to export"}};

      ExportJob job;
      job.name = cmd.value("name", defaultName);
      for (int id : ids) job.events.push_back(*engine_.events().find(id));
      job.codec = parseCodec(cmd.value("codec", settings.exportCodec));
      const auto quality = parseQuality(cmd.value("quality", settings.exportQuality));
      std::optional<double> mbps;
      if (cmd.contains("mbps")) mbps = cmd.value("mbps", settings.exportMbps);
      else if (quality == ExportQuality::Custom) mbps = settings.exportMbps;
      job.bitRate = exportBitrate(job.codec, quality, mbps, settings.outW, settings.outH, settings.rate);

      const std::filesystem::path folder = settings.exportFolder.empty()
                                               ? engine_.folder() / "Exports"
                                               : std::filesystem::path(settings.exportFolder);
      std::error_code ec;
      std::filesystem::create_directories(folder, ec);
      if (ec) return {{"ok", false}, {"error", "cannot create the export folder: " + ec.message()}};
      const std::time_t now = std::time(nullptr);
      std::tm local{};
      localtime_s(&local, &now);
      job.path = exportPath(folder, job.name, local);
      const int id = queue->add(std::move(job));
      return {{"ok", true}, {"id", id}};
    }
    if (name == "export.cancel") {
      if (!engine_.exportQueue()) return {{"ok", false}, {"error", "export is not available"}};
      engine_.exportQueue()->cancel(cmd.value("id", 0));
      return {{"ok", true}};
    }
    if (name == "highlight.play") {
      if (!engine_.outputPlayer()) return {{"ok", false}, {"error", "the output player is not running"}};
      if (cmd.contains("ids")) {
        engine_.outputPlayer()->playEvents(cmd.value("ids", std::vector<int>{}));
      } else if (cmd.contains("listId")) {
        engine_.outputPlayer()->playHighlight(cmd.value("listId", std::string{}));
      } else {
        engine_.outputPlayer()->playHighlight(engine_.events().currentList());
      }
      return {{"ok", true}};
    }

    if (name.rfind("player.", 0) == 0) {
      Player& p = engine_.playerFor(cmd.value("player", std::string("preview")));
      if (name == "player.cue") {
        p.cue(cmd.value("eventId", 0));
      } else if (name == "player.cueAt") {
        p.cueAt(cmd.value("t", int64_t{0}), cmd.value("angle", markAngle()));
      } else if (name == "player.play") {
        p.play();
      } else if (name == "player.pause") {
        p.pause();
      } else if (name == "player.step") {
        p.step(cmd.value("frames", 0));
      } else if (name == "player.direction") {
        p.setDirection(cmd.value("dir", 1));
      } else if (name == "player.lever") {
        p.setLever(cmd.value("speed", 1.0));
      } else if (name == "player.preset") {
        p.setSpeedPreset(cmd.value("speed", 1.0));
      } else if (name == "player.lock") {
        p.lockLever(cmd.value("locked", true));
      } else if (name == "player.angle") {
        p.setAngle(cmd.value("inputId", std::string{}));
      } else if (name == "player.zoom") {
        p.setZoom(zoomFromJson(cmd.value("rect", nlohmann::json::object())));
      } else if (name == "player.live") {
        // Back to the live edge on the angle the operator is on (the first input if none yet).
        const auto& inputs = engine_.show().inputs;
        std::string angle = engine_.playerFor("preview").state().angle;
        if (angle.empty() && !inputs.empty()) angle = inputs.front().id;
        engine_.playerFor("preview").cueAt(engine_.newestTs(), angle);
      } else {
        return {{"ok", false}, {"error", "unknown command '" + name + "'"}};
      }
      return {{"ok", true}};
    }

    return {{"ok", false}, {"error", "unknown command '" + name + "'"}};
  } catch (const std::exception& e) {
    return {{"ok", false}, {"error", e.what()}};
  }
}

}  // namespace br