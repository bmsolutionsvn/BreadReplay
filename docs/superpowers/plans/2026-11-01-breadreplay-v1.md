# BreadReplay v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A standalone Windows instant-replay server: record up to 8 inputs to disk in VMX with one shared clock, cut events on any angle at any speed, play them through Preview/Output players to SDI/OMT/NDI/fullscreen, operated from a B2-layout WebView2 UI.

**Architecture:** A headless C++20 engine (`breadreplay-engine.exe`) owns inputs, per-input recorders (VMX encode → one-minute segment files), two players reading from disk, the outputs and a WebSocket control API; a shell (`BreadReplay.exe`) hosts the React UI in WebView2 and restarts the engine. Inputs, outputs, GPU helpers, the control server, the preview encoder and the shell supervisor are BreadCamera code built from a BreadCamera checkout, not copied.

**Tech Stack:** C++20 (MSVC, VS 2026), CMake + vcpkg, D3D11, libvmx/libomt via OMT-HX, FFmpeg (LGPL), doctest, ixwebsocket, nlohmann-json, WebView2; React 18 + TypeScript + Vite + vitest, `@bmsolutionsvn/design`.

**Spec:** `BreadReplay/docs/superpowers/specs/2026-11-01-breadreplay-design.md` (read it with this plan; § numbers below refer to it).

## Global Constraints

- Repos: **BreadReplay** is a new git repo at `F:\Coding\BreadMedia\BreadReplay` (Task 1 runs `git init`, branch `main`, work on branch `br-v1`). **BreadCamera** changes (Tasks 1, 5, 7) go on BreadCamera branch `bcam-replay-taps`; BreadCamera's own verification command (its plan `docs/superpowers/plans/2026-10-11-remaining-milestones-opencode.md`, "Verification command") must stay green after each of them.
- BreadCamera is found through CMake cache variable `BR_BREADCAMERA_DIR` (default `${CMAKE_CURRENT_SOURCE_DIR}/../BreadCamera`); OMT-HX through BreadCamera's existing `OMTHX_ROOT` (default `../OMT-HX`).
- Compiler flags as BreadCamera: `/W4 /permissive- /utf-8 /EHsc`, `NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00`.
- FFmpeg LGPL only (never `gpl`, `nonfree`, `x264`, `x265`). Every SDK stays behind BreadCamera's `BC_WITH_<NAME>`; a missing SDK never fails the build.
- Tests that need hardware, NDI, OMT, DeckLink or a GPU encoder use BreadCamera's `BC_REQUIRES(bc::test::Req::…)` and are skipped with a printed reason.
- Namespace `br` for BreadReplay code; BreadCamera types keep `bc::`.
- Control API only on 127.0.0.1 with a per-launch token (`bc::ControlServer`).
- UI: English only; signal states `ready|connecting|live|offline`; system errors persist as a block, never toasts; BM design tokens.
- Recording never stops on its own; capture threads never block on encode or disk (§1.3, §4.4).
- Defaults (§10): show rate 60000/1001; output 1920×1080; VMX SQ; Last N 12 s; cue padding 0; default event speed 0.5; speed lock on; transition Fade 0.5 s; low-space warning 30 min; no recording limit; end action `hold`.
- Conventional commit prefixes, one commit per task, message ends with the attribution line the executor's instructions give.
- Build machine: AMD RX 6600 (no NVIDIA); build only through `scripts/build.ps1`.

## Verification command (every BreadReplay task)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
./build/release/br_tests.exe
```
UI tasks also run `npm --prefix ui test` (Node 24, `PATH="/c/Program Files/nodejs:$PATH"`).

## Review Focus

1. **An input whose size or rate differs from the show** (a 720p30 observer in a 1080p59.94 show) plays fitted and letterboxed, never stretched, at the right speed — Task 11 test `Renderer: a 4:3 input is letterboxed`, Task 9 test `Player: a 30 fps angle in a 60 fps show repeats frames`.
2. **An event that crosses a minute-segment boundary or a signal gap** plays straight across the boundary and holds the last frame through the gap — Task 3 test `InputStore: find works across a minute boundary`, Task 10 test `FrameSource: a gap returns the last frame before it, flagged`.
3. **Disk full while saved events protect the oldest minutes** deletes only unprotected segments and keeps recording; with nothing deletable it reports that, never stops — Task 4 tests `Retention: protected segments survive`, `Retention: nothing deletable returns empty`.
4. **Engine restart in the middle of a show** reopens segments (truncating a half-written frame) and events, and recording resumes — Task 2 test `Segment: recovery drops a record whose bytes are missing`, Task 13 test `Engine: a restarted engine reopens the show and keeps recording`.
5. **Marking while Preview reviews an old moment vs. at the live edge** uses Preview's position vs. now; Last N always uses now — Task 13 test `Router: marks use Preview's time when it is reviewing`.

---

## File map

```
BreadReplay/
  CMakeLists.txt  CMakePresets.json  vcpkg.json  .gitignore
  scripts/build.ps1  scripts/test.ps1
  src/core/vmx_codec.{h,cpp}      VMX encode/decode/preview wrapper (Task 1)
  src/core/timecode.{h,cpp}       clock + HH:MM:SS:FF (Task 1)
  src/store/segment.{h,cpp}       one-minute essence+index files (Task 2)
  src/store/input_store.{h,cpp}   all segments of one input (Task 3)
  src/store/retention.{h,cpp}     deletion choice, write meter, pre-flight (Task 4)
  src/record/recorder.{h,cpp}     encode queue → InputStore (Task 6)
  src/events/event_store.{h,cpp}  events, lists, marks (Task 8)
  src/events/show_file.{h,cpp}    show.json load/save (Task 8)
  src/play/player.{h,cpp}         playhead logic, remaining time (Task 9)
  src/play/frame_source.{h,cpp}   decode, cache, prefetch, previews (Task 10)
  src/play/renderer.{h,cpp}       GPU upload, zoom, fit, mix (Task 11)
  src/play/output_player.{h,cpp}  master tick, audio, highlight, outputs (Task 12)
  src/engine/engine.{h,cpp} command_router.{h,cpp} engine_main.cpp (Task 13)
  src/shell/shell_main.cpp        (Task 14)
  tools/soak/soak_main.cpp        (Task 18)
  tests/*.cpp                     doctest, one file per module
  ui/                             React app (Tasks 15–17)
  docs/acceptance.md              owner's hardware checklist (Task 18)
BreadCamera/ (branch bcam-replay-taps)
  CMakeLists.txt                  BC_LIBS_ONLY, current-dir paths (Task 1)
  src/io/input.h                  CpuVideoFrame, CodedVideoFrame, sink hooks (Tasks 1, 7)
  src/io/{decklink,ndi,omt,dshow_capture,mf_capture,ffmpeg}_input.cpp (Tasks 5, 7)
```

