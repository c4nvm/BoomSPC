# Notes and open work

## Open

- **SNES-side engines (Wario's Woods): room to grow.** The song bank has
  no free bytes, so a stream that grows is relocated over another song.
  Fix: append a bank to the image for relocated streams (the header keeps
  a bank byte per track); the editor's one-bank window would need a
  per-track bank.
- **Arrangement editing**: rearranging clips by drag means rewriting the
  jump structure (stream drivers) or the order list (N-SPC). `clips_of()`
  in ui/arrangement.cpp is the input.
- **Grooves**: uneven row lengths (Wario's Woods writes 11-tick beats split
  6+5); the row<->tick mapping is one function today.
- **More drivers**: Rare's Battletoads build, Sunsoft; Capcom song-list
  games (X2/X3, SF2). Fresh RE needed (nothing public): Neverland (Lufia
  1/2), Opus (Nosferatu, Final Stretch...: a nibble-packed stream, editing
  would mean re-encoding), Sculptured Berlioz (Mortal Kombat II, Secret of
  Evermore), Bitmasters SLICK (Earthworm Jim, NBA Jam TE), Wolfteam (Tales
  of Phantasia, Star Ocean), Popful Mail, Elfaria, Energy Breaker, Super
  Tetris 3 (an N-SPC build whose voice pointers are not in the zero page).
  `driver/falcom.cpp` is the smallest stream-driver template.
- Stream drivers on packed rips (Konami, Dragon Quest III) run out of free
  RAM for grown streams; only the other songs' bytes can be reclaimed.
- N-SPC order-list editing, pattern length changes, a "new blank song";
  `make_slide` for engines other than N-SPC; percussion note entry.
- Muted notes (piano roll) are session-only, not saved in `.boomspc`.

## Rare, Donkey Kong Country build (verified on all 29 rips)

- Detection: dispatcher `mov a,[$01]+y / cmp a,#0 / bmi / push x / asl a /
  mov x,a / jmp [!$100F+x]` (command jump table, opcodes 00-30 valid; the
  table runs into code after that), pointer advance `mov a,$4C+x / mov y,$5C+x
  / addw ya,$01 / mov $5C+x,y / mov $4C+x,a` (per-voice stream pointers split
  into low bytes at $4C+v and high bytes at $5C+v; `Position::track_ptr_span`
  covers the 24 bytes for the engine's pointer watch), sequencer accumulator
  `adc $26,$27` and timer latch `mov $FA,$EC`.
- The SNES uploads a song block (header of eight stream pointers at $12A0 in
  every DKC rip) and writes the pointers straight into $4C/$5C, so nothing in
  ARAM references the header: `find_songs` scans for eight words above the
  driver's tables that all parse as streams (silent voices point at a lone
  `00`), skipping the sound-effect pointer table at $2380. Voices 8-15 run
  sound effects on the same engine.
- Stream: `80` rest, `81..FF` note `(n - $80 + transpose) & $7F` into a
  61-word pitch table at $11E1 (index 37 = $1000 = C-4 here); after a note a
  duration byte, or two bytes (high, low) after `2B` (`2C` back to one), or
  none while `06 d` fixed duration is active (`07` clears it). Duration 0
  lasts 1 tick (the countdown wraps straight into the next fetch).
- Commands: `00` end (pointer parks on the byte), `01 i` instrument through
  the sample map at $04E0, `02 l r` volume, `03 lo hi` jump, `04 n lo hi`
  call n times (stack per voice at $DC+v into $0334/$03B4/$0434), `05`
  return, `0B/0C` tempo set/add, `10 a d` ADSR, `11` main volume, `12`
  detune, `13/14` transpose set/add, `15-18` echo/FIR, `19` noise, `1A/1B`
  pitch mod, `1C-20` define volume+ADSR presets 0-4 / `21-25` use them,
  `26/27` vibrato, `28 i l r` instrument + volume, `2A` timer period, `2D`
  jump through a table indexed by $ED (set by `2E` or the SNES), `2F/30`
  pan sweep. Full table with sizes in `rare.cpp`.
- Tick rate: timer 0 at 8000/$FA Hz, every timer tick adds $27 to an 8-bit
  accumulator and the carry runs one sequencer tick, so tps = (8000/$FA) *
  $27/256 (DK Island Swing: 80 * 176/256 = 55). Ticks therefore land on an
  irregular grid (1 or 2 timer periods apart): the playhead clock follows
  the lower quartile of recent sync offsets rather than the minimum.
- Loop = a jump back onto bytes the voice already executed *with the same
  call stack (frames and remaining counts)*: songs also tail-jump into a
  shared subroutine body whose `05` then returns to the caller (Stickerbrush
  Symphony), which is not a loop. Edits are in place only.

## Rare, Donkey Kong Country 2/3 build (verified on all 109 DKC2 rips)

Same engine, reworked by Phillip Wattis; everything above applies except:
- Dispatcher `mov a,[$00]+y / bmi / push x / asl a / mov x,a / jmp
  [!$0FA5+x]` (no `cmp a,#0`), pointers at $44+v / $54+v through zero page
  $00, accumulator `adc $1E,$1F`, timer latch `mov $FA,$E4`, pitch table at
  $1199 extended three octaves down; the note routine adds `#$24` before the
  transpose so a note byte plays the same pitch as in DKC1.
- Songs come from a table: `mov a,!$055D / asl a / mov y,a / mov a,!$1312+y`
  copies the numbered header's pointers into the zero page. The table runs
  up to the first header above it (headers may also sit below it: $1300 is
  song 0). Live pointers still pick the current song: jingles start through
  other paths and leave $055D alone.
- Commands renumbered: `11`, `25`, `28-2A`, `2D-2F` are unused (empty
  slots), `1C n` / `1D n` set note variables that note bytes `E0` / `E1`
  play, `1E` global volume pairs and `20` / `31` use them, `1F` echo delay,
  `21 lo hi` call once (3 bytes; the return adds 4 to a stored pointer-1),
  `22` instrument + transpose + detune + volume + ADSR (8 bytes), `23` mono
  volume, `24` master volume percent, `30` / `32` echo off. `2D` (jump by
  variant) is gone. Table with sizes in `rare.cpp` (`kCmdsDkc2`).
- DKC3 shares the DKC2 build per VGMPF but has not been run here.
  Battletoads is a separate build.

## Rare, Killer Instinct build (verified on all 41 rips)

The DKC2 engine (same dispatcher, pointers at $45+v / $55+v, note offset
`#$24`, note variables at $0E / $16) with its own tables:
- Commands `0C`, `0D`, `11`, `15`, `18-1D`, `24`, `25`, `28-2A`, `2D-2F`
  are empty slots; `1E n` mono volume, `1F lo hi` call once (the handler
  is the DKC2 `21` one: `mov $04,#1 / call push-frame`, which is how the
  build is told apart), `20` / `21` ADSR presets `8F E0` / `8E E0`, `22 i
  t d` instrument + transpose + detune (4 bytes), `23 d` echo delay. `30`
  points into the middle of the transpose handler and is treated as unused.
  Table in `rare.cpp` (`kCmdsKi`).
- The call stack lives at $0354 (low) / $03D4 (high) + v*8 + depth,
  fingerprinted from the return handler (`mov a,!hi+y / mov $55+x,a / mov
  a,!lo+y / mov $45+x,a`).
- The song block is uploaded at $1030: an 18-byte header (eight stream
  pointers, tempo, $23) followed by the streams, and a table at $1042
  indexed by $055C names up to four headers per stage (main, "danger",
  "fight over"). The table ends where the first stream begins, so
  `find_songs` bounds it by the lowest stream pointer seen.

## Capcom (verified on all 35 Mega Man X rips)

Reverse-engineered from the Mega Man X rips and cross-checked with
loveemu's capspc (the command set matches its event list). Format summary
in `capcom.hpp`; the points that were not obvious:
- The song header is eight big-endian words in reverse voice order (word 0
  = voice 7). Mega Man X keeps it at a fixed address ($0DB0, from `mov
  $A1,#$0D / mov $A0,#$AF` in the port handler, plus one for the priority
  byte); the song-list games take `list[n]` + 1 instead.
- Live pointers are split arrays: low bytes at $00+v, high at $08+v (the
  fetch routine `mov y,$08+x / mov a,$00+x / movw $A0,ya`). A voice that
  ends zeroes both.
- Note byte: `(dur_index + 1) << 5 | key`; key 0 is a rest. Durations
  come from three 7-byte tables (normal 3..192, dotted, triplet) picked by
  the control bits at $10+v: `10` dotted (one shot, cleared by the next
  note or rest), `20` triplet, `40` slur (the note keeps sounding and the
  next one does not key on), bits 0-3 index a 16-byte octave table (`00
  0C .. 54 | 18 .. 6C`, bit 3 = two octaves up). The pitch is key - 1 +
  octave + global transpose ($D1, command `0A`) + voice transpose (`0B`);
  the tracker names note number n as semitone n - 1.
- Loops are four counters per voice ($30/$38/$40/$48 + v), no return
  addresses: `0E..11 n hh ll` jumps to `hh ll` and sets the counter on the
  first pass, later passes count down (the target plays n + 1 times, 0 =
  forever). `12..15 f hh ll` breaks out on the last pass (counter == 1)
  and ORs `f` into the control bits. Because nothing is saved by address,
  a rewritten stream can be resumed from inside a loop.
- Voices share bytes freely: Title Screen's voice 0 forward-jumps into
  voice 7's stream, so only a jump to bytes this voice already ran counts
  as its loop.
- Timing: the main loop alternates a DSP-update frame and a sequencer
  frame on timer 0 (latch $40 = 125 Hz), and each sequencer frame
  subtracts the 16-bit tempo from a note counter whose high byte holds the
  remaining ticks: (8000 / latch) / 2 * tempo / 256 ticks per second, 48
  per quarter (tempo $01EB in Boomer Kuwanger = 149.8 BPM, as capspc says).
- Instruments: 6 bytes at the table found from `mov y,#6 / mul ya / adc
  $A0,#lo / adc $A1,#hi` ($47AC): SRCN, ADSR1, ADSR2, GAIN, 8.8 pitch
  multiplier. Pitch = (table[(n-1) % 12] << 1 >> (8 - (n-1) / 12)) *
  mult / 256 with a 13-word octave table at $0D6B.
- Editing: a duration the tables cannot express becomes tied pieces (slur
  on before every piece but the last, restored after), chosen by a small
  DP over the three tables. The unused `$FF` fill behind the song is where
  grown streams go.

## Square AKAO, Super Mario RPG build (verified on all 106 rips)

- Detection: voice loop `mov a,[$29+x] / incw $29 / cmp a,#$C4 / bcc` at
  $0B9B; dispatcher `setc / sbc a,#$C4 / push a / mov x,a / mov a,!$17B9+x /
  and a,#$07` (low 3 bits of the length table = opcode + args; the high
  nibble flags the control commands), jump table `jmp [!$173F+x]` with
  X = index*2. The driver runs 24 logical voices: a song takes slots 0-7
  or 8-15 (`$1D` = 0 or 8, read at song start by `mov a,$1D / asl a / mov
  x,a / mov a,!$2000+y / mov !$1BFC+x,a`), sound effects the rest. Live
  pointers are `$1BFC + (slot+voice)*2`, octave `$183C + slot + voice`; the
  other set holds stale pointers from the previous song, which is why 15 rips
  (smr-110, 117, 119, 207, 208, 218, 219, b04-b09, v0x) did not follow
  before the slot was read. Rips of jingles played through the sound-effect
  path (smr-s06..s29 "Wishing Star" etc., v01-v03 "Yoshi") run on slots
  16-23 from data around $3800; those are not sequenced yet.
- Notes `< $C4`: key = byte % 14 (12 pitch classes, 12 = tie, 13 = rest),
  length index = byte / 14 into `$17F5` = `C0 90 60 48 30 24 20 18 10 0C 08
  06 03` (48 per quarter); index 13 = explicit length byte follows. This is
  the reverse of the other AKAO revisions (key = byte / 14 there). The
  note handler at $0DC4 does `div ya,x` with X = 14.
- Control: `C4/C5` octave up/down, `C6 n` octave, `D4 n .. D5` repeat n
  passes (count-1 kept at `$1C74`, return address `$1D04`, octave saved and
  restored), `D6` leaves the repeat on its last pass (jumps to the address
  saved after `D5`), `D7` sets the loop mark (`$1C2C`), `D0` jumps to the
  mark or ends the voice, `FE` repeat end using a CPU-shared count (`$58`),
  `CD/CE n` jump through the 4-byte sequence table at `$3000` (n*4, +2).
  `D1 n` writes timer 0 (`$FA` = n + `$60`): one timer period = one tick, so
  ticks/s = 8000 / latch (93 at $56). `DE n` instrument: n is a global id,
  `$4680[n]` maps to a slot, `$4701 + 2*slot` = SRCN, `$4740 + 2*slot` = ADSR,
  `$4780 + 2*slot` = tuning. Unused ids hold $FF.
- Song header at `$2000`: 5-byte instrument records (id + 4 bytes) until a
  byte >= $80, then eight absolute track pointers. Voices loop
  independently; the parser records each voice's loop event.
- Other names in `akao.cpp` (volume, pan, slides, vibrato, echo, gate, ...)
  come from reading the handlers and are best effort.
- The rev.1-4 code paths follow loveemu's akaospc (MIT,
  github.com/loveemu/spc_converters_legacy) for fingerprints, the
  per-revision opcode maps, ROM-address relocation in the header, and the
  loop/conditional-jump semantics. They compile and are wired in, but no
  rip of those games was available to test.

