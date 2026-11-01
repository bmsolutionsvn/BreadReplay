# BreadReplay — Design

Date: 2026-11-01. Status: written from the brainstorming session; the owner chose to skip the
remaining questions ("just do it first, I will test and modify later"), so every decision marked
**(call)** was made by the planner and is open to change after the owner tests the first build.

## 1. Intent

BreadReplay is a standalone instant-replay server for live production, in the BM Solutions
ecosystem. It records every camera continuously to disk with one shared timecode, lets one
replay operator cut a replay of any moment on any angle at any speed, and sends it out on one
output that the director's switcher takes like any other input.

- **First users:** the owner's esports shows: 2–3 observer feeds. Replays are 10–15 s, cut right
  after a team fight, and the whole game's replays are played back as a highlight at the end.
- **Next users:** traditional sports with more cameras (8–12+).
- **Reference:** vMix Instant Replay for ease of use; dedicated sports replay servers for the
  screen layout (one cued moment, all angles in sync).
- **Ecosystem place:** its own Windows app on its own PC first. Later BreadStudio gets a lighter
  replay of its own built on the same engine, for a different audience; BreadReplay stays a
  separate product (owner, 2026-11-01). This replaces roadmap stage S4's "replay inside
  BreadStudio" for the dedicated-operator case; S4 keeps playlists/playout and the lighter replay.

### 1.1 Success (v1)

- One PC records **up to 8 inputs at 1080p60/50** continuously while two players read from disk,
  with no dropped input frame caused by BreadReplay.
- From the end of a fight to a replay ready on Preview: **under 3 s** with the "Last N s" key.
- The output never goes black because of BreadReplay: an input drop, a slow disk or a UI crash
  affect only what they touch; the engine keeps recording and the output keeps its last frame.
- A replay plays frame-accurately: a mark is the same instant on every angle.

### 1.2 Non-goals for v1