---

### Task 1: Build scaffold, VMX wrapper, timecode

**Files:**
- Modify (BreadCamera): `CMakeLists.txt`, `src/io/input.h`
- Create (BreadReplay): `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `.gitignore`, `scripts/build.ps1`, `scripts/test.ps1`, `src/core/vmx_codec.{h,cpp}`, `src/core/timecode.{h,cpp}`, `tests/main.cpp`, `tests/vmx_codec_test.cpp`, `tests/timecode_test.cpp`

**Interfaces:**
- Produces (BreadCamera `io/input.h`):
```cpp
enum class CpuPixel { UYVY, NV12, BGRA };
struct CpuVideoFrame {            // valid only during the onCpuVideo call
  const uint8_t* data = nullptr;  // NV12: chroma plane starts at data + stride * height
  int stride = 0; uint32_t width = 0, height = 0;
  CpuPixel format = CpuPixel::UYVY; bool fullRange = false;
  int64_t ptsUs = 0; std::optional<Rate> rate;
};
// added to InputSink, all with defaults so BreadCamera's Camera is unchanged:
virtual bool wantsCpuVideo() const { return false; }
virtual bool wantsGpuVideo() const { return true; }
virtual void onCpuVideo(const CpuVideoFrame&) {}
```
- Produces (BreadReplay):
```cpp
namespace br {
enum class VmxQuality { LQ, SQ, HQ };                     // VMX_PROFILE_LQ / _SQ / _HQ
struct EncodedFrame { std::vector<uint8_t> bytes; uint32_t previewLen = 0; };
struct Size { uint32_t w = 0, h = 0; };
class VmxEncoder { public: VmxEncoder(uint32_t w, uint32_t h, VmxQuality q);
  EncodedFrame encode(const bc::CpuVideoFrame& f); };       // throws std::runtime_error
class VmxDecoder { public: VmxDecoder(uint32_t w, uint32_t h);
  void decodeUYVY(std::span<const uint8_t> frame, uint8_t* dst, int stride);
  void decodePreviewBGRA(std::span<const uint8_t> prefix, uint8_t* dst, int stride);
  static Size previewSize(uint32_t w, uint32_t h); };          // 1920x1080 -> 240x135
uint32_t vmxPreviewLength(std::span<const uint8_t> frame, uint32_t w, uint32_t h);
int64_t nowUs();                                            // Unix epoch µs, QPC-based, monotonic
std::string formatTimecode(int64_t epochUs, bc::Rate rate); // local time of day "HH:MM:SS:FF"
}
```
CMake targets: `br_core` (static, links `bc_core bc_gpu bc_io`, libvmx), `br_tests` (doctest, links `br_*` and BreadCamera's `tests/support/requires.cpp`, `test_media.cpp`).

- [ ] **Step 1: BreadCamera `BC_LIBS_ONLY`.** In BreadCamera `CMakeLists.txt` add `option(BC_LIBS_ONLY "Build only bc_core/bc_gpu/bc_io" OFF)`, replace every `CMAKE_SOURCE_DIR`/`CMAKE_BINARY_DIR` with `CMAKE_CURRENT_SOURCE_DIR`/`CMAKE_CURRENT_BINARY_DIR` (so it works under `add_subdirectory`), and `return()` right after `bc_io`'s last `target_*` call when `BC_LIBS_ONLY` is on. Add the `input.h` additions above. Run BreadCamera's verification command: all previously passing tests still pass. Commit in BreadCamera: `build: BC_LIBS_ONLY and CPU-frame sink hooks for BreadReplay`.
- [ ] **Step 2: BreadReplay skeleton.** `git init`; first commit on `main` is the existing `docs/` (spec + this plan), then branch `br-v1`. `CMakeLists.txt` sets `BC_LIBS_ONLY ON`, `add_subdirectory(${BR_BREADCAMERA_DIR} breadcamera-build)`, copies BreadCamera's flags, presets and the `vcpkg.json` dependency list without `opencv4`. `scripts/build.ps1`/`test.ps1` are BreadCamera's, renamed (`bc_tests`→`br_tests`, the UI step pointing at `ui/`).
- [ ] **Step 3: Write failing tests** in `tests/vmx_codec_test.cpp` and `tests/timecode_test.cpp`:
```cpp
TEST_CASE("VmxCodec: UYVY round trip keeps the picture") {
  // 1920x1080 UYVY horizontal gradient, SQ
  // decodeUYVY(encode(f).bytes) vs source: PSNR on luma >= 38 dB
}
TEST_CASE("VmxCodec: preview decodes from the prefix alone") {
  // e = encode(f); CHECK(e.previewLen > 0); CHECK(e.previewLen < e.bytes.size());
  // decodePreviewBGRA(first previewLen bytes) succeeds; previewSize(1920,1080) == {240,135}
  // mean |preview - 8x8 box-downscaled source| < 8 per channel
}
TEST_CASE("VmxCodec: NV12 and BGRA inputs encode") { /* both decode with PSNR >= 35 dB */ }
TEST_CASE("Timecode: frames count at the nominal rate") {
  // local 01:00:00.500 at Rate{50,1} -> "01:00:00:25"; at Rate{60000,1001} -> "01:00:00:30"
}
TEST_CASE("Timecode: nowUs never goes backwards") { /* 10000 calls, non-decreasing */ }
```
- [ ] **Step 4: Run** `./build/release/br_tests.exe` — FAIL (symbols missing).
- [ ] **Step 5: Implement** `vmx_codec.cpp` over `vmxcodec.h` (`VMX_Create` with `VMX_COLORSPACE_BT709`, `VMX_Encode{UYVY,NV12,BGRA}`, `VMX_SaveTo`, `VMX_GetEncodedPreviewLength`, `VMX_LoadFrom`, `VMX_DecodeUYVY`, `VMX_DecodePreviewBGRA`); one instance per encoder/decoder object, not shared across threads. Frame count = `floor(frac_second * round(rate.fps()))` (non-drop-frame).
- [ ] **Step 6: Run** the verification command — PASS.
- [ ] **Step 7: Commit** `feat: scaffold BreadReplay with VMX codec and timecode`.

### Task 2: Segment files

**Files:** Create `src/store/segment.{h,cpp}`, `tests/segment_test.cpp`

**Interfaces:**
- Consumes: Task 1 `br::EncodedFrame`.
- Produces:
```cpp
struct SegmentHeader { uint32_t version = 1; std::string inputId; uint32_t width = 0, height = 0;
  bc::Rate rate; uint16_t audioChannels = 2; uint32_t sampleRate = 48000; };  // audio: float32 interleaved
