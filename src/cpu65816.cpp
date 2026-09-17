#include "cpu65816.hpp"

void Cpu65816::reset() {
    e = true;
    p = 0x34;
    a = x = y = 0; d = 0; db = 0; pb = 0;
    s = 0x01FF;
    stopped_ = waiting_ = false;
    nmi_pending_ = false; irq_line_ = false;
    pc = rd16(0xFFFC);
}

uint16_t Cpu65816::rd16w(uint32_t a) {
    uint32_t bank = a & 0xFF0000;
    uint16_t lo = rd(a);
    return uint16_t(lo | rd(bank | ((a + 1) & 0xFFFF)) << 8);
}

void Cpu65816::push(uint8_t v) {
    wr(s, v);
    if (e) s = uint16_t(0x0100 | ((s - 1) & 0xFF)); else --s;
}

uint8_t Cpu65816::pop() {
    if (e) s = uint16_t(0x0100 | ((s + 1) & 0xFF)); else ++s;
    return rd(s);
}

void Cpu65816::set_p(uint8_t v) {
    p = v;
    if (e) p |= M | X_;
    if (p & X_) { x &= 0xFF; y &= 0xFF; }
}

void Cpu65816::interrupt(uint16_t vec_native, uint16_t vec_emu, bool brk) {
    waiting_ = false;
    if (e) {
        push16(pc);
        push(uint8_t((p & ~0x10) | 0x20 | (brk ? 0x10 : 0)));
        p = uint8_t((p | I) & ~D);
        pb = 0;
        pc = rd16(vec_emu);
    } else {
        push(pb);
        push16(pc);
        push(p);
        p = uint8_t((p | I) & ~D);
        pb = 0;
        pc = rd16(vec_native);
    }
    cyc_ += 2;
}

void Cpu65816::op_ora(uint16_t v) { if (m8()) { uint8_t r = uint8_t((a | v) & 0xFF); set_a(r); set_nz8(r); } else { a |= v; set_nz16(a); } }
void Cpu65816::op_and(uint16_t v) { if (m8()) { uint8_t r = uint8_t((a & v) & 0xFF); set_a(r); set_nz8(r); } else { a &= v; set_nz16(a); } }
void Cpu65816::op_eor(uint16_t v) { if (m8()) { uint8_t r = uint8_t((a ^ v) & 0xFF); set_a(r); set_nz8(r); } else { a ^= v; set_nz16(a); } }

void Cpu65816::op_adc(uint16_t v) {
    if (m8()) {
        int al = a & 0xFF, c = p & C;
        int r;
        if (p & D) {
            int lo = (al & 0x0F) + (v & 0x0F) + c;
            if (lo > 9) lo += 6;
            int hi = (al >> 4) + ((v & 0xFF) >> 4) + (lo > 15 ? 1 : 0);
            lo &= 0x0F;
            const int bin = al + (v & 0xFF) + c;
            p = uint8_t((p & ~V) | ((~(al ^ v) & (al ^ bin) & 0x80) ? V : 0));
            if (hi > 9) hi += 6;
            r = ((hi << 4) | lo) & 0xFF;
            p = uint8_t((p & ~C) | (hi > 15 ? C : 0));
        } else {
            r = al + (v & 0xFF) + c;
            p = uint8_t((p & ~(C | V)) | (r > 0xFF ? C : 0) | ((~(al ^ v) & (al ^ r) & 0x80) ? V : 0));
            r &= 0xFF;
        }
        set_a(uint16_t(r)); set_nz8(uint8_t(r));
    } else {
        int c = p & C, r;
        if (p & D) {
            int n0 = (a & 0xF) + (v & 0xF) + c; if (n0 > 9) n0 += 6;
            int n1 = ((a >> 4) & 0xF) + ((v >> 4) & 0xF) + (n0 > 15 ? 1 : 0); n0 &= 0xF; if (n1 > 9) n1 += 6;
            int n2 = ((a >> 8) & 0xF) + ((v >> 8) & 0xF) + (n1 > 15 ? 1 : 0); n1 &= 0xF; if (n2 > 9) n2 += 6;
            int n3 = ((a >> 12) & 0xF) + ((v >> 12) & 0xF) + (n2 > 15 ? 1 : 0); n2 &= 0xF;
            const int bin = a + v + c;
            p = uint8_t((p & ~V) | ((~(a ^ v) & (a ^ bin) & 0x8000) ? V : 0));
            if (n3 > 9) n3 += 6;
            r = (n3 << 12 | n2 << 8 | n1 << 4 | n0) & 0xFFFF;
            p = uint8_t((p & ~C) | (n3 > 15 ? C : 0));
        } else {
            r = a + v + c;
            p = uint8_t((p & ~(C | V)) | (r > 0xFFFF ? C : 0) | ((~(a ^ v) & (a ^ r) & 0x8000) ? V : 0));
            r &= 0xFFFF;
        }
        a = uint16_t(r); set_nz16(a);
    }
}

