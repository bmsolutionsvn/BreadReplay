#include "engine/engine.h"

#include <windows.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

#include "core/timecode.h"
#include "engine/packet_convert.h"
#include "engine/preview_encoder.h"
#include "io/remux.h"
#include "engine/adapter_factory.h"
#include "engine/command_router.h"
#include "engine/preview_encoder.h"
#include "engine/surface_hub.h"
#include "play/audio_follow.h"

namespace br {
namespace {
constexpr int kPreviewWidth = 960;
constexpr int kPreviewHeight = 540;
constexpr int kAngleWidth = 240;

std::string stateName(bc::SignalState s) {
  switch (s) {
    case bc::SignalState::Ready: return "ready";
    case bc::SignalState::Connecting: return "connecting";
    case bc::SignalState::Live: return "live";
    default: return "offline";
  }
}

int64_t freeBytesAt(const std::filesystem::path& p) {
  ULARGE_INTEGER freeBytes{};
  if (!GetDiskFreeSpaceExW(p.wstring().c_str(), &freeBytes, nullptr, nullptr)) return 0;
  return static_cast<int64_t>(freeBytes.QuadPart);
}

nlohmann::json zoomJson(const ZoomRect& z) {
  return {{"cx", z.cx}, {"cy", z.cy}, {"scale", z.scale}};
}

bool isAnnexB(std::span<const uint8_t> d) {
  return d.size() >= 4 && d[0] == 0 && d[1] == 0 && (d[2] == 1 || (d[2] == 0 && d[3] == 1));
}

bool hasNal(std::span<const uint8_t> d, int type, bool hevc) {
  for (size_t i = 0; i + 3 < d.size(); ++i) {
    if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
      const int t = hevc ? (d[i + 3] >> 1) & 0x3F : d[i + 3] & 0x1F;
      if (t == type) return true;
    }
  }
  return false;
}

}  // namespace

bool hasParameterSets(std::span<const uint8_t> d, StoredCodec codec) {
  if (codec == StoredCodec::Hevc) return hasNal(d, 32, true) && hasNal(d, 33, true) && hasNal(d, 34, true);
  return hasNal(d, 7, false) && hasNal(d, 8, false);
}

// AVCC/AVC length prefixes to Annex-B. MPEG-TS packets are already Annex-B.
// shortcut: 4-byte lengths only; add 1/2-byte lengths if an AVCC SRT source appears.
std::vector<uint8_t> avccPacketToAnnexB(std::span<const uint8_t> a) {
  std::vector<uint8_t> out;
  size_t pos = 0;
  while (pos + 4 <= a.size()) {
    const uint32_t len = (static_cast<uint32_t>(a[pos]) << 24) | (static_cast<uint32_t>(a[pos + 1]) << 16) |
                         (static_cast<uint32_t>(a[pos + 2]) << 8) | a[pos + 3];
    pos += 4;
    if (len == 0 || pos + len > a.size()) return {};
    out.insert(out.end(), {0, 0, 0, 1});
    out.insert(out.end(), a.begin() + pos, a.begin() + pos + len);
    pos += len;
  }
  return pos == a.size() ? out : std::vector<uint8_t>{};
}

// hvcC configuration record to Annex-B parameter sets.
std::vector<uint8_t> hvccToAnnexB(std::span<const uint8_t> a) {
  if (a.size() < 23 || a[0] != 1) return {};
  std::vector<uint8_t> out;
  size_t pos = 23;
  for (int array = 0; array < a[22]; ++array) {
    if (pos + 3 > a.size()) return {};
    const int count = (a[pos + 1] << 8) | a[pos + 2];
    pos += 3;
    for (int i = 0; i < count; ++i) {
      if (pos + 2 > a.size()) return {};
      const size_t len = (static_cast<size_t>(a[pos]) << 8) | a[pos + 1];
      pos += 2;
      if (pos + len > a.size()) return {};
      out.insert(out.end(), {0, 0, 0, 1});
      out.insert(out.end(), a.begin() + pos, a.begin() + pos + len);
      pos += len;
    }
  }
  return out;
}