struct FrameRecord { int64_t tsUs; int64_t offset; uint32_t videoLen, previewLen, audioLen; }; // 32 bytes packed
class SegmentWriter { public:
  SegmentWriter(std::filesystem::path base, SegmentHeader h);   // creates base.vmxs + base.vmxi
  void append(int64_t tsUs, std::span<const uint8_t> video, uint32_t previewLen, std::span<const float> audio);
  void close();  size_t frames() const;  int64_t bytes() const; };
class SegmentReader { public:
  static std::optional<SegmentReader> open(const std::filesystem::path& base);
  void refresh();                                        // picks up newly flushed index records
  const SegmentHeader& header() const;  std::span<const FrameRecord> records() const;
  std::optional<size_t> indexAtOrBefore(int64_t tsUs) const;
  std::vector<uint8_t> readVideo(size_t i, bool previewOnly) const;
  std::vector<float> readAudio(size_t i) const; };
void recoverSegment(const std::filesystem::path& base); // drops index records past the essence end
```
Essence frame = 24-byte header (`'BRF1'`, tsUs, videoLen, audioLen, previewLen) + audio + video. The writer flushes the index whenever ≥ 0.5 s of timestamps passed since the last flush, and on `close()`. Files open with `FILE_SHARE_READ` for readers.

- [ ] **Step 1: Failing tests:**
```cpp
TEST_CASE("Segment: frames read back in order by timestamp")       // 100 frames, 16683 µs apart; readVideo(i) == input bytes
TEST_CASE("Segment: a reader sees frames flushed while the writer is open") // write 60 frames over 1.0 s of ts; reader.refresh(); records().size() >= 30
TEST_CASE("Segment: previewOnly reads exactly previewLen bytes")
TEST_CASE("Segment: indexAtOrBefore")  // before first -> nullopt; between frames -> earlier; after last -> last
TEST_CASE("Segment: recovery drops a record whose bytes are missing") // 10 frames, truncate .vmxs mid-frame 10, recoverSegment -> 9 records
TEST_CASE("Segment: audio stays with its frame") // frame 5's audio == block written with frame 5
```
- [ ] **Step 2: Run** — FAIL. **Step 3: Implement.** **Step 4: Run** verification — PASS.
- [ ] **Step 5: Commit** `feat: one-minute segment essence and index files`.

### Task 3: InputStore

**Files:** Create `src/store/input_store.{h,cpp}`, `tests/input_store_test.cpp`

**Interfaces:**
- Consumes: Task 2.
- Produces:
```cpp
struct FrameRef { size_t segment; size_t index; int64_t tsUs; };
struct SegmentInfo { std::filesystem::path base; int64_t firstTs, lastTs; int64_t bytes; };
class InputStore { public:
  InputStore(std::filesystem::path dir, SegmentHeader proto); // loads + recovers existing segments
  void append(int64_t tsUs, std::span<const uint8_t> video, uint32_t previewLen, std::span<const float> audio);
  std::optional<FrameRef> find(int64_t tsUs) const;            // at or before, else nullopt
  bool isGapAt(int64_t tsUs) const;                            // no frame within 2 periods before tsUs
  std::vector<uint8_t> readVideo(const FrameRef&, bool previewOnly) const;
  std::vector<float> readAudio(const FrameRef&) const;
  std::vector<SegmentInfo> segments() const;
  bool removeSegment(const std::filesystem::path& base);      // refuses the open (writing) segment
  std::vector<std::pair<int64_t,int64_t>> coverage() const;   // ranges split at gaps > 2 periods
  int64_t newestTs() const;  int64_t bytesOnDisk() const;
  const SegmentHeader& header() const; };
```
A new segment starts when the local wall-clock minute of `tsUs` changes; segment base name `YYYYMMDD-HHMMSS` of its first frame. One writer thread, many reader threads (`std::shared_mutex` around the segment list).

- [ ] **Step 1: Failing tests:** `InputStore: rolls a new segment each minute` (frames spanning 12:00:59.5–12:01:00.5 → 2 segments); `InputStore: find works across a minute boundary`; `InputStore: a gap splits coverage` (frames 0–1 s and 3–4 s → 2 ranges, `isGapAt(2 s)`); `InputStore: reopening loads existing segments`; `InputStore: removeSegment refuses the open segment`.
- [ ] **Step 2–4:** Run (FAIL) → implement → verification (PASS).
- [ ] **Step 5: Commit** `feat: per-input segment store`.

### Task 4: Retention, write meter, pre-flight

**Files:** Create `src/store/retention.{h,cpp}`, `tests/retention_test.cpp`

**Interfaces:**
- Consumes: Task 3 `SegmentInfo`.
- Produces:
```cpp
struct RetentionLimit { std::optional<double> hours; std::optional<int64_t> bytes; };
struct ProtectedRange { int64_t inUs, outUs; };
struct StoredSegment { std::string inputId; SegmentInfo info; bool open; };
std::vector<StoredSegment> segmentsToDelete(std::vector<StoredSegment> all, const RetentionLimit&,
    int64_t freeBytes, int64_t minFreeBytes, std::span<const ProtectedRange> keep);