void Cpu65816::op_sbc(uint16_t v) {
    if (m8()) {
        int al = a & 0xFF, c = p & C, vv = v & 0xFF, r;
        if (p & D) {
            int lo = (al & 0x0F) - (vv & 0x0F) + c - 1;
            if (lo < 0) lo -= 6;
            int hi = (al >> 4) - (vv >> 4) + (lo < 0 ? -1 : 0);
            lo &= 0x0F;
            const int bin = al - vv + c - 1;
            p = uint8_t((p & ~V) | (((al ^ vv) & (al ^ bin) & 0x80) ? V : 0));
            if (hi < 0) hi -= 6;
            r = ((hi << 4) | lo) & 0xFF;
            p = uint8_t((p & ~C) | (hi >= 0 ? C : 0));
        } else {
            r = al - vv + c - 1;
            p = uint8_t((p & ~(C | V)) | (r >= 0 ? C : 0) | (((al ^ vv) & (al ^ r) & 0x80) ? V : 0));
            r &= 0xFF;
        }
        set_a(uint16_t(r)); set_nz8(uint8_t(r));
    } else {
        int c = p & C, r;
        if (p & D) {
            int n0 = (a & 0xF) - (v & 0xF) + c - 1; if (n0 < 0) n0 -= 6;
            int n1 = ((a >> 4) & 0xF) - ((v >> 4) & 0xF) + (n0 < 0 ? -1 : 0); n0 &= 0xF; if (n1 < 0) n1 -= 6;
            int n2 = ((a >> 8) & 0xF) - ((v >> 8) & 0xF) + (n1 < 0 ? -1 : 0); n1 &= 0xF; if (n2 < 0) n2 -= 6;
            int n3 = ((a >> 12) & 0xF) - ((v >> 12) & 0xF) + (n2 < 0 ? -1 : 0); n2 &= 0xF;
            const int bin = a - v + c - 1;
            p = uint8_t((p & ~V) | (((a ^ v) & (a ^ bin) & 0x8000) ? V : 0));
            if (n3 < 0) n3 -= 6;
            r = (n3 << 12 | n2 << 8 | n1 << 4 | n0) & 0xFFFF;
            p = uint8_t((p & ~C) | (n3 >= 0 ? C : 0));
        } else {
            r = a - v + c - 1;
            p = uint8_t((p & ~(C | V)) | (r >= 0 ? C : 0) | (((a ^ v) & (a ^ r) & 0x8000) ? V : 0));
            r &= 0xFFFF;
        }
        a = uint16_t(r); set_nz16(a);
    }
}

void Cpu65816::op_cmp(uint16_t r, uint16_t v, bool w8) {
    if (w8) { int d = (r & 0xFF) - (v & 0xFF); p = uint8_t((p & ~C) | (d >= 0 ? C : 0)); set_nz8(uint8_t(d)); }
    else { int d = r - v; p = uint8_t((p & ~C) | (d >= 0 ? C : 0)); set_nz16(uint16_t(d)); }
}

