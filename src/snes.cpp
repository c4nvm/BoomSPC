#include "snes.hpp"

#include <algorithm>
#include <cstring>

namespace {
const uint8_t kIplRom[64] = {
    0xCD, 0xEF, 0xBD, 0xE8, 0x00, 0xC6, 0x1D, 0xD0, 0xFC, 0x8F, 0xAA, 0xF4, 0x8F, 0xBB, 0xF5, 0x78,
    0xCC, 0xF4, 0xD0, 0xFB, 0x2F, 0x19, 0xEB, 0xF4, 0xD0, 0xFC, 0x7E, 0xF4, 0xD0, 0x0B, 0xE4, 0xF5,
    0xCB, 0xF4, 0xD7, 0x00, 0xFC, 0xD0, 0xF3, 0xAB, 0x01, 0x10, 0xEF, 0x7E, 0xF4, 0x10, 0xEB, 0xBA,
    0xF6, 0xDA, 0x00, 0xBA, 0xF4, 0xC4, 0xF4, 0xDD, 0x5D, 0xD0, 0xDB, 0x1F, 0x00, 0x00, 0xC0, 0xFF,
};

int header_score(const std::vector<uint8_t>& rom, size_t at, bool want_hirom) {
    if (at + 0x20 > rom.size()) return -1;
    int s = 0;
    const uint8_t map = rom[at + 0x15];
    if ((map & 0x0F) == (want_hirom ? 1 : 0)) s += 2;
    if ((map & 0xE0) == 0x20) s += 1;
    const uint16_t sum = uint16_t(rom[at + 0x1E] | rom[at + 0x1F] << 8), comp = uint16_t(rom[at + 0x1C] | rom[at + 0x1D] << 8);
    if ((sum ^ comp) == 0xFFFF) s += 4;
    if (rom[at + 0x17] >= 5 && rom[at + 0x17] <= 0x0D) s += 1;
    const uint16_t reset = uint16_t(rom[at + 0x3C] | rom[at + 0x3D] << 8);
    if (reset >= 0x8000) s += 1;
    return s;
}

}

Snes::Snes(SNES_SPC& spc) : cpu_(*this), spc_(spc) {
    spc_.init_rom(kIplRom);
}

bool Snes::load(const std::vector<uint8_t>& rom, std::string* err) {
    if (rom.size() < 0x8000) { if (err) *err = "ROM too small"; return false; }
    rom_ = std::make_shared<std::vector<uint8_t>>(rom);
    const int lo = header_score(*rom_, 0x7FC0, false), hi = header_score(*rom_, 0xFFC0, true);
    hirom_ = hi > lo;
    reset();
    return true;
}

void Snes::reset() {
    std::fill(wram_.begin(), wram_.end(), 0);
    nmitimen_ = 0; rdnmi_ = 0; hvbjoy_ = 0; nmi_flag_ = false; irq_flag_ = false;
    wmadd_ = 0; wrio_ = 0xFF; mul_a_ = mul_b_ = 0; div_a_ = 0; div_b_ = 0; rddiv_ = rdmpy_ = 0;
    htime_ = vtime_ = 0x1FF; fastrom_ = false;
    std::memset(dma_, 0, sizeof dma_); std::memset(ppu_, 0, sizeof ppu_);
    master_ = 0; line_ = 0; line_start_ = 0; spc_time_ = 0;
    spc_.reset();
    cpu_.reset();
}

uint32_t Snes::rom_addr(uint32_t addr) const {
    const uint32_t bank = (addr >> 16) & 0xFF, off = addr & 0xFFFF;
    uint32_t a;
    if (hirom_) a = ((bank & 0x3F) << 16) | off;
    else a = ((bank & 0x7F) << 15) | (off & 0x7FFF);
    return a % uint32_t(rom_->size());
}

uint8_t Snes::read(uint32_t addr) {
    const uint32_t bank = (addr >> 16) & 0xFF, off = addr & 0xFFFF;
    if (bank == 0x7E || bank == 0x7F) return wram_[((bank - 0x7E) << 16) | off];
    const uint32_t b = bank & 0x7F;
    if (b < 0x40) {
        if (off < 0x2000) return wram_[off];
        if (off < 0x6000) return read_reg(uint16_t(off));
        if (off < 0x8000) return hirom_ && b >= 0x20 ? sram_[(((b - 0x20) << 13) | (off - 0x6000)) & 0x1FFFF] : 0;
        return (*rom_)[rom_addr(addr)];
    }
    if (!hirom_ && b >= 0x70 && b < 0x7E && off < 0x8000) return sram_[(((b - 0x70) << 15) | off) & 0x1FFFF];
    return (*rom_)[rom_addr(addr)];
}

