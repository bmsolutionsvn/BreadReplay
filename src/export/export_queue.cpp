#include "export/export_queue.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

#include "gpu/converter.h"
#include "gpu/device.h"
#include "gpu/texture_pool.h"
#include "play/frame_source.h"
#include "play/output_player.h"
#include "play/player.h"
#include "play/renderer.h"
#include "store/input_store.h"

#include "export/export_encoder.h"
#include "export/mp4_writer.h"

namespace br {
namespace {
nlohmann::json eventJson(const Event& e) {
  nlohmann::json j;
  j["id"] = e.id;
  j["listId"] = e.listId;
  j["name"] = e.name;
  j["tags"] = e.tags;
  j["angleNotes"] = e.angleNotes;
  j["inUs"] = e.inUs;
  j["outUs"] = e.outUs;
  j["angles"] = nlohmann::json::array();
  for (const auto& a : e.angles) j["angles"].push_back({{"tUs", a.tUs}, {"inputId", a.inputId}});
  j["speeds"] = nlohmann::json::array();
  for (const auto& s : e.speeds) j["speeds"].push_back({{"tUs", s.tUs}, {"speed", s.speed}});
  j["zooms"] = nlohmann::json::array();
  for (const auto& z : e.zooms)
    j["zooms"].push_back({{"tUs", z.tUs},
                          {"rect", {{"cx", z.rect.cx}, {"cy", z.rect.cy}, {"scale", z.rect.scale}}}});
  return j;
}
}  // namespace

ExportQueue::ExportQueue(std::map<std::string, const InputStore*> stores, ShowSettings settings)
    : stores_(std::move(stores)), settings_(std::move(settings)) {
  thread_ = std::thread([this] { run(); });
}

ExportQueue::~ExportQueue() { stop(); }

int ExportQueue::add(ExportJob job) {
  std::lock_guard lock(m_);
  // Two jobs queued in the same minute choose the same name before either writes its .part.
  std::filesystem::path path = job.path;
  auto part = [](std::filesystem::path p) {
    p += ".part";
    return p;
  };
  for (int n = 2; reserved_.count(path) || std::filesystem::exists(path) ||
                  std::filesystem::exists(part(path));
       ++n) {
    std::filesystem::path name = job.path.stem();
    name += " (" + std::to_string(n) + ").mp4";
    path = job.path.parent_path() / name;
  }
  reserved_.insert(path);
  job.path = path;
  Entry entry;
  entry.state.id = nextId_++;
  entry.state.name = job.name;
  entry.state.state = "queued";
  const auto utf8 = job.path.u8string();
  entry.state.path = std::string(reinterpret_cast<const char*>(utf8.c_str()), utf8.size());
  entry.job = std::move(job);
  entries_.push_back(std::move(entry));
  cv_.notify_one();
  return entries_.back().state.id;
}

void ExportQueue::cancel(int id) {
  {
    std::lock_guard lock(m_);
    for (auto& e : entries_) {
      if (e.state.id != id) continue;
      if (e.state.state == "queued") e.state.state = "cancelled";
      if (e.state.state == "running") cancelId_ = id;
    }
  }
  cv_.notify_all();
}

std::vector<ExportState> ExportQueue::states() const {
  std::lock_guard lock(m_);
  std::vector<ExportState> out;
  out.reserve(entries_.size());
  for (const auto& e : entries_) out.push_back(e.state);
  return out;
}

std::vector<ProtectedRange> ExportQueue::protectedRanges() const {
  static constexpr int64_t kMarginUs = 3'000'000;
  std::lock_guard lock(m_);
  std::vector<ProtectedRange> out;
  for (const auto& e : entries_) {
    if (e.state.state != "queued" && e.state.state != "running") continue;
    for (const auto& ev : e.job.events) {
      out.push_back({std::min(ev.inUs, ev.outUs) - kMarginUs,
                     std::max(ev.inUs, ev.outUs) + kMarginUs});
    }
  }
  return out;
}

void ExportQueue::stop() {
  stopping_ = true;
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  std::lock_guard lock(m_);
  for (auto& e : entries_) {
    if (e.state.state == "queued") e.state.state = "cancelled";
  }
}

void ExportQueue::run() {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  for (;;) {
    Entry entry;
    bool have = false;
    {
      std::unique_lock lock(m_);
      cv_.wait(lock, [&] {
        if (stopping_) return true;
        for (const auto& e : entries_) {
          if (e.state.state == "queued") return true;
        }
        return false;
      });
      if (stopping_) return;
      for (auto& e : entries_) {
        if (e.state.state != "queued") continue;
        e.state.state = "running";
        entry = e;
        have = true;
        break;
      }
    }
    if (have) runJob(entry);
  }
}

void ExportQueue::runJob(const Entry& entry) {
  const int id = entry.state.id;
  try {
    std::filesystem::create_directories(entry.job.path.parent_path());

    nlohmann::json root;
    root["nextId"] = 1;
    root["rootOrder"] = nlohmann::json::array();
    root["events"] = nlohmann::json::array();
    for (const auto& e : entry.job.events) {
      root["rootOrder"].push_back(e.id);
      root["events"].push_back(eventJson(e));
    }
    EventStore store = EventStore::fromJson(root);

    auto gpu = bc::GpuDevice::create(bc::GpuDevice::Pref::HardwareOrWarp);
    bc::TexturePool pool(*gpu);
    bc::Converter conv(*gpu, pool);
    FrameSource src(stores_, gpu.get(), &pool, 2);
    Renderer renderer(*gpu, pool, conv, settings_.outW, settings_.outH);
    Player player(store, settings_.rate, false, true);
    OutputPlayer op(player, src, renderer, store, settings_);

    auto writer = std::make_unique<Mp4Writer>(Mp4WriterOptions{
        entry.job.path, entry.job.codec, entry.job.bitRate, settings_.outW, settings_.outH,
        settings_.rate});
    Mp4Writer* raw = writer.get();
    std::vector<std::unique_ptr<bc::Output>> outs;
    outs.push_back(std::move(writer));
    op.setOutputs(std::move(outs));

    const int64_t totalTicks = estimateTicks(store, entry.job.events);
    std::vector<int> ids;
    for (const auto& e : entry.job.events) ids.push_back(e.id);
    op.playEvents(ids);

    // ponytail: frames go GPU -> CPU BGRA -> NV12 (swscale); a GPU NV12 path if export speed matters.
    int64_t done = 0;
    bool cancelled = false;
    while (!op.idle()) {
      if (stopping_ || cancelId_.load() == id) {
        cancelled = true;
        break;
      }
      if (raw->error()) {
        finishEntry(id, "failed", *raw->error(), 1.0);
        return;
      }
      op.tickOnce();
      ++done;
      if (done % 5 == 0 || done >= totalTicks)
        setProgress(id, std::min(1.0, static_cast<double>(done) / static_cast<double>(totalTicks)));
    }
    if (cancelled) {
      raw->abort();
      finishEntry(id, "cancelled", {}, 0);
      return;
    }
    raw->finish();
    if (raw->error()) {
      finishEntry(id, "failed", *raw->error(), 1.0);
      return;
    }
    finishEntry(id, "done", {}, 1.0);
  } catch (const std::exception& e) {
    finishEntry(id, "failed", e.what(), 0);
  }
}

int64_t ExportQueue::estimateTicks(EventStore& store, const std::vector<Event>& events) const {
  Player scratch(store, settings_.rate, false, true);
  double totalS = 0;
  for (const auto& e : events) {
    scratch.cueCopy(e);
    double s = scratch.remainingSeconds();
    if (!std::isfinite(s)) s = static_cast<double>(e.outUs - e.inUs) / 1'000'000.0;
    totalS += s;
  }
  if (settings_.transition == "fade" && settings_.fadeS > 0 && events.size() > 1)
    totalS -= settings_.fadeS * static_cast<double>(events.size() - 1);
  if (!(totalS > 0)) totalS = 0.1;
  return std::max<int64_t>(1, std::llround(totalS * settings_.rate.fps()));
}

void ExportQueue::setProgress(int id, double progress) {
  std::lock_guard lock(m_);
  for (auto& e : entries_) {
    if (e.state.id == id) e.state.progress = progress;
  }
}

bool ExportQueue::isFinal(const std::string& state) const {
  return state == "done" || state == "failed" || state == "cancelled";
}

void ExportQueue::finishEntry(int id, const std::string& state, const std::string& error,
                              double progress) {
  std::lock_guard lock(m_);
  for (auto& e : entries_) {
    if (e.state.id != id) continue;
    e.state.state = state;
    e.state.error = error;
    e.state.progress = progress;
    break;
  }
  int finished = 0;
  for (const auto& e : entries_) finished += isFinal(e.state.state) ? 1 : 0;
  for (auto it = entries_.begin(); it != entries_.end() && finished > 5;) {
    if (isFinal(it->state.state)) {
      it = entries_.erase(it);
      --finished;
    } else {
      ++it;
    }
  }
}

}  // namespace br