void Cpu65816::op_bit(uint16_t v) {
    if (m8()) { p = uint8_t((p & ~(N | V | Z)) | (v & 0xC0) | (((a & v) & 0xFF) == 0 ? Z : 0)); }
    else { p = uint8_t((p & ~(N | V | Z)) | ((v >> 8) & 0xC0) | ((a & v) == 0 ? Z : 0)); }
}

void Cpu65816::op_lda(uint16_t v) { set_a(v); if (m8()) set_nz8(uint8_t(v)); else set_nz16(v); }
void Cpu65816::op_ldx(uint16_t v) { set_x(v); if (x8()) set_nz8(uint8_t(v)); else set_nz16(v); }
void Cpu65816::op_ldy(uint16_t v) { set_y(v); if (x8()) set_nz8(uint8_t(v)); else set_nz16(v); }

uint16_t Cpu65816::op_asl(uint16_t v) {
    if (m8()) { p = uint8_t((p & ~C) | ((v & 0x80) ? C : 0)); uint8_t r = uint8_t(v << 1); set_nz8(r); return r; }
    p = uint8_t((p & ~C) | ((v & 0x8000) ? C : 0)); uint16_t r = uint16_t(v << 1); set_nz16(r); return r;
}
uint16_t Cpu65816::op_lsr(uint16_t v) {
    if (m8()) { p = uint8_t((p & ~C) | (v & 1)); uint8_t r = uint8_t((v & 0xFF) >> 1); set_nz8(r); return r; }
    p = uint8_t((p & ~C) | (v & 1)); uint16_t r = uint16_t(v >> 1); set_nz16(r); return r;
}
uint16_t Cpu65816::op_rol(uint16_t v) {
    int c = p & C;
    if (m8()) { p = uint8_t((p & ~C) | ((v & 0x80) ? C : 0)); uint8_t r = uint8_t((v << 1) | c); set_nz8(r); return r; }
    p = uint8_t((p & ~C) | ((v & 0x8000) ? C : 0)); uint16_t r = uint16_t((v << 1) | c); set_nz16(r); return r;
}
uint16_t Cpu65816::op_ror(uint16_t v) {
    int c = p & C;
    if (m8()) { p = uint8_t((p & ~C) | (v & 1)); uint8_t r = uint8_t(((v & 0xFF) >> 1) | (c << 7)); set_nz8(r); return r; }
    p = uint8_t((p & ~C) | (v & 1)); uint16_t r = uint16_t((v >> 1) | (c << 15)); set_nz16(r); return r;
}
uint16_t Cpu65816::op_inc(uint16_t v) { if (m8()) { uint8_t r = uint8_t(v + 1); set_nz8(r); return r; } uint16_t r = uint16_t(v + 1); set_nz16(r); return r; }
uint16_t Cpu65816::op_dec(uint16_t v) { if (m8()) { uint8_t r = uint8_t(v - 1); set_nz8(r); return r; } uint16_t r = uint16_t(v - 1); set_nz16(r); return r; }

void Cpu65816::rmw(uint32_t addr, uint16_t (Cpu65816::*f)(uint16_t)) {
    uint16_t v = read_m(addr);
    ++cyc_;
    write_m(addr, (this->*f)(v));
}

void Cpu65816::branch(bool cond) {
    int8_t off = int8_t(fetch());
    if (cond) { pc = uint16_t(pc + off); ++cyc_; }
}