std::vector<uint8_t> extradataAnnexB(const AVCodecParameters& p, StoredCodec codec) {
  if (!p.extradata || p.extradata_size <= 0) return {};
  const std::span<const uint8_t> ex(p.extradata, static_cast<size_t>(p.extradata_size));
  if (isAnnexB(ex)) return std::vector<uint8_t>(ex.begin(), ex.end());
  if (codec == StoredCodec::H264 && ex[0] == 1) {
    try {
      return bc::avccToAnnexB(ex);
    } catch (const std::exception&) {
      return {};
    }
  }
  if (codec == StoredCodec::Hevc) return hvccToAnnexB(ex);
  return {};
}

std::vector<uint8_t> packetToAnnexB(std::vector<uint8_t> au, StoredCodec codec) {
  if (isAnnexB(au)) return au;
  if (codec == StoredCodec::H264) {
    auto converted = avccPacketToAnnexB(au);
    if (!converted.empty()) return converted;
  }
  return au;  // shortcut: non-Annex-B HEVC is left as received; add when a source needs it
}

StoredColour srtColour(int colorSpace) {
  if (colorSpace == AVCOL_SPC_BT709) return StoredColour::Bt709;
  if (colorSpace == AVCOL_SPC_BT470BG || colorSpace == AVCOL_SPC_SMPTE170M) return StoredColour::Bt601;
  return StoredColour::Unknown;
}

int64_t packetSourcePtsUs(int64_t pts, AVRational timeBase, int64_t arrivalUs) {
  if (pts == AV_NOPTS_VALUE || timeBase.num <= 0 || timeBase.den <= 0) return arrivalUs;
  return av_rescale_q(pts, timeBase, AVRational{1, 1'000'000});
}

void prependParameterSets(std::vector<uint8_t>& au, const AVCodecParameters& p, StoredCodec codec) {
  if (hasParameterSets(au, codec)) return;
  const auto sets = extradataAnnexB(p, codec);
  if (sets.empty()) return;
  std::vector<uint8_t> out;
  out.reserve(sets.size() + au.size());
  out.insert(out.end(), sets.begin(), sets.end());
  out.insert(out.end(), au.begin(), au.end());
  au = std::move(out);
}

Engine::Engine(EngineDeps deps, Show show, std::filesystem::path folder)
    : deps_(std::move(deps)), show_(std::move(show)), folder_(std::move(folder)) {
  showJson_ = folder_ / "show.json";
  show_.events.setRoll(static_cast<int64_t>(show_.settings.preRollS * 1'000'000),
                       static_cast<int64_t>(show_.settings.postRollS * 1'000'000));
  previewPlayer_ = std::make_unique<Player>(show_.events, show_.settings.rate, true);
  outputPlayerState_ = std::make_unique<Player>(show_.events, show_.settings.rate, true);
  rebuildFrameSource();
}

Engine::~Engine() { stop(); }

int64_t Engine::now() const { return deps_.clock ? deps_.clock() : nowUs(); }

void Engine::Sink::onAudio(bc::AudioBlock a) {
  if (auto* rec = engine.recorderFor(inputId)) rec->pushAudio(a.samples, a.channels, a.sampleRate);
}

void Engine::Sink::onState(bc::SignalState s, std::optional<bc::ErrorInfo>) {
  // A reconnect may bring a different codec; let the picture tap be re-decided then.
  if (s == bc::SignalState::Connecting || s == bc::SignalState::Offline) coded_ = false;
  std::lock_guard lock(engine.m_);
  engine.inputStates_[inputId] = stateName(s);
}

void Engine::Sink::onCpuVideo(const bc::CpuVideoFrame& f) {
  if (auto* rec = engine.recorderFor(inputId)) rec->pushVideo(f, engine.now());
}

void Engine::Sink::onCodedVideo(const bc::CodedVideoFrame& f) {
  auto* rec = engine.recorderFor(inputId);
  if (!rec) return;
  std::vector<uint8_t> bytes(f.data.begin(), f.data.end());
  if (f.codec == "vmx") {
    rec->pushCoded(engine.now(), std::move(bytes));
    return;
  }
  const StoredCodec codec = f.codec == "h264" ? StoredCodec::H264 : StoredCodec::Hevc;
  coded_ = true;
  rec->pushCodedAu(engine.now(), f.ptsUs, codec, f.width, f.height, f.keyframe, std::move(bytes),
                   StoredColour::Bt709);  // OMT-HX is full-range BT.709 at every size
}

