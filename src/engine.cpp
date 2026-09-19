#include "engine.hpp"

#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <fstream>

Engine::~Engine() { shutdown(); }

std::string Engine::init() {
    if (dev_) return {};
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return SDL_GetError();

    SDL_AudioSpec want{};
    want.freq     = kSampleRate;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 1024;
    want.callback = &Engine::sdl_callback;
    want.userdata = this;

    SDL_AudioSpec got{};
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
    if (!dev_) return SDL_GetError();
    if (std::getenv("BOOMSPC_DEBUG_AUDIO")) std::fprintf(stderr, "audio: %s, %d Hz, %d samples per buffer\n", SDL_GetCurrentAudioDriver(), got.freq, got.samples);

    if (spc_.init() != nullptr) {
        shutdown();
        return "snes_spc init failed";
    }
    SDL_PauseAudioDevice(dev_, 0);
    return {};
}

void Engine::shutdown() {
    if (dev_) {
        SDL_CloseAudioDevice(dev_);
        dev_ = 0;
    }
}

std::string Engine::load_snsf(const SnsfFile& file) {
    std::lock_guard<std::mutex> lock(mtx_);
    playing_ = false;
    seeking_ = false;
    seek_reached_ = nullptr;
    undo_stack_.clear();
    redo_stack_.clear();
    edit_depth_ = 0;
    undo_open_ = false;
    dirty_ = false;
    snsf_ = std::make_unique<SnsfFile>(file);
    snes_ = std::make_unique<Snes>(spc_);
    std::string err;
    if (!snes_->load(file.rom, &err)) { snes_.reset(); snsf_.reset(); loaded_ = false; return err; }
    file_ = SpcFile{};
    file_.title = file.title; file_.game = file.game; file_.artist = file.artist;
    file_.comments = file.comment; file_.date = file.year; file_.publisher = file.copyright;
    file_.has_id666 = !file.title.empty() || !file.game.empty();
    file_.intro_ms = file.length_ms; file_.fade_ms = file.fade_ms;
    reload_locked();
    loaded_ = true;
    return {};
}

uint8_t Engine::snsf_read(uint32_t addr, bool live) const {
    if (addr < 0x1000) return live ? snes_->wram()[addr] : 0;
    if (addr < 0x2000) return live ? snes_->wram()[0xF000 + (addr - 0x1000)] : 0;
    if (addr < 0x8000) return 0;
    const std::vector<uint8_t>& rom = live ? *snes_->rom_shared() : snsf_->rom;
    const uint32_t off = snes_->rom_offset((uint32_t(bank_window_) << 16) | addr);
    return off < rom.size() ? rom[off] : 0;
}

void Engine::snsf_write(uint32_t addr, uint8_t v, WriteTarget target) {
    if (addr < 0x2000) {
        if (target == kImageOnly) return;
        if (addr < 0x1000) snes_->wram_mut()[addr] = v; else snes_->wram_mut()[0xF000 + (addr - 0x1000)] = v;
        return;
    }
    if (addr < 0x8000) return;
    const uint32_t off = snes_->rom_offset((uint32_t(bank_window_) << 16) | addr);
    if (target != kImageOnly && off < snes_->rom_shared()->size()) (*snes_->rom_shared())[off] = v;
    if (target != kLiveOnly && off < snsf_->rom.size()) snsf_->rom[off] = v;
}

void Engine::live_bytes(uint16_t addr, int n, uint8_t* out) {
    if (snes_) { for (int i = 0; i < n; ++i) out[i] = snsf_read(uint32_t(addr + i) & 0xFFFF, true); }
    else std::memcpy(out, spc_.ram_mut() + addr, size_t(n));
}

void Engine::set_bank_window(int bank) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (bank < 0 || bank == bank_window_) return;
    bank_window_ = bank;
    if (watch_len_) live_bytes(watch_addr_, watch_len_, watch_last_);
}

const uint8_t* Engine::image_ram() {
    if (!snes_) return file_.data.data() + SpcFile::kRamOffset;
    image_view_.assign(0x10000, 0);
    for (uint32_t a = 0x8000; a < 0x10000; ++a) image_view_[a] = snsf_read(a, false);
    return image_view_.data();
}

