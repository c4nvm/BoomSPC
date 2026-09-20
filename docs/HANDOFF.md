# Handoff, 2026-09-20

Where things stand at the end of the 2026-09-19 session. Durable findings
belong in ROADMAP.md; this file is the state of play and the next moves.

## Uncommitted in the tree

`src/driver/nspc.cpp` and `nspc.hpp`, 30 lines, N-SPC length groups:

- Parser: a byte below $80 right after a length group is no longer read as
  a second length. The driver reads one length, one optional qv, then a
  note or a command whatever the byte is, so the second group plays as a
  note of pitch (b - $80), which is garbage. It is now decoded as that
  note (`after_length` in `Parser`).
- `serialize_track`: two length groups in a row collapse to one (last
  length, last qv seen), so an edit can never write the pair the parser
  now refuses.
- `note_semitone` returns -1 instead of 11 for a byte that is not a note.

State: built into `build/boomspc` (the binary is current), `build/edittest`
passes all of it. Not committed, not swept over the rips. `parsecheck` does
not model N-SPC order lists, so it says nothing about this change; the
check worth running is a parse and re-serialize of every N-SPC rip to
confirm no untouched track changes bytes. Suggested message:

    N-SPC: a length byte after a length group is a note, not a length

`~/Downloads/12 Snowy Land (repaired).boomspc` and
`indo 2 (repaired).boomspc` are hand-fixed copies of the two projects that
hit this; the originals next to them are the repro material.

## Tester reports, Discord 2026-09-19 (calb)

Ordered by how much they hurt, with what the code says.

1. **The storage wall is the whole session's complaint.** Deleting a note,
   shortening a note, pull delete and paste all end in "no free RAM was
   found, even in the other songs' data" (`tracker.cpp:362`, `:471`,
   `:481`). Two things feed it: every edit re-serializes the whole track
   and asks `find_space_or_reclaim` (`tracker.cpp:275`) for a fresh run,
   and an N-SPC note-length change inserts a rest, so shortening a note
   grows the stream. Worth checking first: whether a rewrite that fits
   inside the track's own old span is written in place rather than
   relocated, and whether the bytes released by the previous rewrite
   (fa643a2) are in the free map the very next edit builds
   (`tracker.cpp:137-200`). "Deleting notes doesn't seem to do anything"
   points at one of those two.
2. **Restart jumps to another song and cannot be undone.** `Engine::restart`
   reloads the image, which boots the song that was playing at rip time. If
   the user is editing a different song, `locate` fails, `update` falls
   into the rescan path (`tracker.cpp:60-64`) and `analyze` clears
   `song_pinned` (`tracker.cpp:38`) and re-picks. `reparse` already knows
   how to keep the song by `order_addr` (`tracker.cpp:66-78`); the rescan
   should do the same instead of unpinning. The "other songs just seek
   endlessly" report is the same thing seen from the other side: a seek
   into a song the driver is not playing never arrives.
3. **Paste applies to some voices and not others.** `paste_block`
   (`sequencer_panel.cpp:983`) commits voice by voice and throws away
   `commit`'s return (`:834`), so a voice that runs out of RAM is skipped
   silently while the voices after it are still written. One undo step
   covers it, but the pattern is left half pasted, which is the "cutting
   and pasting seems to leave some of the channel behind" report and the
   large-selection one. Either check the whole block for space before
   writing anything, or stop at the first failure and say which voice.
4. **Mute at row 14 in "12 Snowy Land"**. Not reproduced yet. The project
   file is in `~/Downloads`. The crash calb posted from the same afternoon
   is not this one and is already closed: it is the Inindo `indo 2.boomspc`
   access violation at 2026-09-19 18:35 UTC on build 0.5.1 (commit 18c1cd8,
   the pre-rebase copy of 4d0cfee, committed 14:16 local), action
   PLAY_CURSOR, status "seeking to order 0 row 22". 52b76bd fixed exactly
   that 25 minutes later and shipped in 0.5.2: the seek lambda held a raw
   pointer to the tracker's driver and a rescan on the UI thread replaced
   it under the audio thread. The faulting address in the log
   (`0x3a34206563697040`) is printable ASCII, so the read had already gone
   through storage recycled into text, which is the use-after-free
   signature. Ask calb to update before triaging any further log; this
   build also predates the module+RVA frames (9299069), which is why its
   stack cannot be resolved.
5. **macOS: the UI is offset and gets worse away from the top left.** That
   is a 2x pointer/render mismatch: `main.cpp:98` sets
   `SDL_WINDOW_ALLOW_HIGHDPI` on everything but Windows while the backend
   is `SDL_Renderer`, so on Retina the renderer output is twice the window
   size and mouse events stay in points. Dropping the flag the way the
   Windows path does, or setting the render scale, is the experiment.
   No Mac here to test on, and macOS is not a goal, so this stays a note.

## Already shipped, tell calb

- The free ARAM readout sits next to Reclaim in the sequencer panel
  (533f6a3), which answers "is there a display to show how much memory is
  left".
- Reclaim no longer touches a song that shares bytes with the current one
  (298eaff), and rewrites give their unused bytes back (fa643a2), so the
  channel-goes-dead report from before 0.5.2 should be gone.
- Still open and asked twice: why one .spc holds several songs. ROADMAP
  already has the tooltip item; a sentence in the Discord thread costs
  nothing in the meantime. The last question ("could I start over from a
  fresh spc?") needs whatever was in the screenshot, so it needs asking.

## Suggested order next session

1. Commit the N-SPC change after the rip sweep.
2. Keep the song pinned across a rescan (small, and it takes away the
   worst surprise).
3. Make paste all-or-nothing.
4. Then the storage wall, which is the real work.