void Engine::Sink::onPacket(std::shared_ptr<const bc::MediaPacket> p) {
  if (!p || !p->video || !p->params) return;
  const AVCodecID id = p->params->codec_id;
  const StoredCodec codec =
      id == AV_CODEC_ID_H264 ? StoredCodec::H264 : id == AV_CODEC_ID_HEVC ? StoredCodec::Hevc : StoredCodec::Vmx;
  if (codec == StoredCodec::Vmx) return;  // other SRT codecs keep the decoded-picture path
  coded_ = true;                          // the registration packet has no data
  if (!p->pkt) return;
  auto* rec = engine.recorderFor(inputId);
  if (!rec) return;
  std::vector<uint8_t> au = packetToAnnexB(
      std::vector<uint8_t>(p->pkt->data, p->pkt->data + p->pkt->size), codec);
  if (au.empty()) return;
  const bool keyframe = (p->pkt->flags & AV_PKT_FLAG_KEY) != 0;
  if (keyframe) prependParameterSets(au, *p->params, codec);
  const int64_t arrivalUs = engine.now();
  const int64_t srcPtsUs = packetSourcePtsUs(p->pkt->pts, p->timeBase, arrivalUs);
  rec->pushCodedAu(arrivalUs, srcPtsUs, codec, static_cast<uint32_t>(p->params->width),
                   static_cast<uint32_t>(p->params->height), keyframe, std::move(au),
                   srtColour(p->params->color_space));
}

RecorderStats Engine::recorderStats(const std::string& id) {
  std::lock_guard lock(m_);
  const auto it = recorders_.find(id);
  return it == recorders_.end() ? RecorderStats{} : it->second->stats();
}

Recorder* Engine::recorderFor(const std::string& id) {
  auto it = recorders_.find(id);
  return it == recorders_.end() ? nullptr : it->second.get();
}

InputStore* Engine::storeFor(const std::string& id) {
  auto it = stores_.find(id);
  return it == stores_.end() ? nullptr : it->second.get();
}

int64_t Engine::periodUs() const {
  return static_cast<int64_t>(1'000'000.0 / show_.settings.rate.fps() + 0.5);
}

void Engine::start() {
  if (started_) return;
  started_ = true;
  recording_ = true;
  running_ = true;

  buildInputs();
  rebuildFrameSource();

  previewPlayer_ = std::make_unique<Player>(show_.events, show_.settings.rate, true);
  outputPlayerState_ = std::make_unique<Player>(show_.events, show_.settings.rate, true);
  renderer_ = std::make_unique<Renderer>(*deps_.gpu, *deps_.pool, *deps_.conv, show_.settings.outW,
                                         show_.settings.outH);
  previewRenderer_ = std::make_unique<Renderer>(*deps_.gpu, *deps_.pool, *deps_.conv, kPreviewWidth,
                                                kPreviewHeight);
  outputPlayer_ = std::make_unique<OutputPlayer>(*outputPlayerState_, *source_, *renderer_, show_.events,
                                                 show_.settings);
  buildOutputs();
  outputPlayer_->start();
  recordStart();

  // The H.264 encoders open lazily on the first fallback client (previewLoop); native video
  // needs none.
  previewThread_ = std::thread([this] { previewLoop(); });
  retentionThread_ = std::thread([this] { retentionLoop(); });
  statusThread_ = std::thread([this] { statusLoop(); });
}

void Engine::stop() {
  running_ = false;
  if (previewThread_.joinable()) previewThread_.join();
  if (retentionThread_.joinable()) retentionThread_.join();
  if (statusThread_.joinable()) statusThread_.join();
  recordStop();
  if (outputPlayer_) outputPlayer_->stop();
  previewEncoder_.reset();
  outputEncoder_.reset();
  outputPlayer_.reset();
  previewPlayer_.reset();
  outputPlayerState_.reset();
  renderer_.reset();
  previewRenderer_.reset();
  source_.reset();
  inputs_.clear();
  sinks_.clear();
  recorders_.clear();
  stores_.clear();
  inputStates_.clear();
  fpsSamples_.clear();
  fpsValues_.clear();
  started_ = false;
}