std::string Engine::export_snsf(const std::string& path) {
    std::vector<uint8_t> rom;
    std::map<std::string, std::string> tags;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!snes_ || !snsf_) return "no SNSF set loaded";
        rom = snsf_->rom;
        tags = snsf_->tags;
    }
    std::vector<uint8_t> prog(8);
    const uint32_t size = uint32_t(rom.size());
    prog[4] = uint8_t(size); prog[5] = uint8_t(size >> 8); prog[6] = uint8_t(size >> 16); prog[7] = uint8_t(size >> 24);
    prog.insert(prog.end(), rom.begin(), rom.end());
    uLongf packed_len = compressBound(uLong(prog.size()));
    std::vector<uint8_t> packed(packed_len);
    if (compress2(packed.data(), &packed_len, prog.data(), uLong(prog.size()), 9) != Z_OK) return "zlib failed";
    packed.resize(packed_len);
    const uint32_t crc = uint32_t(crc32(0, packed.data(), uInt(packed.size())));
    std::ofstream out(path, std::ios::binary);
    if (!out) return "could not open " + path;
    auto u32 = [&](uint32_t v) { char b[4] = {char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; out.write(b, 4); };
    out.write("PSF", 3); out.put(char(0x23));
    u32(0); u32(uint32_t(packed.size())); u32(crc);
    out.write(reinterpret_cast<const char*>(packed.data()), std::streamsize(packed.size()));
    out.write("[TAG]", 5);
    for (auto& [k, v] : tags) { if (k.empty() || k[0] == '_') continue; out << k << '=' << v << '\n'; }
    out << "comment=edited with BoomSPC\n";
    if (!out) return "write failed";
    dirty_ = false;
    return {};
}

void Engine::run_silent(double seconds) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!loaded_) return;
    int16_t buf[256 * 2];
    for (int64_t left = int64_t(seconds * kSampleRate); left > 0; left -= 256) {
        if (run_locked(256, buf)) break;
        sample_pairs_ += 256;
        if (watch_len_) watch_check_locked(sample_pairs_);
    }
}

blargg_err_t Engine::run_locked(int frames, int16_t* out) {
    if (snes_) { snes_->run(frames, out); return nullptr; }
    return spc_.play(frames * 2, out);
}

std::string Engine::load(const SpcFile& file) {
    std::lock_guard<std::mutex> lock(mtx_);
    playing_ = false;
    seeking_ = false;
    seek_reached_ = nullptr;
    snes_.reset();
    snsf_.reset();
    file_ = file;
    undo_stack_.clear();
    redo_stack_.clear();
    edit_depth_ = 0;
    undo_open_ = false;
    dirty_ = false;
    reload_locked();
    if (!cpu_error_.empty()) {
        loaded_ = false;
        return cpu_error_;
    }
    loaded_ = true;
    return {};
}

void Engine::reload_locked() {
    cpu_error_.clear();
    preview_voice_ = -1;
    preview_tail_ = 0;
    koff_pending_ = false;
    if (snes_) {
        snes_->reset();
    } else {
        blargg_err_t err = spc_.load_spc(file_.data.data(), long(file_.data.size()));
        if (err) {
            cpu_error_ = err;
            return;
        }
        if (clear_echo_on_load_) spc_.clear_echo();
    }
    filter_.clear();
    apply_settings_locked();
    watch_events_.clear();
    watch_has_pending_ = false;
    if (watch_len_) std::memcpy(watch_last_, spc_.ram_mut() + watch_addr_, size_t(watch_len_));
    met_left_ = 0;
    met_last_row_ = INT64_MIN;
    sample_pairs_ = 0;
}

void Engine::apply_settings_locked() {
    spc_.mute_voices(mute_mask_);
    spc_.set_tempo(tempo_);
    filter_.set_gain(gain_);
    filter_.set_bass(bass_);
}

void Engine::play()   { if (loaded_) playing_ = true; }
void Engine::pause() {
    if (seeking_) cancel_seek();
    std::lock_guard<std::mutex> lock(mtx_);
    playing_ = false;
    // A preview's release tail would keep clocking the DSP, and with it every
    // voice of the song, for two seconds after the pause.
    if (preview_voice_ < 0) preview_tail_ = 0;
}
void Engine::toggle() { (playing_ || seeking_) ? pause() : play(); }

void Engine::restart() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!loaded_) return;
    seeking_ = false;
    seek_reached_ = nullptr;
    reload_locked();
}

void Engine::seek(std::function<bool(const uint8_t*, int64_t)> reached, bool from_start, double limit_seconds) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!loaded_) return;
    if (from_start) reload_locked();
    seek_reached_ = std::move(reached);
    seek_limit_ = sample_pairs_ + int64_t(std::max(1.0, limit_seconds) * kSampleRate);
    seeking_ = true;
}

