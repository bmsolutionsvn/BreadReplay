#include "engine/command_router.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

nlohmann::json CommandRouter::handle(const nlohmann::json& cmd) {
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
      if (name == "mark.in") {
        engine_.events().markIn(markTime(false), markAngle());
      } else if (name == "mark.out") {
        engine_.events().markOut(markTime(false), markAngle(), lastNUs);
      } else {
        engine_.events().lastN(engine_.newestTs(), lastNUs, markAngle());
      }
      return {{"ok", true}};
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
      engine_.events().remove(cmd.value("id", 0));
      return {{"ok", true}};
    }
    if (name == "event.clearCuts") {
      engine_.events().clearCuts(cmd.value("id", 0));
      return {{"ok", true}};
    }
    if (name == "list.new") return {{"ok", true}, {"id", engine_.events().newList(cmd.value("name", std::string("Game")))}};
    if (name == "list.select") {
      engine_.events().selectList(cmd.value("id", std::string{}));
      return {{"ok", true}};
    }
    if (name == "list.reorder") {
      engine_.events().reorder(cmd.value("id", std::string{}), cmd.value("order", std::vector<int>{}));
      return {{"ok", true}};
    }
    if (name == "highlight.play") {
      if (!engine_.outputPlayer()) return {{"ok", false}, {"error", "the output player is not running"}};
      engine_.outputPlayer()->playHighlight(cmd.value("listId", std::string{}));
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