void Engine::openShow(Show show, std::filesystem::path folder) {
  const bool wasStarted = started_;
  stop();
  show_ = std::move(show);
  folder_ = std::move(folder);
  showJson_ = folder_ / "show.json";
  history_.clear();
  show_.events.setRoll(static_cast<int64_t>(show_.settings.preRollS * 1'000'000),
                       static_cast<int64_t>(show_.settings.postRollS * 1'000'000));
  eventsVersionSent_ = 0;
  (void)wasStarted;
  start();
}

void Engine::buildInputs() {
  bc::DefaultAdapterFactory factory(*deps_.gpu, *deps_.pool);
  for (const auto& in : show_.inputs) {
    if (stores_.count(in.id)) continue;
    SegmentHeader header;
    header.inputId = in.id;
    header.width = show_.settings.outW;
    header.height = show_.settings.outH;
    header.rate = show_.settings.rate;
    stores_[in.id] = std::make_unique<InputStore>(folder_ / in.id, header);
    recorders_[in.id] = std::make_unique<Recorder>(*stores_[in.id], show_.settings.quality);
    sinks_[in.id] = std::make_unique<Sink>(*this, in.id);
    inputStates_[in.id] = "ready";
    try {
      inputs_[in.id] = deps_.makeInput ? deps_.makeInput(in.input) : factory.makeInput(in.input);
    } catch (const std::exception& e) {
      inputStates_[in.id] = "offline";
      (void)e;
    }
  }
}

void Engine::buildOutputs() {
  if (!outputPlayer_) return;
  std::vector<std::unique_ptr<bc::Output>> outs;
  bc::CameraConfig cam;
  cam.name = show_.name;
  bc::DefaultAdapterFactory factory(*deps_.gpu, *deps_.pool);
  for (const auto& cfg : show_.outputs) {
    if (!cfg.enabled) continue;
    try {
      outs.push_back(deps_.makeOutput ? deps_.makeOutput(cfg) : factory.makeOutput(cfg, cam, show_.settings.rate));
    } catch (const std::exception&) {
      // a bad output never takes the engine down
    }
  }
  outputPlayer_->setOutputs(std::move(outs));
}

void Engine::rebuildFrameSource() {
  std::map<std::string, const InputStore*> map;
  for (const auto& [id, store] : stores_) map[id] = store.get();
  source_ = std::make_unique<FrameSource>(std::move(map), deps_.gpu, deps_.pool);
}

void Engine::recordStart() {
  if (!started_) return;
  for (auto& [id, input] : inputs_) {
    if (!input) continue;
    input->start(*sinks_[id]);
  }
  recording_ = true;
}

void Engine::recordStop() {
  for (auto& [id, input] : inputs_) {
    if (input) input->stop();
  }
  recording_ = false;
}

int64_t Engine::newestTs() const {
  int64_t newest = 0;
  for (const auto& [id, store] : stores_) newest = std::max(newest, store->newestTs());
  return newest;
}

Player& Engine::playerFor(const std::string& which) {
  if (which == "output" && outputPlayerState_) return *outputPlayerState_;
  if (previewPlayer_) return *previewPlayer_;
  static Player fallback(show_.events, show_.settings.rate, true);
  return fallback;
}

void Engine::take() {
  if (!previewPlayer_ || !outputPlayerState_) return;
  outputPlayerState_->restore(previewPlayer_->state());
  outputPlayerState_->play();
}

Preflight Engine::preflight() {
  return br::preflight(folder_, static_cast<int>(show_.inputs.size()), 18.0);
}