void Engine::cancel_seek() {
    std::lock_guard<std::mutex> lock(mtx_);
    seeking_ = false;
    seek_reached_ = nullptr;
}

bool Engine::seek_step_locked() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(12);
    int16_t scratch[64 * 2];
    while (std::chrono::steady_clock::now() < deadline) {
        for (int k = 0; k < 64; ++k) {
            blargg_err_t err = run_locked(64, scratch);
            if (err) { cpu_error_ = err; return true; }
            sample_pairs_ += 64;
            if (seek_reached_ && seek_reached_(spc_.ram_mut(), sample_pairs_)) return true;
            if (sample_pairs_ >= seek_limit_) return true;
        }
    }
    return false;
}

void Engine::set_mute_mask(int mask) {
    std::lock_guard<std::mutex> lock(mtx_);
    mute_mask_ = mask & 0xFF;
    spc_.mute_voices(mute_mask_);
}

void Engine::preview_on(int voice, const uint8_t regs[8]) {
    if (!loaded_ || voice < 0 || voice > 7) return;
    std::lock_guard<std::mutex> lock(mtx_);
    SPC_DSP& dsp = spc_.dsp_mut();
    if (preview_voice_ >= 0 && preview_voice_ != voice) dsp.write(0x5C, 1 << preview_voice_);
    for (int i = 0; i < 8; ++i) dsp.write(voice * 0x10 + i, regs[i]);
    if (playing_) dsp.write(0x5C, 0);
    else { dsp.write(0x5C, uint8_t(~(1 << voice))); koff_pending_ = true; }
    dsp.write(0x4C, 1 << voice);
    preview_voice_ = voice;
    preview_tail_ = 0;
}

void Engine::preview_off() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (preview_voice_ < 0) return;
    spc_.dsp_mut().write(0x5C, 1 << preview_voice_);
    koff_pending_ = true;
    preview_voice_ = -1;
    preview_tail_ = kSampleRate * 2;
}

void Engine::set_tempo(int tempo) {
    std::lock_guard<std::mutex> lock(mtx_);
    tempo_ = std::clamp(tempo, 0x10, 0x400);
    spc_.set_tempo(tempo_);
    if (snes_) snes_->set_tempo(tempo_);
}

void Engine::set_filter(bool enabled, int gain, int bass) {
    std::lock_guard<std::mutex> lock(mtx_);
    filter_enabled_ = enabled;
    gain_ = std::clamp(gain, 0, 0x400);
    bass_ = std::clamp(bass, int(SPC_Filter::bass_none), int(SPC_Filter::bass_max));
    filter_.set_gain(gain_);
    filter_.set_bass(bass_);
}

std::string Engine::last_cpu_error() const { return cpu_error_; }

void Engine::snapshot(EngineSnapshot& out) {
    std::lock_guard<std::mutex> lock(mtx_);
    out.loaded = loaded_;
    if (!loaded_) return;
    const SPC_DSP& dsp = spc_.dsp_ref();
    for (int i = 0; i < SPC_DSP::register_count; ++i) out.dsp[i] = uint8_t(dsp.read(i));
    if (snes_) {
        std::memset(out.ram, 0, sizeof out.ram);
        for (uint32_t a = 0; a < 0x2000; ++a) out.ram[a] = snsf_read(a, true);
        for (uint32_t a = 0x8000; a < 0x10000; ++a) out.ram[a] = snsf_read(a, true);
        out.snes_rom = snes_->rom_shared();
        out.snes_bank = bank_window_;
    } else {
        std::memcpy(out.ram, spc_.ram(), sizeof out.ram);
        out.snes_rom.reset();
        out.snes_bank = -1;
    }
    out.cpu.pc  = spc_.cpu_pc();
    out.cpu.a   = spc_.cpu_a();
    out.cpu.x   = spc_.cpu_x();
    out.cpu.y   = spc_.cpu_y();
    out.cpu.sp  = spc_.cpu_sp();
    out.cpu.psw = spc_.cpu_psw();
    // Between callbacks the position advances on the wall clock, bounded by
    // what the last callback rendered ahead.
    out.sample_pairs = sample_pairs_;
    if (playing_ && !seeking_ && cb_time_us_ > 0 && cb_samples_ == sample_pairs_) {
        const int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const int64_t ahead = std::clamp<int64_t>((now - cb_time_us_) * kSampleRate / 1000000, 0, cb_frames_);
        out.sample_pairs = cb_samples_ + ahead;
    }
}

