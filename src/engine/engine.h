#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/vmx_codec.h"
#include "events/show_file.h"
#include "io/input.h"
#include "io/output.h"
#include "play/frame_source.h"
#include "play/output_player.h"
#include "play/player.h"
#include "play/renderer.h"
#include "record/recorder.h"
#include "store/input_store.h"
#include "store/retention.h"

namespace bc {
class PreviewEncoder;
class SurfaceHub;
}

namespace br {

struct EngineDeps {
  bc::GpuDevice* gpu = nullptr;
  bc::TexturePool* pool = nullptr;
  bc::Converter* conv = nullptr;
  std::function<std::unique_ptr<bc::Input>(const bc::InputConfig&)> makeInput;
  std::function<std::unique_ptr<bc::Output>(const bc::OutputConfig&)> makeOutput;
  std::function<int64_t()> clock;  // injectable for tests
};

// An SRT packet's AVColorSpace as the stored hint: only BT.709 and BT.601 are recognised.
StoredColour srtColour(int colorSpace);

// Owns the show, the per-input stores and recorders, both players, the outputs and the disk
// monitor. The control API only reaches it through CommandRouter.
class Engine {
 public:
  Engine(EngineDeps deps, Show show, std::filesystem::path folder);
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  void start();
  void stop();
  void openShow(Show show, std::filesystem::path folder);

  Show& show() { return show_; }
  EventStore& events() { return show_.events; }
  const std::filesystem::path& folder() const { return folder_; }
  int64_t now() const;
  int64_t newestTs() const;

  Player& playerFor(const std::string& which);
  // One preview-loop step: a never-cued or live Preview moves to the newest timestamp.
  void followLiveEdge();
  // A window just connected: the next Preview and Output pictures are keyframes, so it can start
  // decoding at once instead of waiting (or never, with encoders that send no periodic keyframes).
  void requestKeyframes();
  void take();
  void recordStart();
  void recordStop();
  bool recording() const { return recording_; }
  void saveShow();

  Preflight preflight();
  void applySettings(const nlohmann::json& partial);
  void addInput(const ShowInput& in);
  void updateInput(const ShowInput& in);
  void removeInput(const std::string& id);
  void addOutput(const bc::OutputConfig& out);
  void updateOutput(const bc::OutputConfig& out);
  void removeOutput(const std::string& id);

  nlohmann::json statusJson();
  nlohmann::json handle(const nlohmann::json& cmd);

  void setSender(std::function<void(const std::string&)> text, std::function<void(const std::vector<uint8_t>&)> binary);
  void setSurfaces(bc::SurfaceHub* hub);                     // null = native video off
  void setBinaryWanted(std::function<bool()> fn);             // true while any client sent wantPreviews
  void setNativeVideoReason(std::string reason);              // statusJson's reason when surfaces are off

  // For tests and the current-list highlight.
  OutputPlayer* outputPlayer() { return outputPlayer_.get(); }
  const InputStore* store(const std::string& id) const;
  RecorderStats recorderStats(const std::string& id);
  const FrameSource* frameSource() const { return source_.get(); }

 private:
  struct Sink final : bc::InputSink {
    Sink(Engine& e, std::string id) : engine(e), inputId(std::move(id)) {}
    void onVideo(bc::VideoFrameIn) override {}
    void onAudio(bc::AudioBlock a) override;
    void onState(bc::SignalState s, std::optional<bc::ErrorInfo> err) override;
    // A compressed input switches to packets/access units when it arrives as H.264/HEVC; it then
    // needs no decoded picture at all.
    bool wantsCpuVideo() const override { return !coded_.load(); }
    bool wantsGpuVideo() const override { return false; }
    bool wantsCodedVmx() const override { return true; }
    bool wantsCodedHx() const override { return true; }
    bool wantsPackets() const override { return true; }
    bool wantsDecodedAudio() const override { return true; }
    void onCpuVideo(const bc::CpuVideoFrame& f) override;
    void onCodedVideo(const bc::CodedVideoFrame& f) override;
    void onPacket(std::shared_ptr<const bc::MediaPacket> p) override;
    Engine& engine;
    std::string inputId;
    std::atomic<bool> coded_{false};  // the packet path owns this input's video
  };

  void buildInputs();
  void buildOutputs();
  void rebuildFrameSource();
  void startPreviewThread();
  void previewLoop();
  void retentionLoop();
  void statusLoop();
  void retentionTick();
  int64_t periodUs() const;
  Recorder* recorderFor(const std::string& id);
  InputStore* storeFor(const std::string& id);

  EngineDeps deps_;
  Show show_;
  std::filesystem::path folder_;
  std::filesystem::path showJson_;

  std::mutex m_;  // show, stores, lists
  std::recursive_mutex cmdM_;  // control commands and show persistence, one at a time
  bool recording_ = false;
  bool started_ = false;
  std::atomic<bool> running_{false};

  std::map<std::string, std::unique_ptr<InputStore>> stores_;
  std::map<std::string, std::unique_ptr<Recorder>> recorders_;
  std::map<std::string, std::unique_ptr<bc::Input>> inputs_;
  std::map<std::string, std::unique_ptr<Sink>> sinks_;
  std::map<std::string, std::string> inputStates_;
  std::vector<std::unique_ptr<bc::Output>> outputsOwned_;

  std::unique_ptr<FrameSource> source_;
  std::unique_ptr<Renderer> renderer_;
  std::unique_ptr<Renderer> previewRenderer_;
  std::unique_ptr<OutputPlayer> outputPlayer_;
  std::unique_ptr<Player> previewPlayer_;
  std::unique_ptr<Player> outputPlayerState_;

  std::unique_ptr<bc::PreviewEncoder> previewEncoder_;
  std::unique_ptr<bc::PreviewEncoder> outputEncoder_;
  // Set by requestKeyframes (control thread) and consumed by previewLoop (preview thread), so the
  // lazily opened encoders are never touched across threads.
  std::atomic<bool> previewKeyframe_{false};
  std::atomic<bool> outputKeyframe_{false};
  std::string lastEventJsonVersion_;
  std::map<std::string, std::pair<int64_t, uint64_t>> fpsSamples_;
  std::map<std::string, double> fpsValues_;

  std::thread previewThread_, retentionThread_, statusThread_;
  std::function<void(const std::string&)> sendText_;
  std::function<void(const std::vector<uint8_t>&)> sendBinary_;
  bc::SurfaceHub* surfaces_ = nullptr;
  std::function<bool()> binaryWanted_;
  std::string nativeReason_;
  WriteRateMeter writeMeter_;
  int64_t lastBytes_ = 0;
  int64_t lastRetentionUs_ = 0;
  int64_t lastSaveUs_ = 0;
  nlohmann::json alerts_ = nlohmann::json::array();
  uint64_t eventsVersionSent_ = 0;
};

}  // namespace br