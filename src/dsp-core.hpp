// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  dsp-core.hpp  [Layer 1]  Pure C++20 real-time DSP engine for all 5 units.
//
//  Wait-free real-time contract (enforced by construction):
//    * process_block() never allocates, never locks, never touches the OS.
//      Every buffer is sized in prepare() for kMaxSampleRate / kMaxBlock.
//    * All UI <-> audio communication goes through std::atomic<> members.
//    * FTZ/DAZ is armed at the top of every block (x86_64 MXCSR, ARM64 FPCR).
//    * A 20 Hz one-pole DC blocker is the very first stage on every channel.
//    * Up to 8 channels. On 6/8-channel input index 3 (LFE) bypasses every
//      HPF / notch and only receives the shared limiter gain.
//    * Sample-rate changes are detected per block and every coefficient /
//      delay length is recomputed and cleared immediately (no allocation).
//    * The final stage replaces non-finite samples with 0 and clamps to ±1.
// =============================================================================
#ifndef SEVENTHWELL_MODEL600_DSP_CORE_HPP
#define SEVENTHWELL_MODEL600_DSP_CORE_HPP

#include "shared-state.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#  include <xmmintrin.h>
#  include <pmmintrin.h>
#  define M600_ARCH_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#  define M600_ARCH_ARM64 1
#endif

namespace sw::model600::dsp {

// -----------------------------------------------------------------------------
//  Limits & constants
// -----------------------------------------------------------------------------
inline constexpr int   kMaxChannels   = 8;
inline constexpr int   kMaxSampleRate = 192000;
inline constexpr int   kMaxBlock      = 8192;      // Larger blocks are chunked.
inline constexpr float kPi            = 3.14159265358979323846f;
inline constexpr float kTwoPi         = 6.28318530717958647692f;
inline constexpr float kSilenceDb     = -120.0f;

enum CharacterMode : int { CHAR_SOFT_ASMR = 0, CHAR_BROADCAST = 1, CHAR_FPS_LOUD = 2 };
enum SpaceFxMode   : int { FX_DRY = 0, FX_KARAOKE = 1, FX_BATH = 2, FX_HALL = 3, FX_CAVE = 4, FX_RADIO = 5 };
enum SceneMacro    : int { SCENE_FPS_GAME = 0, SCENE_TALK = 1, SCENE_SINGING = 2, SCENE_ASMR = 3 };

inline float clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
inline float db_to_lin(float db) noexcept { return m600_db_to_lin(db); }
inline float lin_to_db(float lin) noexcept { return m600_lin_to_db(lin); }
inline float sanitize(float x) noexcept {
    if (!std::isfinite(x)) return 0.0f;
    return clampf(x, -1.0f, 1.0f);
}
// Time constant (ms) -> one-pole coefficient for the given rate.
inline float tc_coeff(float ms, float fs) noexcept {
    if (ms <= 0.0f) return 1.0f;
    return 1.0f - std::exp(-1000.0f / (ms * fs));
}

// -----------------------------------------------------------------------------
//  Denormal guard: FTZ/DAZ on x86_64, FZ bit (24) in FPCR on ARM64.
// -----------------------------------------------------------------------------
class DenormalGuard {
public:
    DenormalGuard() noexcept {
#if defined(M600_ARCH_X86)
        saved_ = _mm_getcsr();
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
        _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif defined(M600_ARCH_ARM64)
#  if defined(_MSC_VER)
        saved_ = _ReadStatusReg(ARM64_FPCR);
        _WriteStatusReg(ARM64_FPCR, saved_ | (1ull << 24));
#  else
        std::uint64_t fpcr = 0;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        saved_ = fpcr;
        fpcr |= (1ull << 24); // FZ: flush denormals to zero.
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#  endif
#endif
    }
    ~DenormalGuard() noexcept {
#if defined(M600_ARCH_X86)
        _mm_setcsr(saved_);
#elif defined(M600_ARCH_ARM64)
#  if defined(_MSC_VER)
        _WriteStatusReg(ARM64_FPCR, saved_);
#  else
        __asm__ __volatile__("msr fpcr, %0" : : "r"(saved_));
#  endif
#endif
    }
    DenormalGuard(const DenormalGuard&) = delete;
    DenormalGuard& operator=(const DenormalGuard&) = delete;
private:
#if defined(M600_ARCH_X86)
    unsigned int saved_ = 0;
#elif defined(M600_ARCH_ARM64)
    std::uint64_t saved_ = 0;
#endif
};

// -----------------------------------------------------------------------------
//  One-pole high-pass (DC blocker, 20 Hz).
// -----------------------------------------------------------------------------
class OnePoleHP {
public:
    void set(float cutoff_hz, float fs) noexcept {
        const float x = std::exp(-kTwoPi * cutoff_hz / fs);
        a_ = x;
        clear();
    }
    void clear() noexcept { x1_ = 0.0f; y1_ = 0.0f; }
    float process(float x) noexcept {
        const float y = x - x1_ + a_ * y1_;
        x1_ = x; y1_ = y;
        return y;
    }
private:
    float a_ = 0.9974f, x1_ = 0.0f, y1_ = 0.0f;
};

// -----------------------------------------------------------------------------
//  Cytomic (Andrew Simper) trapezoidal-integration State Variable Filter.
//  Coefficient changes are zipper-free because the integrator states carry
//  the filter energy independently of the coefficients.
// -----------------------------------------------------------------------------
class SVF {
public:
    enum Type : int { LOWPASS, HIGHPASS, BANDPASS, NOTCH, PEAK, BELL, LOWSHELF, HIGHSHELF };

    void set(Type type, float cutoff_hz, float q, float gain_db, float fs) noexcept {
        type_ = type;
        cutoff_hz = clampf(cutoff_hz, 10.0f, fs * 0.45f);
        q = (q < 0.05f) ? 0.05f : q;
        const float A = std::pow(10.0f, gain_db / 40.0f);
        float g = std::tan(kPi * cutoff_hz / fs);
        float k = 1.0f / q;
        switch (type_) {
            case BELL:      k = 1.0f / (q * A); break;
            case LOWSHELF:  g /= std::sqrt(A); break;
            case HIGHSHELF: g *= std::sqrt(A); break;
            default: break;
        }
        a1_ = 1.0f / (1.0f + g * (g + k));
        a2_ = g * a1_;
        a3_ = g * a2_;
        switch (type_) {
            case LOWPASS:   m0_ = 0.0f; m1_ = 0.0f; m2_ = 1.0f; break;
            case HIGHPASS:  m0_ = 1.0f; m1_ = -k;   m2_ = -1.0f; break;
            case BANDPASS:  m0_ = 0.0f; m1_ = k;    m2_ = 0.0f; break; // Unity-peak (normalized) band.
            case NOTCH:     m0_ = 1.0f; m1_ = -k;   m2_ = 0.0f; break;
            case PEAK:      m0_ = 1.0f; m1_ = -k;   m2_ = -2.0f; break;
            case BELL:      m0_ = 1.0f; m1_ = k * (A * A - 1.0f); m2_ = 0.0f; break;
            case LOWSHELF:  m0_ = 1.0f; m1_ = k * (A - 1.0f); m2_ = A * A - 1.0f; break;
            case HIGHSHELF: m0_ = A * A; m1_ = k * (1.0f - A) * A; m2_ = 1.0f - A * A; break;
        }
    }
    void clear() noexcept { ic1eq_ = 0.0f; ic2eq_ = 0.0f; }
    float process(float v0) noexcept {
        const float v3 = v0 - ic2eq_;
        const float v1 = a1_ * ic1eq_ + a2_ * v3;
        const float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;
        ic1eq_ = 2.0f * v1 - ic1eq_;
        ic2eq_ = 2.0f * v2 - ic2eq_;
        return m0_ * v0 + m1_ * v1 + m2_ * v2;
    }
private:
    Type  type_ = LOWPASS;
    float a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float m0_ = 0.0f, m1_ = 0.0f, m2_ = 1.0f;
    float ic1eq_ = 0.0f, ic2eq_ = 0.0f;
};

// -----------------------------------------------------------------------------
//  Generic biquad (Direct Form II transposed) for the BS.1770 K-weighting.
// -----------------------------------------------------------------------------
class Biquad {
public:
    void set(float b0, float b1, float b2, float a1, float a2) noexcept {
        b0_ = b0; b1_ = b1; b2_ = b2; a1_ = a1; a2_ = a2; clear();
    }
    void clear() noexcept { z1_ = 0.0f; z2_ = 0.0f; }
    float process(float x) noexcept {
        const float y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return y;
    }
    // Analytic RBJ-style designs used to retune K-weighting at any rate.
    void design_high_shelf(float f0, float gain_db, float q, float fs) noexcept {
        const float A = std::pow(10.0f, gain_db / 40.0f);
        const float w0 = kTwoPi * f0 / fs;
        const float cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.0f * q);
        const float sqA2a = 2.0f * std::sqrt(A) * alpha;
        const float b0 =      A * ((A + 1.0f) + (A - 1.0f) * cw + sqA2a);
        const float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw);
        const float b2 =      A * ((A + 1.0f) + (A - 1.0f) * cw - sqA2a);
        const float a0 =           (A + 1.0f) - (A - 1.0f) * cw + sqA2a;
        const float a1 =  2.0f *   ((A - 1.0f) - (A + 1.0f) * cw);
        const float a2 =           (A + 1.0f) - (A - 1.0f) * cw - sqA2a;
        set(b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0);
    }
    void design_high_pass(float f0, float q, float fs) noexcept {
        const float w0 = kTwoPi * f0 / fs;
        const float cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.0f * q);
        const float b0 =  (1.0f + cw) * 0.5f;
        const float b1 = -(1.0f + cw);
        const float b2 =  (1.0f + cw) * 0.5f;
        const float a0 =  1.0f + alpha;
        const float a1 = -2.0f * cw;
        const float a2 =  1.0f - alpha;
        set(b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0);
    }
private:
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float z1_ = 0.0f, z2_ = 0.0f;
};

// -----------------------------------------------------------------------------
//  Envelope follower with independent attack / release (ms).
// -----------------------------------------------------------------------------
class Envelope {
public:
    void set(float attack_ms, float release_ms, float fs) noexcept {
        att_ = tc_coeff(attack_ms, fs);
        rel_ = tc_coeff(release_ms, fs);
    }
    void clear() noexcept { env_ = 0.0f; }
    float process(float x) noexcept {
        const float a = std::fabs(x);
        env_ += ((a > env_) ? att_ : rel_) * (a - env_);
        return env_;
    }
    float value() const noexcept { return env_; }
    void set_value(float v) noexcept { env_ = v; }
private:
    float att_ = 0.01f, rel_ = 0.001f, env_ = 0.0f;
};

// -----------------------------------------------------------------------------
//  Linear slew limiter with a maximum rate in units/second.
// -----------------------------------------------------------------------------
class Slew {
public:
    void set(float max_rate_per_s, float fs) noexcept { step_ = max_rate_per_s / fs; }
    void reset(float v) noexcept { v_ = v; }
    float process(float target) noexcept {
        const float d = target - v_;
        if (d > step_) v_ += step_;
        else if (d < -step_) v_ -= step_;
        else v_ = target;
        return v_;
    }
    float value() const noexcept { return v_; }
private:
    float step_ = 1.0f, v_ = 0.0f;
};

// -----------------------------------------------------------------------------
//  ITU-R BS.1770-4 loudness meter (K-weighting, 400 ms / 3 s windows, gated
//  integrated loudness via a fixed histogram so no allocation ever happens).
// -----------------------------------------------------------------------------
class LoudnessMeter {
public:
    static constexpr int kHistBins   = 760;     // -70 .. +6 LUFS, 0.1 LU bins.
    static constexpr int kHopMs      = 100;
    static constexpr int kShortHops  = 30;      // 3 s
    static constexpr int kMomentHops = 4;       // 400 ms