class WriteRateMeter { public: void add(int64_t bytes, int64_t nowUs); double bytesPerSec(int64_t nowUs) const; }; // 5 s window
std::optional<double> secondsLeft(int64_t freeBytes, double writeBytesPerSec);   // nullopt when rate == 0
struct Preflight { bool canRecord; double minutesFree; double writeMBps; std::vector<std::string> warnings; std::optional<std::string> error; };
Preflight preflight(const std::filesystem::path& folder, int inputs, double mbpsPerInput); // ≤3 s / ≤256 MB test write, then deleted
```
Deletion: oldest `firstTs` first, never `open`, never a segment overlapping a `ProtectedRange`; stop once the limit holds and `freeBytes ≥ minFreeBytes` (minFreeBytes = 2 minutes of current write rate, computed by the caller). Pre-flight warnings when `minutesFree < 30` or measured write speed < 1.5× the needed rate.

- [ ] **Step 1: Failing tests:** `Retention: oldest goes first`; `Retention: protected segments survive`; `Retention: the hours limit deletes beyond it`; `Retention: nothing deletable returns empty` (all protected or open); `Retention: secondsLeft` (10 GB at 100 MB/s → 100 s); `Preflight: a temp folder can record` (`canRecord`, `minutesFree > 0`, `writeMBps > 0`); `Preflight: an unwritable folder is refused with a message`.
- [ ] **Step 2–4:** FAIL → implement → PASS. **Step 5: Commit** `feat: retention, disk rate and pre-flight`.

### Task 5: CPU frames from BreadCamera inputs (BreadCamera branch `bcam-replay-taps`)

**Files:** Modify BreadCamera `src/io/decklink_input.cpp`, `ndi_input.cpp`, `omt_input.cpp`, `dshow_capture_input.cpp`, `mf_capture_input.cpp`, `ffmpeg_input.cpp`; test `tests/ffmpeg_input_test.cpp` (BreadCamera)

**Interfaces:**
- Consumes: Task 1 `InputSink::wantsCpuVideo/wantsGpuVideo/onCpuVideo`.
- Produces: every listed adapter calls `sink.onCpuVideo(frame)` with the CPU picture it already has (before any upload) when `wantsCpuVideo()`, and skips the GPU upload and `onVideo` when `!wantsGpuVideo()`. `FfmpegInput` with a CPU sink decodes in software and converts with swscale to UYVY. `BreadCallInput` is unchanged (GPU only; documented as not recordable in v1).

- [ ] **Step 1: Failing test** (BreadCamera): `FfmpegInput: a CPU-video sink gets UYVY frames and no textures` — `writeTestTs(tmp, 1280, 720, 30, 2)`, sink with `wantsCpuVideo=true, wantsGpuVideo=false`; after 2 s ≥ 45 `onCpuVideo` calls, all `CpuPixel::UYVY`, 1280×720, `onVideo` count 0, `ptsUs` increasing.
- [ ] **Step 2:** Run BreadCamera tests — FAIL. **Step 3:** Implement in all six adapters (hardware ones untested here: say "not verified on hardware" in the commit body).
- [ ] **Step 4:** BreadCamera verification — PASS. **Step 5: Commit** (BreadCamera) `feat: CPU-frame tap on inputs for recording`.

### Task 6: Recorder

**Files:** Create `src/record/recorder.{h,cpp}`, `tests/recorder_test.cpp`

**Interfaces:**
- Consumes: Task 1 `VmxEncoder`, `bc::CpuVideoFrame`; Task 3 `InputStore`.
- Produces:
```cpp
struct RecorderStats { uint64_t framesIn, framesWritten, dropped; int queued; double encodeMsAvg; };
class Recorder { public:
  Recorder(InputStore& store, VmxQuality q, int queueDepth = 8, int threads = 2);
  ~Recorder();                                         // stop()
  void pushVideo(const bc::CpuVideoFrame& f, int64_t tsUs); // copies; drops + counts when full; never blocks
  void pushCoded(int64_t tsUs, std::vector<uint8_t> vmx);   // OMT pass-through; computes previewLen (Task 1)
  void pushAudio(std::span<const float> samples, int channels, int sampleRate); // attached to the next video frame
  RecorderStats stats() const;  void stop(); };
