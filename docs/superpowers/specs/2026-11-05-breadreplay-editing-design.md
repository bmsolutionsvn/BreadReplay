# BreadReplay — Editing: Multicam Timeline, Groups, Multi-Play and Export (Design)

Date: 2026-11-05. Owner decisions are marked **(owner)**; calls the designer made are marked
**(call)** so the owner can check them. Mockups (approved ones marked) are in
`F:\Coding\BreadMedia\.superpowers\brainstorm\16760-1793820474\content\`.

## 1. Intent

BreadReplay already records every camera, cuts replays live and plays a list as a highlight
(design `2026-11-01-breadreplay-design.md`). The owner runs esports shows: replays of 10–15 s cut
right after a fight, then the game's replays played as a highlight. What is missing:

- **Shaping a replay after the fact** feels too stiff. Today the timeline only lets you drag a
  camera switch; trimming is `[` / `]` one frame at a time. The owner wants the direct
  manipulation of Premiere or CapCut: drag either end, move things, split, zoom.
- **Organising a show's replays** by game (Game 1, Game 2…), with names that can change and
  replays that can move between groups.
- **Playing a chosen set** of replays (not just a whole list), in an order the operator can change.
- **Getting replays out as files** (exports) — v1 of BreadReplay left export out (its §1.2).
- **Knowing which camera a Mark uses.** The live flow is good **(owner)**, but it is hard to see
  which camera Mark In/Out will record. Today the target is the camera Preview shows
  (`CommandRouter::markAngle()`), marked only by a thin outline on its tile.

Success: during a live game nothing gets slower and the operator never doubts where a mark goes;
between games the operator reshapes replays, sorts them into games and exports a highlight
without leaving the app.

## 2. Goals, non-goals, anti-goals

**Goals**
1. A multicam replay timeline (§4) — editing on the recording, one track per camera.
2. Clear PGM / PVW / mark-target language across the screen (§5).
3. Groups with a root level (§6).
4. Multi-select, play a selection in order, reorder it (§7).
5. Export to MP4, H.264 or H.265, with a quality choice (§8).
6. Undo / redo for every edit (§4.4) **(call)**.
7. Cleanup: free the disk of everything no replay uses (§9) **(owner)**.

**Non-goals (this round)**
- Two cameras on screen at once (picture-in-picture, split screen): the owner chose one camera on
  air at a time **(owner)**.
- A separate sequence editor (CapCut-style clip track): the highlight and export reuse the replay
  timeline and the group order **(owner)**.
- Music or stings under a highlight, ProRes, direct upload to YouTube/Facebook **(call)**.
- Audio editing; audio stays muted below 100 % speed as today.

**Anti-goals (a failure even if it works)**
- Anything that slows the live flow: an extra click or key before Mark / Take, a modal dialog,
  focus stolen from the hotkeys.
- Any stutter or delay on the on-air output caused by editing or exporting.
- Telling instead of showing: on-screen sentences such as "marks go here" **(owner)**.

## 3. Constraints

- The UI drives the engine only through the control API (BreadReplay design §8); BreadStudio
  will drive the same commands later. Every new action is an engine command.
- Existing hotkeys keep their meaning (F1–F8 camera to Preview, `[`/`]` trim, mark keys, take).
- Edits apply to the event store and are saved with the show, as today; a crash loses nothing
  that was saved.
- Export never shares the on-air player and never preempts it (its own decode lane and encoder;
  lower priority).
- The product FFmpeg has **no libx264** (seen 2026-11-05): export uses the GPU encoders the
  BreadCamera libraries already open (NVENC / AMF / QSV), with Media Foundation as the fallback.
- New or touched UI text is at least 11 px; text on colour meets WCAG AA (4.5:1) **(call)**.

## 4. The multicam replay timeline **(owner: A + "multicam tracks")**

Mockups: `timeline-directions.html` (A chosen), `multicam-and-groups.html` (option 1 chosen).

### 4.1 Layout

- Top: the whole-recording bar (as today) with replay ticks and the view box; dragging the box
  moves the view.
- Under it, the ruler and **one track per camera** (input). Each track shows that camera's
  recording faintly (gaps red, as today).
- The selected replay is a window across all tracks (its In and Out). Inside it, **one block is
  on air at each moment**: the block sits on its camera's track. Blocks are the replay's angle
  cuts (`Event::angles`): block *i* spans `angles[i].tUs` to the next cut (or Out) on
  `angles[i].inputId`'s track.
- Under the tracks: the speed lane and the zoom lane for the replay (today's speed lane, plus zoom).
- Playheads: Preview's in green, Output's in red when they are in view (§5).

### 4.2 Actions

| Action | Gesture | Effect on the event |
|---|---|---|
| Trim start / end | drag the window's left / right handle | `inUs` / `outUs` (`[` / `]` still step one frame) |
| Move a camera switch | drag the edge between two blocks | that cut's `tUs` |
| Change a block's camera | drag the block up / down to another track | that cut's `inputId` |
| Move a block | drag the block sideways | its own cut and the next cut move together, clamped by the neighbours **(call)** |
| Split | `S`, or right-click → Split at playhead | a new cut at the playhead, same camera |
| Delete a block | select it + `Delete` | the cut is removed; the previous block extends (the first block's next one extends back) |
| Slip the replay | drag the window's top bar | In, Out and every cut and speed/zoom point move together **(call)** |
| Speed / zoom stretch | drag its edges in the lanes; right-click for a value | `speeds` / `zooms` points |
| Pan / zoom | wheel pans, Ctrl+wheel zooms at the cursor, `F` fits the replay | view only |

- Everything snaps to frames; with snapping on (default) edges also snap to the playheads, In/Out
  and other switch points (Shift while dragging disables) **(call)**.
- A drag previews locally and sends **one** engine command on release (as the switch drag does
  today), so editing never floods the engine.
- Out of range is impossible, not an error: edges clamp to recorded media and to neighbours.

### 4.3 Multi-select strip **(owner)**

When more than one replay is selected (§7), a "play order" strip appears above the tracks: the
selected replays as blocks in play order. Dragging a block there reorders them (the same order the
list shows). Clicking a block selects that replay for editing below.

### 4.4 Undo / redo **(call)**

Ctrl+Z / Ctrl+Y (and Ctrl+Shift+Z) undo and redo any change to replays, groups, order and
selection-independent edits, including a new mark. The engine keeps the history (a bounded stack
of event-store snapshots, e.g. 200), so any client sees the same history. Live playout is not an
edit and is never undone. The history lives in memory only: it is cleared when the show is closed
or the app restarts **(call)**.

### 4.5 What is on air never changes under the operator **(call)**

The Output player plays a **frozen copy** of a replay (or of a highlight's whole list), taken at
Take / Play. Timeline edits, group moves and undo change the store, and so the *next* play, but
never the picture on air. Today `Player::tick()` re-reads the event every frame; that changes for
Output. (Live riding during Output, the lever and the camera keys, still acts on the picture at
once, as today; see §4.6.)

### 4.6 Live riding records only what it plays over **(owner)**

- While a replay **plays** (Preview or Output), the camera keys, the speed lever and zoom still
  record into it, so riding a replay live works as today.
- A pass replaces only **the stretch it actually played over**, from where play started to where
  it stopped. Points the operator placed after that stay. Today `overwriteFrom` erases every later
  point; that goes.
- One pass is **one** undo step, however many lever ticks it had.
- While a replay is **cued or paused**, the camera keys only switch Preview's camera, which sets
  the mark target (§5). They do not edit the replay.

## 5. PGM / PVW / mark target **(owner: A, "shown, not told")**

Mockup: `mark-target-v2.html` (approved).

- **Red = on air (PGM):** the Output monitor's frame and tag `PGM [n]`, a `PGM` tally on that
  camera's tile, the red Output playhead.
- **Green = Preview (PVW):** the Preview monitor's frame and tag `PVW [n]`, a `PVW` tally on its
  tile, the green Preview playhead.
- **Camera colour + ◎ = where marks land.** The target is the camera Preview shows (unchanged
  rule). Its tile gets a thick frame in the camera's colour and the ◎ badge; the other tiles dim;
  Mark In / Mark Out show ◎ and the camera number in its colour; that camera's track is outlined.
- On each Mark: a short stamp on Preview, `IN [2] 41:12.4` (or OUT), ~1.5 s, and a pin on the
  target camera's track **(call: duration)**.
- Colour is never the only cue: the camera number and name always appear beside it.
- Tally colours: PGM `#e5484d` (text on `#b3261e`), PVW `#30a46c` (text on `#17694a`).
- **Camera colours move off red and green** so a camera never reads as a tally: Cam 1 `#5aa0ff`,
  Cam 2 `#e4a94b`, Cam 3 `#c084fc`, Cam 4 `#f472b6`; Cams 5–8 are the builder's, under the rule:
  no hue within ±25° of the tally red or green, and at least 3:1 against the dark track **(call)**.