void Snes::write(uint32_t addr, uint8_t v) {
    const uint32_t bank = (addr >> 16) & 0xFF, off = addr & 0xFFFF;
    if (bank == 0x7E || bank == 0x7F) { wram_[((bank - 0x7E) << 16) | off] = v; return; }
    const uint32_t b = bank & 0x7F;
    if (b < 0x40) {
        if (off < 0x2000) { wram_[off] = v; return; }
        if (off < 0x6000) { write_reg(uint16_t(off), v); return; }
        if (off < 0x8000) { if (hirom_ && b >= 0x20) sram_[(((b - 0x20) << 13) | (off - 0x6000)) & 0x1FFFF] = v; return; }
        return;
    }
    if (!hirom_ && b >= 0x70 && b < 0x7E && off < 0x8000) sram_[(((b - 0x70) << 15) | off) & 0x1FFFF] = v;
}

int Snes::spc_now() const { return spc_time_; }

uint8_t Snes::read_reg(uint16_t addr) {
    if (addr >= 0x2140 && addr < 0x2180) return uint8_t(spc_.read_port(spc_now(), addr & 3));
    switch (addr) {
        case 0x2134: return uint8_t(mpy_);
        case 0x2135: return uint8_t(mpy_ >> 8);
        case 0x2136: return uint8_t(mpy_ >> 16);
        case 0x2137: return 0;
        case 0x2138: case 0x2139: case 0x213A: case 0x213B: return 0;
        case 0x213C: return uint8_t(((master_ - line_start_) / 4) & 0xFF);
        case 0x213D: return uint8_t(line_);
        case 0x213E: return 0x01;
        case 0x213F: return 0x02;
        case 0x2180: { uint8_t v = wram_[wmadd_ & 0x1FFFF]; wmadd_ = (wmadd_ + 1) & 0x1FFFF; return v; }
        case 0x4016: case 0x4017: return 0;
        case 0x4210: { uint8_t v = uint8_t((nmi_flag_ ? 0x80 : 0) | 0x02); nmi_flag_ = false; return v; }
        case 0x4211: { uint8_t v = irq_flag_ ? 0x80 : 0; irq_flag_ = false; cpu_.irq(false); return v; }
        case 0x4212: {
            const bool vblank = line_ >= kVblankLine;
            const bool hblank = (master_ - line_start_) >= 1096 || (master_ - line_start_) < 4;
            return uint8_t((vblank ? 0x80 : 0) | (hblank ? 0x40 : 0));
        }
        case 0x4213: return wrio_;
        case 0x4214: return uint8_t(rddiv_);
        case 0x4215: return uint8_t(rddiv_ >> 8);
        case 0x4216: return uint8_t(rdmpy_);
        case 0x4217: return uint8_t(rdmpy_ >> 8);
        default:
            if (addr >= 0x4300 && addr < 0x4380) return dma_[(addr >> 4) & 7][addr & 15];
            if (addr >= 0x2100 && addr < 0x2140) return ppu_[addr - 0x2100];
            return 0;
    }
}