```
Encode in parallel on `threads` workers, write in arrival order (sequence numbers). One `VmxEncoder` per worker, recreated when the frame size changes; a size change also closes the open segment (InputStore starts a new one with the new header).

- [ ] **Step 1: Failing tests:** `Recorder: 180 frames at 60 fps all reach the store in order`; `Recorder: a full queue drops and counts, push never blocks` (`threads=1, queueDepth=2`, 100 instant pushes of 1080p UYVY: `dropped > 0`, `framesIn == 100`, slowest `pushVideo` < 10 ms); `Recorder: audio stays with its frame`; `Recorder: a size change starts a new segment`.
- [ ] **Step 2–4:** FAIL → implement → PASS. **Step 5: Commit** `feat: per-input VMX recorder`.

### Task 7: OMT coded pass-through (BreadCamera + BreadReplay)

**Files:** Modify BreadCamera `src/io/input.h`, `src/io/omt_input.cpp`; BreadReplay `tests/recorder_test.cpp`

**Interfaces:**
- Produces (BreadCamera `input.h`):
```cpp
struct CodedVideoFrame { std::span<const uint8_t> data; std::string codec; uint32_t width, height; int64_t ptsUs; };
virtual bool wantsCodedVmx() const { return false; }
virtual void onCodedVideo(const CodedVideoFrame&) {}
```
`OmtInput` receives with OMT-HX `PixelFormat::Coded` when the sink `wantsCodedVmx()` and the source sends full OMT; it calls `onCodedVideo` with `codec == "vmx"`. An HX (HEVC/H.264) source falls back to decoded CPU frames (Task 5 path).

- [ ] **Step 1: Failing tests:** BreadCamera `OmtInput: a coded sink gets VMX bytes` (`BC_REQUIRES(Req::Omt)`, local OMT sender, ≥ 20 frames, `codec == "vmx"`); BreadReplay `Recorder: pushCoded stores the frame with its preview length` (bytes from `VmxEncoder`, stored `previewLen == EncodedFrame.previewLen`).
- [ ] **Step 2–4:** FAIL → implement → both verifications PASS. **Step 5: Commit** each repo: `feat: OMT VMX pass-through to the recorder`.

### Task 8: Events, lists, show file

**Files:** Create `src/events/event_store.{h,cpp}`, `src/events/show_file.{h,cpp}`, `tests/event_store_test.cpp`, `tests/show_file_test.cpp`

**Interfaces:**
- Consumes: Task 4 `RetentionLimit`, `ProtectedRange`; Task 1 `VmxQuality`.
- Produces:
```cpp
struct ZoomRect { float cx = 0.5f, cy = 0.5f, scale = 1.0f; };       // scale 1..4
struct AngleCut { int64_t tUs; std::string inputId; };
struct SpeedPoint { int64_t tUs; double speed; };
struct ZoomPoint { int64_t tUs; ZoomRect rect; };
struct Event { int id; std::string listId, name; std::vector<std::string> tags; std::map<std::string,std::string> angleNotes;
  int64_t inUs, outUs; std::vector<AngleCut> angles; std::vector<SpeedPoint> speeds; std::vector<ZoomPoint> zooms; };
struct EventList { std::string id, name; std::vector<int> order; };
class EventStore { public:
  explicit EventStore(int64_t preRollUs = 0, int64_t postRollUs = 0);
  Event& markIn(int64_t t, const std::string& angle);   // reuses an event that has In but no Out yet
  Event& markOut(int64_t t, const std::string& angle, int64_t lastNUs); // no open In -> event [t-lastN, t]
  Event& lastN(int64_t nowUs, int64_t nUs, const std::string& angle);
  void trim(int id, int64_t inDeltaUs, int64_t outDeltaUs);  void remove(int id);  void clearCuts(int id);
  void recordAngle(int id, int64_t t, const std::string& inputId); // erases points with tUs >= t, inserts
  void recordSpeed(int id, int64_t t, double s);  void recordZoom(int id, int64_t t, ZoomRect r);
  std::string newList(const std::string& name);  void selectList(const std::string& id);
  void reorder(const std::string& listId, std::vector<int> order);
  const Event* find(int id) const;  const std::vector<EventList>& lists() const;  const std::string& currentList() const;
  std::vector<ProtectedRange> protectedRanges() const;  uint64_t version() const; // bumps on every change
  nlohmann::json toJson() const;  static EventStore fromJson(const nlohmann::json&); };
std::string angleAt(const Event&, int64_t t);   double speedAt(const Event&, int64_t t, double fallback);
ZoomRect zoomAt(const Event&, int64_t t);
struct ShowSettings { bc::Rate rate{60000,1001}; uint32_t outW = 1920, outH = 1080; VmxQuality quality = VmxQuality::SQ;
  double lastNSeconds = 12, preRollS = 0, postRollS = 0, defaultSpeed = 0.5; std::string transition = "fade";
  double fadeS = 0.5; int lowSpaceMinutes = 30; RetentionLimit limit; std::string endAction = "hold", slatePath;
  std::map<std::string, std::string> hotkeys; std::string recordingFolder; };
struct ShowInput { std::string id, name; bc::InputConfig input; };
struct Show { std::string name; std::vector<ShowInput> inputs; std::vector<bc::OutputConfig> outputs; ShowSettings settings; EventStore events; };
Show loadShow(const std::filesystem::path& showJson);   void saveShow(const std::filesystem::path& showJson, const Show&); // tmp + rename
```
A new event gets one `AngleCut{inUs, angle}` and one `SpeedPoint{inUs, defaultSpeed}`; ids increase from 1 and are never reused; the first list "Game 1" exists from the start.

- [ ] **Step 1: Failing tests:** `Events: lastN makes an event ending now` (now=100 s, N=12 → in 88 s, out 100 s, in current list); `Events: pre/post roll widen marks`; `Events: a second Mark In moves the open In`; `Events: Mark Out without In uses Last N`; `Events: angleAt follows cuts`; `Events: recordAngle overwrites what comes after`; `Events: ids are never reused`; `Events: json round trip`; `ShowFile: save leaves no temp file and loads back equal`.
- [ ] **Step 2–4:** FAIL → implement → PASS. **Step 5: Commit** `feat: events, lists and show file`.

### Task 9: Player logic

**Files:** Create `src/play/player.{h,cpp}`, `tests/player_test.cpp`

**Interfaces:**
- Consumes: Task 8 `EventStore`, `angleAt/speedAt/zoomAt`.
- Produces:
```cpp
struct PlayerState { int64_t posUs = 0; std::optional<int> eventId; std::string angle; ZoomRect zoom;
  double lever = 1.0; bool leverLocked = true; bool overriding = false; bool playing = false; int dir = +1; };