void Engine::applySettings(const nlohmann::json& j) {
  auto& s = show_.settings;
  if (j.contains("rate")) {
    if (const auto r = bc::parseShowRate(j.value("rate", std::string{}))) s.rate = *r;
  }
  if (j.contains("outW")) s.outW = j.value("outW", s.outW);
  if (j.contains("outH")) s.outH = j.value("outH", s.outH);
  if (j.contains("quality")) {
    const auto q = j.value("quality", std::string("SQ"));
    s.quality = q == "LQ" ? VmxQuality::LQ : (q == "HQ" ? VmxQuality::HQ : VmxQuality::SQ);
  }
  if (j.contains("lastNSeconds")) s.lastNSeconds = j.value("lastNSeconds", s.lastNSeconds);
  if (j.contains("preRollS")) s.preRollS = j.value("preRollS", s.preRollS);
  if (j.contains("postRollS")) s.postRollS = j.value("postRollS", s.postRollS);
  if (j.contains("defaultSpeed")) s.defaultSpeed = j.value("defaultSpeed", s.defaultSpeed);
  if (j.contains("transition")) s.transition = j.value("transition", s.transition);
  if (j.contains("fadeS")) s.fadeS = j.value("fadeS", s.fadeS);
  if (j.contains("lowSpaceMinutes")) s.lowSpaceMinutes = j.value("lowSpaceMinutes", s.lowSpaceMinutes);
  if (j.contains("limit")) {
    const auto& l = j["limit"];
    s.limit.hours.reset();
    s.limit.bytes.reset();
    if (l.contains("hours")) s.limit.hours = l.value("hours", 0.0);
    if (l.contains("bytes")) s.limit.bytes = l.value("bytes", int64_t{0});
  }
  if (j.contains("endAction")) s.endAction = j.value("endAction", s.endAction);
  if (j.contains("slatePath")) s.slatePath = j.value("slatePath", s.slatePath);
  if (j.contains("recordingFolder")) s.recordingFolder = j.value("recordingFolder", s.recordingFolder);
  if (j.contains("hotkeys")) s.hotkeys = j.value("hotkeys", std::map<std::string, std::string>{});
  show_.events.defaultSpeed = s.defaultSpeed;
  show_.events.setRoll(static_cast<int64_t>(s.preRollS * 1'000'000),
                       static_cast<int64_t>(s.postRollS * 1'000'000));
}

void Engine::addInput(const ShowInput& in) {
  show_.inputs.push_back(in);
  if (started_) {
    buildInputs();
    rebuildFrameSource();
    if (recording_) recordStart();
  }
}

void Engine::updateInput(const ShowInput& in) {
  removeInput(in.id);
  addInput(in);
}

void Engine::removeInput(const std::string& id) {
  if (auto it = inputs_.find(id); it != inputs_.end() && it->second) it->second->stop();
  inputs_.erase(id);
  sinks_.erase(id);
  recorders_.erase(id);
  stores_.erase(id);
  inputStates_.erase(id);
  show_.inputs.erase(std::remove_if(show_.inputs.begin(), show_.inputs.end(),
                                    [&](const ShowInput& i) { return i.id == id; }),
                     show_.inputs.end());
  if (started_) rebuildFrameSource();
}

void Engine::addOutput(const bc::OutputConfig& out) {
  show_.outputs.push_back(out);
  buildOutputs();
}

void Engine::updateOutput(const bc::OutputConfig& out) {
  for (auto& o : show_.outputs) {
    if (o.id == out.id) o = out;
  }
  buildOutputs();
}

void Engine::removeOutput(const std::string& id) {
  show_.outputs.erase(std::remove_if(show_.outputs.begin(), show_.outputs.end(),
                                     [&](const bc::OutputConfig& o) { return o.id == id; }),
                      show_.outputs.end());
  buildOutputs();
}

const InputStore* Engine::store(const std::string& id) const {
  auto it = stores_.find(id);
  return it == stores_.end() ? nullptr : it->second.get();
}

void Engine::saveShow() {
  std::lock_guard lock(cmdM_);
  show_.events.setRoll(static_cast<int64_t>(show_.settings.preRollS * 1'000'000),
                       static_cast<int64_t>(show_.settings.postRollS * 1'000'000));
  br::saveShow(showJson_, show_);
}

void Engine::setSender(std::function<void(const std::string&)> text,
                       std::function<void(const std::vector<uint8_t>&)> binary) {
  sendText_ = std::move(text);
  sendBinary_ = std::move(binary);
}

void Engine::setSurfaces(bc::SurfaceHub* hub) {
  std::lock_guard lock(m_);
  surfaces_ = hub;
}

void Engine::setBinaryWanted(std::function<bool()> fn) {
  std::lock_guard lock(m_);
  binaryWanted_ = std::move(fn);
}

void Engine::setNativeVideoReason(std::string reason) {
  std::lock_guard lock(m_);
  nativeReason_ = std::move(reason);
}