- 4K, 120 fps, interlaced output, HDR.
- Genlock, reading SDI embedded timecode (LTC/VITC).
- Interpolated (optical-flow) slow motion; v1 repeats frames.
- Export of clips/highlights to MP4 or ProRes files.
- MIDI, Stream Deck, jog/shuttle or T-bar controllers (keyboard + mouse in v1).
- A second replay output (vMix's channel B). One output, as decided.
- Tally or status links to the director's switcher; the director only takes the output.
- Login, billing, cloud. BreadReplay is free and offline, like BreadCamera (roadmap R9).

### 1.3 Anti-goals (would be a failure even if it works)

- Recording that stops or pauses on its own (disk full, slow disk, one bad input).
- A replay that is "near" the marked frame instead of exact (lesson from obs-multireplay:
  "exact or refused").
- A screen that needs study during a fight: the operator must read state at a glance.

## 2. What we learned from existing systems

| System | What it does | What we take |
|---|---|---|
| **vMix Replay** (commercial) | Up to 8 cameras, continuous AVI 4:2:2 recording, events with per-angle checkboxes and notes, per-event speed that later events inherit, highlight reel with transitions and music, a replay MultiView output | Event model with per-angle notes; speed inheritance; highlight with transitions. Music bed later. |
| **Futatabi** (free software, Nageru project) | Separate replay PC; intra-only MJPEG; a *workspace* that survives a crash/power loss; clip list → playlist; Cue in/Cue out keys with *cue padding*; "repeated cue-in" and "instant clip" workflows; default 0.5× speed; a master-speed lever **locked at 100 % by default**; audio only near 100 %; remaining time exported over REST so the switcher can cut back automatically; a cheap DJ controller (Behringer CMD PL-1) used as the slow-mo remote | Crash-proof workspace; cue padding; speed lock; remaining-time status in the control API; audio at 100 % only; DJ-controller idea for v2. |
| **CasparCG "MAV" replay** (open source) | One essence file + one index file per input: index = header + one 64-bit byte offset per frame; audio stored with each frame; one writer, many readers | Our storage format follows this shape (essence + per-frame index, audio beside each frame). |
| **obs-multireplay** (GPL, OBS plugin) | Up to 8 angles on one shared timeline; **pre-flight before REC** (free space in minutes, measured disk speed, cameras present); live per-angle health that names a dead camera; marking keys that reach backwards (−5/−10/−20 s); pre/post-roll; trim to the frame; review while recording; reverse and frame step; tags; MP4 export by stream copy; hardware controllers via small bridges | Pre-flight and health panel; backwards-reaching keys ("Last N s"); trim by frame; reverse/frame step; "exact or refused". |
| **obs-sports-replay** (GPL, OBS plugin) | Compressed buffer, end action (freeze / return / loop), sponsor intro/outro bumpers around a replay | End action setting; a "REPLAY" sting before/after a replay is useful for esports — v2. |

Licence note: obs-multireplay and obs-sports-replay are GPL; Futatabi is GPL. We take ideas only,
no code. libvmx, libomt and the BreadCamera code are ours/MIT.

## 3. Architecture

```
┌──────────────── BreadReplay.exe (shell) ─────────────────┐
│ WebView2 UI (React + @bmsolutionsvn/design)   supervisor │
└──────────────▲───────────────────────────────────────────┘
               │ WebSocket 127.0.0.1 + per-launch token (JSON + binary previews)
┌──────────────┴──────── breadreplay-engine.exe ───────────────────────────┐
│ Inputs ─▶ Recorder (per input: VMX encode ─▶ segment writer) ─▶ disk    │
│                                    disk ─▶ Player PREVIEW ─▶ UI only     │
│                                    disk ─▶ Player OUTPUT  ─▶ Outputs     │
│ Event store · Disk monitor · Control API · Preview streams to UI         │
└──────────────────────────────────────────────────────────────────────────┘
```

- Same split as BreadCamera (spec `BreadCamera/docs/superpowers/specs/2026-10-08-breadcamera-design.md` §2):
  a headless engine that owns inputs, recording, playback and outputs, and a shell that hosts the
  UI and restarts the engine (exit or 3 s without heartbeat). Closing the UI never stops recording.
- **Later BreadStudio extension:** BreadStudio drives the same engine through the same control API
  (or links the engine library). Nothing in the engine may depend on the UI. **(call)**
- **Code reuse (call):** BreadReplay builds the BreadCamera source it needs (`src/core`, `src/gpu`,
  `src/io` adapters: DeckLink, AJA, NDI, OMT, SRT/FFmpeg, BreadCall, DirectShow/MF capture; outputs:
  DeckLink, OMT, NDI, fullscreen) straight from a BreadCamera checkout given by the CMake variable
  `BR_BREADCAMERA_DIR` (default `../BreadCamera`). No copies, no fork. When a third product needs
  the same code, extract a shared library then, not now. BreadCamera's effects (matte, colour,
  upscaler, autoframe) are not built.
- **Language and tools:** C++20, CMake + vcpkg, VS 2026, D3D11 — the BreadCamera toolchain
  (`BreadCamera/CLAUDE.md` "Build machine facts"). The build PC has an AMD RX 6600; NVIDIA-only
  code stays optional and labelled untested.

## 4. Recording

### 4.0 Show

A **show** is one production (a match day): a folder `<recording folder>/<show>/` holding
`show.json` (inputs, outputs, settings, event lists and events) and the recordings. Opening a show
reopens everything as it was. One show is open at a time. Inputs: up to 16 configurable, tested
at 8 (call).

### 4.1 Per input

```
Input adapter ─▶ frame in CPU memory (UYVY / NV12 / BGRA) ─▶ VMX encode ─▶ segment writer
            └─▶ (OMT input sending VMX) ─────────── coded frame ─────────▶ segment writer
```

- **Codec: VMX** from the owned libvmx fork (built through `OMT-HX/cmake/omt.cmake`). Intra-only,
  4:2:2, SIMD on the CPU, any GPU vendor. Default quality profile **SQ (call)**; LQ/SQ/HQ is a
  setting. Encode runs on the input's own recorder thread pool, never on the capture thread.