struct TickResult { std::string inputId; int64_t sourceUs; ZoomRect zoom; double speed; bool ended; bool angleChanged; };
class Player { public:
  Player(EventStore& events, bc::Rate showRate, bool recordCuts);
  void cue(int eventId);  void cueAt(int64_t t, const std::string& angle);
  void play();  void pause();  void setDirection(int dir);
  void step(int frames);                         // ± show periods, pauses
  void setLever(double s);                       // ignored while locked
  void setSpeedPreset(double s);                 // keys 1-4: works while locked
  void lockLever(bool locked);
  void setAngle(const std::string& id);  void setZoom(ZoomRect r);
  TickResult tick();                             // advance by one show period
  double remainingSeconds() const;               // to Out, see below
  const PlayerState& state() const;  void restore(const PlayerState&); };
```
Speed while an event plays: recorded `speedAt` unless the operator moved the lever or pressed a preset this pass (`overriding`), then the lever value, recorded with `recordSpeed`. Angle and zoom changes are recorded with `recordAngle/recordZoom` when `recordCuts` and an event is cued. `remainingSeconds` integrates `dt / speed` over [pos, out] with recorded speeds, or the lever speed for the rest of the event when overriding. Speed 0 → `remainingSeconds` returns +inf and the UI shows "--".

- [ ] **Step 1: Failing tests:** `Player: 50 % advances half a period per tick`; `Player: reports ended at Out and stops`; `Player: reverse stops at In`; `Player: remaining time at 50 % for a 5 s event is 10 s`; `Player: an angle cut switches the source at its time`; `Player: the lever is locked by default but presets work`; `Player: moving the lever records speed points`; `Player: a 30 fps angle in a 60 fps show repeats frames` (`sourceUs` step 8341 µs at 50 %, consumer picks frame at-or-before).
- [ ] **Step 2–4:** FAIL → implement → PASS. **Step 5: Commit** `feat: player playhead and remaining time`.

### Task 10: FrameSource

**Files:** Create `src/play/frame_source.{h,cpp}`, `tests/frame_source_test.cpp`

**Interfaces:**
- Consumes: Task 3 `InputStore`; Task 1 `VmxDecoder`.
- Produces:
```cpp
struct DecodedFrame { int64_t tsUs; uint32_t w, h; std::vector<uint8_t> uyvy; std::vector<float> audio;
  int channels, sampleRate; bool inGap; };
struct PreviewImage { int64_t tsUs; uint32_t w, h; std::vector<uint8_t> bgra; };
class FrameSource { public:
  FrameSource(std::map<std::string, const InputStore*> stores, int threads = 4, size_t cacheFrames = 32);
  std::shared_ptr<const DecodedFrame> get(const std::string& inputId, int64_t tsUs); // at or before; nullptr if none ever
  void prefetch(const std::string& inputId, int64_t fromUs, int64_t stepUs, int count);
  std::shared_ptr<const PreviewImage> preview(const std::string& inputId, int64_t tsUs);
  uint64_t diskReads() const; };
```
In a gap `get` returns the last frame before it with `inGap = true`. Cache is LRU keyed by (input, frame tsUs).

- [ ] **Step 1: Failing tests:** `FrameSource: get returns the frame at or before`; `FrameSource: a cached frame is not read twice` (`diskReads` unchanged); `FrameSource: a gap returns the last frame before it, flagged`; `FrameSource: preview is 240x135 and reads only the prefix`; `FrameSource: prefetch makes the next get a cache hit`.
- [ ] **Step 2–4:** FAIL → implement → PASS. **Step 5: Commit** `feat: decoded frame source with cache and previews`.

### Task 11: Renderer

**Files:** Create `src/play/renderer.{h,cpp}`, (if needed) `src/play/shaders/mix_linear.hlsl`, `tests/renderer_test.cpp`

**Interfaces:**
- Consumes: Task 10 `DecodedFrame`; Task 8 `ZoomRect`; BreadCamera `bc::GpuDevice`, `bc::TexturePool`, `bc::Converter` (`toLinear`, `crop`, `fit`, `toBgra8`, `solid`, `uploadBgra8`).
- Produces:
```cpp
class Renderer { public:
  Renderer(bc::GpuDevice&, bc::TexturePool&, bc::Converter&, uint32_t outW, uint32_t outH);
  bc::TextureRef render(const DecodedFrame& f, ZoomRect z);       // linear RGBA16F, outW x outH
  bc::TextureRef mix(const bc::TextureRef& a, const bc::TextureRef& b, float t); // t=0 -> a
  bc::TextureRef slate(const std::string& imagePath);              // black when path empty/unreadable
};
```
Upload UYVY the way BreadCamera's DeckLink input does (`io/uyvy.cpp`, `PixelLayout::UYVY`, limited range), then `toLinear` → `crop(cx - 0.5/scale, cy - 0.5/scale, 1/scale, 1/scale)` (clamped inside the frame) → `fit(outW, outH)`. `mix` is a small compute shader compiled with BreadCamera's `bc_shader` pattern.

- [ ] **Step 1: Failing tests** (WARP device, `GpuDevice::Pref::HardwareOrWarp`): `Renderer: a grey frame renders grey`; `Renderer: zoom 2x on the top-left shows the top-left colour` (quadrant-coloured input); `Renderer: a 4:3 input is letterboxed` (pillar columns black, centre coloured); `Renderer: mix at 0.5 averages`.
- [ ] **Step 2–4:** FAIL → implement → PASS. **Step 5: Commit** `feat: GPU renderer with zoom, fit and mix`.

### Task 12: OutputPlayer (tick, audio, highlight, outputs)

**Files:** Create `src/play/output_player.{h,cpp}`, `src/play/audio_follow.{h,cpp}`, `tests/output_player_test.cpp`, `tests/audio_follow_test.cpp`

**Interfaces:**
- Consumes: Tasks 9–11; BreadCamera `bc::Output`, `bc::OutFrame`, `bc::TickSchedule`, `bc::DefaultAdapterFactory::makeOutput`, `bc::ReadbackSlot`; tests use BreadCamera `tests/support/fake_output.h`.
- Produces:
```cpp
class AudioFollow { public:
  AudioFollow(int sampleRate = 48000, int channels = 2);
  std::vector<float> forTick(const DecodedFrame* f, double speed, bool angleChanged, uint32_t frames);
}; // samples only when |speed-1| < 0.01, else silence; 10 ms linear crossfade on angleChanged
class OutputPlayer { public:
  OutputPlayer(Player& p, FrameSource& src, Renderer& r, const ShowSettings& s);
  void setOutputs(std::vector<std::unique_ptr<bc::Output>> outs);
  void playHighlight(const std::string& listId);       // events in list order, Cut or Fade between
  void tickOnce();                                     // one show period: tick player, fetch, render, submit
  void start();  void stop();                          // tickOnce on a TickSchedule thread
  bool idle() const; };