    void prepare(float fs) noexcept {
        fs_ = fs;
        for (int c = 0; c < kMaxChannels; ++c) {
            shelf_[c].design_high_shelf(1681.974450955533f, 3.999843853973347f, 0.7071752369554196f, fs);
            hp_[c].design_high_pass(38.13547087602444f, 0.5003270373238773f, fs);
        }
        hop_len_ = static_cast<int>(fs * (kHopMs / 1000.0f));
        if (hop_len_ < 1) hop_len_ = 1;
        reset();
    }
    void reset() noexcept {
        hop_pos_ = 0; hop_sum_ = 0.0; hop_write_ = 0; hops_filled_ = 0;
        for (auto& v : hop_ring_) v = 0.0;
        for (auto& b : hist_) b = 0;
        hist_count_ = 0;
        momentary_ = kSilenceDb; short_term_ = kSilenceDb; integrated_ = kSilenceDb;
        for (int c = 0; c < kMaxChannels; ++c) { shelf_[c].clear(); hp_[c].clear(); }
    }
    // Feed one frame of `nch` samples (already post-fader).
    void push_frame(const float* frame, int nch) noexcept {
        double e = 0.0;
        const int n = nch < kMaxChannels ? nch : kMaxChannels;
        for (int c = 0; c < n; ++c) {
            if ((nch == 6 || nch == 8) && c == 3) continue; // LFE excluded per BS.1770.
            float y = shelf_[c].process(frame[c]);
            y = hp_[c].process(y);
            const float w = (nch >= 5 && (c == 4 || c == 5)) ? 1.41f : 1.0f; // surround +1.5 dB.
            e += static_cast<double>(y) * static_cast<double>(y) * static_cast<double>(w);
        }
        hop_sum_ += e;
        if (++hop_pos_ >= hop_len_) {
            const double mean = hop_sum_ / static_cast<double>(hop_len_);
            hop_ring_[hop_write_] = mean;
            hop_write_ = (hop_write_ + 1) % kShortHops;
            if (hops_filled_ < kShortHops) ++hops_filled_;
            hop_pos_ = 0; hop_sum_ = 0.0;
            recompute();
        }
    }
    float momentary()  const noexcept { return momentary_; }
    float short_term() const noexcept { return short_term_; }
    float integrated() const noexcept { return integrated_; }

private:
    static float to_lufs(double mean_sq) noexcept {
        return mean_sq > 1.0e-12 ? static_cast<float>(-0.691 + 10.0 * std::log10(mean_sq)) : kSilenceDb;
    }
    void recompute() noexcept {
        // Momentary = mean of last 4 hops, Short-term = last 30 hops.
        double m = 0.0, s = 0.0; int mc = 0, sc = 0;
        for (int i = 0; i < hops_filled_; ++i) {
            const int idx = (hop_write_ - 1 - i + kShortHops * 2) % kShortHops;
            s += hop_ring_[idx]; ++sc;
            if (i < kMomentHops) { m += hop_ring_[idx]; ++mc; }
        }
        momentary_  = mc ? to_lufs(m / mc) : kSilenceDb;
        short_term_ = sc ? to_lufs(s / sc) : kSilenceDb;
        // Integrated: each 400 ms block (75 % overlap) above the -70 absolute
        // gate goes into the histogram; relative gate (-10 LU) applied on read.
        if (hops_filled_ >= kMomentHops) {
            const float bl = momentary_;
            if (bl > -70.0f) {
                int bin = static_cast<int>((bl + 70.0f) * 10.0f);
                if (bin < 0) bin = 0;
                if (bin >= kHistBins) bin = kHistBins - 1;
                ++hist_[bin];
                ++hist_count_;
            }
            integrated_ = compute_integrated();
        }
    }
    float compute_integrated() const noexcept {
        if (hist_count_ == 0) return kSilenceDb;
        // First pass: ungated mean (absolute gate already applied).
        double sum = 0.0; std::uint64_t n = 0;
        for (int b = 0; b < kHistBins; ++b) {
            if (!hist_[b]) continue;
            const double lufs = -70.0 + (b + 0.5) * 0.1;
            sum += std::pow(10.0, (lufs + 0.691) / 10.0) * hist_[b];
            n += hist_[b];
        }
        if (!n) return kSilenceDb;
        const double gate = -0.691 + 10.0 * std::log10(sum / static_cast<double>(n)) - 10.0;
        sum = 0.0; n = 0;
        for (int b = 0; b < kHistBins; ++b) {
            if (!hist_[b]) continue;
            const double lufs = -70.0 + (b + 0.5) * 0.1;
            if (lufs < gate) continue;
            sum += std::pow(10.0, (lufs + 0.691) / 10.0) * hist_[b];
            n += hist_[b];
        }
        return n ? static_cast<float>(-0.691 + 10.0 * std::log10(sum / static_cast<double>(n))) : kSilenceDb;
    }

    float  fs_ = 48000.0f;
    Biquad shelf_[kMaxChannels], hp_[kMaxChannels];
    int    hop_len_ = 4800, hop_pos_ = 0, hop_write_ = 0, hops_filled_ = 0;
    double hop_sum_ = 0.0;
    std::array<double, kShortHops> hop_ring_{};
    std::array<std::uint32_t, kHistBins> hist_{};
    std::uint64_t hist_count_ = 0;
    float  momentary_ = kSilenceDb, short_term_ = kSilenceDb, integrated_ = kSilenceDb;
};

// -----------------------------------------------------------------------------
//  Inter-sample true-peak detector using 4-point cubic (Hermite) interpolation
//  at 3 intermediate positions (4x oversampled estimate).
// -----------------------------------------------------------------------------
class TruePeak {
public:
    void clear() noexcept { x0_ = x1_ = x2_ = x3_ = 0.0f; }
    // Returns the max |value| across the sample and its 3 interpolated points.
    float push(float x) noexcept {
        x0_ = x1_; x1_ = x2_; x2_ = x3_; x3_ = x;
        float peak = std::fabs(x2_);
        for (int i = 1; i < 4; ++i) {
            const float t = static_cast<float>(i) * 0.25f;
            const float v = hermite(x0_, x1_, x2_, x3_, t);
            const float a = std::fabs(v);
            if (a > peak) peak = a;
        }
        return peak;
    }
private:
    static float hermite(float y0, float y1, float y2, float y3, float t) noexcept {
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }
    float x0_ = 0.0f, x1_ = 0.0f, x2_ = 0.0f, x3_ = 0.0f;
};

// -----------------------------------------------------------------------------
//  1.5 ms look-ahead brickwall limiter with ISP detection, program-dependent
//  auto-release (Fairchild 600 lineage: 20 ms for transients < 15 ms, 180 ms
//  for sustained > 50 ms peaks), and an optional 0.0 ms zero-latency mode.
// -----------------------------------------------------------------------------
class LookaheadLimiter {
public:
    static constexpr float kLookaheadMs = 1.5f;
    static constexpr int   kMaxLook = static_cast<int>(kMaxSampleRate * kLookaheadMs / 1000.0f) + 8;

    void prepare(float fs) noexcept {
        fs_ = fs;
        look_ = static_cast<int>(fs * kLookaheadMs / 1000.0f);
        if (look_ < 1) look_ = 1;
        if (look_ > kMaxLook - 1) look_ = kMaxLook - 1;
        win_ = look_ * 2;
        att_coeff_ = 1.0f - std::exp(-5.0f / static_cast<float>(look_)); // ~99% by arrival.
        clear();
    }
    void clear() noexcept {
        for (auto& ch : delay_) ch.fill(0.0f);
        gain_req_.fill(1.0f);
        gain_env_ = 1.0f; wpos_ = 0; over_count_ = 0; rel_coeff_ = tc_coeff(180.0f, fs_);
        deque_head_ = deque_tail_ = 0;
        for (auto& tp : tp_) tp.clear();
        gr_db_ = 0.0f;
    }
    void set_ceiling_db(float db) noexcept { ceiling_ = db_to_lin(db); }
    void set_zero_latency(bool z) noexcept { zero_latency_ = z; }
    int latency_samples() const noexcept { return zero_latency_ ? 0 : look_; }
    float gr_db() const noexcept { return gr_db_; }

    // Process one multi-channel frame in place. `ceiling_scale` is the
    // cooperative Summing Guard factor (0..1). LFE and all channels receive
    // identical gain so the image never shifts.
    void process_frame(float* frame, int nch, float ceiling_scale) noexcept {
        const float ceil = ceiling_ * ceiling_scale;
        // 1. Inter-sample peak across channels for this frame.
        float peak = 0.0f;
        for (int c = 0; c < nch; ++c) {
            const float p = tp_[c].push(frame[c]);
            if (p > peak) peak = p;
        }
        float req = 1.0f;
        if (peak > ceil && peak > 1.0e-9f) req = ceil / peak;

        if (zero_latency_) {
            // No delay: instant attack on the current sample + hard ceiling.
            gain_env_ = req < gain_env_ ? req : gain_env_ + rel_coeff_ * (1.0f - gain_env_);
            update_release(req);
            for (int c = 0; c < nch; ++c) {
                const float v = frame[c] * gain_env_;
                frame[c] = clampf(v, -ceil, ceil);
            }
            gr_db_ = lin_to_db(gain_env_);
            return;
        }

        // 2. Sliding-window minimum of required gain (monotonic deque).
        gain_req_[wpos_] = req;
        while (deque_head_ != deque_tail_ &&
               gain_req_[deque_[(deque_tail_ - 1 + kMaxLook * 2) % (kMaxLook * 2)]] >= req) {
            deque_tail_ = (deque_tail_ - 1 + kMaxLook * 2) % (kMaxLook * 2);
        }
        deque_[deque_tail_] = wpos_;
        deque_tail_ = (deque_tail_ + 1) % (kMaxLook * 2);
        // Evict indices older than win_.
        const int oldest = (wpos_ - win_ + kMaxLook * 2) % (kMaxLook * 2);
        while (deque_head_ != deque_tail_ && ring_dist(deque_[deque_head_], wpos_) >= win_) {
            deque_head_ = (deque_head_ + 1) % (kMaxLook * 2);
        }
        (void)oldest;
        const float win_min = (deque_head_ != deque_tail_) ? gain_req_[deque_[deque_head_]] : 1.0f;

        // 3. Smooth: fast attack reaching the target before the peak arrives,
        //    program-dependent release when the event ends.
        if (win_min < gain_env_) gain_env_ += att_coeff_ * (win_min - gain_env_);
        else                     gain_env_ += rel_coeff_ * (win_min - gain_env_);
        update_release(win_min);

        // 4. Delay the audio by look_ samples and apply the gain.
        const int rpos = (wpos_ - look_ + kMaxLook * 2) % (kMaxLook * 2);
        for (int c = 0; c < nch; ++c) {
            delay_[c][wpos_] = frame[c];
            const float v = delay_[c][rpos] * gain_env_;
            frame[c] = clampf(v, -ceil, ceil); // Safety net for the last 1 %.
        }
        wpos_ = (wpos_ + 1) % (kMaxLook * 2);
        gr_db_ = lin_to_db(gain_env_);
    }

private:
    static int ring_dist(int older, int newer) noexcept {
        return (newer - older + kMaxLook * 2) % (kMaxLook * 2);
    }
    // Track over-event duration and pick 20 ms (< 15 ms transient), 180 ms
    // (>= 50 ms sustained) or an interpolated release in between.
    void update_release(float req) noexcept {
        if (req < 0.999f) {
            ++over_count_;
        } else if (over_count_ > 0) {
            const float dur_ms = 1000.0f * static_cast<float>(over_count_) / fs_;
            float rel_ms;
            if (dur_ms < 15.0f) rel_ms = 20.0f;
            else if (dur_ms >= 50.0f) rel_ms = 180.0f;
            else rel_ms = 20.0f + (dur_ms - 15.0f) / 35.0f * 160.0f;
            rel_coeff_ = tc_coeff(rel_ms, fs_);
            over_count_ = 0;
        }
    }