nlohmann::json Engine::statusJson() {
  std::lock_guard lock(m_);
  nlohmann::json j;
  j["type"] = "status";
  j["recording"] = recording_;
  j["inputs"] = nlohmann::json::array();
  const int64_t t = now();
  for (const auto& in : show_.inputs) {
    nlohmann::json ji;
    ji["id"] = in.id;
    ji["name"] = in.name;
    ji["state"] = inputStates_.count(in.id) ? inputStates_[in.id] : "offline";
    uint64_t frames = 0, dropped = 0;
    int queued = 0;
    if (auto it = recorders_.find(in.id); it != recorders_.end()) {
      const auto s = it->second->stats();
      frames = s.framesWritten;
      dropped = s.dropped;
      queued = s.queued;
    }
    auto& sample = fpsSamples_[in.id];
    if (sample.first == 0) {
      sample = {t, frames};
    } else if (t - sample.first >= 1'000'000) {
      fpsValues_[in.id] = static_cast<double>(frames - sample.second) * 1'000'000.0 /
                          static_cast<double>(t - sample.first);
      sample = {t, frames};
    }
    ji["fpsIn"] = fpsValues_.count(in.id) ? fpsValues_[in.id] : 0.0;
    ji["dropped"] = dropped;
    ji["queued"] = queued;
    if (auto it = stores_.find(in.id); it != stores_.end()) {
      ji["coverage"] = nlohmann::json::array();
      for (const auto& r : it->second->coverage()) ji["coverage"].push_back({r.first, r.second});
    }
    j["inputs"].push_back(ji);
  }
  const int64_t freeB = freeBytesAt(folder_);
  const double rate = writeMeter_.bytesPerSec(t);
  j["disk"] = {{"freeBytes", freeB}, {"secondsLeft", br::secondsLeft(freeB, rate)}, {"writeMBps", rate / 1'000'000.0}};
  j["alerts"] = alerts_;
  j["hotkeys"] = show_.settings.hotkeys;
  auto playerJson = [&](Player& p) {
    nlohmann::json jp;
    jp["posUs"] = p.state().posUs;
    jp["tc"] = formatTimecode(p.state().posUs, show_.settings.rate);
    jp["eventId"] = p.state().eventId ? nlohmann::json(*p.state().eventId) : nlohmann::json(nullptr);
    jp["angle"] = p.state().angle;
    jp["playing"] = p.state().playing;
    jp["speed"] = p.state().lever;
    jp["locked"] = p.state().leverLocked;
    jp["zoom"] = zoomJson(p.state().zoom);
    return jp;
  };
  nlohmann::json players;
  if (previewPlayer_) players["preview"] = playerJson(*previewPlayer_);
  if (outputPlayerState_) {
    players["output"] = playerJson(*outputPlayerState_);
    const double rem = outputPlayerState_->remainingSeconds();
    players["output"]["remainingS"] = std::isfinite(rem) ? nlohmann::json(rem) : nlohmann::json(nullptr);
  }
  j["players"] = players;
  j["events"] = {{"version", show_.events.version()}};
  j["edit"] = {{"canUndo", history_.canUndo()}, {"canRedo", history_.canRedo()}};
  j["nativeVideo"] = surfaces_ ? nlohmann::json(true)
                               : nlohmann::json(nativeReason_.empty() ? "native video is off" : nativeReason_);
  return j;
}