- No explanatory sentences on screen.

## 6. Groups **(owner)**

Mockup: `multicam-and-groups.html` (groups panel).

- **Root:** with no group, replays sit in one plain list at the root. A new show starts with no
  group (today `EventStore::ensureList` creates "Game 1"; that goes). An existing show keeps its
  lists as groups.
- **Target group:** selecting a group makes new replays (Mark In/Out, Last N) go into it. It is
  marked the same way as the camera target (◎) **(call)**. Clicking it again, or the root, sends
  new replays to the root **(call)**.
- Create (`+ Group`, default name "Game N"), rename (double-click or right-click → Rename,
  inline; **not F2**, which stays Camera 2 **(call)**), delete (its
  replays move to the root; asks once) **(call: delete behaviour)**, collapse / expand.
- Move: drag one replay or a selection onto a group (or the root); drag inside a group to reorder.
- Each group: Play (highlight, top to bottom, Fade or Cut as set), Export.
- Saved with the show, like replays today.

## 7. Multi-select and play **(owner)**

- Ctrl+click adds or removes; Shift+click selects a range; Ctrl+A selects a group's replays.
- **Play selection** plays the selected replays one after another **in list order, top to
  bottom**, each with its own cuts, speeds and zoom, Fade or Cut between them as the highlight
  does. A selection across groups plays in displayed order **(call)**.