    float fs_ = 48000.0f;
    int   look_ = 72, win_ = 144, wpos_ = 0;
    float ceiling_ = 0.891f; // -1 dBFS
    bool  zero_latency_ = false;
    float att_coeff_ = 0.1f, rel_coeff_ = 0.001f, gain_env_ = 1.0f, gr_db_ = 0.0f;
    int   over_count_ = 0;
    std::array<std::array<float, kMaxLook * 2>, kMaxChannels> delay_{};
    std::array<float, kMaxLook * 2> gain_req_{};
    std::array<int,   kMaxLook * 2> deque_{};
    int   deque_head_ = 0, deque_tail_ = 0;
    std::array<TruePeak, kMaxChannels> tp_{};
};

// -----------------------------------------------------------------------------
//  Noise gate with 4 dB hysteresis and 120 ms hold. RNNoise-aware relax.
// -----------------------------------------------------------------------------
class NoiseGate {
public:
    void prepare(float fs) noexcept {
        fs_ = fs;
        det_.set(1.0f, 40.0f, fs);
        open_coeff_ = tc_coeff(2.0f, fs);
        close_coeff_ = tc_coeff(60.0f, fs);
        hold_samples_ = static_cast<int>(fs * 0.120f);
        clear();
    }
    void clear() noexcept { det_.clear(); gain_ = 1.0f; hold_ = 0; is_open_ = true; }
    // open_db / close_db: closing threshold = open - 4 dB hysteresis.
    void set_threshold(float open_db, bool upstream_denoiser) noexcept {
        if (upstream_denoiser) open_db -= 8.0f; // RNNoise upstream -> relax gate.
        open_lin_  = db_to_lin(open_db);
        close_lin_ = db_to_lin(open_db - 4.0f);
    }
    void set_range_db(float range_db) noexcept { floor_ = db_to_lin(range_db); }
    float process(float x) noexcept {
        const float env = det_.process(x);
        if (is_open_) {
            if (env < close_lin_) { if (++hold_ > hold_samples_) { is_open_ = false; hold_ = 0; } }
            else hold_ = 0;
        } else if (env > open_lin_) { is_open_ = true; hold_ = 0; }
        const float target = is_open_ ? 1.0f : floor_;
        gain_ += ((target > gain_) ? open_coeff_ : close_coeff_) * (target - gain_);
        return x * gain_;
    }
    bool is_open() const noexcept { return is_open_; }
    float gain() const noexcept { return gain_; }
private:
    float fs_ = 48000.0f;
    Envelope det_;
    float open_lin_ = 0.01f, close_lin_ = 0.0063f, floor_ = 0.0316f; // -30 dB floor
    float open_coeff_ = 0.1f, close_coeff_ = 0.01f, gain_ = 1.0f;
    int hold_samples_ = 5760, hold_ = 0;
    bool is_open_ = true;
};

// -----------------------------------------------------------------------------
//  Split-band smart de-esser (5.5 - 8.5 kHz) with a dynamic bell cut.
// -----------------------------------------------------------------------------
class DeEsser {
public:
    void prepare(float fs) noexcept {
        detect_.set(SVF::BANDPASS, 7000.0f, 1.4f, 0.0f, fs);
        wide_.set(SVF::BANDPASS, 1500.0f, 0.5f, 0.0f, fs);
        env_s_.set(0.3f, 25.0f, fs);
        env_w_.set(0.3f, 25.0f, fs);
        fs_ = fs;
        cut_db_ = 0.0f;
        cut_.set(SVF::BELL, 7000.0f, 1.4f, 0.0f, fs);
        clear();
    }
    void clear() noexcept { detect_.clear(); wide_.clear(); cut_.clear(); env_s_.clear(); env_w_.clear(); cut_db_ = 0.0f; }
    void set_depth_db(float max_cut_db) noexcept { max_cut_ = -std::fabs(max_cut_db); }
    float process(float x) noexcept {
        const float s = env_s_.process(detect_.process(x));
        const float w = env_w_.process(wide_.process(x)) + 1.0e-6f;
        const float ratio = s / w;                      // Sibilance dominance.
        float target = 0.0f;
        if (ratio > 1.2f) target = clampf(-(ratio - 1.2f) * 6.0f, max_cut_, 0.0f);
        cut_db_ += 0.05f * (target - cut_db_);
        if (std::fabs(cut_db_ - last_set_) > 0.1f) {
            cut_.set(SVF::BELL, 7000.0f, 1.4f, cut_db_, fs_);
            last_set_ = cut_db_;
        }
        return cut_.process(x);
    }
private:
    float fs_ = 48000.0f;
    SVF detect_, wide_, cut_;
    Envelope env_s_, env_w_;
    float cut_db_ = 0.0f, last_set_ = 0.0f, max_cut_ = -8.0f;
};

// -----------------------------------------------------------------------------
//  Band-pass centroid voice pitch tracker (800-1600 / 1600-3000 Hz) with a
//  500 Hz/s slew and a speaker-bleed guard (UNIT 03 loud in the same ms).
// -----------------------------------------------------------------------------
class PitchTracker {
public:
    void prepare(float fs) noexcept {
        low_.set(SVF::BANDPASS, 1131.0f, 1.2f, 0.0f, fs);   // ~800..1600
        high_.set(SVF::BANDPASS, 2190.0f, 1.2f, 0.0f, fs);  // ~1600..3000
        form_.set(SVF::BANDPASS, 1118.0f, 0.6f, 0.0f, fs);  // 500..2500 formant
        full_.set(SVF::BANDPASS, 1000.0f, 0.3f, 0.0f, fs);
        env_l_.set(5.0f, 80.0f, fs); env_h_.set(5.0f, 80.0f, fs);
        env_f_.set(5.0f, 80.0f, fs); env_a_.set(5.0f, 80.0f, fs);
        slew_.set(500.0f, fs);
        slew_.reset(1600.0f);
        clear();
    }
    void clear() noexcept { low_.clear(); high_.clear(); form_.clear(); full_.clear();
        env_l_.clear(); env_h_.clear(); env_f_.clear(); env_a_.clear(); activity_ = 0.0f; }
    void process(float x, bool bleed_suspect) noexcept {
        const float l = env_l_.process(low_.process(x));
        const float h = env_h_.process(high_.process(x));
        const float f = env_f_.process(form_.process(x));
        const float a = env_a_.process(full_.process(x)) + 1.0e-7f;
        const float sum = l + h + 1.0e-9f;
        const float centroid = (1200.0f * l + 2300.0f * h) / sum;
        center_hz_ = slew_.process(clampf(centroid, 1200.0f, 2600.0f));
        // Activity: formant dominance over the whole band, gated at -50 dBFS.
        float act = (a > 0.00316f) ? clampf((f / a) * 1.5f, 0.0f, 1.0f) : 0.0f;
        if (bleed_suspect) act *= 0.35f; // Speaker bleed -> damp ducking send.
        activity_ += 0.001f * (act - activity_);
        formant_ratio_ = f / a;
    }
    float center_hz() const noexcept { return center_hz_; }
    float activity() const noexcept { return activity_; }
    float formant_ratio() const noexcept { return formant_ratio_; }
private:
    SVF low_, high_, form_, full_;
    Envelope env_l_, env_h_, env_f_, env_a_;
    Slew slew_;
    float center_hz_ = 1600.0f, activity_ = 0.0f, formant_ratio_ = 0.0f;
};

// -----------------------------------------------------------------------------
//  Pre-limiter space FX: Freeverb-lineage stereo reverb with pre-damping
//  (180 Hz HPF + 4.5 kHz LPF on the send), soft-tail spillover (50 ms send
//  fade, 2 s tail), and a RADIO band-limited voice mode.
// -----------------------------------------------------------------------------
class SpaceFx {
public:
    static constexpr int kCombs = 8, kAllpasses = 4;
    static constexpr int kMaxComb = 2400 * 4;   // Sized for 192 kHz (1557 @ 44.1k * 4.35).
    static constexpr int kMaxAll  = 600 * 4;

    void prepare(float fs) noexcept {
        fs_ = fs;
        // Sizes allocated once; no reallocation on rate change.
        if (combs_[0][0].empty()) {
            for (auto& ch : combs_) for (auto& c : ch) c.assign(kMaxComb, 0.0f);
            for (auto& ch : allp_)  for (auto& a : ch) a.assign(kMaxAll, 0.0f);
        }
        retune();
        clear();
    }
    void retune() noexcept {
        static constexpr int comb_44[kCombs] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static constexpr int all_44[kAllpasses] = {556, 441, 341, 225};
        const float scale = fs_ / 44100.0f;
        for (int c = 0; c < kCombs; ++c) {
            comb_len_[0][c] = std::min(kMaxComb - 1, static_cast<int>(static_cast<float>(comb_44[c]) * scale));
            comb_len_[1][c] = std::min(kMaxComb - 1, static_cast<int>(static_cast<float>(comb_44[c] + 23) * scale));
        }
        for (int a = 0; a < kAllpasses; ++a) {
            all_len_[0][a] = std::min(kMaxAll - 1, static_cast<int>(static_cast<float>(all_44[a]) * scale));
            all_len_[1][a] = std::min(kMaxAll - 1, static_cast<int>(static_cast<float>(all_44[a] + 23) * scale));
        }
        for (int ch = 0; ch < 2; ++ch) {
            pre_hp_[ch].set(SVF::HIGHPASS, 180.0f, 0.707f, 0.0f, fs_);
            pre_lp_[ch].set(SVF::LOWPASS, 4500.0f, 0.707f, 0.0f, fs_);
            radio_hp_[ch].set(SVF::HIGHPASS, 300.0f, 0.9f, 0.0f, fs_);
            radio_lp_[ch].set(SVF::LOWPASS, 3400.0f, 0.9f, 0.0f, fs_);
            radio_mid_[ch].set(SVF::BELL, 1800.0f, 1.0f, 4.0f, fs_);
        }
        send_coeff_ = tc_coeff(50.0f, fs_);
        tail_samples_ = static_cast<int>(fs_ * 2.0f);
        apply_mode();
    }
    void clear() noexcept {
        for (auto& ch : combs_) for (auto& c : ch) std::fill(c.begin(), c.end(), 0.0f);
        for (auto& ch : allp_)  for (auto& a : ch) std::fill(a.begin(), a.end(), 0.0f);
        for (auto& ch : comb_pos_) ch.fill(0);
        for (auto& ch : all_pos_) ch.fill(0);
        for (auto& ch : comb_lp_) ch.fill(0.0f);
        for (int ch = 0; ch < 2; ++ch) { pre_hp_[ch].clear(); pre_lp_[ch].clear();
            radio_hp_[ch].clear(); radio_lp_[ch].clear(); radio_mid_[ch].clear(); }
        send_gain_ = 0.0f; tail_countdown_ = 0;
    }
    void set_mode(int mode) noexcept {
        if (mode == mode_) return;
        mode_ = mode;
        apply_mode();
        if (mode_ == FX_DRY) tail_countdown_ = tail_samples_; // Soft tail spillover.
    }
    int mode() const noexcept { return mode_; }
    bool active() const noexcept { return mode_ != FX_DRY || tail_countdown_ > 0; }