- Encode from the frame in CPU memory where the adapter has it (DeckLink, AJA, NDI, OMT, DirectShow
  give CPU frames). Where the adapter only has a GPU texture (FFmpeg d3d11va decode), read back
  once. The builder adds a CPU-frame tap to the adapters that upload today.
- **OMT pass-through:** an OMT source that sends full (VMX) frames is written as received, with no
  decode and no encode. This needs a coded-frame receive path in the OMT input (OMT-HX
  `PixelFormat::Coded`).
- **Free preview:** VMX can decode a 1/8-size picture (240×135 at 1080p) from the first part of a
  frame (`VMX_GetEncodedPreviewLength`, `VMX_DecodePreview*`). The angle row, timeline thumbnails
  and fast scrubbing read only that prefix. No separate proxy files.
- **Audio:** each input's audio (48 kHz, its channel count, float or s16 — builder) is stored with
  the video frame it arrived with.
- **Timestamps:** every frame gets the engine's clock (QueryPerformanceCounter mapped to time of
  day) on arrival, after subtracting the adapter's known capture latency where it reports one.
  All inputs share this one clock, so a mark is the same instant on every angle. Inputs keep
  their own frame rate; the player maps output ticks to source frames by timestamp.

### 4.2 Storage format (call)

Per input, per minute (wall clock), two files in
`<recording folder>/<show>/<input id>/`:

- `YYYYMMDD-HHMMSS.vmxs` — essence: for each frame, a small header (timestamp, video length,
  audio length, preview length), the audio block, then the VMX frame.
- `YYYYMMDD-HHMMSS.vmxi` — index: header (format version, input id, size, rate, colour space,
  sample format) then one fixed-size record per frame: timestamp µs (int64), essence offset
  (int64), video length, preview length, audio length (uint32 each).

Rules:
- One writer per input, sequential appends, large buffered writes. Readers open the files being
  written with shared read and trust only index records already flushed.
- The index is flushed at least every 0.5 s: a crash or power loss loses at most the last 0.5 s.
- One-minute segments make deletion clean and keep any lookup to a binary search in one small
  index file.
- On engine start, the workspace (segments + events) is reopened as it was (Futatabi's workspace
  lesson). A segment whose last records point past the end of the essence is truncated to the
  last whole frame.

### 4.3 Disk budget (estimates to confirm by the benchmark in the plan)

| Inputs at 1080p60, VMX SQ ≈150 Mbps | Write | Per hour |
|---|---|---|
| 3 | ≈55 MB/s | ≈200 GB |
| 8 | ≈150 MB/s | ≈540 GB |
| 12 | ≈225 MB/s | ≈810 GB |

Plus two players reading at full size (≈40 MB/s) and preview reads (small). Advice in the UI and
docs: a TLC NVMe used only for recording; QLC drives slow down once their cache fills.

### 4.4 Retention and disk monitor (owner: "limit of record, monitor space left")

Settings:
- **Recording folder** (drive).
- **Recording limit** in hours or GB per show. When reached, the oldest minute segments are
  deleted, **except segments that overlap a saved event** (call).
- **Low-space warning** at N minutes left (default 30).

Always on screen while recording: free space, **time left at the current write rate**, write
speed, and per-input health (fps in, dropped, encoder queue). Behaviour:
- Disk full or limit reached → delete oldest unprotected segments; never stop recording **(call)**.
  If nothing can be deleted, the alert says so and frames are dropped and counted per input; the
  engine keeps running.
- Disk too slow (writer queue growing) → a persistent alert that names the disk; frames are
  dropped at the encoder queue, never by blocking capture.
- **Pre-flight on Record** (from obs-multireplay): free space in minutes for the configured inputs,
  a 3 s measured write speed, inputs present. It warns; it refuses only what cannot work
  (folder not writable).