void Engine::sdl_callback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<Engine*>(userdata);
    int16_t* out = reinterpret_cast<int16_t*>(stream);
    int frames = len / int(2 * sizeof(int16_t));
    self->render(out, frames);
    self->cb_samples_ = self->sample_pairs_.load();
    self->cb_frames_ = frames;
    self->cb_time_us_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    std::lock_guard<std::mutex> lock(self->mtx_);
    if (self->capture_)
        for (int i = 0; i < frames; ++i) self->capture_->push_back(int16_t((out[i * 2] + out[i * 2 + 1]) / 2));
}

void Engine::render(int16_t* out, int frames) {
    const int count = frames * 2;
    std::lock_guard<std::mutex> lock(mtx_);

    if (!loaded_) {
        std::memset(out, 0, size_t(count) * sizeof(int16_t));
        return;
    }
    if (seeking_) {
        std::memset(out, 0, size_t(count) * sizeof(int16_t));
        if (seek_step_locked()) {
            seeking_ = false;
            seek_reached_ = nullptr;
            watch_events_.clear();
            watch_has_pending_ = false;
            if (watch_len_) std::memcpy(watch_last_, spc_.ram_mut() + watch_addr_, size_t(watch_len_));
            met_last_row_ = INT64_MIN;
            playing_ = cpu_error_.empty();
        }
        return;
    }
    if (!playing_) {
        if (preview_voice_ < 0 && preview_tail_ <= 0) {
            std::memset(out, 0, size_t(count) * sizeof(int16_t));
            return;
        }
        spc_.run_dsp_only(count, out);
        if (koff_pending_) { spc_.dsp_mut().write(0x5C, 0); koff_pending_ = false; }
        if (preview_voice_ < 0) preview_tail_ -= frames;
        if (filter_enabled_) filter_.run(out, count);
        return;
    }

    const int total_ms = file_.total_ms();
    const double pos_ms = position_seconds() * 1000.0;
    if (total_ms > 0 && pos_ms >= total_ms) {
        if (loop_) {
            reload_locked();
        } else {
            playing_ = false;
            std::memset(out, 0, size_t(count) * sizeof(int16_t));
            return;
        }
    }

    blargg_err_t err = nullptr;
    const int slice = watch_len_ ? 64 : frames;   // 2 ms slices while watching pointers
    for (int done = 0; done < frames && !err; done += slice) {
        const int n = std::min(slice, frames - done);
        err = run_locked(n, out + done * 2);
        if (!err && watch_len_) watch_check_locked(sample_pairs_ + done + n);
    }
    if (koff_pending_) { spc_.dsp_mut().write(0x5C, 0); koff_pending_ = false; }
    if (err) {
        cpu_error_ = err;
        playing_ = false;
        std::memset(out, 0, size_t(count) * sizeof(int16_t));
        return;
    }
    if (filter_enabled_) {
        filter_.run(out, count);
    } else if (gain_ != SPC_Filter::gain_unit) {
        for (int i = 0; i < count; ++i)
            out[i] = int16_t(std::clamp((out[i] * gain_) >> 8, -32768, 32767));
    }

    if (total_ms > 0 && file_.fade_ms > 0) {
        const double fade_start = double(file_.intro_ms);
        if (pos_ms + (1000.0 * frames / kSampleRate) > fade_start) {
            for (int i = 0; i < frames; ++i) {
                double t = pos_ms + 1000.0 * i / kSampleRate;
                double g = 1.0 - (t - fade_start) / file_.fade_ms;
                g = std::clamp(g, 0.0, 1.0);
                out[i * 2]     = int16_t(out[i * 2] * g);
                out[i * 2 + 1] = int16_t(out[i * 2 + 1] * g);
            }
        }
    }

    mix_metronome_locked(out, frames, sample_pairs_);
    sample_pairs_ += frames;
}

void Engine::set_watch(uint16_t addr, int len) {
    std::lock_guard<std::mutex> lock(mtx_);
    len = std::clamp(len, 0, std::min(32, 0x10000 - int(addr)));
    if (addr == watch_addr_ && len == watch_len_) return;
    watch_addr_ = addr;
    watch_len_ = len;
    watch_events_.clear();
    watch_has_pending_ = false;
    if (loaded_ && len) live_bytes(addr, len, watch_last_);
}

