# BreadReplay v1 — owner's acceptance checklist

Run this on the show PC with the real cameras. Every line is a pass/fail the owner can judge.

## Hardware
- [ ] 3 SDI/NDI observer inputs record for 1 h with **0 dropped frames** (status bar per input).
- [ ] Space after a fight → Preview is cued on the last 12 s in **under 3 s**.
- [ ] Take → the SDI output plays the replay; Preview stays where it was.
- [ ] Switch angle mid-replay, let it end, replay the same event: the angle change replays the same.
- [ ] Play a Game highlight: events in list order, Fade between them.
- [ ] Pull the recording drive's free space under the warning: the disk alert stays on screen.
- [ ] Kill `breadreplay-engine.exe` in Task Manager: it is back within 3 s and the events are intact.
- [ ] Close the window: while recording the dialog says "Stop recording and quit BreadReplay?" and "Stop and quit" stops the engine; while stopped Close quits at once.

## Measured on the build PC

`br_soak --inputs 3 --minutes 2 --size 1920x1080 --rate 60 --quality SQ`:
- 7200 frames per input, **0 dropped**, encode 4.2 ms avg / 6.2 ms p99
- 39.3 MB/s total (141 GB/h per 3 inputs), readers 8907 reads / 0 misses

`br_soak --inputs 6 --minutes 2 --size 1920x1080 --rate 60 --quality SQ`:
- 7200 frames per input, **0 dropped**, encode 6.6 ms avg / 10.5 ms p99
- 78.4 MB/s total (282 GB/h), readers 7758 reads / 0 misses

`br_soak --inputs 8 --minutes 10 --size 1920x1080 --rate 60 --quality SQ`:
- **Does not hold on this PC.** Input 4 wrote 31958 of 36000 generated frames and dropped 969;
  inputs 3, 5, 6 and 7 dropped 17–351; inputs 0–2 dropped 0–2.
- 101.0 MB/s total (363 GB/h), encode 12–15 ms avg (80 ms p99 at the worst input), readers 0 misses.
- **The input count that holds is 6** at 1080p60 SQ on the build PC (0 drops over 2 min).
  A second 8-input 2-min run with one libvmx thread per encoder dropped 177–0 per input, so the
  ceiling is CPU encode time, not disk (disk wrote 99–101 MB/s in both runs).

The spec's §4.3 estimates are ≈150 Mbps per 1080p60 VMX SQ input (≈55 MB/s for 3, ≈150 MB/s for 8).
The measured 3-input rate is 39.3 MB/s, 29 % below that estimate, so the spec table is corrected
below the measurement.