```
Idle (paused or ended, no highlight next): resubmit the held frame (`endAction == "hold"`) or the slate. Fade: during the last `fadeS` of an event, also tick a second `Player` cued on the next event and `mix`. Outputs that `needsCpu()` get one shared readback per tick via `ReadbackSlot`.

- [ ] **Step 1: Failing tests:** `AudioFollow: 100 % passes samples`; `AudioFollow: slow motion is silent`; `AudioFollow: an angle change crossfades over 10 ms` (480 samples ramp); `OutputPlayer: a 2 s event at 100 % submits 120 frames at 60 fps then holds` (fake output, `tickOnce` × 150, last 30 identical); `OutputPlayer: highlight plays the list in order` (3 events, frames' source ts monotonic per event, event order 1,2,3); `OutputPlayer: a slow output never slows ticks` (fake output sleeping 100 ms; 60 `tickOnce` < 1.5 s).
- [ ] **Step 2–4:** FAIL → implement → PASS. **Step 5: Commit** `feat: output player with audio follow and highlight`.

### Task 13: Engine, command router, engine executable

**Files:** Create `src/engine/engine.{h,cpp}`, `src/engine/command_router.{h,cpp}`, `src/engine/engine_main.cpp`, `tests/router_test.cpp`, `tests/engine_test.cpp`; CMake target `breadreplay-engine` (also compiles BreadCamera `src/engine/control_server.cpp`, `preview_encoder.cpp`, `preview_hub.cpp` by path)

**Interfaces:**
- Consumes: everything above; `bc::ControlServer`, `bc::PreviewEncoder`, `bc::DefaultAdapterFactory::makeInput`.
- Produces: `Engine` (owns show, stores, recorders, sinks bridging `bc::InputSink` → `Recorder` with `wantsCpuVideo=true, wantsGpuVideo=false, wantsCodedVmx=true`, both players, OutputPlayer, retention timer every 10 s, status at 10 Hz); `CommandRouter::handle(const nlohmann::json&) -> nlohmann::json`. Commands (`{"cmd": …}`; reply `{"ok":true,…}` or `{"ok":false,"error":"…"}`):

| cmd | fields |
|---|---|
| `show.new` / `show.open` / `show.save` | `folder`,`name` / `path` / – |
| `input.add` / `input.update` / `input.remove` | `ShowInput` json / `id` |
| `output.add` / `output.update` / `output.remove` | `bc::OutputConfig` json / `id` |
| `settings.set` | partial `ShowSettings` json |
| `record.preflight` / `record.start` / `record.stop` | – |
| `player.cue` / `player.cueAt` / `player.play` / `player.pause` / `player.step` / `player.direction` / `player.lever` / `player.preset` / `player.lock` / `player.angle` / `player.zoom` / `player.live` | `player` = `preview`\|`output`, plus `eventId` / `t`,`angle` / `frames` / `dir` / `speed` / `locked` / `inputId` / `rect` |
| `take` | – (Preview state → Output, Output plays; Preview unchanged) |
| `mark.in` / `mark.out` / `mark.lastN` | `n` (seconds, optional) |
| `event.update` / `event.trim` / `event.delete` / `event.clearCuts` | `id` + fields / `inFrames`,`outFrames` |
| `list.new` / `list.select` / `list.reorder` | `name` / `id` / `id`,`order` |
| `highlight.play` | `listId` (default current) |

Status event `{"type":"status"}`: `recording`, `inputs[{id,name,state,fpsIn,dropped,queued,coverage}]`, `disk{freeBytes,secondsLeft,writeMBps}`, `alerts[{id,text,since}]` (persist until the condition clears), `players{preview,output}` each `{posUs,tc,eventId,angle,playing,speed,locked,zoom}` plus `output.remainingS`, `events{version}` (full list sent on `version` change as `{"type":"events"}`). Binary previews: Preview and Output at 960×540, 30 fps, through `bc::PreviewEncoder`/`encodePreviewMessage` with ids `"preview"`/`"output"`; angle row 240×135 BGRA at 10 fps with a 16-byte header (`'BRAP'`, input index, w, h) — the builder may switch the angle row to JPEG if bandwidth needs it.
Mark time rule: Preview within 1 s of `newestTs` = live → `now`; else Preview's `posUs`. `mark.lastN` always `now`. Retention alert text: `"Disk {name}: {minutes} min left"`, `"Disk {name} too slow: {need} MB/s needed, {have} MB/s"`, `"Disk full and every old minute is protected by an event"`.

- [ ] **Step 1: Failing tests** (`router_test.cpp` with an `Engine` built on fake inputs/outputs and an injectable clock): `Router: unknown command returns an error`; `Router: mark.lastN creates an event ending at the newest timestamp`; `Router: marks use Preview's time when it is reviewing`; `Router: take copies Preview to Output and plays it`; `Router: status carries remainingS`; (`engine_test.cpp`) `Engine: two file inputs record 3 s each` (`writeTestTs` 1280×720 30 fps, ≥ 80 frames per store); `Engine: a restarted engine reopens the show and keeps recording` (destroy, recreate on the same folder: events equal, newestTs grows).
- [ ] **Step 2–4:** FAIL → implement → PASS. **Step 5: Commit** `feat: replay engine and control API`.

