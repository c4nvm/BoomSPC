# Local changes to snes_spc 0.9.0

Source: https://github.com/blarggs-audio-libraries/snes_spc (pristine mirror of
blargg's snes_spc-0.9.0). License: LGPL 2.1, see LICENSE.txt.

Only the accurate DSP is vendored (`fast_dsp/` and the C wrappers `spc.*` /
`dsp.*` are omitted).

## SNES_SPC.h

Added read-only inspection accessors in the second `public:` block so the UI
can display live DSP registers, ARAM and SPC700 registers without touching the
emulation:

- `SPC_DSP const& dsp_ref() const`
- `uint8_t const* ram() const` / `uint8_t* ram_mut()`
- `cpu_pc() / cpu_a() / cpu_x() / cpu_y() / cpu_sp() / cpu_psw()`

For note previews (host-side key-on, no driver involvement):

- `SPC_DSP& dsp_mut()`
- `void run_dsp_only(int count, sample_t* out)` in SNES_SPC_misc.cpp: clocks
  the DSP alone for `count` samples so a preview voice sounds while the CPU
  (and the sound driver) stays frozen.

Everything else is untouched. When updating the library, re-apply this block.