- Reordering a selection: drag in the list, or on the timeline strip (§4.3). Both change the same
  order: each group's own list order. A drop across a group boundary, in the list or on the
  strip, moves the replay into that group, so there is one order model and no separate
  "selection order" **(call)**.
- Keys: the new single keys (`S` split, `F` fit, `Delete`, Ctrl+A, Ctrl+Z/Y) join the remappable
  hotkey list in Settings; none collides with today's defaults. Like today's hotkeys, they act
  everywhere except while a text field (a rename) has focus **(call)**.
- Single-click on one replay still loads it in Preview as today.

## 8. Export **(owner: MP4, H.264 or H.265, quality or bitrate)**

- **What:** a group, the current selection, or one replay (right-click → Export) — rendered
  exactly as it plays: cuts, speeds, zooms, Fade/Cut, audio as played (muted below 100 %).
- **Format:** MP4. Codec H.264 or H.265, chosen per export. Quality: **High / Medium / Small**
  or an exact bitrate in Mbps. Presets for 1080p30 **(call)**: H.264 20 / 10 / 5 Mbps,
  H.265 12 / 6 / 3 Mbps, scaled by pixels × frame rate. AAC 48 kHz stereo 192 kb/s **(call)**.
  The show's raster and rate.
- **Where:** a folder set once in Settings; file named `<Group or Selection or replay name> -
  YYYY-MM-DD HHmm.mp4` **(call)**; never overwrites (adds ` (2)`).
- **How it runs:** in the background, from the recordings, as fast as the GPU allows but below
  the on-air player's priority. Progress and Cancel in the status bar; several exports queue one
  at a time **(call)**. A failed export says why and leaves no half file under the final name.
- The export dialog remembers the last codec and quality.

## 9. Cleanup **(owner: "remove all video outside the marked replays, to save disk")**

- A **Clean up** button (in the recording/disk area, not the live controls) deletes every
  recorded picture and sound that no replay uses, across all cameras.
- **Kept:** for every replay, on **every** camera (a camera can still be switched in later), its
  In to Out plus a margin of 3 s each side, so a trim can still grow a little **(call)**.
- **Never touched:** the segments being written, and the last 5 minutes of the recording, so a
  late Last N still works **(call)**.
- **Granularity: the replay, not the minute.** Retention today deletes whole one-minute segments,
  which would keep most of the recording around short replays. Cleanup instead rewrites each
  closed segment down to the kept ranges. Compressed (H.264/HEVC) ranges start at the keyframe at
  or before the range, so the kept stretch can begin up to one GOP early **(call)**.
- Before it runs, one confirmation shows how much disk it frees and that it cannot be undone. It
  is the only modal in this design, and it lives outside the live flow.
- It runs in the background, below the players' priority, and never stalls Preview or Output. A
  replay that is playing keeps playing.
- Afterwards the timeline shows the removed stretches as gaps; trims and undo clamp to what is
  left.

## 10. Left to the builder

- How to split this into plans and in what order (a sensible cut: (1) groups, multi-select, play
  selection and the PGM/PVW/target visuals; (2) the timeline editing, live-riding passes and
  undo; (3) export; (4) cleanup).
- Command names and shapes (they extend the existing `event.*`, `list.*`, `highlight.*` families),
  and the status fields the UI needs (export progress, undo/redo availability).
- The offline renderer's structure: reuse the output player's composition (cuts, speed, zoom,
  fades) rather than a second copy of it.
- Encoder selection and fallbacks within the constraint in §3; how H.265 is offered when no
  hardware HEVC encoder exists (hide the choice or explain it).
- Exact visuals within §5 (spacing, icon drawing), keeping the approved mockup's look.
- Tests: engine tests for groups/move/delete-to-root, undo/redo, selection play order, export
  (frame-accurate against the player's output for a short replay with cuts, speed and a fade,
  and no output-player stall while exporting); UI tests for each gesture in §4.2.
