stall_play_patch.md

Summary
-------
Problem: after a stall/rebuffer the virtual player used an absolute-deadline scheduler that attempted to "catch up" to the original timeline. That meant the first frame(s) immediately after a stall could be played with a shortened wait (e.g., 30ms instead of full 40ms for 25fps), causing a bursty/accelerated playback that doesn't match the expected stable post-rebuffer pacing.

Change implemented
------------------
File changed: `build/src/player.c`

What I changed:
- Made the `start_ms` playback baseline mutable instead of const.
- Calculated `frame_interval_ms = 1000.0 / (double)fps` up front.
- On stall recovery (when buffer refills and playback resumes) I "rebase" the playback timeline by resetting `start_ms` so subsequent deadlines are scheduled from the current time:

  start_ms = now - ((double)played_frames * frame_interval_ms);

Rationale
---------
- The original absolute scheduling keeps the timeline anchored to the initial start time. After a stall this causes the player to play subsequent frames immediately (or with shortened waits) until it reaches a positive future deadline. That is efficient for catch-up but undesirable when you want playback to resume at the regular frame cadence after rebuffering.
- Rebasing prevents the accelerated catch-up behavior: after a stall, the next deadline becomes now + frame_interval_ms so frames play at the intended interval.

Code delta (conceptual)
------------------------
- Before:
  const double start_ms = now_ms_mono();

- After:
  double start_ms = now_ms_mono();
  const double frame_interval_ms = 1000.0 / (double)fps;
  ...
  // on stall recovery
  double now = now_ms_mono();
  start_ms = now - ((double)played_frames * frame_interval_ms);

How to test
-----------
1. Start the client with a short MPD and small buffer so stalls occur (or introduce an artificial delay in decrypt/inference).
2. Observe `logger` player events (`stall_start` / `stall_end`) and verify that the timestamps between `consume_frame` events after `stall_end` are approximately equal to `frame_interval_ms`.
3. For profiling, add a short debug print of "lateness" (now - deadline) before/after `sleep_until_deadline_ms` to confirm there is no accelerated burst.

Follow-ups / alternatives
-------------------------
- Optionally use a configurable threshold: rebase only if stall > X ms (so very small stall still allows gentle catch-up). I can add a CLI flag for this.
- If you want to preserve absolute timeline but avoid bursting, another strategy is to limit the maximum negative wait used for catch-up.

If you want, I can either: (A) add a debug log to `player.c` that prints pre/post-deadline offsets; or (B) add a CLI flag to control rebasing threshold. Which would you like?