    // In-place stereo (ch 0/1). Extra channels are left dry by the caller.
    void process_frame(float* frame, int nch) noexcept {
        if (!active()) return;
        const float target_send = (mode_ == FX_DRY) ? 0.0f : 1.0f;
        send_gain_ += send_coeff_ * (target_send - send_gain_);
        if (mode_ == FX_DRY && tail_countdown_ > 0) --tail_countdown_;

        if (mode_ == FX_RADIO) {
            for (int ch = 0; ch < std::min(nch, 2); ++ch) {
                float v = radio_hp_[ch].process(frame[ch]);
                v = radio_mid_[ch].process(v);
                v = radio_lp_[ch].process(v);
                v = std::tanh(v * 2.2f) * 0.8f;            // Gentle transistor grit.
                frame[ch] = frame[ch] * (1.0f - send_gain_) + v * send_gain_;
            }
            return;
        }

        const int chs = std::min(nch, 2);
        float in_mono = 0.0f;
        for (int ch = 0; ch < chs; ++ch) in_mono += frame[ch];
        in_mono *= (chs == 2 ? 0.5f : 1.0f);

        for (int ch = 0; ch < 2; ++ch) {
            // Pre-de-ess & damping on the send so sibilance never rings.
            float send = pre_lp_[ch].process(pre_hp_[ch].process(in_mono)) * send_gain_ * 0.015f;
            float acc = 0.0f;
            for (int c = 0; c < kCombs; ++c) {
                float* buf = combs_[ch][c].data();
                const int len = comb_len_[ch][c];
                int& p = comb_pos_[ch][c];
                const float out = buf[p];
                comb_lp_[ch][c] = out * (1.0f - damp_) + comb_lp_[ch][c] * damp_;
                buf[p] = send + comb_lp_[ch][c] * feedback_;
                if (++p >= len) p = 0;
                acc += out;
            }
            for (int a = 0; a < kAllpasses; ++a) {
                float* buf = allp_[ch][a].data();
                const int len = all_len_[ch][a];
                int& p = all_pos_[ch][a];
                const float bufout = buf[p];
                const float v = -acc + bufout;
                buf[p] = acc + bufout * 0.5f;
                if (++p >= len) p = 0;
                acc = v;
            }
            wet_[ch] = acc;
        }
        const float w = wet_level_;
        if (chs == 2) {
            frame[0] += (wet_[0] * (1.0f + width_) + wet_[1] * (1.0f - width_)) * 0.5f * w;
            frame[1] += (wet_[1] * (1.0f + width_) + wet_[0] * (1.0f - width_)) * 0.5f * w;
        } else if (chs == 1) {
            frame[0] += (wet_[0] + wet_[1]) * 0.5f * w;
        }
    }

private:
    void apply_mode() noexcept {
        switch (mode_) {
            case FX_KARAOKE: feedback_ = 0.78f; damp_ = 0.45f; wet_level_ = 0.55f; width_ = 0.6f; break;
            case FX_BATH:    feedback_ = 0.86f; damp_ = 0.25f; wet_level_ = 0.85f; width_ = 0.3f; break;
            case FX_HALL:    feedback_ = 0.91f; damp_ = 0.55f; wet_level_ = 0.70f; width_ = 1.0f; break;
            case FX_CAVE:    feedback_ = 0.95f; damp_ = 0.70f; wet_level_ = 0.95f; width_ = 0.8f; break;
            default:         feedback_ = 0.78f; damp_ = 0.45f; wet_level_ = 0.55f; width_ = 0.6f; break;
        }
    }
    float fs_ = 48000.0f;
    int mode_ = FX_DRY;
    float feedback_ = 0.78f, damp_ = 0.45f, wet_level_ = 0.5f, width_ = 0.6f;
    float send_gain_ = 0.0f, send_coeff_ = 0.01f;
    int tail_samples_ = 96000, tail_countdown_ = 0;
    std::array<std::array<std::vector<float>, kCombs>, 2> combs_{};
    std::array<std::array<std::vector<float>, kAllpasses>, 2> allp_{};
    std::array<std::array<int, kCombs>, 2> comb_len_{}, comb_pos_{};
    std::array<std::array<int, kAllpasses>, 2> all_len_{}, all_pos_{};
    std::array<std::array<float, kCombs>, 2> comb_lp_{};
    float wet_[2] = {0.0f, 0.0f};
    SVF pre_hp_[2], pre_lp_[2], radio_hp_[2], radio_lp_[2], radio_mid_[2];
};

// -----------------------------------------------------------------------------
//  10-second cue-card auto calibration with VAD, silence guard, flat-top clip
//  detection, 90-98 percentile trimmed reference and Lombard offsets.
// -----------------------------------------------------------------------------
class Calibrator {
public:
    static constexpr int kBins = 1200;          // -120..0 dB, 0.1 dB bins.
    static constexpr float kFrameMs = 10.0f;

    // Retune for a new rate. A finished result survives a rate change; a
    // measurement that is still running is aborted (its frames would mix two
    // rates) and the UI is expected to offer a re-run.
    void prepare(float fs) noexcept {
        fs_ = fs;
        frame_len_ = static_cast<int>(fs * kFrameMs / 1000.0f);
        if (frame_len_ < 8) frame_len_ = 8;
        form_.set(SVF::BANDPASS, 1118.0f, 0.6f, 0.0f, fs);
        if (running_) { running_ = false; done_ = false; aborted_ = true; }
        pos_ = 0; peak_ = 0.0f; e_form_ = 0.0; e_all_ = 0.0; flat_run_ = 0;
    }
    bool aborted() const noexcept { return aborted_; }
    void reset() noexcept {
        hist_.fill(0);
        frames_total_ = 0; frames_voice_ = 0; pos_ = 0; peak_ = 0.0f; e_form_ = 0.0; e_all_ = 0.0;
        clip_frames_ = 0; frame_clipped_ = false;
        flat_run_ = 0; clip_events_ = 0; running_ = false; done_ = false; aborted_ = false; form_.clear();
        result_gain_db_ = 0.0f; result_ok_ = false; result_clip_ = false; result_silent_ = false;
        result_ref_db_ = kSilenceDb;
    }
    void start(float duration_s) noexcept {
        reset();
        target_frames_ = static_cast<int>(duration_s * 1000.0f / kFrameMs);
        running_ = true;
    }
    bool running() const noexcept { return running_; }
    bool done() const noexcept { return done_; }
    float progress() const noexcept {
        return target_frames_ ? clampf(static_cast<float>(frames_total_) / static_cast<float>(target_frames_), 0.0f, 1.0f) : 0.0f;
    }
    // Feed raw post-DC-block, pre-gain mono sample (max over channels).
    void push(float x) noexcept {
        if (!running_) return;
        const float a = std::fabs(x);
        if (a > peak_) peak_ = a;
        const float f = form_.process(x);
        e_form_ += static_cast<double>(f) * f;
        e_all_  += static_cast<double>(x) * x;
        // Flat-top: 3+ consecutive samples saturated at >= -0.1 dBFS.
        if (a >= 0.98855f) { if (++flat_run_ == 3) { ++clip_events_; frame_clipped_ = true; } }
        else flat_run_ = 0;
        if (++pos_ >= frame_len_) {
            const float pk_db = lin_to_db(peak_);
            const float rms_db = e_all_ > 0.0 ? static_cast<float>(10.0 * std::log10(e_all_ / frame_len_)) : kSilenceDb;
            const double ratio = e_all_ > 1.0e-12 ? e_form_ / e_all_ : 0.0;
            const bool voiced = rms_db > -50.0f && ratio > 0.20;
            if (voiced) {
                int bin = static_cast<int>((pk_db + 120.0f) * 10.0f);
                bin = bin < 0 ? 0 : (bin >= kBins ? kBins - 1 : bin);
                ++hist_[bin]; ++frames_voice_;
            }
            if (frame_clipped_) ++clip_frames_;
            frame_clipped_ = false;
            ++frames_total_; pos_ = 0; peak_ = 0.0f; e_form_ = 0.0; e_all_ = 0.0;
            if (frames_total_ >= target_frames_) finish();
        }
    }
    // Lombard-aware finish: reference = trimmed mean of the 90-98 percentile.
    void set_character(int character_mode) noexcept { character_ = character_mode; }
    float result_gain_db() const noexcept { return result_gain_db_; }
    float result_reference_db() const noexcept { return result_ref_db_; }
    bool  result_ok() const noexcept { return result_ok_; }
    bool  result_clipped() const noexcept { return result_clip_; }
    bool  result_silent() const noexcept { return result_silent_; }
    int   clip_events() const noexcept { return clip_events_; }
    // Total time (s) of 10 ms frames that contained a flat-top clip.
    float clipped_seconds() const noexcept { return static_cast<float>(clip_frames_) * kFrameMs / 1000.0f; }
    // Peak the calibration aims for: -6 dBFS plus the character's Lombard offset.
    float target_peak_db() const noexcept {
        switch (character_) {
            case CHAR_SOFT_ASMR: return -6.0f;
            case CHAR_BROADCAST: return -7.5f;
            default:             return -9.0f;
        }
    }
    float voice_ratio() const noexcept {
        return frames_total_ ? static_cast<float>(frames_voice_) / static_cast<float>(frames_total_) : 0.0f;
    }

private:
    void finish() noexcept {
        running_ = false; done_ = true;
        result_clip_ = clip_events_ > 0;
        if (voice_ratio() < 0.20f) { result_silent_ = true; result_ok_ = false; return; }
        // Percentile walk over the histogram of voiced-frame peaks.
        const std::uint32_t n = static_cast<std::uint32_t>(frames_voice_);
        const std::uint32_t p90 = static_cast<std::uint32_t>(static_cast<float>(n) * 0.90f);
        const std::uint32_t p98 = static_cast<std::uint32_t>(static_cast<float>(n) * 0.98f);
        std::uint32_t acc = 0; double sum = 0.0; std::uint32_t cnt = 0;
        for (int b = 0; b < kBins; ++b) {
            const std::uint32_t c = hist_[b];
            if (!c) continue;
            const std::uint32_t lo = acc, hi = acc + c;
            // Count how many of this bin's samples fall in [p90, p98).
            const std::uint32_t s = std::max(lo, p90), e = std::min(hi, p98);
            if (e > s) { sum += (-120.0 + (b + 0.5) * 0.1) * (e - s); cnt += (e - s); }
            acc = hi;
        }
        if (!cnt) { result_ok_ = false; return; }
        result_ref_db_ = static_cast<float>(sum / cnt);
        float offset = 0.0f;
        switch (character_) {
            case CHAR_SOFT_ASMR: offset = 0.0f;  break;
            case CHAR_BROADCAST: offset = -1.5f; break;
            default:             offset = -3.0f; break;
        }
        const float target = -6.0f + offset;
        result_gain_db_ = clampf(target - result_ref_db_, -18.0f, 24.0f);
        result_ok_ = true;
    }

    float fs_ = 48000.0f;
    int frame_len_ = 480, pos_ = 0, frames_total_ = 0, frames_voice_ = 0, target_frames_ = 1000;
    float peak_ = 0.0f; double e_form_ = 0.0, e_all_ = 0.0;
    int flat_run_ = 0, clip_events_ = 0, clip_frames_ = 0, character_ = CHAR_BROADCAST;
    bool frame_clipped_ = false;
    bool running_ = false, done_ = false, aborted_ = false;
    std::array<std::uint32_t, kBins> hist_{};
    SVF form_;
    float result_gain_db_ = 0.0f, result_ref_db_ = kSilenceDb;
    bool result_ok_ = false, result_clip_ = false, result_silent_ = false;
};

