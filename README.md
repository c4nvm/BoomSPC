<p align="center"><img src="assets/logo.png" width="160" alt="BoomSPC"></p>

# BoomSPC

spc700 player and tracker. loads .spc rips, plays them with blargg's snes_spc
core, and if it recognizes the game's sound driver it shows the song as a
tracker / piano roll / arrangement you can actually edit and save back out as
.spc, .wav or a .boomspc project. it also plays snsf sets (games where the
sequencer runs on the snes cpu instead of the spc) with a built in 65816.

drivers it knows right now:

- nintendo n-spc (smw, zelda, earthbound, kirby, addmusick stuff) plus the
  licensee builds: intelligent systems (fire emblem, tetris attack), human
  (clock tower), tose, falcom (ys iv), lemmings, quintet (terranigma),
  konami (castlevania iv)
- software creations / follin (plok, equinox)
- square akao (super mario rpg, ff4/5/6, chrono trigger, secret of mana,
  romancing saga, live a live, front mission, seiken densetsu 3, bahamut lagoon)
- rare (dkc 1/2/3, killer instinct)
- capcom (mega man x)
- konami (contra iii, axelay, dracula x, goemon 2, sparkster, parodius, batman returns)
- hudson (super bomberman 2-5, tengai makyou zero)
- chunsoft (dragon quest v, torneko, kamaitachi, otogirisou)
- heartbeat (dragon quest iii and vi)
- mint (shien's revenge, combatribes), compile (kirby's avalanche),
  pandora box (arabian nights), prism kikaku (dual orb ii), graphic
  research (derby jockey 2), ascii (ardy lightfoot), falcom (ys v)
- warios woods (snes side engine, needs the .minisnsf + .snsflib)

if the driver isnt recognized it still plays, you just dont get the editor.

## building

you need cmake 3.24+, a c++20 compiler, sdl2 and zlib. imgui gets pulled in
at configure time so dont worry about that one.

### linux

```sh
# fedora
sudo dnf install cmake ninja-build gcc-c++ SDL2-devel zlib-devel
# debian / ubuntu
sudo apt install cmake ninja-build g++ libsdl2-dev zlib1g-dev

cmake -S . -B build -G Ninja
cmake --build build
./build/boomspc song.spc      # or a .boomspc / .minisnsf
```

the file picker uses zenity or kdialog, whichever your desktop has. if you
have neither just type the path in the player panel, it works the same.

### windows

two ways.

**cross compile from linux with mingw** (this is how i build the zips):

```sh
# grab the official SDL2 mingw dev package from github and a mingw zlib
# (build zlib with -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc)
cmake -S . -B winbuild -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake \
  -DCMAKE_PREFIX_PATH="/path/to/SDL2-2.30.11/x86_64-w64-mingw32;/path/to/zlib-mingw" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build winbuild
```

the c++ runtime is linked statically so the exe only needs `SDL2.dll` and
`libzlib.dll` next to it (plus the `assets` folder). thats the whole
distribution.

**native with visual studio**: install `sdl2` and `zlib` through vcpkg, then
`cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`
and open the solution or `cmake --build build --config Release`.

the tools and the headless ui were tested under wine. the file dialog on
windows is the normal win32 one and hasnt been clicked on real windows yet,
so if it does something dumb tell me.

macos should be fine too (brew sdl2, system zlib) but i havent tried.

## using it

- `enter` play/stop, `ctrl+r` restart, `shift+enter` play from the cursor.
  you can also click a row number or the ruler in the piano roll to start
  from there. `f1`-`f8` mute voices.