## The Software Creations (Follin) driver, as reverse-engineered from Plok

- Detection: the byte fetch is `mov a,[$30+x] / inc $30+x / bne / inc $31+x /
  or a,#0 / ret` ($0785 in Plok, $0D2F in Equinox); the dispatcher `cmp a,#$xx
  / bcs / asl a / mov y,a / mov a,!hi+y / push a / mov a,!lo+y / push a / ret`
  can live elsewhere, and its compare gives the first end opcode ($BA in
  Plok, $B8 in Equinox: Plok added B8/B9). Everything else (handlers,
  argument counts, pitch table values) is identical between the builds.
  X = voice * 2; `$80+X` counts ticks down and a byte is fetched when it hits
  zero. Commands `$80..$B9` jump through a table at $0B6F (index =
  `(op << 1) & $FF`), `$BA+` and `$80` end the voice.
- Song slots: `$1380 + v*10 + slot` (lo) / `$1385 + v*10 + slot` (hi), 5
  slots, `$E4` = current slot. Slot 1 is the music in every Plok rip; 2..4
  are jingles/effects, 0 is silence. The tables are identical across the
  set; the music data lives at $1400+.
- Stream: note `< $80` (0 = rest) then a duration byte, unless `86 n` set a
  default (then only the note; `87` = read one explicit duration next).
  `81 lo hi` jump, `82 lo hi` call, `83` return, `84 n .. 85` repeat (loop
  counters live on the same per-voice stack at `$0380`, 8 entries per voice,
  index in `$50+X`), `A3 n` / `A4 n` = jump / call to a random entry of the
  n pointers that follow (LFSR in `$EB`, `$10B9`): this is how the Follins
  randomise variations. `A5/A6 f` set/clear flag, `A7/A8 f lo hi` jump if
  set/clear, `A9 f` wait (re-executes each tick).
