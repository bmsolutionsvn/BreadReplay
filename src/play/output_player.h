#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "events/event_store.h"
#include "events/show_file.h"
#include "io/output.h"
#include "play/audio_follow.h"
#include "play/frame_source.h"
#include "play/player.h"
#include "play/renderer.h"

namespace br {

// The master tick: advance the player, fetch and render one frame, and submit it to every
// output. A slow output never holds the tick (its own queue absorbs it).
class OutputPlayer {
 public:
  OutputPlayer(Player& p, FrameSource& src, Renderer& r, EventStore& events, const ShowSettings& s);
  void setOutputs(std::vector<std::unique_ptr<bc::Output>> outs);
  void playHighlight(const std::string& listId);  // events in list order, Cut or Fade between
  void playEvents(std::vector<int> ids);          // the given replays, in displayed order
  void tickOnce();                                // one show period
  void start();
  void stop();
  bool idle() const;
  std::optional<int> currentEvent() const { return player_.state().eventId; }
  uint64_t ticks() const { return tick_; }
  bc::TextureRef lastPicture() const {
    std::lock_guard lock(heldMutex_);
    return held_;
  }

 private:
  bc::TextureRef renderAt(const TickResult& tr);
  void advanceHighlight();
  void submit(bc::TextureRef pic, const TickResult& tr, bool angleChanged);

  Player& player_;
  FrameSource& src_;
  Renderer& renderer_;
  EventStore& events_;
  const ShowSettings& settings_;
  std::vector<std::unique_ptr<bc::Output>> outs_;
  AudioFollow audio_;
  uint64_t tick_ = 0;
  mutable std::mutex heldMutex_;
  bc::TextureRef held_;

  std::vector<Event> highlight_;
  size_t highlightIndex_ = 0;
  bool playingHighlight_ = false;
  std::unique_ptr<Player> fadePlayer_;
  bool fading_ = false;
  int64_t fadeElapsedUs_ = 0;

  std::thread thread_;
  std::atomic<bool> running_{false};
};

}  // namespace br