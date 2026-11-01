#pragma once
#include <nlohmann/json.hpp>

namespace br {
class Engine;

// Turns one control-API command into engine calls and a {"ok":…} reply.
class CommandRouter {
 public:
  explicit CommandRouter(Engine& e) : engine_(e) {}
  nlohmann::json handle(const nlohmann::json& cmd);

 private:
  int64_t markTime(bool lastN) const;
  std::string markAngle() const;
  Engine& engine_;
};

}  // namespace br