void Engine::take_watch(std::vector<WatchEvent>& out) {
    std::lock_guard<std::mutex> lock(mtx_);
    out.insert(out.end(), watch_events_.begin(), watch_events_.end());
    watch_events_.clear();
}

void Engine::watch_check_locked(int64_t sample) {
    uint8_t cur[32];
    live_bytes(watch_addr_, watch_len_, cur);
    const uint8_t* ram = cur;
    if (watch_has_pending_) {
        if (std::memcmp(ram, watch_pending_.bytes, size_t(watch_len_)) == 0) {
            std::memcpy(watch_last_, watch_pending_.bytes, size_t(watch_len_));
            watch_has_pending_ = false;
            const SPC_DSP& dsp = spc_.dsp_mut();
            for (int v = 0; v < 8; ++v) for (int r = 0; r < 8; ++r) watch_pending_.regs[v][r] = uint8_t(dsp.read(v * 0x10 + r));
            if (watch_events_.size() < 1024) watch_events_.push_back(watch_pending_);
            return;
        }
    } else if (std::memcmp(ram, watch_last_, size_t(watch_len_)) == 0) {
        return;
    }
    watch_pending_.sample = sample;
    std::memcpy(watch_pending_.bytes, ram, sizeof watch_pending_.bytes);
    watch_has_pending_ = true;
}

void Engine::set_metronome(const Metronome& m) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!m.on) { met_.on = false; met_left_ = 0; return; }
    if (m.samples_per_row > 0) {
        int64_t row_now = int64_t(std::floor(m.ref_row + double(sample_pairs_ - m.ref_samples) / m.samples_per_row));
        if (!met_.on || row_now < met_last_row_) met_last_row_ = row_now;
    }
    met_ = m;
}

void Engine::mix_metronome_locked(int16_t* out, int frames, int64_t first_sample) {
    if (!met_.on || met_.samples_per_row <= 0) return;
    const int hi1 = std::max(1, met_.hi1), hi2 = std::max(1, met_.hi2);
    for (int i = 0; i < frames; ++i) {
        const int64_t s = first_sample + i;
        const int64_t row = int64_t(std::floor(met_.ref_row + double(s - met_.ref_samples) / met_.samples_per_row));
        if (row > met_last_row_) {
            met_last_row_ = row;
            static const bool dbg = std::getenv("BOOMSPC_DEBUG_SYNC") != nullptr;
            if (dbg && (row % hi1 == 0)) std::fprintf(stderr, "click row %lld sample %lld\n", (long long)row, (long long)s);
            if (row % hi2 == 0)      { met_gain_ = 0.55f * met_.volume; met_left_ = kSampleRate / 80; }
            else if (row % hi1 == 0) { met_gain_ = 0.18f * met_.volume; met_left_ = kSampleRate / 200; }
        }
        if (met_left_ > 0) {
            met_rng_ = met_rng_ * 1664525u + 1013904223u;
            const float n = (float(int32_t(met_rng_)) / 2147483648.0f) * met_gain_ * 32767.0f;
            out[i * 2]     = int16_t(std::clamp(int(out[i * 2]) + int(n), -32768, 32767));
            out[i * 2 + 1] = int16_t(std::clamp(int(out[i * 2 + 1]) + int(n), -32768, 32767));
            met_gain_ *= 0.9985f;
            --met_left_;
        }
    }
}

void Engine::write_ram(uint16_t addr, const uint8_t* data, size_t n, WriteTarget target) {
    if (!loaded_ || n == 0) return;
    std::lock_guard<std::mutex> lock(mtx_);
    Patch p{addr, {}, {}, target};
    p.before.resize(n);
    p.after.assign(data, data + n);
    uint8_t* live = spc_.ram_mut();
    for (size_t i = 0; i < n; ++i) {
        uint16_t a = uint16_t(addr + i);
        if (snes_) { p.before[i] = snsf_read(a, target != kImageOnly); snsf_write(a, data[i], target); continue; }
        p.before[i] = target == kImageOnly ? file_.data[SpcFile::kRamOffset + a] : live[a];
        if (target != kImageOnly) live[a] = data[i];
        if (target != kLiveOnly) file_.data[SpcFile::kRamOffset + a] = data[i];
    }
    if (edit_depth_ > 0 && undo_open_ && !undo_stack_.empty()) {
        undo_stack_.back().patches.push_back(std::move(p));
    } else {
        undo_stack_.push_back(UndoEntry{{std::move(p)}});
        undo_open_ = edit_depth_ > 0;
    }
    if (undo_stack_.size() > 512) undo_stack_.erase(undo_stack_.begin());
    redo_stack_.clear();
    dirty_ = true;
}

