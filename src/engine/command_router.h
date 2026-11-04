#pragma once
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "events/event_store.h"

namespace br {
class Engine;

// Turns one control-API command into engine calls and a {"ok":…} reply.
class CommandRouter {
 public:
  explicit CommandRouter(Engine& e) : engine_(e) {}
  nlohmann::json handle(const nlohmann::json& cmd);

 private:
  nlohmann::json handleImpl(const nlohmann::json& cmd);
  bool isEdit(const std::string& name, const nlohmann::json& cmd) const;
  bool isPassSettling(const std::string& name) const;
  void settlePass();
  int64_t markTime(bool lastN) const;
  std::string markAngle() const;
  Engine& engine_;
  // Store as it was when the current live-riding pass began; one pass, one undo step.
  std::optional<EventStore> passBefore_;
};

}  // namespace br