## 5. Playback

### 5.1 Two players, one output (owner: B2)

- **Preview player** — operator only; its pictures go only to the UI.
- **Output player** — on air; its frames go to every configured output.
- **Take** hands Preview's event, angle, position and speed to Output, which starts playing.
  Preview stays where it was **(call)**, so the operator can cue the next one.
- Each player state: source position (timestamp), event (optional), current angle, speed,
  direction, playing/paused.

### 5.2 Frame production

- The output player runs on a master tick at the **show frame rate** (setting: 25, 29.97, 30, 50,
  59.94, 60). At each tick it advances source time by `tick × speed`, finds the frame of the
  current angle at that timestamp (nearest at or before), decodes it on a CPU worker
  (`VMX_DecodeUYVY`/`BGRA`), uploads it, and hands it to outputs. Decode is pipelined a few frames
  ahead in the play direction.
- **Output format:** the show has one output size (default 1920×1080) and rate. An input of another
  size is fitted and letterboxed, never stretched (BreadCamera's rule).
- **Slow motion:** v1 repeats the nearest source frame **(call)**. Optical-flow interpolation
  (Futatabi does it on the GPU) is a later option.
- **Reverse and frame step** both ways (from obs-multireplay); pause holds the frame.
- **Zoom:** a crop rectangle per player (centre + scale, up to 4× — call), applied on the GPU when
  scaling to the output size. Zoom changes are saved in the event like angle changes.
- When Output is idle: **end action** setting — hold the last frame (default) or show a slate
  image (call; from obs-sports-replay).
- **Outputs:** BreadCamera's DeckLink SDI, OMT, NDI and fullscreen adapters, unchanged, each behind
  its bounded queue so a slow output never slows the player.

### 5.3 Speed

- A speed lever (UI slider, keys) from 0 to 100 % **(call: no fast-forward above 100 % in v1)**.
- Default speed for a new event: **50 %** (Futatabi's common default; call). Keys 1–4 = 100/75/50/25 %.
- **Speed lock** (Futatabi): the lever is locked unless unlocked, so a stray drag cannot change a
  replay on air.

### 5.4 Audio (owner: "audio follow input")

- The output plays the audio of the angle on air; an angle cut switches the audio with it, with a
  ≈10 ms crossfade.
- Audio plays only at 100 % speed; below that it is muted **(call)**. Pitch-kept slow audio later.
- Highlight fades fade the audio with the picture.

## 6. Events, marking and the highlight

### 6.1 Event

```
Event { id, list, name, tags, in, out,
        angleCuts: [{t, inputId}],   // which input shows from timecode t (owner)
        speedPoints: [{t, speed}],
        zoomPoints: [{t, rect}] }
```

- `in`/`out` are timestamps on the shared clock, so an event covers every angle.
- **Angle cuts are recorded:** while an event plays (in Preview or Output), switching angle,
  moving the speed lever or zooming writes a point at the current source time, replacing any
  later points from the same pass **(call)**. "Clear cuts" resets an event to one angle.
- Event lists: one per game/match ("Game 1", "Game 2"…), created by the operator; new events go
  to the current list.

### 6.2 Marking keys (defaults, all remappable)

| Key | Action |
|---|---|
| I / O | Mark In / Mark Out (pressing In again moves the open In: Futatabi's "repeated cue-in") |
| Space | **Last N s** — an event ending now, N = 12 s default (setting) |
| Shift+Space | Last 2N s |
| F1–F8 | Switch Preview's angle |
| 1–4 | Speed 100/75/50/25 % |
| L | Unlock/lock the speed lever |
| Enter | Take (Preview → Output) |
| P | Play/pause the selected player |
| ← / → | Frame step (Shift = 1 s) |
| Z | Zoom mode (mouse wheel + drag on the monitor) |
| [ / ] | Trim In / Out of the selected event by one frame |

- **Cue padding** (Futatabi): optional pre-roll added to In and post-roll to Out, default 0.
- **Which time a mark uses** (vMix's live/recorded modes): when Preview is at the live edge, marks
  use *now* (the newest recorded timestamp); when Preview is reviewing, marks use Preview's
  position (call). "Last N s" always ends at *now*.
- Timecode shown everywhere as time of day `HH:MM:SS:FF` at the show rate.

### 6.3 Highlight (owner: "play all as highlight", "transitions fade or cut")

- Plays the current list's events in order, each with its saved angle cuts, speeds and zoom.
- Between events: **Cut** or **Fade** (duration setting, default 0.5 s). A fade needs a third
  decode stream briefly.
- The operator can drop or reorder events in the list before rolling.

### 6.4 Remaining time (owner)

- The Output monitor shows a large **remaining** countdown and a progress bar, computed from the
  event's speed points, using the current lever speed for the part ahead (an estimate when the
  operator rides the lever — Futatabi documents the same limit).
- The same value is in the control API status, so a future BreadStudio extension or a talkback
  overlay can use it.

## 7. Operator UI (B2 — mockups in `BreadMedia/.superpowers/brainstorm/9968-1793549723/content/`, `b2-timeline-v3.html` is the approved one)

- **Top:** Preview (blue frame, operator only) and Output (red frame, on air, with the remaining
  countdown), the speed lever beside them, the event list on the right with Highlight under it.
- **Angle row:** every input at Preview's timecode (1/8 VMX previews); click = switch angle.
- **Timeline:** a whole-game bar (event ticks, a box for the zoomed span) and a zoomed track
  (≈30 s): ruler, In/Out brackets, playhead, the event's angle/speed blocks (editable by drag),
  and one thin lane per input showing recording (green) and signal gaps (red).
- **Controls bar:** In, Out, Last N s, jog, Take.
- **Status bar:** disk free / time left / write speed, per-input health, alerts that stay until
  dealt with (BM UX contract: system errors persist, never as toasts).
- **Settings dialog:** inputs, outputs, show frame rate, recording folder and limit, VMX quality,
  Last N, cue padding, hotkeys, end action/slate.
- English only, the four signal states, BM design tokens — as BreadCamera.
- Monitor pictures to the UI: Preview and Output at ≈960×540 at full rate via BreadCamera's
  preview encoder path; angle row at 240×135 from VMX previews (builder chooses transport).

## 8. Control API

WebSocket on 127.0.0.1 with a per-launch token, JSON commands and status events, as BreadCamera's
control server. Commands cover: show (open/new), inputs, outputs, record start/stop, players
(cue, play, pause, step, speed, angle, zoom, take), events (mark, last-N, edit, trim, delete,
reorder, lists), highlight, settings. Status includes per-input health, disk, players, remaining
time. The UI uses nothing else, so BreadStudio can later drive the engine the same way.

## 9. Failure rules

| Failure | Behaviour |
|---|---|
| Input drops | Its lane shows a gap; recording of others continues; reconnect with backoff (BreadCamera rules). |
| Encoder falls behind | Drop at the encoder queue, count, alert; capture never blocks. |
| Disk slow/full | §4.4. |
| Output adapter fails | That output errors and retries; the others and recording continue. |
| Decode miss at a tick | Output repeats its last frame; counted. |
| UI crash | Engine continues; shell restarts the UI. |
| Engine crash | Shell restarts it; it reopens the workspace and resumes recording within 3 s. |

## 10. Settings defaults (owner can change)

Show rate 59.94; VMX SQ; Last N 12 s; cue padding 0; default event speed 50 %; speed lock on;
transitions Fade 0.5 s; low-space warning 30 min; recording limit off (fill the disk, then roll).

## 11. Left to the builder

- Thread and buffer sizes, queue depths, write block size, whether to use unbuffered I/O.
- Exact audio sample format in the essence.
- How preview pictures travel to the UI.
- Component structure of the React UI within the BM design package.
- How each adapter exposes its CPU frame for encoding.
