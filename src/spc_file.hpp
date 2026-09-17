// SPC file container: header + ID666 tags + optional xid6 extended tags.
// Layout reference: https://wiki.superfamicom.org/spc-and-rsn-file-format
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SpcFile {
    static constexpr size_t kMinSize   = 0x10180;
    static constexpr size_t kBaseSize  = 0x10200;
    static constexpr size_t kRamOffset = 0x100;
    static constexpr size_t kDspOffset = 0x10100;
    static constexpr size_t kIplOffset = 0x101C0;

    std::vector<uint8_t> data;   // raw file bytes, handed to the emulator as-is
    std::string          path;

    std::string title, game, artist, dumper, comments, date, ost, publisher;
    int  intro_ms = 0;     // seconds-before-fade from ID666, or xid6 intro+loop*count
    int  fade_ms  = 0;
    bool has_id666 = false;
    bool binary_tags = false;
    uint8_t default_mutes = 0;   // ID666 channel-disable bitmask

    uint16_t pc = 0;
    uint8_t  a = 0, x = 0, y = 0, psw = 0, sp = 0;

    int total_ms() const { return intro_ms > 0 ? intro_ms + fade_ms : 0; }

    const uint8_t* ram() const { return data.data() + kRamOffset; }
    const uint8_t* dsp() const { return data.data() + kDspOffset; }
};

std::string load_spc_file(const std::string& path, SpcFile& out);
std::string parse_spc(std::vector<uint8_t> bytes, SpcFile& out);