### Task 14: Shell

**Files:** Create `src/shell/shell_main.cpp`; CMake target `BreadReplay` (WIN32) compiling BreadCamera `src/shell/supervisor.cpp`, `process_launcher.cpp` by path

**Interfaces:** Consumes `breadreplay-engine.exe` (port + token on its command line, same as BreadCamera's shell ↔ engine). Produces the desktop window that serves `ui/` beside the exe in WebView2 with `?port=&token=`.

Adapt BreadCamera's `shell_main.cpp`: title "BreadReplay", engine name, close asks "Stop recording and quit BreadReplay?" with primary button "Stop and quit"; no tray.

- [ ] **Step 1:** Build; run `build/release/BreadReplay.exe`; the window opens, the UI placeholder shows "Engine connected". Kill `breadreplay-engine.exe` in Task Manager → it is back within 3 s and the UI reconnects.
- [ ] **Step 2: Commit** `feat: BreadReplay shell`.

### Task 15: UI foundation (client, state, hotkeys)

**Files:** Create `ui/` (copy BreadCamera `ui/` tooling: `package.json` renamed `breadreplay-ui`, `vite.config.ts`, `tsconfig.json`, `.npmrc`, `index.html`), `ui/src/api/client.ts`, `ui/src/state/store.ts`, `ui/src/hotkeys.ts`, tests beside each

**Interfaces:**
- Consumes: Task 13 command and status shapes.
- Produces: `connect(port, token): Client` with `send(cmd: Command): Promise<Reply>`, `onStatus`, `onEvents`, `onPreview(id, frame)`; `useStatus()`, `useEvents()` hooks; `defaultHotkeys: Record<string, Command>` matching spec §6.2 (I, O, Space, Shift+Space, F1–F8, 1–4, L, Enter, P, ←/→ with Shift, Z, [ / ]); `keyToCommand(e: KeyboardEvent, map, ctx): Command | null` (ignores keys while typing in inputs).

- [ ] **Step 1: Failing tests:** `hotkeys: Space sends mark.lastN`; `hotkeys: F3 sets Preview angle to the third input`; `hotkeys: keys are ignored in text fields`; `hotkeys: a remapped key wins`; `store: an alert stays until a status without it arrives`.
- [ ] **Step 2–4:** `npm --prefix ui test` FAIL → implement → PASS. **Step 5: Commit** `feat(ui): client, state and hotkeys`.

### Task 16: UI operator screen (B2)

**Files:** Create `ui/src/screen/{Monitors,SpeedLever,EventList,AngleRow,Timeline,ControlsBar,StatusBar}.tsx`, `ui/src/format.ts`, tests beside each. Visual reference: `BreadMedia/.superpowers/brainstorm/9968-1793549723/content/b2-timeline-v3.html`.

**Interfaces:** Consumes Task 15. Produces the screen described in spec §7.

- [ ] **Step 1: Failing tests:** `format: remaining 7.4 s at 59.94 prints -00:07:24` (`-MM:SS:FF`); `format: infinite remaining prints --`; `Timeline: time maps to x and back within one pixel`; `Timeline: dragging an angle block sends event.update`; `AngleRow: clicking a tile sends player.angle for preview`; `ControlsBar: Take sends take`; `StatusBar: shows time left and a persistent disk alert`; `Monitors: Output shows the remaining countdown and a red frame`.
- [ ] **Step 2–4:** FAIL → implement → PASS (and `scripts/build.ps1` builds the UI into `build/release/ui`). **Step 5: Commit** `feat(ui): B2 operator screen`.

### Task 17: UI settings dialog

**Files:** Create `ui/src/dialogs/{Settings,InputsTab,OutputsTab,RecordingTab,PlaybackTab,HotkeysTab}.tsx`, tests

**Interfaces:** Consumes Task 15; sends `input.*`, `output.*`, `settings.set`, `record.preflight`. Input kinds offered: those BreadCamera's factory makes (`srt`, `ndi`, `omt`, `decklink`, `aja` (labelled "experimental, not verified on hardware"), `capture`); output kinds: `decklink`, `omt`, `ndi`, `fullscreen`.

- [ ] **Step 1: Failing tests:** `Settings: saving sends settings.set with only changed fields`; `Settings: a recording limit of 0 shows an inline error`; `Settings: Run pre-flight shows minutes free and warnings`; `Settings: a hotkey conflict is shown inline`.
- [ ] **Step 2–4:** FAIL → implement → PASS. **Step 5: Commit** `feat(ui): settings dialog`.

### Task 18: Soak tool and acceptance checklist

**Files:** Create `tools/soak/soak_main.cpp` (target `br_soak`), `docs/acceptance.md`

**Interfaces:** Consumes Tasks 3, 6, 10. `br_soak --inputs 8 --minutes 10 --size 1920x1080 --rate 60 --quality SQ --folder D:\BrSoak` pushes a moving synthetic UYVY pattern into N Recorders on real time while two FrameSources read random 12 s ranges at 100 % and 50 %; prints per input `dropped`, `encodeMsAvg`, `encodeMsP99`, overall write MB/s, GB per hour, reader misses; exit code 1 on any dropped frame.

`docs/acceptance.md` (owner's checklist): 3 SDI/NDI observer inputs record for 1 h with 0 drops; Space after a fight → Preview cued < 3 s; Take → SDI out plays; angle switch mid-replay is saved and replays the same; Game highlight with Fade; pull the recording drive's free space under the warning → alert stays; kill the engine → back < 3 s, events intact.

- [ ] **Step 1:** Run `br_soak --inputs 3 --minutes 2` → exit 0; record the printed numbers in `docs/acceptance.md` under "Measured on the build PC" (replace the §4.3 estimates if they differ by more than 20 %).
- [ ] **Step 2:** Run `--inputs 8 --minutes 10`; record the result (pass or the input count that holds).
- [ ] **Step 3: Commit** `test: soak tool and acceptance checklist`.