// -----------------------------------------------------------------------------
//  1 kHz bleep (-18 dBFS, 5 ms cosine fades) and cough mute (fade mute).
// -----------------------------------------------------------------------------
class Bleep {
public:
    void prepare(float fs) noexcept { fs_ = fs; fade_len_ = static_cast<int>(fs * 0.005f); phase_ = 0.0f; remaining_ = 0; }
    void trigger(float seconds) noexcept { remaining_ = static_cast<int>(seconds * fs_); total_ = remaining_; phase_ = 0.0f; }
    void stop() noexcept { if (remaining_ > fade_len_) remaining_ = fade_len_; }
    bool active() const noexcept { return remaining_ > 0; }
    // Returns the bleep sample and advances. Caller mutes the source.
    float next() noexcept {
        if (remaining_ <= 0) return 0.0f;
        float env = 1.0f;
        const int elapsed = total_ - remaining_;
        if (elapsed < fade_len_)        env = 0.5f - 0.5f * std::cos(kPi * static_cast<float>(elapsed) / static_cast<float>(fade_len_));
        else if (remaining_ < fade_len_) env = 0.5f - 0.5f * std::cos(kPi * static_cast<float>(remaining_) / static_cast<float>(fade_len_));
        const float s = std::sin(phase_) * 0.12589f * env; // -18 dBFS
        phase_ += kTwoPi * 1000.0f / fs_;
        if (phase_ >= kTwoPi) phase_ -= kTwoPi;
        --remaining_;
        return s;
    }
    float env_mute() const noexcept {
        if (remaining_ <= 0) return 1.0f;
        const int elapsed = total_ - remaining_;
        if (elapsed < fade_len_)         return 0.5f + 0.5f * std::cos(kPi * static_cast<float>(elapsed) / static_cast<float>(fade_len_));
        if (remaining_ < fade_len_)      return 0.5f + 0.5f * std::cos(kPi * static_cast<float>(remaining_) / static_cast<float>(fade_len_));
        return 0.0f;
    }
private:
    float fs_ = 48000.0f, phase_ = 0.0f;
    int fade_len_ = 240, remaining_ = 0, total_ = 0;
};

class CoughMute {
public:
    void prepare(float fs) noexcept { coeff_ = tc_coeff(30.0f, fs); fs_ = fs; gain_ = 1.0f; hold_ = 0; held_ = false; }
    void press(bool held_down, float seconds = 3.0f) noexcept {
        held_ = held_down;
        if (held_down) hold_ = -1; else hold_ = static_cast<int>(seconds * fs_);
    }
    void release() noexcept { held_ = false; if (hold_ < 0) hold_ = 0; }
    bool active() const noexcept { return held_ || hold_ > 0; }
    float next() noexcept {
        const bool muting = held_ || hold_ > 0;
        if (hold_ > 0) --hold_;
        gain_ += coeff_ * ((muting ? 0.0f : 1.0f) - gain_);
        return gain_;
    }
private:
    float fs_ = 48000.0f, coeff_ = 0.01f, gain_ = 1.0f;
    int hold_ = 0; bool held_ = false;
};

// -----------------------------------------------------------------------------
//  3-second Record & Loop A/B audition. Pre-allocated stereo buffers.
// -----------------------------------------------------------------------------
class AuditionRecorder {
public:
    static constexpr int kSeconds = 3;
    enum State : int { IDLE = 0, RECORDING = 1, PLAY_DRY = 2, PLAY_WET = 3 };

    void prepare() noexcept {
        if (dry_.empty()) {
            dry_.assign(static_cast<std::size_t>(kMaxSampleRate) * kSeconds * 2, 0.0f);
            wet_.assign(static_cast<std::size_t>(kMaxSampleRate) * kSeconds * 2, 0.0f);
        }
        state_.store(IDLE, std::memory_order_relaxed); pos_ = 0; len_ = 0;
    }
    void start_record(float fs) noexcept { len_ = static_cast<int>(fs) * kSeconds; pos_ = 0; state_.store(RECORDING, std::memory_order_release); }
    void start_loop() noexcept { pos_ = 0; state_.store(PLAY_DRY, std::memory_order_release); }
    void stop() noexcept { state_.store(IDLE, std::memory_order_release); }
    int state() const noexcept { return state_.load(std::memory_order_acquire); }
    bool capturing() const noexcept { return state() == RECORDING; }
    bool playing() const noexcept { const int s = state(); return s == PLAY_DRY || s == PLAY_WET; }
    void capture(float dry_l, float dry_r, float wet_l, float wet_r) noexcept {
        if (state() != RECORDING || pos_ >= len_) { if (pos_ >= len_ && state() == RECORDING) state_.store(IDLE, std::memory_order_release); return; }
        const std::size_t i = static_cast<std::size_t>(pos_) * 2;
        dry_[i] = dry_l; dry_[i + 1] = dry_r; wet_[i] = wet_l; wet_[i + 1] = wet_r;
        ++pos_;
    }
    // Overwrites the frame with the looped audition (alternating DRY / WET).
    void playback(float* frame, int nch) noexcept {
        const int s = state();
        if ((s != PLAY_DRY && s != PLAY_WET) || len_ <= 0) return;
        const std::vector<float>& src = (s == PLAY_DRY) ? dry_ : wet_;
        const std::size_t i = static_cast<std::size_t>(pos_) * 2;
        const float l = src[i], r = src[i + 1];
        if (nch >= 2) { frame[0] = l; frame[1] = r; for (int c = 2; c < nch; ++c) frame[c] = 0.0f; }
        else frame[0] = (l + r) * 0.5f;
        if (++pos_ >= len_) { pos_ = 0; state_.store(s == PLAY_DRY ? PLAY_WET : PLAY_DRY, std::memory_order_release); }
    }
private:
    std::vector<float> dry_, wet_;
    std::atomic<int> state_{IDLE};
    int pos_ = 0, len_ = 0;
};

// -----------------------------------------------------------------------------
//  SPSC ring buffer tap: Pre-FX (post-comp dry) mono audio for the speech
//  recognizer. Producer = audio thread, consumer = recognizer thread.
// -----------------------------------------------------------------------------
class PreFxTap {
public:
    static constexpr int kCapacity = 1 << 17; // 131072 samples (~2.7 s @ 48k)
    void prepare() noexcept { if (buf_.empty()) buf_.assign(kCapacity, 0.0f); w_.store(0); r_.store(0); }
    void push(float x) noexcept {
        const int w = w_.load(std::memory_order_relaxed);
        const int next = (w + 1) & (kCapacity - 1);
        if (next == r_.load(std::memory_order_acquire)) return; // Full: drop.
        buf_[static_cast<std::size_t>(w)] = x;
        w_.store(next, std::memory_order_release);
    }
    // Consumer: pops up to `max` samples into `out`; returns count.
    int pop(float* out, int max) noexcept {
        int n = 0;
        int r = r_.load(std::memory_order_relaxed);
        const int w = w_.load(std::memory_order_acquire);
        while (r != w && n < max) { out[n++] = buf_[static_cast<std::size_t>(r)]; r = (r + 1) & (kCapacity - 1); }
        r_.store(r, std::memory_order_release);
        return n;
    }
    int available() const noexcept {
        return (w_.load(std::memory_order_acquire) - r_.load(std::memory_order_acquire) + kCapacity) & (kCapacity - 1);
    }
private:
    std::vector<float> buf_;
    std::atomic<int> w_{0}, r_{0};
};

// -----------------------------------------------------------------------------
//  Parameter block. Written by UI / OBS thread, read once per block by audio.
// -----------------------------------------------------------------------------
struct Params {
    // Common
    std::atomic<float> input_gain_db{0.0f};
    std::atomic<float> output_trim_db{0.0f};
    std::atomic<float> ceiling_db{-1.0f};
    std::atomic<int>   zero_latency{0};
    std::atomic<int>   bypass{0};
    std::atomic<float> pro_scale{1.0f};         // 1.0 when licensed, 0.0 otherwise.
    // UNIT 01
    std::atomic<int>   character{CHAR_BROADCAST};
    std::atomic<int>   space_fx{FX_DRY};
    std::atomic<int>   auto_mono{1};
    std::atomic<int>   upstream_denoiser{0};
    std::atomic<float> gate_open_db{-42.0f};
    std::atomic<int>   gate_enabled{1};
    std::atomic<float> hpf_hz{100.0f};
    std::atomic<int>   whisper_comp{1};
    std::atomic<int>   presence_eq{1};
    std::atomic<float> deesser_depth_db{8.0f};
    std::atomic<int>   mic_muted{0};
    // UNIT 02
    std::atomic<float> collab_target_lufs{-14.5f};
    std::atomic<float> collab_duck_db{-3.5f};
    // UNIT 03
    std::atomic<float> game_ceiling_db{-6.0f};
    std::atomic<float> game_scoop_db{-5.0f};
    // UNIT 04
    std::atomic<float> bgm_pocket_db{-8.5f};
    std::atomic<int>   bgm_safe_mode{0};        // Singing / recording-safe: -3 dB
    std::atomic<int>   bgm_auto_swell{1};
    // UNIT 05
    std::atomic<int>   mono_check{0};            // Collapse the monitored sum to mono.
    // Scene macro bookkeeping
    std::atomic<int>   scene{SCENE_TALK};

    void apply_scene(int scene_macro) noexcept {
        scene.store(scene_macro, std::memory_order_release);
        switch (scene_macro) {
            case SCENE_FPS_GAME:
                character.store(CHAR_FPS_LOUD); game_scoop_db.store(-5.0f); bgm_pocket_db.store(-8.5f);
                bgm_safe_mode.store(0); collab_duck_db.store(-3.5f); space_fx.store(FX_DRY); break;
            case SCENE_TALK:
                character.store(CHAR_BROADCAST); game_scoop_db.store(-4.0f); bgm_pocket_db.store(-8.5f);
                bgm_safe_mode.store(0); collab_duck_db.store(-3.5f); break;
            case SCENE_SINGING:
                character.store(CHAR_BROADCAST); bgm_pocket_db.store(-3.0f); bgm_safe_mode.store(1);
                game_scoop_db.store(-2.0f); collab_duck_db.store(-2.0f); space_fx.store(FX_HALL); break;
            case SCENE_ASMR:
                character.store(CHAR_SOFT_ASMR); bgm_pocket_db.store(-6.0f); bgm_safe_mode.store(0);
                game_scoop_db.store(-3.0f); auto_mono.store(0); space_fx.store(FX_DRY); gate_open_db.store(-55.0f); break;
            default: break;
        }
    }
    // Panic reset: everything to factory except the calibrated input gain.
    void factory_reset_keep_calibration() noexcept {
        output_trim_db.store(0.0f); ceiling_db.store(-1.0f); zero_latency.store(0); bypass.store(0);
        character.store(CHAR_BROADCAST); space_fx.store(FX_DRY); auto_mono.store(1);
        gate_open_db.store(-42.0f); gate_enabled.store(1); hpf_hz.store(100.0f); whisper_comp.store(1);
        presence_eq.store(1); deesser_depth_db.store(8.0f); collab_target_lufs.store(-14.5f);
        collab_duck_db.store(-3.5f); game_ceiling_db.store(-6.0f); game_scoop_db.store(-5.0f);
        bgm_pocket_db.store(-8.5f); bgm_safe_mode.store(0); bgm_auto_swell.store(1); scene.store(SCENE_TALK);
        mono_check.store(0);
    }
};