- The eight voices must be parsed in lockstep, one tick at a time in voice
  order, because `A5/A6` set/clear global flags and `A9 f` stalls a voice
  (one tick per re-execution) until another voice sets the flag; `A7/A8`
  branch on them. Equinox synchronises its voices this way throughout.
- A duration byte of `00` lasts 256 ticks: `dec $80+x` wraps. Long rests are
  written `00 00`.
- A backward `81` jump is the voice's loop only when its target was already
  executed by that voice; shared blocks frequently sit at lower addresses
  than the streams that jump into them.
- Voice state written to the DSP each tick from zero page: VOL L/R `$00/$01+X`,
  pitch `$10/$11+X`, SRCN `$20+X`, GAIN `$21+X`. No ADSR (ADSR1 stays 0). So
  `89 n` = instrument = sample number, and envelopes are `8C a b c` (GAIN
  computed at $0FBE), `97 n` presets (7 bytes each at $13D8), `A2` inline.
- Pitch: `$1200+n` / `$1255+n` (lo/hi), 85 entries, 12 per octave, entry
  $3E = $1000 (unity). pitch = table[note + transpose[srcn]] * (1 +
  tune[srcn]/256); transpose at $38B8, tune at $38E5 (45 instruments).
- Tempo: `B6 n` writes timer 2 (`$FC`). Ticks per second measured as
  16000 / n (80 at the usual $C8).