- **tracker**: `space` toggles edit mode. piano keys write notes, hex digits
  do the other fields. right click a cell for the furnace style block menu.
  `ctrl+\` folds a voice down to just note + effect.
- **piano roll**: drag on empty space to place a note, drag a note to move
  it, grab the right edge to resize. double click a note to get its editor
  (pitch, length, instrument, on/off, and every effect sitting on it).
  shift click deletes. `s`+drag makes a pitch slide, `i`+click grabs the
  notes instrument. ctrl+wheel zooms time, ctrl+shift+wheel zooms pitch.
  effects on a note show up as little colored tabs at the end of it.
- **arrangement**: shows how the song jumps around (calls, repeats, loops)
  as clips per voice. click one to select it, double click to play from it.
- **fit** on the toolbar reads the grid and the beat off the song. it runs
  on load anyway. the effects panel lists every command the driver has, the
  event panel edits whatever is under the cursor.
- every edit goes straight into the emulated ram so you hear it right away,
  and you can undo. editing inside a repeated / called section writes that
  pass out as plain data. if a stream grows it gets moved to free ram, and
  game rips basically never have any, so tick **reclaim** to let it overwrite
  the other songs in the bank.
- settings has themes, fonts (theres a letter spacing slider if the pixel
  font is too wide for you), key bindings, follow mode. saved to
  `boomspc_theme.ini` / `boomspc_keys.ini`.

## tools

- `spc2wav in.spc out.wav [seconds]`
- `spcdump in.spc [--run S] [--tracks] [--disasm ADDR[:N]] [--dumpram FILE]`
  dumps what the parser sees. this is what i use when reverse engineering a
  driver.
- `parsecheck song.spc [seconds]` plays the rip and checks every note the
  driver fetches against the parse (n-spc order lists included). if this
  passes the driver is right.
- `edittest` runs the edit tests. set `DKC_SPC=`, `KI_SPC=`, `PLOK_SPC=`,
  `SMRPG_SPC=`, `MMX_SPC=`, `WW_SNSF=`, `KONAMI_SPC=`, `KONAMI1_SPC=`,
  `HUDSON_SPC=`, `CHUN_SPC=`, `MINT_SPC=`, `COMPILE_SPC=`, `PANDORA_SPC=`,
  `PRISM_SPC=`, `GRAPHRES_SPC=`, `ASCII_SPC=`, `FALCOM_SPC=`,
  `HEARTBEAT_SPC=` to a rip to also test live edits.
- `ptrfind song.spc seconds lo hi [min]` lists RAM words that walk through
  an address range while the rip plays (finding a driver's live pointers).
- headless ui for scripting / screenshots:
  `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/boomspc song.spc --size 1400x900 --script "wait 60; click 600 256; key Space; run WIN_ABOUT; shot a.bmp"`
  (`move`, `click`, `rclick`, `mdown`/`mup`, `wheel X Y`, `hold`/`release`,
  `text`, `run ACTION`). `--record out.wav` saves the audio.

## layout

```
src/engine.*                   snes_spc behind sdl audio, ram edits with undo, export, snsf mode
src/snes.* cpu65816.* snsf.*   the snes side for snsf sets
src/driver/seq.*               the song model and the driver interface
src/driver/<engine>.*          one file per sound driver
src/driver/stream_edit.cpp     shared editing for the one-stream-per-voice drivers
src/tracker.*                  edit ops, relocation, free space search
src/ui/                        one file per panel. actions.* is hotkeys, theme.* is settings
tools/                         spc2wav, spcdump, parsecheck, edittest
docs/ROADMAP.md                driver format notes and stuff that isnt done yet
```

## credits / license

mit. the parts that arent mine:

- `third_party/snes_spc` by shay green (blargg), lgpl 2.1, linked statically
- dear imgui (omar cornut, mit), sdl2 (zlib license), zlib (gailly / adler)
- stb_image (sean barrett, public domain)
- the spc700 opcode table comes from the snesdev wiki (cc0). the 64 byte
  spc700 ipl boot rom that the snsf player needs is sony's
- bundled pixel fonts in `assets/fonts`: mega man x and quake pc by patrick
  h. lauke (cc by 3.0), the plok fonts by paradigmthegreat (cc by-sa 3.0),
  all from fontstruct. licenses are in that folder
- the akao and capcom drivers were checked against loveemu's akaospc and
  capspc (mit). the n-spc stuff leans on the smw / addmusick / earthbound
  community disassemblies
- editing model and colors are inspired by furnace and openmpt, the piano
  roll gestures by famistudio. no code from any of them
- the plok / equinox, rare, capcom and warios woods drivers were reverse
  engineered from the rips for this project