// -----------------------------------------------------------------------------
//  Telemetry snapshot published by the engine every block (atomic floats).
// -----------------------------------------------------------------------------
struct Telemetry {
    std::atomic<float> true_peak_db{kSilenceDb};
    std::atomic<float> peak_hold_db{kSilenceDb};
    std::atomic<float> rms_db{kSilenceDb};
    std::atomic<float> lufs_m{kSilenceDb};
    std::atomic<float> lufs_s{kSilenceDb};
    std::atomic<float> lufs_i{kSilenceDb};
    std::atomic<float> gr_db{0.0f};
    std::atomic<float> duck_db{0.0f};
    std::atomic<float> phase_corr{1.0f};
    std::atomic<float> voice_center_hz{1600.0f};
    std::atomic<float> voice_activity{0.0f};
    std::atomic<int>   gate_open{1};
    std::atomic<int>   hint_bgm_misplug{0};
    std::atomic<int>   auto_mono_engaged{0};
    std::atomic<int>   bridge_mode{0};
    std::atomic<int>   acapella_pad{0};
    std::atomic<int>   bgm_swelling{0};
    std::atomic<int>   bgm_cough_lift{0};
    std::atomic<float> sample_rate{48000.0f};
    std::atomic<int>   channels{2};
    std::atomic<std::uint32_t> scream_defense_count{0}; // Limiter engagements > 3 dB.
    std::atomic<float> session_peak_db{kSilenceDb};

    void reset_holds() noexcept {
        peak_hold_db.store(kSilenceDb); session_peak_db.store(kSilenceDb);
    }
};

// -----------------------------------------------------------------------------
//  The per-unit engine.
// -----------------------------------------------------------------------------
class Engine {
public:
    explicit Engine(Mode mode) noexcept : mode_(mode) {}

    Mode mode() const noexcept { return mode_; }
    Params&    params()    noexcept { return params_; }
    Telemetry& telemetry() noexcept { return tele_; }
    Calibrator& calibrator() noexcept { return calib_; }
    AuditionRecorder& audition() noexcept { return audition_; }
    PreFxTap& prefx_tap() noexcept { return tap_; }
    Bleep& bleep() noexcept { return bleep_; }
    CoughMute& cough() noexcept { return cough_; }
    LoudnessMeter& loudness() noexcept { return lufs_; }
    void attach_bus(SharedBus* bus) noexcept { bus_ = bus; }
    int latency_samples() const noexcept { return limiter_.latency_samples(); }

    // Non-RT. Allocates everything once for kMaxSampleRate.
    void prepare(float fs, int nch) noexcept {
        fs_ = clampf(fs, 8000.0f, static_cast<float>(kMaxSampleRate));
        nch_ = nch < 1 ? 1 : (nch > kMaxChannels ? kMaxChannels : nch);
        if (scratch_.empty()) scratch_.assign(static_cast<std::size_t>(kMaxBlock) * kMaxChannels, 0.0f);
        fx_.prepare(fs_);
        audition_.prepare();
        tap_.prepare();
        retune_all();
        tele_.sample_rate.store(fs_); tele_.channels.store(nch_);
    }

    // RT entry point. planar[c][i], frames may exceed kMaxBlock (chunked).
    void process_block(float** planar, int nch, int frames) noexcept {
        DenormalGuard ftz;
        if (nch < 1 || frames < 1 || !planar) return;
        if (nch > kMaxChannels) nch = kMaxChannels;
        if (nch != nch_) { nch_ = nch; tele_.channels.store(nch_); }
        int done = 0;
        while (done < frames) {
            const int n = std::min(kMaxBlock, frames - done);
            process_chunk(planar, nch, done, n);
            done += n;
        }
    }

    // Must be called from the audio path when the host reports a new rate.
    void notify_sample_rate(float fs) noexcept {
        if (std::fabs(fs - fs_) < 0.5f) return;
        fs_ = clampf(fs, 8000.0f, static_cast<float>(kMaxSampleRate));
        retune_all();
        tele_.sample_rate.store(fs_);
    }

    void reset_meters() noexcept { lufs_.reset(); tele_.reset_holds(); }
    void start_calibration(float seconds = 10.0f) noexcept {
        calib_.set_character(params_.character.load());
        calib_.start(seconds);
    }
    // Apply a finished calibration result to the input gain (UI thread).
    bool commit_calibration() noexcept {
        if (!calib_.done() || !calib_.result_ok()) return false;
        params_.input_gain_db.store(calib_.result_gain_db(), std::memory_order_release);
        return true;
    }

private:
    // ---- (Re)tune every stage for the current rate -------------------------
    void retune_all() noexcept {
        for (int c = 0; c < kMaxChannels; ++c) {
            dc_[c].set(20.0f, fs_);
            hpf_[c].set(SVF::HIGHPASS, params_.hpf_hz.load(), 0.707f, 0.0f, fs_);
            mud_[c].set(SVF::BELL, 180.0f, 1.2f, 0.0f, fs_);
            scoop300_[c].set(SVF::BELL, 300.0f, 1.0f, -2.0f, fs_);
            pres32_[c].set(SVF::BELL, 3200.0f, 1.2f, 2.5f, fs_);
            notch_[c].set(SVF::BELL, 1600.0f, 2.0f, 0.0f, fs_);
            deess_[c].prepare(fs_);
            band_lo_[c].set(SVF::BANDPASS, 170.0f, 0.9f, 0.0f, fs_);   // 120..240
            band_all_[c].set(SVF::BANDPASS, 1000.0f, 0.25f, 0.0f, fs_);
            env_lo_[c].set(3.0f, 60.0f, fs_); env_all_[c].set(3.0f, 60.0f, fs_);
            level_env_[c].set(0.5f, 50.0f, fs_);
            rms_env_[c].set(30.0f, 300.0f, fs_);
            transient_env_[c].set(0.1f, 5.0f, fs_);
            slow_env_[c].set(15.0f, 120.0f, fs_);
        }
        gate_.prepare(fs_);
        gate_.set_threshold(params_.gate_open_db.load(), params_.upstream_denoiser.load() != 0);
        pitch_.prepare(fs_);
        form_env_.set(5.0f, 80.0f, fs_);
        whisper_env_.set(5.0f, 120.0f, fs_);
        limiter_.prepare(fs_);
        limiter_.set_ceiling_db(mode_ == MODE_GAME ? params_.game_ceiling_db.load() : params_.ceiling_db.load());
        limiter_.set_zero_latency(params_.zero_latency.load() != 0);
        fx_.retune(); fx_.clear();
        lufs_.prepare(fs_);
        calib_.prepare(fs_);
        bleep_.prepare(fs_);
        cough_.prepare(fs_);
        duck_coeff_att_ = tc_coeff(8.0f, fs_);
        duck_coeff_rel_ = tc_coeff(250.0f, fs_);
        swell_coeff_up_ = tc_coeff(1500.0f, fs_);
        swell_coeff_dn_ = tc_coeff(100.0f, fs_);
        pad_coeff_ = tc_coeff(800.0f, fs_);
        leveler_coeff_ = tc_coeff(600.0f, fs_);
        mono_coeff_ = tc_coeff(200.0f, fs_);
        gain_smooth_coeff_ = tc_coeff(20.0f, fs_);
        corr_coeff_ = tc_coeff(300.0f, fs_);
        for (auto& tp : tp_meter_) tp.clear();
        duck_gain_ = 1.0f; swell_gain_ = 1.0f; pad_gain_ = 1.0f; leveler_gain_ = 1.0f; mono_mix_ = 0.0f;
        smooth_in_gain_ = db_to_lin(params_.input_gain_db.load());
        smooth_out_gain_ = db_to_lin(params_.output_trim_db.load());
        last_notch_hz_ = 0.0f; last_notch_db_ = 0.0f; last_mud_db_ = 0.0f;
        bgm_hint_samples_ = 0;
        music_like_samples_ = 0;
        mono_diff_samples_ = 0;
        corr_num_ = 0.0f; corr_den_l_ = 0.0f; corr_den_r_ = 0.0f;
        block_counter_ = 0;
    }