void Engine::begin_edit() { ++edit_depth_; undo_open_ = false; }
void Engine::end_edit() { if (edit_depth_ > 0) --edit_depth_; if (edit_depth_ == 0) undo_open_ = false; }

void Engine::apply_locked(const Patch& p, bool forward) {
    const std::vector<uint8_t>& src = forward ? p.after : p.before;
    uint8_t* live = spc_.ram_mut();
    for (size_t i = 0; i < src.size(); ++i) {
        uint16_t a = uint16_t(p.addr + i);
        if (snes_) { snsf_write(a, src[i], p.target); continue; }
        if (p.target != kImageOnly) live[a] = src[i];
        if (p.target != kLiveOnly) file_.data[SpcFile::kRamOffset + a] = src[i];
    }
}

bool Engine::undo() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (undo_stack_.empty()) return false;
    UndoEntry u = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    for (auto it = u.patches.rbegin(); it != u.patches.rend(); ++it) apply_locked(*it, false);
    redo_stack_.push_back(std::move(u));
    undo_open_ = false;
    return true;
}

bool Engine::redo() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (redo_stack_.empty()) return false;
    UndoEntry u = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    for (const Patch& p : u.patches) apply_locked(p, true);
    undo_stack_.push_back(std::move(u));
    undo_open_ = false;
    return true;
}

std::string Engine::export_spc(const std::string& path) {
    std::vector<uint8_t> copy;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!loaded_) return "nothing loaded";
        if (snes_) return "an SNSF set has no SPC image to export (the sequencer runs on the SNES side)";
        copy = file_.data;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) return "could not open " + path;
    out.write(reinterpret_cast<const char*>(copy.data()), std::streamsize(copy.size()));
    if (!out) return "write failed";
    dirty_ = false;
    return {};
}

std::vector<uint8_t> Engine::file_image() {
    std::lock_guard<std::mutex> lock(mtx_);
    return file_.data;
}

std::string Engine::export_wav(const std::string& path, double seconds) {
    std::vector<uint8_t> copy;
    int fade_ms = 0, intro_ms = 0;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!loaded_) return "nothing loaded";
        if (snes_) return "WAV export of SNSF sets is not supported yet (use --record)";
        copy = file_.data;
        fade_ms = file_.fade_ms;
        intro_ms = file_.intro_ms;
    }
    if (seconds <= 0) seconds = intro_ms > 0 ? (intro_ms + fade_ms) / 1000.0 : 60.0;

    SNES_SPC spc;
    SPC_Filter filter;
    if (spc.init()) return "snes_spc init failed";
    if (blargg_err_t e = spc.load_spc(copy.data(), long(copy.size()))) return e;
    spc.clear_echo();
    filter.set_gain(gain_);
    filter.set_bass(bass_);

    const int frames = int(seconds * kSampleRate);
    std::vector<int16_t> pcm(size_t(frames) * 2);
    for (int i = 0; i < frames * 2; ) {
        int n = std::min(4096, frames * 2 - i);
        if (blargg_err_t e = spc.play(n, pcm.data() + i)) return e;
        if (filter_enabled_) filter.run(pcm.data() + i, n);
        i += n;
    }
    if (intro_ms > 0 && fade_ms > 0) {
        for (int i = 0; i < frames; ++i) {
            double t = 1000.0 * i / kSampleRate;
            if (t <= intro_ms) continue;
            double g = std::clamp(1.0 - (t - intro_ms) / fade_ms, 0.0, 1.0);
            pcm[size_t(i) * 2] = int16_t(pcm[size_t(i) * 2] * g);
            pcm[size_t(i) * 2 + 1] = int16_t(pcm[size_t(i) * 2 + 1] * g);
        }
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) return "could not open " + path;
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<char*>(&v), 2); };
    const uint32_t bytes = uint32_t(pcm.size() * 2);
    out.write("RIFF", 4); u32(36 + bytes); out.write("WAVE", 4);
    out.write("fmt ", 4); u32(16); u16(1); u16(2); u32(kSampleRate); u32(kSampleRate * 4); u16(4); u16(16);
    out.write("data", 4); u32(bytes);
    out.write(reinterpret_cast<const char*>(pcm.data()), bytes);
    return out ? std::string{} : "write failed";
}