void Engine::retentionTick() {
  std::lock_guard lock(m_);
  const int64_t t = now();
  std::vector<StoredSegment> all;
  for (const auto& [id, store] : stores_) {
    for (const auto& info : store->segments()) all.push_back(StoredSegment{id, info, store->newestTs() == info.lastTs});
  }
  int64_t bytes = 0;
  for (const auto& s : all) bytes += s.info.bytes;
  writeMeter_.add(std::max<int64_t>(0, bytes - lastBytes_), t);
  lastBytes_ = bytes;
  const double rate = writeMeter_.bytesPerSec(t);
  const int64_t freeB = freeBytesAt(folder_);
  const int64_t minFree = static_cast<int64_t>(rate * 120.0);  // two minutes of the current write rate
  const auto keep = show_.events.protectedRanges();
  const auto del = segmentsToDelete(all, show_.settings.limit, freeB, minFree, keep);
  for (const auto& d : del) {
    if (auto it = stores_.find(d.inputId); it != stores_.end()) it->second->removeSegment(d.info.base);
  }

  auto setAlert = [&](const std::string& id, const std::string& text, bool on) {
    auto found = std::find_if(alerts_.begin(), alerts_.end(), [&](const nlohmann::json& a) { return a.value("id", "") == id; });
    if (on && found == alerts_.end()) alerts_.push_back({{"id", id}, {"text", text}, {"since", t}});
    if (!on && found != alerts_.end()) alerts_.erase(found);
    if (on && found != alerts_.end()) found->operator[]("text") = text;
  };
  const double secondsLeftNow = rate > 0 ? static_cast<double>(freeB) / rate : 1e18;
  setAlert("disk-space", "Disk " + folder_.string() + ": " + std::to_string(static_cast<int>(secondsLeftNow / 60)) + " min left",
           secondsLeftNow < show_.settings.lowSpaceMinutes * 60.0);
  setAlert("disk-full", "Disk full and every old minute is protected by an event",
           freeB < minFree && del.empty());
  lastRetentionUs_ = t;
}

void Engine::followLiveEdge() {
  // At the live edge Preview follows the newest recorded timestamp; while reviewing it stays where
  // the operator put it. A Preview never cued (no angle yet) also follows: it starts at the edge.
  const int64_t newest = newestTs();
  const auto pv = previewPlayer_->state();
  // "Never cued" includes an angle restored from the workspace with no position yet.
  const bool neverCued = pv.angle.empty() || pv.posUs <= 0;
  if (newest <= 0 || pv.eventId || (!neverCued && std::llabs(pv.posUs - newest) > 1'000'000)) return;
  std::string angle = pv.angle;
  if (angle.empty()) {
    std::unique_lock cmdLock(cmdM_, std::try_to_lock);
    if (cmdLock.owns_lock() && !show_.inputs.empty()) angle = show_.inputs.front().id;
  }
  if (!angle.empty()) previewPlayer_->cueAt(newest, angle);
}

void Engine::requestKeyframes() {
  previewKeyframe_ = true;
  outputKeyframe_ = true;
}