    void process_chunk(float** planar, int nch, int offset, int n) noexcept {
        // ---- Snapshot params once per chunk (atomic reads only) ----
        const bool bypass = params_.bypass.load(std::memory_order_acquire) != 0;
        const float pro = params_.pro_scale.load(std::memory_order_acquire);
        const int character = params_.character.load(std::memory_order_acquire);
        const float in_gain_t = db_to_lin(params_.input_gain_db.load(std::memory_order_acquire));
        const float out_gain_t = db_to_lin(params_.output_trim_db.load(std::memory_order_acquire));
        const int space = params_.space_fx.load(std::memory_order_acquire);
        const bool mic_muted = params_.mic_muted.load(std::memory_order_acquire) != 0;
        const bool zero_lat = params_.zero_latency.load(std::memory_order_acquire) != 0;
        const float hpf_hz = params_.hpf_hz.load(std::memory_order_acquire);
        const float gate_open = params_.gate_open_db.load(std::memory_order_acquire);
        const bool gate_on = params_.gate_enabled.load(std::memory_order_acquire) != 0;
        const bool denoiser = params_.upstream_denoiser.load(std::memory_order_acquire) != 0;
        const bool whisper = params_.whisper_comp.load(std::memory_order_acquire) != 0;
        const bool presence = params_.presence_eq.load(std::memory_order_acquire) != 0;
        const bool auto_mono = params_.auto_mono.load(std::memory_order_acquire) != 0 && character != CHAR_SOFT_ASMR;
        const float ceiling_db = (mode_ == MODE_GAME) ? params_.game_ceiling_db.load() : params_.ceiling_db.load();
        const float collab_target = params_.collab_target_lufs.load();
        const float collab_duck = params_.collab_duck_db.load();
        const float game_scoop = params_.game_scoop_db.load();
        const float bgm_pocket = params_.bgm_safe_mode.load() ? -3.0f : params_.bgm_pocket_db.load();
        const bool bgm_swell_on = params_.bgm_auto_swell.load() != 0;
        const bool mono_check = params_.mono_check.load(std::memory_order_acquire) != 0;
        const bool lfe_layout = (nch == 6 || nch == 8);

        // Coefficient refresh when a UI knob moved (cheap: only on change).
        if (std::fabs(hpf_hz - last_hpf_hz_) > 0.5f) {
            for (int c = 0; c < nch; ++c) hpf_[c].set(SVF::HIGHPASS, clampf(hpf_hz, 85.0f, 130.0f), 0.707f, 0.0f, fs_);
            last_hpf_hz_ = hpf_hz;
        }
        if (std::fabs(gate_open - last_gate_db_) > 0.1f || denoiser != last_denoiser_) {
            gate_.set_threshold(gate_open, denoiser); last_gate_db_ = gate_open; last_denoiser_ = denoiser;
        }
        if (std::fabs(ceiling_db - last_ceiling_db_) > 0.01f) { limiter_.set_ceiling_db(ceiling_db); last_ceiling_db_ = ceiling_db; }
        if (zero_lat != last_zero_lat_) { limiter_.set_zero_latency(zero_lat); last_zero_lat_ = zero_lat; }
        const float deess_depth = params_.deesser_depth_db.load();
        if (std::fabs(deess_depth - last_deess_) > 0.1f) { for (int c = 0; c < nch; ++c) deess_[c].set_depth_db(deess_depth); last_deess_ = deess_depth; }
        if (mode_ == MODE_VOICE) fx_.set_mode(space);

        // Bus-derived state (all atomic reads).
        const float guard_scale = bus_ ? bus_->summing_guard_scale() : 1.0f;
        const bool bridge = (mode_ == MODE_VOICE) && bus_ && bus_->standalone_vc_active();
        float voice_act = 0.0f;
        const float voice_hz = bus_ ? bus_->read_voice_center(&voice_act) : 1600.0f;
        const bool acapella = (mode_ == MODE_VOICE) && bus_ && bus_->is_acapella_chat();
        const bool swell = (mode_ == MODE_BGM) && bgm_swell_on && bus_ && bus_->should_bgm_swell();
        const bool cough_lift = (mode_ == MODE_BGM) && bus_ && bus_->any_cough_active();
        const bool game_loud_now = bus_ ? bus_game_loud() : false;

        tele_.bridge_mode.store(bridge ? 1 : 0, std::memory_order_relaxed);
        tele_.acapella_pad.store(acapella ? 1 : 0, std::memory_order_relaxed);
        tele_.bgm_swelling.store(swell ? 1 : 0, std::memory_order_relaxed);
        tele_.bgm_cough_lift.store(cough_lift ? 1 : 0, std::memory_order_relaxed);

        float frame[kMaxChannels];
        float dry_frame[kMaxChannels];
        float block_peak = 0.0f, block_rms_acc = 0.0f;
        float duck_db_now = 0.0f;
        float gr_min = 0.0f;

        for (int i = 0; i < n; ++i) {
            const int idx = offset + i;
            // ---- 0. Gather + DC block (always-on, pre-everything) ----
            for (int c = 0; c < nch; ++c) {
                float x = planar[c][idx];
                if (!std::isfinite(x)) x = 0.0f;
                x = dc_[c].process(x);
                frame[c] = x; dry_frame[c] = x;
            }

            // Calibration taps the raw (pre-gain) signal.
            if (mode_ == MODE_VOICE && calib_.running()) {
                float mx = 0.0f;
                for (int c = 0; c < nch; ++c) if (std::fabs(frame[c]) > std::fabs(mx)) mx = frame[c];
                calib_.push(mx);
            }

            // ---- 1. Input gain (smoothed) ----
            smooth_in_gain_ += gain_smooth_coeff_ * (in_gain_t - smooth_in_gain_);
            for (int c = 0; c < nch; ++c) frame[c] *= smooth_in_gain_;

            if (!bypass) {
                switch (mode_) {
                    case MODE_VOICE:    process_voice_frame(frame, nch, lfe_layout, character, gate_on, whisper, presence, auto_mono, bridge, game_loud_now, pro); break;
                    case MODE_COLLAB:   process_collab_frame(frame, nch, lfe_layout, collab_target, collab_duck, voice_hz, voice_act, pro, duck_db_now); break;
                    case MODE_GAME:     process_game_frame(frame, nch, lfe_layout, game_scoop, voice_hz, voice_act, pro, duck_db_now); break;
                    case MODE_BGM:      process_bgm_frame(frame, nch, lfe_layout, bgm_pocket, voice_hz, voice_act, swell, cough_lift, pro, duck_db_now); break;
                    case MODE_DIRECTOR:
                        // Metering only; Mono Check folds L/R so phase problems become audible.
                        if (mono_check && nch >= 2) {
                            const float m = (frame[0] + frame[1]) * 0.5f;
                            frame[0] = m; frame[1] = m;
                        }
                        break;
                }
            }

            // ---- Emergency tools (UNIT 01 only) ----
            if (mode_ == MODE_VOICE) {
                if (bleep_.active()) {
                    const float mute = bleep_.env_mute();
                    const float tone = bleep_.next();
                    for (int c = 0; c < nch; ++c) frame[c] = frame[c] * mute + tone;
                }
                if (cough_.active() || cough_gain_ < 0.999f) {
                    cough_gain_ = cough_.next();
                    for (int c = 0; c < nch; ++c) frame[c] *= cough_gain_;
                }
                if (mic_muted) for (int c = 0; c < nch; ++c) frame[c] = 0.0f;
            }

            // ---- Acapella solo pad (-1.5 dB) ----
            if (mode_ == MODE_VOICE) {
                const float pad_t = acapella ? db_to_lin(-1.5f) : 1.0f;
                pad_gain_ += pad_coeff_ * (pad_t - pad_gain_);
                for (int c = 0; c < nch; ++c) frame[c] *= pad_gain_;
            }

            // ---- Output trim (smoothed) ----
            smooth_out_gain_ += gain_smooth_coeff_ * (out_gain_t - smooth_out_gain_);
            for (int c = 0; c < nch; ++c) frame[c] *= smooth_out_gain_;

            // ---- Look-ahead brickwall limiter (shared GR across all ch) ----
            if (!bypass && mode_ != MODE_DIRECTOR && !bridge) {
                limiter_.process_frame(frame, nch, guard_scale);
            }
            const float gr = limiter_.gr_db();
            if (gr < gr_min) gr_min = gr;

            // ---- Audition capture / playback ----
            if (mode_ == MODE_VOICE) {
                if (audition_.capturing())
                    audition_.capture(dry_frame[0], nch > 1 ? dry_frame[1] : dry_frame[0], frame[0], nch > 1 ? frame[1] : frame[0]);
                if (audition_.playing()) audition_.playback(frame, nch);
            }

            // ---- Final sanitizer + meters + write back ----
            float fpk = 0.0f, sq = 0.0f;
            for (int c = 0; c < nch; ++c) {
                const float v = sanitize(frame[c]);
                frame[c] = v;
                planar[c][idx] = v;
                const float p = tp_meter_[c].push(v);
                if (p > fpk) fpk = p;
                if (!(lfe_layout && c == 3)) sq += v * v;
            }
            if (fpk > block_peak) block_peak = fpk;
            block_rms_acc += sq / static_cast<float>(nch);
            lufs_.push_frame(frame, nch);
            if (nch >= 2) {
                const float l = frame[0], r = frame[1];
                corr_num_ += corr_coeff_ * (l * r - corr_num_);
                corr_den_l_ += corr_coeff_ * (l * l - corr_den_l_);
                corr_den_r_ += corr_coeff_ * (r * r - corr_den_r_);
            }
        }

        // ---- Publish telemetry (once per chunk) ----
        const float tp_db = lin_to_db(block_peak);
        const float rms_db = block_rms_acc > 0.0f ? 10.0f * std::log10(block_rms_acc / static_cast<float>(n)) : kSilenceDb;
        tele_.true_peak_db.store(tp_db, std::memory_order_relaxed);
        if (tp_db > tele_.peak_hold_db.load(std::memory_order_relaxed)) tele_.peak_hold_db.store(tp_db, std::memory_order_relaxed);
        if (tp_db > tele_.session_peak_db.load(std::memory_order_relaxed)) tele_.session_peak_db.store(tp_db, std::memory_order_relaxed);
        tele_.rms_db.store(rms_db, std::memory_order_relaxed);
        tele_.lufs_m.store(lufs_.momentary(), std::memory_order_relaxed);
        tele_.lufs_s.store(lufs_.short_term(), std::memory_order_relaxed);
        tele_.lufs_i.store(lufs_.integrated(), std::memory_order_relaxed);
        tele_.gr_db.store(gr_min, std::memory_order_relaxed);
        tele_.duck_db.store(duck_db_now, std::memory_order_relaxed);
        if (gr_min < -3.0f && !scream_latched_) { tele_.scream_defense_count.fetch_add(1, std::memory_order_relaxed); scream_latched_ = true; }
        else if (gr_min > -0.5f) scream_latched_ = false;
        if (nch >= 2) {
            const float den = std::sqrt(corr_den_l_ * corr_den_r_) + 1.0e-9f;
            tele_.phase_corr.store(clampf(corr_num_ / den, -1.0f, 1.0f), std::memory_order_relaxed);
        } else tele_.phase_corr.store(1.0f, std::memory_order_relaxed);
        tele_.gate_open.store(gate_.is_open() ? 1 : 0, std::memory_order_relaxed);
        if (mode_ == MODE_VOICE) {
            tele_.voice_center_hz.store(pitch_.center_hz(), std::memory_order_relaxed);
            tele_.voice_activity.store(pitch_.activity(), std::memory_order_relaxed);
        }

        // ---- Bus publication ----
        if (bus_) {
            const float energy = block_rms_acc / static_cast<float>(n);
            bus_->publish_telemetry(tp_db, rms_db, lufs_.short_term(), gr_min, duck_db_now, energy, tp_db > -6.0f);
            bus_->set_signal_present(rms_db > -60.0f);
            if (mode_ == MODE_VOICE) {
                bus_->set_mic_muted(mic_muted);
                bus_->set_cough_active(cough_.active());
                // While muted or cough-muted the voice must not keep ducking others.
                const bool silenced = mic_muted || cough_.active();
                bus_->publish_voice_center(pitch_.center_hz(), silenced ? 0.0f : pitch_.activity());
            }
            bus_->beat();
        }
        ++block_counter_;
    }

    bool bus_game_loud() const noexcept {
        if (!bus_ || !bus_->raw()) return false;
        const BusLayout* b = bus_->raw();
        const std::int64_t t = now_us();
        for (std::size_t i = 0; i < kMaxSlots; ++i) {
            const auto& s = b->unit_slots[i];
            if (s.in_use.load(std::memory_order_acquire) == 0) continue;
            if (s.mode.load(std::memory_order_acquire) != MODE_GAME) continue;
            if (t - s.heartbeat_us.load(std::memory_order_acquire) > SharedBus::kMeterDecayUs) continue;
            if (s.is_loud_now.load(std::memory_order_acquire)) return true;
        }
        return false;
    }

