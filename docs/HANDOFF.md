# Handoff, 2026-09-20

Where things stand after the 2026-09-20 session. Durable findings belong
in ROADMAP.md; this file is the state of play and the next moves.

## Done this session (six commits, in order)

1. **N-SPC: a length byte after a length group is a note.** Swept: every
   N-SPC rip in `~/Downloads` (1500 files, 260 000 tracks) parses and
   re-serializes to the same bytes. 7500 tracks contain such a byte; all
   of them are garbage parses of unused patterns (a track that starts with
   a note and no length), none in a song that plays.
2. **The pinned song survives a rescan.** `Tracker::pin_song` re-selects
   the song by header address after `analyze`. The rescan path only runs
   when `locate` fails at load (rips dumped between patterns, or a
   project whose song is not the one the rip boots), which is why the
   SMW headless test never reached it; a harness that forces
   `rescans_left = 1, pos.valid = false` confirmed the song is kept.
3. **Undo/redo drop the pending releases** (a queued release would zero
   the track undo just brought back).
4. **Block edits are all or nothing.** `begin_block`/`end_block` in the
   sequencer wrap every block lambda; the first failing voice reverts the
   block through `Engine::revert_edit` (undo without a redo entry) and the
   status names the voice. A nested `begin_edit` no longer splits the undo
   step (`engine.cpp:536`). Verified headless with a temporary env hook
   that failed voice 3: the grid was pixel-identical before and after.
5. **A reclaim sweeps the rest of the song it overwrote.** The victim's
   bytes that nothing parses any more are zeroed, so they come back as
   free RAM instead of leaking. Truncated tracks are left out of the
   victim map.
6. **A track grows in place when the bytes after it are free.** Before,
   every growing rewrite hopped to the start of the largest free run: the
   total stayed right (fa643a2 releases the old bytes) but the largest run
   shrank by the whole track each edit. Now a run of edits on one voice
   costs only the bytes it adds. The byte after a terminator was reserved
   by mistake (`mark(t.end_addr)` predates `end_addr` being exclusive).

## What the storage wall actually was

calb's `12 Snowy Land.boomspc` (Smart Ball, EarthBound type) on 0.5.2:
the rip had no zero bytes at all, so every growing edit reclaimed, and
each reclaim destroyed a song whose remainder leaked (2185 bytes dead in
the saved project). Since c22c24c the same rip shows 10.9 KB free (the
$FF fill at $8600-$AFFF), so on a current build Smart Ball never needs
Reclaim. The leaked bytes in the existing project stay leaked: "dead"
bytes above the songs cannot be handed out in general (N-SPC rips carry
5-40 KB of non-song data there: SFX sequences, instrument tables), so
there is no safe salvage pass. calb should start that song again from
the rip.

"Deleting a note doesn't seem to do anything" is still unexplained; it
may be the ties-cannot-be-deleted item in ROADMAP. Needs a repro.

## Still open from the 2026-09-19 reports

- **Mute at row 14 in "12 Snowy Land"**: not reproduced. Project in
  `~/Downloads`.
- **macOS 2x offset**: `main.cpp:98` sets `SDL_WINDOW_ALLOW_HIGHDPI` with
  the SDL_Renderer backend, so on Retina the output is twice the window
  and the mouse stays in points. Drop the flag as Windows does, or set
  the render scale. No Mac here; not a goal.
- Ask calb to update before triaging any further crash log (the
  2026-09-19 one was the 0.5.1 seek use-after-free, fixed in 52b76bd).
- Tell calb: free ARAM readout next to Reclaim (533f6a3); reclaim never
  touches a song sharing bytes with the current one (298eaff); the
  channel-goes-dead report is gone; and why one .spc holds several songs
  (the rip is the whole 64 KB of audio RAM, most drivers keep the song
  bank resident).

## Scratchpad harnesses (rebuild with `mk.sh name`, see the memory notes)

`nspcrt` (N-SPC round-trip sweep), `growtest file [song]` (repeated
note entry on one voice with reclaim on, printing free stats and the
song list with `SONGS=1`), `projinfo file.boomspc|.spc [x]` (songs,
free stats, a 64-byte-per-cell map of ARAM by class, leaked ranges),
`deadcount` (non-song bytes above the songs per rip), `pintest`,
`dumpv file song pat voice`. `BOOMSPC_DEBUG_FREE=1` traces releases,
sweeps and the free-run search.

## Suggested order next session

1. Version 0.5.3 with these six plus fa643a2, d59d18f, c22c24c, 533f6a3.
2. Repro for "deleting a note does nothing" and the row-14 mute.
3. Placement policy: a relocated track goes to the start of the largest
   run, so the second voice edited lands right after the first and takes
   its growing room. Best fit for tracks that are unlikely to grow, or a
   small slack after each relocated track, is the next step if testers
   still hit relocation churn.
