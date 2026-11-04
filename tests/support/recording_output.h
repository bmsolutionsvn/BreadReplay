#pragma once
#include <vector>

#include "io/output.h"

namespace br::test {
// Records every frame the tick hands it, synchronously: a test reads exactly this tick's frame.
class RecordingOutput final : public bc::Output {
 public:
  RecordingOutput(bool needsCpu, bool wantsGpu) : needsCpu_(needsCpu), wantsGpu_(wantsGpu) {}
  bool needsCpu() const override { return needsCpu_; }
  bool wantsGpuRgba8() const override { return wantsGpu_; }
  void start() override {}
  void submit(bc::OutFrame f) override { frames.push_back(std::move(f)); }
  void stop() override {}
  bc::OutputStatus status() const override { return {}; }

  std::vector<bc::OutFrame> frames;

 private:
  bool needsCpu_, wantsGpu_;
};
}  // namespace br::test