    // ---- UNIT 01 // VOCAL MASTER -------------------------------------------
    void process_voice_frame(float* frame, int nch, bool lfe, int character, bool gate_on, bool whisper,
                             bool presence, bool auto_mono, bool bridge, bool game_loud, float pro) noexcept {
        (void)pro;
        // Auto Mono: 12 dB sustained L/R RMS difference -> route live side to both.
        if (nch >= 2) {
            const float l = rms_env_[0].process(frame[0]);
            const float r = rms_env_[1].process(frame[1]);
            const float diff_db = std::fabs(lin_to_db(l + 1.0e-6f) - lin_to_db(r + 1.0e-6f));
            if (auto_mono && diff_db >= 12.0f && (l > 0.001f || r > 0.001f)) {
                if (mono_diff_samples_ < 1 << 30) ++mono_diff_samples_;
            } else mono_diff_samples_ = 0;
            const bool engage = mono_diff_samples_ > static_cast<int>(fs_ * 0.5f);
            mono_mix_ += mono_coeff_ * ((engage ? 1.0f : 0.0f) - mono_mix_);
            if (mono_mix_ > 0.001f) {
                const float src = (l >= r) ? frame[0] : frame[1];
                frame[0] = frame[0] * (1.0f - mono_mix_) + src * mono_mix_;
                frame[1] = frame[1] * (1.0f - mono_mix_) + src * mono_mix_;
            }
            tele_.auto_mono_engaged.store(mono_mix_ > 0.5f ? 1 : 0, std::memory_order_relaxed);
        }

        // BGM mis-plug hint: low crest factor + wide stereo for 30 s.
        if (nch >= 2) {
            const float pk = std::max(std::fabs(frame[0]), std::fabs(frame[1]));
            const float rm = (rms_env_[0].value() + rms_env_[1].value()) * 0.5f + 1.0e-6f;
            const float crest = pk / rm;
            const float corr = tele_.phase_corr.load(std::memory_order_relaxed);
            const bool music_like = rm > 0.01f && crest < 3.5f && corr < 0.7f;
            if (music_like) { if (music_like_samples_ < 1 << 30) ++music_like_samples_; }
            else music_like_samples_ = std::max(0, music_like_samples_ - 4);
            tele_.hint_bgm_misplug.store(music_like_samples_ > static_cast<int>(fs_ * 30.0f) ? 1 : 0, std::memory_order_relaxed);
        }

        // Mono analysis signal for detectors.
        float mono = 0.0f; int cnt = 0;
        for (int c = 0; c < nch; ++c) { if (lfe && c == 3) continue; mono += frame[c]; ++cnt; }
        mono /= static_cast<float>(cnt ? cnt : 1);

        // Pitch tracker + bleed guard (runs even in bridge mode).
        pitch_.process(mono, game_loud);

        if (bridge) {
            tap_.push(mono);
            return; // Link Bridge Mode: analysis + bus only, no double processing.
        }

        // De-plosive HPF + dynamic mud cut (120-240 Hz energy ratio spike).
        const float lo = env_lo_[0].process(band_lo_[0].process(mono));
        const float al = env_all_[0].process(band_all_[0].process(mono)) + 1.0e-6f;
        const float mud_ratio = lo / al;
        float mud_target = 0.0f;
        if (mud_ratio > 0.55f) mud_target = clampf(-(mud_ratio - 0.55f) * 20.0f, -6.0f, 0.0f);
        mud_db_ += 0.002f * (mud_target - mud_db_);
        if (std::fabs(mud_db_ - last_mud_db_) > 0.15f) {
            for (int c = 0; c < nch; ++c) mud_[c].set(SVF::BELL, 180.0f, 1.2f, mud_db_, fs_);
            last_mud_db_ = mud_db_;
        }
        for (int c = 0; c < nch; ++c) {
            if (lfe && c == 3) continue;
            frame[c] = mud_[c].process(hpf_[c].process(frame[c]));
        }

        // Noise gate with hysteresis (applied uniformly across channels).
        if (gate_on) {
            const float g_before = gate_.gain();
            gate_.process(mono);
            const float g = gate_.gain();
            (void)g_before;
            for (int c = 0; c < nch; ++c) frame[c] *= g;
        }

        // Formant-gated upward whisper comp: -45..-20 dB -> up to +8 dB.
        if (whisper) {
            const float env = whisper_env_.process(mono);
            const float env_db = lin_to_db(env + 1.0e-7f);
            float boost_db = 0.0f;
            if (env_db > -45.0f && env_db < -20.0f && pitch_.formant_ratio() > 0.45f) {
                boost_db = (-20.0f - env_db) / 25.0f * 8.0f; // Linear ramp over the window.
                if (character == CHAR_SOFT_ASMR) boost_db *= 0.5f;
            }
            whisper_gain_ += 0.002f * (db_to_lin(boost_db) - whisper_gain_);
            for (int c = 0; c < nch; ++c) frame[c] *= whisper_gain_;
        }

        // Pre-FX tap for the speech recognizer (post-comp dry voice).
        {
            float m2 = 0.0f; int k = 0;
            for (int c = 0; c < nch; ++c) { if (lfe && c == 3) continue; m2 += frame[c]; ++k; }
            tap_.push(m2 / static_cast<float>(k ? k : 1));
        }

        // Presence EQ + smart de-esser.
        for (int c = 0; c < nch; ++c) {
            if (lfe && c == 3) continue;
            float v = frame[c];
            if (presence) { v = scoop300_[c].process(v); v = pres32_[c].process(v); }
            v = deess_[c].process(v);
            frame[c] = v;
        }

        // Pre-limiter space FX (stereo pair only; spillover handled inside).
        fx_.process_frame(frame, nch);
    }

    // Shared helper: retune the voice-tracking notch when the centre moved.
    void update_voice_notch(float hz, float depth_db, int nch, bool lfe) noexcept {
        if (std::fabs(hz - last_notch_hz_) > 25.0f || std::fabs(depth_db - last_notch_db_) > 0.2f) {
            for (int c = 0; c < nch; ++c) { if (lfe && c == 3) continue; notch_[c].set(SVF::BELL, hz, 2.0f, depth_db, fs_); }
            last_notch_hz_ = hz; last_notch_db_ = depth_db;
        }
    }

    // ---- UNIT 02 // COLLAB RIDER -------------------------------------------
    void process_collab_frame(float* frame, int nch, bool lfe, float target_lufs, float duck_db,
                              float voice_hz, float voice_act, float pro, float& duck_out) noexcept {
        // Auto-leveling toward target (max +6 dB), driven by short-term LUFS.
        const float st = lufs_.short_term();
        float want_db = 0.0f;
        if (st > -60.0f) want_db = clampf(target_lufs - st, -12.0f, 6.0f);
        leveler_gain_ += leveler_coeff_ * (db_to_lin(want_db) - leveler_gain_);
        // Mid-band ducking when UNIT 01 speaks.
        const float depth = duck_db * clampf(voice_act, 0.0f, 1.0f) * pro;
        duck_env_ += ((depth < duck_env_) ? duck_coeff_att_ : duck_coeff_rel_) * (depth - duck_env_);
        update_voice_notch(voice_hz, duck_env_, nch, lfe);
        for (int c = 0; c < nch; ++c) {
            float v = frame[c] * leveler_gain_;
            if (!(lfe && c == 3)) v = notch_[c].process(v);
            frame[c] = v;
        }
        duck_out = duck_env_;
    }

    // ---- UNIT 03 // GAME IMPACT --------------------------------------------
    void process_game_frame(float* frame, int nch, bool lfe, float scoop_db,
                            float voice_hz, float voice_act, float pro, float& duck_out) noexcept {
        // Transient absorber: fast vs slow envelope ratio flags gunshots /
        // explosions; the limiter ceiling (-6 dBFS) does the actual catching,
        // here we pre-soften only the transient excess so footsteps stay intact.
        float mono = 0.0f; int k = 0;
        for (int c = 0; c < nch; ++c) { if (lfe && c == 3) continue; mono += frame[c]; ++k; }
        mono /= static_cast<float>(k ? k : 1);
        const float fast = transient_env_[0].process(mono);
        const float slow = slow_env_[0].process(mono) + 1.0e-6f;
        const float ratio = fast / slow;
        float soften = 1.0f;
        if (ratio > 4.0f && fast > db_to_lin(-6.0f)) soften = clampf(4.0f / ratio, 0.5f, 1.0f);
        transient_gain_ += ((soften < transient_gain_) ? 0.5f : 0.01f) * (soften - transient_gain_);
        const float depth = scoop_db * clampf(voice_act, 0.0f, 1.0f) * pro;
        duck_env_ += ((depth < duck_env_) ? duck_coeff_att_ : duck_coeff_rel_) * (depth - duck_env_);
        update_voice_notch(voice_hz, duck_env_, nch, lfe);
        for (int c = 0; c < nch; ++c) {
            float v = frame[c] * transient_gain_;
            if (!(lfe && c == 3)) v = notch_[c].process(v);
            frame[c] = v;
        }
        duck_out = duck_env_;
    }

    // ---- UNIT 04 // BGM POCKET ---------------------------------------------
    void process_bgm_frame(float* frame, int nch, bool lfe, float pocket_db, float voice_hz,
                           float voice_act, bool swell, bool cough_lift, float pro, float& duck_out) noexcept {
        const float depth = pocket_db * clampf(voice_act, 0.0f, 1.0f) * pro;
        duck_env_ += ((depth < duck_env_) ? duck_coeff_att_ : duck_coeff_rel_) * (depth - duck_env_);
        update_voice_notch(voice_hz, duck_env_, nch, lfe);
        // Auto swell: +4 dB slow rise (AFK), 100 ms return. Cough mute lifts the
        // BGM +2 dB with a fast 100 ms move so the gap is not heard as silence.
        float swell_t = 1.0f;
        float coeff = swell_coeff_dn_;
        if (swell) {
            swell_t = db_to_lin(4.0f);
            if (swell_t > swell_gain_) coeff = swell_coeff_up_;
        } else if (cough_lift) {
            swell_t = db_to_lin(2.0f);
        }
        swell_gain_ += coeff * (swell_t - swell_gain_);
        if (nch >= 2) {
            // M/S: pocket the Mid only; Side stays untouched.
            const float m = (frame[0] + frame[1]) * 0.5f;
            const float s = (frame[0] - frame[1]) * 0.5f;
            const float m2 = notch_[0].process(m);
            frame[0] = (m2 + s) * swell_gain_;
            frame[1] = (m2 - s) * swell_gain_;
            for (int c = 2; c < nch; ++c) {
                float v = frame[c];
                if (!(lfe && c == 3)) v = notch_[c].process(v);
                frame[c] = v * swell_gain_;
            }
        } else {
            frame[0] = notch_[0].process(frame[0]) * swell_gain_;
        }
        duck_out = duck_env_;
    }

    // ---- State ---------------------------------------------------------------
    Mode mode_;
    float fs_ = 48000.0f;
    int nch_ = 2;
    Params params_;
    Telemetry tele_;
    SharedBus* bus_ = nullptr;
    std::vector<float> scratch_;

    OnePoleHP dc_[kMaxChannels];
    SVF hpf_[kMaxChannels], mud_[kMaxChannels], scoop300_[kMaxChannels], pres32_[kMaxChannels], notch_[kMaxChannels];
    SVF band_lo_[kMaxChannels], band_all_[kMaxChannels];
    Envelope env_lo_[kMaxChannels], env_all_[kMaxChannels], level_env_[kMaxChannels], rms_env_[kMaxChannels];
    Envelope transient_env_[kMaxChannels], slow_env_[kMaxChannels];
    DeEsser deess_[kMaxChannels];
    NoiseGate gate_;
    PitchTracker pitch_;
    Envelope form_env_, whisper_env_;
    LookaheadLimiter limiter_;
    SpaceFx fx_;
    LoudnessMeter lufs_;
    Calibrator calib_;
    Bleep bleep_;
    CoughMute cough_;
    AuditionRecorder audition_;
    PreFxTap tap_;
    std::array<TruePeak, kMaxChannels> tp_meter_{};

    float duck_coeff_att_ = 0.01f, duck_coeff_rel_ = 0.001f, duck_env_ = 0.0f, duck_gain_ = 1.0f;
    float swell_coeff_up_ = 0.0001f, swell_coeff_dn_ = 0.001f, swell_gain_ = 1.0f;
    float pad_coeff_ = 0.0001f, pad_gain_ = 1.0f;
    float leveler_coeff_ = 0.0001f, leveler_gain_ = 1.0f;
    float mono_coeff_ = 0.0001f, mono_mix_ = 0.0f;
    float gain_smooth_coeff_ = 0.001f, smooth_in_gain_ = 1.0f, smooth_out_gain_ = 1.0f;
    float corr_coeff_ = 0.0001f, corr_num_ = 0.0f, corr_den_l_ = 0.0f, corr_den_r_ = 0.0f;
    float whisper_gain_ = 1.0f, transient_gain_ = 1.0f, cough_gain_ = 1.0f;
    float mud_db_ = 0.0f, last_mud_db_ = 0.0f;
    float last_notch_hz_ = 0.0f, last_notch_db_ = 0.0f;
    float last_hpf_hz_ = 100.0f, last_gate_db_ = -42.0f, last_ceiling_db_ = -1.0f, last_deess_ = 8.0f;
    bool  last_denoiser_ = false, last_zero_lat_ = false, scream_latched_ = false;
    int   bgm_hint_samples_ = 0, music_like_samples_ = 0, mono_diff_samples_ = 0;
    std::uint64_t block_counter_ = 0;
};

} // namespace sw::model600::dsp

#endif // SEVENTHWELL_MODEL600_DSP_CORE_HPP