void Snes::write_reg(uint16_t addr, uint8_t v) {
    if (addr >= 0x2140 && addr < 0x2180) { spc_.write_port(spc_now(), addr & 3, v); return; }
    switch (addr) {
        case 0x211B: m7a_ = uint16_t((m7a_ >> 8) | (v << 8)); mpy_ = int32_t(int16_t(m7a_)) * int8_t(m7b_); return;
        case 0x211C: m7b_ = v; mpy_ = int32_t(int16_t(m7a_)) * int8_t(m7b_); return;
        case 0x2180: wram_[wmadd_ & 0x1FFFF] = v; wmadd_ = (wmadd_ + 1) & 0x1FFFF; return;
        case 0x2181: wmadd_ = (wmadd_ & 0x1FF00) | v; return;
        case 0x2182: wmadd_ = (wmadd_ & 0x100FF) | (uint32_t(v) << 8); return;
        case 0x2183: wmadd_ = (wmadd_ & 0x0FFFF) | (uint32_t(v & 1) << 16); return;
        case 0x4200: nmitimen_ = v; if (!(v & 0x30)) { irq_flag_ = false; cpu_.irq(false); } return;
        case 0x4201: wrio_ = v; return;
        case 0x4202: mul_a_ = v; return;
        case 0x4203: mul_b_ = v; rdmpy_ = uint16_t(mul_a_ * mul_b_); return;
        case 0x4204: div_a_ = uint16_t((div_a_ & 0xFF00) | v); return;
        case 0x4205: div_a_ = uint16_t((div_a_ & 0x00FF) | (v << 8)); return;
        case 0x4206: div_b_ = v; if (v) { rddiv_ = uint16_t(div_a_ / v); rdmpy_ = uint16_t(div_a_ % v); } else { rddiv_ = 0xFFFF; rdmpy_ = div_a_; } return;
        case 0x4207: htime_ = uint16_t((htime_ & 0x100) | v); return;
        case 0x4208: htime_ = uint16_t((htime_ & 0xFF) | ((v & 1) << 8)); return;
        case 0x4209: vtime_ = uint16_t((vtime_ & 0x100) | v); return;
        case 0x420A: vtime_ = uint16_t((vtime_ & 0xFF) | ((v & 1) << 8)); return;
        case 0x420B: do_dma(v); return;
        case 0x420C: return;
        case 0x420D: fastrom_ = v & 1; return;
        default:
            if (addr >= 0x4300 && addr < 0x4380) { dma_[(addr >> 4) & 7][addr & 15] = v; return; }
            if (addr >= 0x2100 && addr < 0x2140) { ppu_[addr - 0x2100] = v; return; }
            return;
    }
}

void Snes::do_dma(uint8_t mask) {
    static const uint8_t kPat[8][4] = {{0, 0, 0, 0}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1}, {0, 1, 2, 3}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1}};
    static const int kPatLen[8] = {1, 2, 2, 4, 4, 4, 2, 4};
    for (int ch = 0; ch < 8; ++ch) {
        if (!(mask & (1 << ch))) continue;
        uint8_t* r = dma_[ch];
        const uint8_t params = r[0];
        const bool to_a = params & 0x80;
        const int mode = params & 7;
        const int step = (params & 0x08) ? 0 : (params & 0x10) ? -1 : 1;
        uint32_t a = uint32_t(r[2]) | uint32_t(r[3]) << 8 | uint32_t(r[4]) << 16;
        uint32_t count = uint32_t(r[5]) | uint32_t(r[6]) << 8;
        if (count == 0) count = 0x10000;
        for (uint32_t i = 0; i < count; ++i) {
            const uint16_t b = uint16_t(0x2100 + uint8_t(r[1] + kPat[mode][i % uint32_t(kPatLen[mode])]));
            if (to_a) write(a, read_reg(b));
            else write_reg(b, read(a));
            a = (a & 0xFF0000) | uint16_t((a & 0xFFFF) + step);
            master_ += 8;
        }
        r[2] = uint8_t(a); r[3] = uint8_t(a >> 8);
        r[5] = r[6] = 0;
    }
}

void Snes::end_line() {
    line_start_ += kLineCycles;
    if (++line_ >= kLines) line_ = 0;
    if (line_ == kVblankLine) {
        nmi_flag_ = true;
        if (nmitimen_ & 0x80) cpu_.nmi();
    }
    const bool virq = nmitimen_ & 0x20, hirq = nmitimen_ & 0x10;
    if ((virq && line_ == vtime_) || (hirq && !virq)) { irq_flag_ = true; cpu_.irq(true); }
}

void Snes::tick_to(int64_t target) {
    while (master_ < target) {
        if (cpu_.stopped() || cpu_.waiting()) {
            master_ = std::min<int64_t>(target, line_start_ + kLineCycles);
        } else {
            const int cycles = cpu_.step();
            master_ += int64_t(cycles) * (fastrom_ ? 6 : 8);
        }
        while (master_ >= line_start_ + kLineCycles) end_line();
    }
}

void Snes::run(int frames, int16_t* out) {
    const double kMasterPerSample = kMasterHz / 32000.0 * tempo_ / 256.0;
    spc_.set_output(out, frames * 2);
    for (int i = 0; i < frames; ++i) {
        const int64_t start = master_;
        const int64_t target = int64_t(double(start) + kMasterPerSample);
        while (master_ < target) {
            spc_time_ = int(std::min<int64_t>(31, (master_ - start) * 32 / int64_t(kMasterPerSample)));
            const int64_t step_end = std::min<int64_t>(target, master_ + 64);
            tick_to(step_end);
        }
        spc_time_ = 31;
        spc_.end_frame(32);
        spc_time_ = 0;
    }
}
