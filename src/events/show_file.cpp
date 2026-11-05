#include "events/show_file.h"

#include <windows.h>

#include <fstream>
#include <stdexcept>

namespace br {
namespace {
nlohmann::json settingsToJson(const ShowSettings& s) {
  nlohmann::json j;
  j["rate"] = s.rate.str();
  j["outW"] = s.outW;
  j["outH"] = s.outH;
  j["quality"] = s.quality == VmxQuality::LQ ? "LQ" : (s.quality == VmxQuality::HQ ? "HQ" : "SQ");
  j["lastNSeconds"] = s.lastNSeconds;
  j["preRollS"] = s.preRollS;
  j["postRollS"] = s.postRollS;
  j["defaultSpeed"] = s.defaultSpeed;
  j["transition"] = s.transition;
  j["fadeS"] = s.fadeS;
  j["lowSpaceMinutes"] = s.lowSpaceMinutes;
  if (s.limit.hours) j["limit"]["hours"] = *s.limit.hours;
  if (s.limit.bytes) j["limit"]["bytes"] = *s.limit.bytes;
  j["endAction"] = s.endAction;
  j["slatePath"] = s.slatePath;
  j["hotkeys"] = s.hotkeys;
  j["recordingFolder"] = s.recordingFolder;
  j["exportFolder"] = s.exportFolder;
  j["exportCodec"] = s.exportCodec;
  j["exportQuality"] = s.exportQuality;
  j["exportMbps"] = s.exportMbps;
  return j;
}

ShowSettings settingsFromJson(const nlohmann::json& j) {
  ShowSettings s;
  if (j.contains("rate")) {
    if (const auto r = bc::parseShowRate(j.value("rate", std::string{}))) s.rate = *r;
  }
  s.outW = j.value("outW", 1920u);
  s.outH = j.value("outH", 1080u);
  const auto q = j.value("quality", std::string("SQ"));
  s.quality = q == "LQ" ? VmxQuality::LQ : (q == "HQ" ? VmxQuality::HQ : VmxQuality::SQ);
  s.lastNSeconds = j.value("lastNSeconds", 12.0);
  s.preRollS = j.value("preRollS", 0.0);
  s.postRollS = j.value("postRollS", 0.0);
  s.defaultSpeed = j.value("defaultSpeed", 0.5);
  s.transition = j.value("transition", std::string("fade"));
  s.fadeS = j.value("fadeS", 0.5);
  s.lowSpaceMinutes = j.value("lowSpaceMinutes", 30);
  if (j.contains("limit")) {
    const auto& l = j["limit"];
    if (l.contains("hours")) s.limit.hours = l.value("hours", 0.0);
    if (l.contains("bytes")) s.limit.bytes = l.value("bytes", int64_t{0});
  }
  s.endAction = j.value("endAction", std::string("hold"));
  s.slatePath = j.value("slatePath", std::string{});
  s.hotkeys = j.value("hotkeys", std::map<std::string, std::string>{});
  s.recordingFolder = j.value("recordingFolder", std::string{});
  s.exportFolder = j.value("exportFolder", std::string{});
  s.exportCodec = j.value("exportCodec", std::string("h264"));
  s.exportQuality = j.value("exportQuality", std::string("high"));
  s.exportMbps = j.value("exportMbps", 10.0);
  return s;
}

nlohmann::json inputToJson(const ShowInput& in) {
  return {{"id", in.id}, {"name", in.name}, {"kind", in.input.kind}, {"params", in.input.params}};
}

ShowInput inputFromJson(const nlohmann::json& j) {
  ShowInput in;
  in.id = j.value("id", std::string{});
  in.name = j.value("name", std::string{});
  in.input.kind = j.value("kind", std::string("srt"));
  in.input.params = j.value("params", nlohmann::json::object());
  return in;
}
}  // namespace

Show loadShow(const std::filesystem::path& showJson) {
  std::ifstream f(showJson, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open show file: " + showJson.string());
  nlohmann::json j;
  try {
    f >> j;
  } catch (const std::exception& e) {
    throw std::runtime_error("bad show file " + showJson.string() + ": " + e.what());
  }
  Show s;
  s.name = j.value("name", std::string{});
  for (const auto& in : j.value("inputs", nlohmann::json::array())) s.inputs.push_back(inputFromJson(in));
  for (const auto& out : j.value("outputs", nlohmann::json::array())) s.outputs.push_back(bc::outputFromJson(out));
  s.settings = settingsFromJson(j.value("settings", nlohmann::json::object()));
  s.events = EventStore::fromJson(j.value("events", nlohmann::json::object()));
  s.events.setRoll(static_cast<int64_t>(s.settings.preRollS * 1'000'000),
                   static_cast<int64_t>(s.settings.postRollS * 1'000'000));
  return s;
}

void saveShow(const std::filesystem::path& showJson, const Show& show) {
  nlohmann::json j;
  j["name"] = show.name;
  j["inputs"] = nlohmann::json::array();
  for (const auto& in : show.inputs) j["inputs"].push_back(inputToJson(in));
  j["outputs"] = nlohmann::json::array();
  for (const auto& out : show.outputs) j["outputs"].push_back(bc::outputToJson(out));
  j["settings"] = settingsToJson(show.settings);
  j["events"] = show.events.toJson();

  std::filesystem::create_directories(showJson.parent_path());
  const auto tmp = std::filesystem::path(showJson.string() + ".tmp");
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write show file: " + tmp.string());
    f << j.dump(2);
    f.flush();
    if (!f) throw std::runtime_error("cannot write show file: " + tmp.string());
  }
  if (!MoveFileExW(tmp.wstring().c_str(), showJson.wstring().c_str(), MOVEFILE_REPLACE_EXISTING)) {
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    throw std::runtime_error("cannot replace show file: " + showJson.string());
  }
}

}  // namespace br