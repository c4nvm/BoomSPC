// 65816 CPU core for SNSF playback: the SNES CPU runs a game's sound engine
// while the SPC700 only plays what it is told. Complete instruction set in
// native and emulation mode; timing is approximate (a per-instruction cycle
// count, no bus-speed distinction), which is plenty for a sound engine that
// paces itself on NMI.
#pragma once

#include <cstdint>

struct Bus65816 {
    virtual ~Bus65816() = default;
    virtual uint8_t read(uint32_t addr) = 0;
    virtual void    write(uint32_t addr, uint8_t v) = 0;
};

class Cpu65816 {
public:
    explicit Cpu65816(Bus65816& bus) : bus_(bus) {}
    void reset();
    int  step();
    void nmi() { nmi_pending_ = true; }
    void irq(bool level) { irq_line_ = level; }
    bool stopped() const { return stopped_; }
    bool waiting() const { return waiting_; }

    uint16_t a = 0, x = 0, y = 0, s = 0x01FF, d = 0, pc = 0;
    uint8_t  db = 0, pb = 0, p = 0x34;
    bool     e = true;   // emulation mode

    enum { C = 0x01, Z = 0x02, I = 0x04, D = 0x08, X_ = 0x10, M = 0x20, V = 0x40, N = 0x80 };
    bool m8() const { return e || (p & M); }
    bool x8() const { return e || (p & X_); }

private:
    Bus65816& bus_;
    bool nmi_pending_ = false, irq_line_ = false, stopped_ = false, waiting_ = false;
    int  cyc_ = 0;

    uint8_t  rd(uint32_t a) { ++cyc_; return bus_.read(a & 0xFFFFFF); }
    void     wr(uint32_t a, uint8_t v) { ++cyc_; bus_.write(a & 0xFFFFFF, v); }
    uint16_t rd16(uint32_t a) { uint16_t lo = rd(a); return uint16_t(lo | rd(a + 1) << 8); }
    uint16_t rd16w(uint32_t a);
    uint8_t  fetch() { uint8_t v = rd(uint32_t(pb) << 16 | pc); pc = uint16_t(pc + 1); return v; }
    uint16_t fetch16() { uint16_t lo = fetch(); return uint16_t(lo | fetch() << 8); }
    void push(uint8_t v);
    uint8_t pop();
    void push16(uint16_t v) { push(uint8_t(v >> 8)); push(uint8_t(v)); }
    uint16_t pop16() { uint16_t lo = pop(); return uint16_t(lo | pop() << 8); }
    void set_nz8(uint8_t v) { p = uint8_t((p & ~(N | Z)) | (v & 0x80) | (v == 0 ? Z : 0)); }
    void set_nz16(uint16_t v) { p = uint8_t((p & ~(N | Z)) | ((v >> 8) & 0x80) | (v == 0 ? Z : 0)); }
    void set_p(uint8_t v);
    void interrupt(uint16_t vec_native, uint16_t vec_emu, bool brk);

    uint32_t dp_addr(uint8_t off) { return uint16_t(d + off); }
    uint32_t dpx_addr(uint8_t off) { return e && (d & 0xFF) == 0 ? uint16_t((d & 0xFF00) | ((off + x) & 0xFF)) : uint16_t(d + off + x); }
    uint32_t dpy_addr(uint8_t off) { return e && (d & 0xFF) == 0 ? uint16_t((d & 0xFF00) | ((off + y) & 0xFF)) : uint16_t(d + off + y); }
    uint32_t abs_addr(uint16_t a) { return uint32_t(db) << 16 | a; }
    uint32_t absx_addr(uint16_t a) { return (uint32_t(db) << 16) + a + x; }
    uint32_t absy_addr(uint16_t a) { return (uint32_t(db) << 16) + a + y; }
    uint32_t ind_addr(uint8_t off) { return uint32_t(db) << 16 | rd16w(dp_addr(off)); }
    uint32_t indx_addr(uint8_t off) { return uint32_t(db) << 16 | rd16w(dpx_addr(off)); }
    uint32_t indy_addr(uint8_t off) { return (uint32_t(db) << 16) + rd16w(dp_addr(off)) + y; }
    uint32_t indl_addr(uint8_t off) { uint32_t a = dp_addr(off); return rd(a) | rd(a + 1) << 8 | rd(a + 2) << 16; }
    uint32_t indly_addr(uint8_t off) { return (indl_addr(off) + y) & 0xFFFFFF; }
    uint32_t sr_addr(uint8_t off) { return uint16_t(s + off); }
    uint32_t srindy_addr(uint8_t off) { return (uint32_t(db) << 16) + rd16(sr_addr(off)) + y; }

    uint16_t read_m(uint32_t a) { return m8() ? rd(a) : rd16(a); }
    uint16_t read_x(uint32_t a) { return x8() ? rd(a) : rd16(a); }
    void write_m(uint32_t a, uint16_t v) { wr(a, uint8_t(v)); if (!m8()) wr(a + 1, uint8_t(v >> 8)); }
    void write_x(uint32_t a, uint16_t v) { wr(a, uint8_t(v)); if (!x8()) wr(a + 1, uint8_t(v >> 8)); }
    uint16_t imm_m() { return m8() ? fetch() : fetch16(); }
    uint16_t imm_x() { return x8() ? fetch() : fetch16(); }

    void op_ora(uint16_t v); void op_and(uint16_t v); void op_eor(uint16_t v);
    void op_adc(uint16_t v); void op_sbc(uint16_t v); void op_cmp(uint16_t r, uint16_t v, bool w8);
    void op_bit(uint16_t v); void op_lda(uint16_t v); void op_ldx(uint16_t v); void op_ldy(uint16_t v);
    uint16_t op_asl(uint16_t v); uint16_t op_lsr(uint16_t v); uint16_t op_rol(uint16_t v); uint16_t op_ror(uint16_t v);
    uint16_t op_inc(uint16_t v); uint16_t op_dec(uint16_t v);
    void rmw(uint32_t a, uint16_t (Cpu65816::*f)(uint16_t));
    void branch(bool cond);
    void set_a(uint16_t v) { a = m8() ? uint16_t((a & 0xFF00) | (v & 0xFF)) : v; }
    void set_x(uint16_t v) { x = x8() ? (v & 0xFF) : v; }
    void set_y(uint16_t v) { y = x8() ? (v & 0xFF) : v; }
};
