// A minimal SNES for SNSF playback: the 65816, WRAM, the cartridge ROM
// (LoROM or HiROM), the DSP-less hardware registers a sound engine can
// touch (NMI / vblank timing, multiply-divide, DMA, WRAM port) and the four
// APU ports bridged to the SPC700 emulator. No PPU, no controllers.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "SNES_SPC.h"
#include "cpu65816.hpp"

class Snes : public Bus65816 {
public:
    explicit Snes(SNES_SPC& spc);
    bool load(const std::vector<uint8_t>& rom, std::string* err);
    void reset();
    void run(int frames, int16_t* out);

    SNES_SPC& spc() { return spc_; }
    bool cpu_stopped() const { return cpu_.stopped(); }
    void set_tempo(int t) { tempo_ = t; }
    bool hirom() const { return hirom_; }
    double frame_rate() const { return kMasterHz / (kLines * kLineCycles); }

    uint8_t read(uint32_t addr) override;
    void    write(uint32_t addr, uint8_t v) override;

    static constexpr double kMasterHz = 21477272.0;
    static constexpr int kLines = 262, kLineCycles = 1364, kVblankLine = 225;

    std::shared_ptr<std::vector<uint8_t>> rom_shared() const { return rom_; }
    const uint8_t* wram() const { return wram_.data(); }
    uint8_t* wram_mut() { return wram_.data(); }
    uint32_t rom_offset(uint32_t addr) const { return rom_addr(addr); }

private:
    std::shared_ptr<std::vector<uint8_t>> rom_ = std::make_shared<std::vector<uint8_t>>();
    std::vector<uint8_t> wram_ = std::vector<uint8_t>(0x20000);
    std::vector<uint8_t> sram_ = std::vector<uint8_t>(0x20000);
    bool hirom_ = false, fastrom_ = false;
    Cpu65816 cpu_;
    SNES_SPC& spc_;
    int tempo_ = 256;

    int64_t master_ = 0;      // master cycles since reset
    int     line_ = 0;        // 0..261
    int64_t line_start_ = 0;  // master cycle at which the current line began
    int     spc_time_ = 0;    // SPC clocks already spent in the current SPC frame

    uint8_t nmitimen_ = 0, rdnmi_ = 0, hvbjoy_ = 0;
    bool    nmi_flag_ = false;
    uint8_t wrio_ = 0xFF;
    uint8_t mul_a_ = 0, mul_b_ = 0; uint16_t div_a_ = 0; uint8_t div_b_ = 0;
    uint16_t rddiv_ = 0, rdmpy_ = 0;
    uint32_t wmadd_ = 0;
    uint16_t htime_ = 0x1FF, vtime_ = 0x1FF;
    bool    irq_flag_ = false;
    uint8_t dma_[8][16] = {};
    uint8_t ppu_[0x40] = {};
    uint16_t m7a_ = 0; uint8_t m7b_ = 0; uint8_t m7_latch_ = 0; int32_t mpy_ = 0;
    uint8_t apu_in_[4] = {};   // last values the CPU wrote (for reads of $2140-3 mirrors of own writes: not needed, ports read the SPC)

    uint8_t  read_reg(uint16_t addr);
    void     write_reg(uint16_t addr, uint8_t v);
    void     do_dma(uint8_t mask);
    void     tick_to(int64_t target);
    void     end_line();
    int      spc_now() const;
    uint32_t rom_addr(uint32_t addr) const;
};