- Pitch: `8E/8F delay rate n` is a sweep restarted by every key-on (after
  `delay` ticks the pitch offset moves `rate` DSP units per tick; with
  n > 0 it reverses every n ticks, first leg n/2, a triangle wobble; n = 0
  runs to the note's end) until `91`. `90 n` is the glide speed: the
  sounding pitch moves n units per tick toward the note's pitch every
  tick, so a legato note (`9F` mode) slides in from the previous one; 0
  snaps. Plok's slides are `note/1  90 n  9F  target/dur  90 00  9E`, and
  that is what the piano roll writes for a slide.
- Other commands (argument counts in `follin.cpp`): 88 transpose, 8A/8B
  volume L/R, 92/93 note cut timing,
  94/95 portamento, 96 vibrato, 98/99 echo, 9A noise clock, 9B/9C/9D
  per-note restart modes, 9E/9F key-on / legato, A0 skip transpose once,
  AA/AB noise, AC-AF echo volume/feedback/FIR, B0 pan sweep, B1/B2 pitch
  mod, B3 volume+pan, B5 sound effect, B7 silence, B8 extended, B9 stack check.

## The stream drivers (2026-09)

`src/driver/stream.*` is a table-driven base for one-program-per-voice
drivers; each format file supplies `decode`, `spec`, `track_start`,
`song_headers` and the live pointer layout, everything else (parsing with
call/repeat frames, song scanning, position, retime, editing, relocation)
is shared. Every driver's header comment holds the format notes; all were
verified with `parsecheck` on complete Zophar rip sets and the live edit
test. Konami v1-v6, Hudson v0-v2, Chunsoft (winter/summer), Square (all
revisions incl. Suzuki's SD3/Bahamut build), Mint, Compile, Pandora Box,
Prism Kikaku, Graphic Research, ASCII (Ardy Lightfoot build), Falcom (Ys
V), Heartbeat (DQ3/6). Pitfalls worth remembering:

- A driver's pointer copy can start mid-stream (Konami): the program loops
  by running into bytes it already played; the parser synthesises a jump
  event so relocation keeps the loop.
- Relocated jumps are re-found by address, then (edited targets) by tick;
  a target inside a called section that still exists keeps its address.
- Optional length bytes distinguished by value (ASCII) must not be seen
  when an event is re-decoded from its own bytes: unused bytes hold $80.
- Repeat counters may live inside the stream (Falcom): a relocated copy
  carries the live count with it, so playback can move to it.
- A voice pinned to a fixed address (Graphic Research voice 0) is moved
  with a jump trampoline written after the old bytes are released.
- Echo buffers grow when a song sets a larger EDL than the DSP holds at
  snapshot time (`echo_length`); free space above ESA is not free.
- N-SPC licensee builds: command base and length table come from the
  driver's dispatch code (VGMTrans-style signatures), Konami and Falcom Ys
  IV store song pointers relative to a base, Intelligent Systems builds
  have multi-byte note parameters and table-sized commands (FA/FC/F9),
  Quintet's FF takes three arguments.

## What was learned about N-SPC (worth keeping)

- Tracks are packed back to back **without terminators** in most game data.
  A pattern ends when the *first* voice reads `00`; the others simply stop
  wherever they are. So pattern length = min over tracks, and a parsed track
  usually runs into the next track's bytes before it finds a `00`.
- Vanilla SMW instruments are **5 bytes** (srcn, adsr, adsr, gain, pitch hi);
  EarthBound and AddmusicK use 6 (with a pitch fraction). SMW keeps a
  separate 6-byte percussion table (`$5FA5`) whose 6th byte is the note.
- The SMW command-length table lives at `$0FC2` and stores total sizes;
  EarthBound's at `$0C21` stores argument counts. AddmusicK's is at `$13DE`
  and changes several vanilla entries, so it must be read from RAM.
- AddmusicK keeps the global instrument table at `$1844` and per-song custom
  instruments (numbers 30+) right before the song data. Its dumps start with
  an empty zero page; live pointers only exist once the driver runs.
- Live pointers: `$30-$3F` track pointers, `$40` next order-list entry, in
  every build seen so far. The code still scans for them structurally.
- ARAM in a game rip is full. Growing anything means reclaiming the other
  songs in the bank (or the echo buffer).
- A rip can be taken with the next song already queued in the I/O ports
  (Zelda "17a The Silly Pink Rabbit!" starts on song @D86A and switches to
  @DCA7 within a second), so the tracker re-picks the current song twice a
  second from the live pointers unless the user chose one in the song combo.
- Song scanning skips the sample directory, each BRR sample's blocks and the
  echo buffer by exact byte range, not "everything above DIR": Zelda ALttP
  keeps a second song bank at $D000-$FFFF behind samples ending at $BA61
  (the Dark World set), and the playing song was invisible until then.
- The pitch table (13 words, $085F..$10BE) is the same in every build seen;
  `pitch = table[n % 12] >> (5 - n / 12)`, times the instrument's 8.8
  multiplier. `note_pitch()` looks the table up in ARAM anyway.
- Length bytes are sticky per voice; giving one note its own `len [qv]`
  means restoring the previous state right after it (`ensure_own_length`).
  A qv that was never set in a track cannot be "restored", so the first
  velocity edit in such a track propagates to the following notes, visibly.
