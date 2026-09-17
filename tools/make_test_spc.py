#!/usr/bin/env python3
"""Generates test/tone.spc: a synthetic SPC whose SPC700 program keys on one
voice playing a looping square-wave BRR sample and steps its pitch every ~0.25s.
Handy for smoke-testing the player without a real game rip."""
import struct, sys, pathlib

out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "test/tone.spc")

ram = bytearray(0x10000)

# --- sample directory at 0x0100 (DIR = 0x01): entry 0 -> BRR at 0x1000 ---
BRR = 0x1000
ram[0x0100:0x0104] = struct.pack("<HH", BRR, BRR)

# --- BRR: two 16-sample blocks of a square wave, shift 11, loop back to start ---
def block(hdr):
    nib = [0x7] * 8 + [0x9] * 8          # +7 / -7
    return bytes([hdr] + [(nib[i] << 4) | nib[i + 1] for i in range(0, 16, 2)])
ram[BRR:BRR + 9]      = block(0xB2)        # shift 11, loop
ram[BRR + 9:BRR + 18] = block(0xB3)        # shift 11, loop + end

# --- SPC700 program at 0x0200 ---
# Tiny assembler: list of (label|bytes|branch) items.
code = []
labels = {}
def emit(*b): code.extend(b)
def label(n): labels[n] = len(code)
def branch(op, target): code.append((op, target))   # resolved below

emit(0x8F, 0x4C, 0xF2)        # mov $F2,#$4C   ; DSP addr = KON
emit(0x8F, 0x01, 0xF3)        # mov $F3,#$01   ; key on voice 0
emit(0xE8, 0x10)              # mov a,#$10     ; PITCHH start
label("loop")
emit(0x8F, 0x03, 0xF2)        # mov $F2,#$03   ; DSP addr = V0 PITCHH
emit(0xC4, 0xF3)              # mov $F3,a
emit(0x8D, 0x00)              # mov y,#0
label("d1")
emit(0xCD, 0x00)              # mov x,#0
label("d2")
emit(0x1D)                    # dec x
branch(0xD0, "d2")            # bne d2
emit(0xDC)                    # dec y
branch(0xD0, "d1")            # bne d1
emit(0xBC)                    # inc a
emit(0x28, 0x1F)              # and a,#$1F
emit(0x08, 0x08)              # or  a,#$08     ; keep pitch >= 0x0800
branch(0x2F, "loop")          # bra loop

prog = bytearray()
for item in code:
    if isinstance(item, tuple):
        op, tgt = item
        rel = labels[tgt] - (len(prog) + 2)
        prog += bytes([op, rel & 0xFF])
    else:
        prog.append(item)
PC = 0x0200
ram[PC:PC + len(prog)] = prog

# --- DSP registers ---
dsp = bytearray(128)
dsp[0x0C] = dsp[0x1C] = 0x7F           # MVOL L/R
dsp[0x6C] = 0x20                        # FLG: echo write off, not muted, not reset
dsp[0x5D] = 0x01                        # DIR = 0x0100
dsp[0x00] = dsp[0x01] = 0x60            # V0 VOL L/R
dsp[0x02], dsp[0x03] = 0x00, 0x10       # V0 pitch 0x1000
dsp[0x04] = 0x00                        # V0 SRCN
dsp[0x05] = 0x00                        # ADSR off
dsp[0x07] = 0x7F                        # GAIN direct max

# --- file ---
hdr = bytearray(0x100)
hdr[0x00:0x21] = b"SNES-SPC700 Sound File Data v0.30"
hdr[0x21:0x23] = b"\x1a\x1a"
hdr[0x23] = 26                           # ID666 present
hdr[0x24] = 30
hdr[0x25:0x27] = struct.pack("<H", PC)
hdr[0x2B] = 0xEF                         # SP
def txt(off, n, s): hdr[off:off + n] = s.encode().ljust(n, b"\0")
txt(0x2E, 32, "Test Tone")
txt(0x4E, 32, "BoomSPC")
txt(0x6E, 16, "synthetic")
txt(0x7E, 32, "square wave, pitch steps")
txt(0x9E, 11, "09/15/2026")
txt(0xA9, 3, "4")
txt(0xAC, 5, "1000")
txt(0xB1, 32, "tools/make_test_spc.py")

out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(bytes(hdr) + bytes(ram) + bytes(dsp) + bytes(64) + bytes(64))
print(f"wrote {out} ({out.stat().st_size} bytes), program {len(prog)} bytes")