int Cpu65816::step() {
    cyc_ = 0;
    if (stopped_) return 8;
    if (nmi_pending_) { nmi_pending_ = false; interrupt(0xFFEA, 0xFFFA, false); return cyc_; }
    if (irq_line_ && !(p & I)) { interrupt(0xFFEE, 0xFFFE, false); return cyc_; }
    if (irq_line_ && waiting_) waiting_ = false;
    if (waiting_) return 8;

    const uint8_t op = fetch();
    switch (op) {
#define ALU_GROUP(base, OPNAME) \
        case base + 0x01: OPNAME(read_m(indx_addr(fetch()))); break; \
        case base + 0x03: OPNAME(read_m(sr_addr(fetch()))); break; \
        case base + 0x05: OPNAME(read_m(dp_addr(fetch()))); break; \
        case base + 0x07: OPNAME(read_m(indl_addr(fetch()))); break; \
        case base + 0x09: OPNAME(imm_m()); break; \
        case base + 0x0D: OPNAME(read_m(abs_addr(fetch16()))); break; \
        case base + 0x0F: { uint32_t l = fetch16(); l |= uint32_t(fetch()) << 16; OPNAME(read_m(l)); } break; \
        case base + 0x11: OPNAME(read_m(indy_addr(fetch()))); break; \
        case base + 0x12: OPNAME(read_m(ind_addr(fetch()))); break; \
        case base + 0x13: OPNAME(read_m(srindy_addr(fetch()))); break; \
        case base + 0x15: OPNAME(read_m(dpx_addr(fetch()))); break; \
        case base + 0x17: OPNAME(read_m(indly_addr(fetch()))); break; \
        case base + 0x19: OPNAME(read_m(absy_addr(fetch16()))); break; \
        case base + 0x1D: OPNAME(read_m(absx_addr(fetch16()))); break; \
        case base + 0x1F: { uint32_t l = fetch16(); l |= uint32_t(fetch()) << 16; OPNAME(read_m((l + x) & 0xFFFFFF)); } break;
#define OP_CMPA(v) op_cmp(a, v, m8())
        ALU_GROUP(0x00, op_ora)
        ALU_GROUP(0x20, op_and)
        ALU_GROUP(0x40, op_eor)
        ALU_GROUP(0x60, op_adc)
        ALU_GROUP(0xA0, op_lda)
        ALU_GROUP(0xC0, OP_CMPA)
        ALU_GROUP(0xE0, op_sbc)
#undef ALU_GROUP
        case 0x81: write_m(indx_addr(fetch()), a); break;
        case 0x83: write_m(sr_addr(fetch()), a); break;
        case 0x85: write_m(dp_addr(fetch()), a); break;
        case 0x87: write_m(indl_addr(fetch()), a); break;
        case 0x8D: write_m(abs_addr(fetch16()), a); break;
        case 0x8F: { uint32_t l = fetch16(); l |= uint32_t(fetch()) << 16; write_m(l, a); } break;
        case 0x91: write_m(indy_addr(fetch()), a); break;
        case 0x92: write_m(ind_addr(fetch()), a); break;
        case 0x93: write_m(srindy_addr(fetch()), a); break;
        case 0x95: write_m(dpx_addr(fetch()), a); break;
        case 0x97: write_m(indly_addr(fetch()), a); break;
        case 0x99: write_m(absy_addr(fetch16()), a); break;
        case 0x9D: write_m(absx_addr(fetch16()), a); break;
        case 0x9F: { uint32_t l = fetch16(); l |= uint32_t(fetch()) << 16; write_m((l + x) & 0xFFFFFF, a); } break;
        case 0x0A: set_a(op_asl(m8() ? (a & 0xFF) : a)); break;
        case 0x06: rmw(dp_addr(fetch()), &Cpu65816::op_asl); break;
        case 0x0E: rmw(abs_addr(fetch16()), &Cpu65816::op_asl); break;
        case 0x16: rmw(dpx_addr(fetch()), &Cpu65816::op_asl); break;
        case 0x1E: rmw(absx_addr(fetch16()), &Cpu65816::op_asl); break;
        case 0x2A: set_a(op_rol(m8() ? (a & 0xFF) : a)); break;
        case 0x26: rmw(dp_addr(fetch()), &Cpu65816::op_rol); break;
        case 0x2E: rmw(abs_addr(fetch16()), &Cpu65816::op_rol); break;
        case 0x36: rmw(dpx_addr(fetch()), &Cpu65816::op_rol); break;
        case 0x3E: rmw(absx_addr(fetch16()), &Cpu65816::op_rol); break;
        case 0x4A: set_a(op_lsr(m8() ? (a & 0xFF) : a)); break;
        case 0x46: rmw(dp_addr(fetch()), &Cpu65816::op_lsr); break;
        case 0x4E: rmw(abs_addr(fetch16()), &Cpu65816::op_lsr); break;
        case 0x56: rmw(dpx_addr(fetch()), &Cpu65816::op_lsr); break;
        case 0x5E: rmw(absx_addr(fetch16()), &Cpu65816::op_lsr); break;
        case 0x6A: set_a(op_ror(m8() ? (a & 0xFF) : a)); break;
        case 0x66: rmw(dp_addr(fetch()), &Cpu65816::op_ror); break;
        case 0x6E: rmw(abs_addr(fetch16()), &Cpu65816::op_ror); break;
        case 0x76: rmw(dpx_addr(fetch()), &Cpu65816::op_ror); break;
        case 0x7E: rmw(absx_addr(fetch16()), &Cpu65816::op_ror); break;
        case 0x1A: set_a(op_inc(m8() ? (a & 0xFF) : a)); break;
        case 0x3A: set_a(op_dec(m8() ? (a & 0xFF) : a)); break;
        case 0xE6: rmw(dp_addr(fetch()), &Cpu65816::op_inc); break;
        case 0xEE: rmw(abs_addr(fetch16()), &Cpu65816::op_inc); break;
        case 0xF6: rmw(dpx_addr(fetch()), &Cpu65816::op_inc); break;
        case 0xFE: rmw(absx_addr(fetch16()), &Cpu65816::op_inc); break;
        case 0xC6: rmw(dp_addr(fetch()), &Cpu65816::op_dec); break;
        case 0xCE: rmw(abs_addr(fetch16()), &Cpu65816::op_dec); break;
        case 0xD6: rmw(dpx_addr(fetch()), &Cpu65816::op_dec); break;
        case 0xDE: rmw(absx_addr(fetch16()), &Cpu65816::op_dec); break;
        case 0xE8: set_x(uint16_t(x + 1)); if (x8()) set_nz8(uint8_t(x)); else set_nz16(x); break;
        case 0xC8: set_y(uint16_t(y + 1)); if (x8()) set_nz8(uint8_t(y)); else set_nz16(y); break;
        case 0xCA: set_x(uint16_t(x - 1)); if (x8()) set_nz8(uint8_t(x)); else set_nz16(x); break;
        case 0x88: set_y(uint16_t(y - 1)); if (x8()) set_nz8(uint8_t(y)); else set_nz16(y); break;
        case 0x89: { uint16_t v = imm_m(); if (m8()) p = uint8_t((p & ~Z) | (((a & v) & 0xFF) == 0 ? Z : 0)); else p = uint8_t((p & ~Z) | ((a & v) == 0 ? Z : 0)); } break;
        case 0x24: op_bit(read_m(dp_addr(fetch()))); break;
        case 0x2C: op_bit(read_m(abs_addr(fetch16()))); break;
        case 0x34: op_bit(read_m(dpx_addr(fetch()))); break;
        case 0x3C: op_bit(read_m(absx_addr(fetch16()))); break;
        case 0x04: case 0x0C: case 0x14: case 0x1C: {
            uint32_t addr = (op & 0x08) ? abs_addr(fetch16()) : dp_addr(fetch());
            uint16_t v = read_m(addr);
            const uint16_t am = m8() ? (a & 0xFF) : a;
            p = uint8_t((p & ~Z) | ((v & am) == 0 ? Z : 0));
            ++cyc_;
            write_m(addr, (op & 0x10) ? uint16_t(v & ~am) : uint16_t(v | am));
        } break;
        case 0xA2: op_ldx(imm_x()); break;
        case 0xA6: op_ldx(read_x(dp_addr(fetch()))); break;
        case 0xAE: op_ldx(read_x(abs_addr(fetch16()))); break;
        case 0xB6: op_ldx(read_x(dpy_addr(fetch()))); break;
        case 0xBE: op_ldx(read_x(absy_addr(fetch16()))); break;
        case 0xA0: op_ldy(imm_x()); break;
        case 0xA4: op_ldy(read_x(dp_addr(fetch()))); break;
        case 0xAC: op_ldy(read_x(abs_addr(fetch16()))); break;
        case 0xB4: op_ldy(read_x(dpx_addr(fetch()))); break;
        case 0xBC: op_ldy(read_x(absx_addr(fetch16()))); break;
        case 0x86: write_x(dp_addr(fetch()), x); break;
        case 0x8E: write_x(abs_addr(fetch16()), x); break;
        case 0x96: write_x(dpy_addr(fetch()), x); break;
        case 0x84: write_x(dp_addr(fetch()), y); break;
        case 0x8C: write_x(abs_addr(fetch16()), y); break;
        case 0x94: write_x(dpx_addr(fetch()), y); break;
        case 0x64: write_m(dp_addr(fetch()), 0); break;
        case 0x9C: write_m(abs_addr(fetch16()), 0); break;
        case 0x74: write_m(dpx_addr(fetch()), 0); break;
        case 0x9E: write_m(absx_addr(fetch16()), 0); break;
        case 0xE0: op_cmp(x, imm_x(), x8()); break;
        case 0xE4: op_cmp(x, read_x(dp_addr(fetch())), x8()); break;
        case 0xEC: op_cmp(x, read_x(abs_addr(fetch16())), x8()); break;
        case 0xC0: op_cmp(y, imm_x(), x8()); break;
        case 0xC4: op_cmp(y, read_x(dp_addr(fetch())), x8()); break;
        case 0xCC: op_cmp(y, read_x(abs_addr(fetch16())), x8()); break;
        case 0x10: branch(!(p & N)); break;
        case 0x30: branch(p & N); break;
        case 0x50: branch(!(p & V)); break;
        case 0x70: branch(p & V); break;
        case 0x80: branch(true); break;
        case 0x90: branch(!(p & C)); break;
        case 0xB0: branch(p & C); break;
        case 0xD0: branch(!(p & Z)); break;
        case 0xF0: branch(p & Z); break;
        case 0x82: { int16_t off = int16_t(fetch16()); pc = uint16_t(pc + off); ++cyc_; } break;
        case 0x4C: pc = fetch16(); break;
        case 0x5C: { uint16_t a16 = fetch16(); pb = fetch(); pc = a16; } break;
        case 0x6C: { uint16_t a16 = fetch16(); pc = rd16w(a16); } break;
        case 0x7C: { uint16_t a16 = fetch16(); pc = rd16w((uint32_t(pb) << 16) | uint16_t(a16 + x)); ++cyc_; } break;
        case 0xDC: { uint16_t a16 = fetch16(); pc = rd16w(a16); pb = rd(uint16_t(a16 + 2)); } break;
        case 0x20: { uint16_t a16 = fetch16(); push16(uint16_t(pc - 1)); pc = a16; ++cyc_; } break;
        case 0x22: { uint16_t a16 = fetch16(); uint8_t bank = fetch(); push(pb); push16(uint16_t(pc - 1)); pb = bank; pc = a16; ++cyc_; } break;
        case 0xFC: { uint16_t a16 = fetch16(); push16(uint16_t(pc - 1)); pc = rd16w((uint32_t(pb) << 16) | uint16_t(a16 + x)); ++cyc_; } break;
        case 0x60: pc = uint16_t(pop16() + 1); cyc_ += 2; break;
        case 0x6B: pc = uint16_t(pop16() + 1); pb = pop(); cyc_ += 2; break;
        case 0x40:
            if (e) { set_p(pop()); pc = pop16(); }
            else { set_p(pop()); pc = pop16(); pb = pop(); }
            cyc_ += 2;
            break;
        case 0x00: fetch(); interrupt(0xFFE6, 0xFFFE, true); break;
        case 0x02: fetch(); interrupt(0xFFE4, 0xFFF4, false); break;
        case 0x48: if (m8()) push(uint8_t(a)); else push16(a); break;
        case 0x68: if (m8()) { uint8_t v = pop(); set_a(v); set_nz8(v); } else { a = pop16(); set_nz16(a); } break;
        case 0xDA: if (x8()) push(uint8_t(x)); else push16(x); break;
        case 0xFA: if (x8()) { x = pop(); set_nz8(uint8_t(x)); } else { x = pop16(); set_nz16(x); } break;
        case 0x5A: if (x8()) push(uint8_t(y)); else push16(y); break;
        case 0x7A: if (x8()) { y = pop(); set_nz8(uint8_t(y)); } else { y = pop16(); set_nz16(y); } break;
        case 0x08: push(uint8_t(p | (e ? 0x30 : 0))); break;
        case 0x28: set_p(pop()); break;
        case 0x0B: push16(d); break;
        case 0x2B: d = pop16(); set_nz16(d); break;
        case 0x8B: push(db); break;
        case 0xAB: db = pop(); set_nz8(db); break;
        case 0x4B: push(pb); break;
        case 0xF4: push16(fetch16()); break;
        case 0xD4: push16(rd16w(dp_addr(fetch()))); break;
        case 0x62: { int16_t off = int16_t(fetch16()); push16(uint16_t(pc + off)); } break;
        case 0x18: p &= ~C; break;
        case 0x38: p |= C; break;
        case 0x58: p &= ~I; break;
        case 0x78: p |= I; break;
        case 0xB8: p &= ~V; break;
        case 0xD8: p &= ~D; break;
        case 0xF8: p |= D; break;
        case 0xC2: { uint8_t m = fetch(); set_p(uint8_t(p & ~m)); } break;
        case 0xE2: { uint8_t m = fetch(); set_p(uint8_t(p | m)); } break;
        case 0xFB: {
            bool c = p & C;
            p = uint8_t((p & ~C) | (e ? C : 0));
            e = c;
            if (e) { p |= M | X_; x &= 0xFF; y &= 0xFF; s = uint16_t(0x0100 | (s & 0xFF)); }
        } break;
        case 0xAA: set_x(a); if (x8()) set_nz8(uint8_t(x)); else set_nz16(x); break;
        case 0xA8: set_y(a); if (x8()) set_nz8(uint8_t(y)); else set_nz16(y); break;
        case 0x8A: set_a(x); if (m8()) set_nz8(uint8_t(a)); else set_nz16(a); break;
        case 0x98: set_a(y); if (m8()) set_nz8(uint8_t(a)); else set_nz16(a); break;
        case 0x9A: s = e ? uint16_t(0x0100 | (x & 0xFF)) : x; break;
        case 0xBA: set_x(s); if (x8()) set_nz8(uint8_t(x)); else set_nz16(x); break;
        case 0x9B: set_y(x); if (x8()) set_nz8(uint8_t(y)); else set_nz16(y); break;
        case 0xBB: set_x(y); if (x8()) set_nz8(uint8_t(x)); else set_nz16(x); break;
        case 0x5B: d = a; set_nz16(d); break;
        case 0x7B: a = d; set_nz16(a); break;
        case 0x1B: s = e ? uint16_t(0x0100 | (a & 0xFF)) : a; break;
        case 0x3B: a = s; set_nz16(a); break;
        case 0xEB: a = uint16_t((a << 8) | (a >> 8)); set_nz8(uint8_t(a)); ++cyc_; break;
        case 0x44: case 0x54: {
            uint8_t dst = fetch(), src = fetch();
            db = dst;
            wr(uint32_t(dst) << 16 | y, rd(uint32_t(src) << 16 | x));
            if (op == 0x54) { set_x(uint16_t(x + 1)); set_y(uint16_t(y + 1)); }
            else { set_x(uint16_t(x - 1)); set_y(uint16_t(y - 1)); }
            --a;
            if (a != 0xFFFF) pc = uint16_t(pc - 3);
            cyc_ += 5;
        } break;
        case 0xEA: ++cyc_; break;
        case 0x42: fetch(); break;
        case 0xCB: waiting_ = true; break;
        case 0xDB: stopped_ = true; break;
        default: ++cyc_; break;
    }
    return cyc_ + 1;
}
