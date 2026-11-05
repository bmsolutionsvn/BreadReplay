#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "core/rate.h"
#include "core/show.h"
#include "core/vmx_codec.h"
#include "events/event_store.h"
#include "store/retention.h"

namespace br {

struct ShowSettings {
  bc::Rate rate{60000, 1001};
  uint32_t outW = 1920, outH = 1080;
  VmxQuality quality = VmxQuality::SQ;
  double lastNSeconds = 12, preRollS = 0, postRollS = 0, defaultSpeed = 0.5;
  std::string transition = "fade";
  double fadeS = 0.5;
  int lowSpaceMinutes = 30;
  RetentionLimit limit;
  std::string endAction = "hold", slatePath;
  std::map<std::string, std::string> hotkeys;
  std::string recordingFolder;
  std::string exportFolder;
  std::string exportCodec = "h264";
  std::string exportQuality = "high";
  double exportMbps = 10.0;
};

struct ShowInput {
  std::string id, name;
  bc::InputConfig input;
};

struct Show {
  std::string name;
  std::vector<ShowInput> inputs;
  std::vector<bc::OutputConfig> outputs;
  ShowSettings settings;
  EventStore events;
};

Show loadShow(const std::filesystem::path& showJson);  // throws std::runtime_error
void saveShow(const std::filesystem::path& showJson, const Show&);  // tmp + rename

}  // namespace br