void Engine::previewLoop() {
  const auto frameTime = std::chrono::milliseconds(33);
  auto next = std::chrono::steady_clock::now();
  int angleTick = 0;
  std::vector<std::string> angleIds;
  // Each part fails on its own: a broken Preview encode must not blank Output or the angle row.
  // A failure never stops the engine (the next tick tries again); each new message is logged once.
  std::set<std::string> logged;
  auto guarded = [&](const char* part, auto&& fn) {
    try {
      fn();
    } catch (const std::exception& e) {
      const std::string msg = std::string(part) + ": " + e.what();
      if (logged.size() < 64 && logged.insert(msg).second) std::fprintf(stderr, "[breadreplay] %s\n", msg.c_str());
    }
  };
  while (running_) {
    next += frameTime;
    std::this_thread::sleep_until(next);
    if (!running_) break;
    // A fallback client asked for H.264/raw pictures; no client means the encoders stay closed.
    const bool wantBinary = binaryWanted_ && binaryWanted_();
    if (!wantBinary) {
      previewEncoder_.reset();
      outputEncoder_.reset();
    }
    guarded("preview", [&] {
      if (!previewPlayer_) return;
      followLiveEdge();
      // The playhead moves even without a picture; only the encode needs an encoder.
      const TickResult tr = previewPlayer_->tick();
      if (!source_ || !previewRenderer_) return;
      const auto frame = source_->get(tr.inputId, tr.sourceUs, 1);  // Preview's own decoder lane
      if (!frame) return;
      const auto pic = previewRenderer_->render(*frame, tr.zoom);
      if (surfaces_ && surfaces_->wanted().contains("preview")) surfaces_->present("preview", *pic);
      if (!wantBinary || !sendBinary_) return;
      if (!previewEncoder_) previewEncoder_ = std::make_unique<bc::PreviewEncoder>(kPreviewWidth, kPreviewHeight, 30);
      if (previewKeyframe_.exchange(false)) previewEncoder_->forceKeyframe();
      const auto cpu = previewRenderer_->toCpu(pic);
      for (const auto& p : previewEncoder_->encode(cpu, tr.sourceUs)) {
        sendBinary_(bc::encodePreviewMessage("preview", p));
      }
    });
    guarded("output", [&] {
      if (!outputPlayer_) return;
      const auto pic = outputPlayer_->lastPicture();
      if (!pic) return;
      if (surfaces_ && surfaces_->wanted().contains("output")) surfaces_->present("output", *pic);
      if (!wantBinary || !deps_.conv || !sendBinary_) return;
      if (!outputEncoder_) outputEncoder_ = std::make_unique<bc::PreviewEncoder>(kPreviewWidth, kPreviewHeight, 30);
      if (outputKeyframe_.exchange(false)) outputEncoder_->forceKeyframe();
      const auto bgra = deps_.conv->toBgra8(*pic, kPreviewWidth, kPreviewHeight);
      const auto cpu = deps_.conv->readback(*bgra);
      for (const auto& p : outputEncoder_->encode(cpu, now())) {
        sendBinary_(bc::encodePreviewMessage("output", p));
      }
    });
    guarded("angles", [&] {
      if (!source_ || !previewPlayer_) return;
      // Native angle tiles update every tick; the raw BRAP message keeps today's every-3rd-tick
      // rate for fallback clients.
      const bool fallbackTick = wantBinary && sendBinary_ && angleTick % 3 == 0;
      ++angleTick;
      const std::set<std::string> wanted = surfaces_ ? surfaces_->wanted() : std::set<std::string>{};
      bool anyNative = false;
      for (const auto& id : wanted) {
        if (id.rfind("angle:", 0) == 0) {
          anyNative = true;
          break;
        }
      }
      if (!anyNative && !fallbackTick) return;
      const int64_t at = previewPlayer_->state().posUs;
      std::vector<std::string> ids;
      {
        std::unique_lock cmdLock(cmdM_, std::try_to_lock);
        if (cmdLock.owns_lock()) {
          ids.clear();
          for (const auto& in : show_.inputs) ids.push_back(in.id);
          angleIds = ids;
        } else {
          ids = angleIds;
        }
      }
      uint32_t index = 0;
      for (const auto& id : ids) {
        if (const auto img = source_->preview(id, at)) {
          if (anyNative && deps_.conv && wanted.contains("angle:" + id)) {
            const auto tex = deps_.conv->uploadBgra8(img->bgra.data(), img->w, img->h, img->w * 4);
            const auto linear = deps_.conv->toLinear(*tex, bc::PixelLayout::BGRA8, true);
            surfaces_->present("angle:" + id, *linear);
          }
          if (fallbackTick) {
            std::vector<uint8_t> msg(16);
            msg[0] = 'B';
            msg[1] = 'R';
            msg[2] = 'A';
            msg[3] = 'P';
            std::memcpy(&msg[4], &index, 4);
            std::memcpy(&msg[8], &img->w, 4);
            std::memcpy(&msg[12], &img->h, 4);
            msg.insert(msg.end(), img->bgra.begin(), img->bgra.end());
            sendBinary_(msg);
          }
        }
        ++index;
      }
    });
  }
}

void Engine::retentionLoop() {
  while (running_) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!running_) break;
    if (now() - lastRetentionUs_ >= 10'000'000) {
      try {
        retentionTick();
      } catch (const std::exception&) {
      }
    }
  }
}

void Engine::statusLoop() {
  while (running_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!running_ || !sendText_) continue;
    try {
      sendText_(statusJson().dump());
      const uint64_t version = show_.events.version();
      std::unique_lock cmdLock(cmdM_, std::try_to_lock);
      if (!cmdLock.owns_lock()) continue;
      if (version != eventsVersionSent_) {
        eventsVersionSent_ = version;
        nlohmann::json e;
        e["type"] = "events";
        e["version"] = version;
        e["events"] = show_.events.toJson();
        cmdLock.unlock();
        sendText_(e.dump());
        cmdLock.lock();
      }
      // The workspace survives a crash: events and settings reach show.json within a few seconds.
      const int64_t t = now();
      if (t - lastSaveUs_ > 5'000'000) {
        lastSaveUs_ = t;
        saveShow();
      }
    } catch (const std::exception&) {
    }
  }
}

nlohmann::json Engine::handle(const nlohmann::json& cmd) {
  std::lock_guard lock(cmdM_);
  return router_.handle(cmd);
}

}  // namespace br