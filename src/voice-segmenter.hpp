// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  voice-segmenter.hpp  [Layer 1]
//
//  Front end for the embedded speech recognizer:
//    * Resampler16k        - any host rate -> 16 kHz mono (Butterworth
//                            anti-alias + linear interpolation).
//    * UtteranceSegmenter  - energy VAD with an adaptive noise floor that
//                            cuts the 16 kHz stream into single utterances
//                            (pre-roll, hangover, min / max length).
//
//  Pure C++20, no allocation on the per-sample path except when an utterance
//  is handed out. Runs on the recognizer's capture thread, never the audio
//  thread.
// =============================================================================
#ifndef SEVENTHWELL_MODEL600_VOICE_SEGMENTER_HPP
#define SEVENTHWELL_MODEL600_VOICE_SEGMENTER_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <vector>

namespace sw::model600::voice {

inline constexpr int kAsrRate = 16000;

// -----------------------------------------------------------------------------
//  Resampler16k: 4th-order Butterworth low-pass at 7 kHz (two RBJ biquads),
//  then linear interpolation onto the 16 kHz grid. Plenty for speech
//  recognition; not meant for listening.
// -----------------------------------------------------------------------------
class Resampler16k {
public:
    explicit Resampler16k(double src_rate = 48000.0) { reset(src_rate); }

    void reset(double src_rate) {
        src_rate_ = src_rate > 1000.0 ? src_rate : 48000.0;
        step_ = src_rate_ / static_cast<double>(kAsrRate);
        pos_ = 0.0;
        prev_ = 0.0f;
        have_prev_ = false;
        const double fc = std::min(7000.0, 0.45 * src_rate_);
        const double qs[2] = {0.54119610, 1.30656296};       // 4th-order Butterworth.
        for (int i = 0; i < 2; ++i) design(lp_[i], fc, qs[i]);
    }

    // Appends the resampled block to `out`.
    void process(const float* in, int n, std::vector<float>& out) {
        for (int i = 0; i < n; ++i) {
            float x = in[i];
            x = lp_[0].run(x);
            x = lp_[1].run(x);
            if (!have_prev_) { prev_ = x; have_prev_ = true; pos_ = 0.0; continue; }
            // Emit every output sample whose time falls in (prev, x].
            while (pos_ <= 1.0) {
                const float t = static_cast<float>(pos_);
                out.push_back(prev_ + (x - prev_) * t);
                pos_ += step_;
            }
            pos_ -= 1.0;
            prev_ = x;
        }
    }

private:
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float run(float x) noexcept {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    void design(Biquad& b, double fc, double q) const {
        const double w0 = 2.0 * 3.14159265358979323846 * fc / src_rate_;
        const double cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b.b0 = static_cast<float>((1.0 - cw) * 0.5 / a0);
        b.b1 = static_cast<float>((1.0 - cw) / a0);
        b.b2 = b.b0;
        b.a1 = static_cast<float>(-2.0 * cw / a0);
        b.a2 = static_cast<float>((1.0 - alpha) / a0);
        b.z1 = b.z2 = 0.0f;
    }

    double src_rate_ = 48000.0, step_ = 3.0, pos_ = 0.0;
    float prev_ = 0.0f;
    bool have_prev_ = false;
    Biquad lp_[2];
};

// -----------------------------------------------------------------------------
//  UtteranceSegmenter: 20 ms frames at 16 kHz.
//    voiced  = frame RMS > max(noise floor + 12 dB, -55 dBFS)
//    warmup  = first 500 ms only measure the room noise
//    start   = 3 voiced frames in a row (60 ms), with 300 ms pre-roll kept
//    end     = 600 ms without a voiced frame (natural pause after a command)
//    max     = 8 s (force-cut a run-on sentence)
//    min     = 250 ms of voiced frames, otherwise discarded (clicks, coughs)
//  The noise floor follows the signal quickly downwards and slowly upwards
//  (3 dB/s) and is frozen while an utterance is open.
// -----------------------------------------------------------------------------
class UtteranceSegmenter {
public:
    static constexpr int   kFrame          = kAsrRate / 50;   // 20 ms.
    static constexpr int   kStartFrames    = 3;
    static constexpr int   kHangFrames     = 30;              // 600 ms.
    static constexpr int   kPreRollFrames  = 15;              // 300 ms.
    static constexpr int   kMaxFrames      = 400;             // 8 s.
    static constexpr int   kMinVoiced      = 13;              // ~250 ms.
    static constexpr float kMarginDb       = 12.0f;
    static constexpr float kAbsFloorDb     = -55.0f;
    static constexpr int   kWarmupFrames   = 25;              // 500 ms.

    // Feeds 16 kHz samples. Completed utterances are appended to `done`.
    void push(const float* x, int n, std::vector<std::vector<float>>& done) {
        for (int i = 0; i < n; ++i) {
            frame_[static_cast<std::size_t>(fill_++)] = x[i];
            if (fill_ == kFrame) { fill_ = 0; on_frame(done); }
        }
    }

    // Flushes an open utterance (e.g. on stop). Returns true if one was emitted.
    bool flush(std::vector<std::vector<float>>& done) {
        if (!open_) return false;
        close(done);
        return true;
    }

    bool  in_utterance() const noexcept { return open_; }
    float noise_floor_db() const noexcept { return floor_db_; }

private:
    void on_frame(std::vector<std::vector<float>>& done) {
        double acc = 0.0;
        for (float s : frame_) acc += static_cast<double>(s) * s;
        const float rms = static_cast<float>(std::sqrt(acc / kFrame));
        const float db = rms > 1.0e-9f ? 20.0f * std::log10(rms) : -180.0f;
        const float thresh = std::max(floor_db_ + kMarginDb, kAbsFloorDb);
        const bool voiced = db > thresh;

        if (warmup_ < kWarmupFrames) {
            // First 500 ms only measure the room: the floor starts at its mean.
            warm_acc_ += db;
            if (++warmup_ == kWarmupFrames) floor_db_ = std::max(-90.0f, warm_acc_ / kWarmupFrames);
            return;
        }

        if (!open_) {
            // Floor falls fast and rises slowly (3 dB/s), so steady noise is
            // absorbed while speech onsets stay well above it.
            if (db < floor_db_) floor_db_ = 0.7f * floor_db_ + 0.3f * db;
            else floor_db_ = std::min(db, floor_db_ + 3.0f / 50.0f);
            preroll_.emplace_back(frame_.begin(), frame_.end());
            if (static_cast<int>(preroll_.size()) > kPreRollFrames + kStartFrames) preroll_.pop_front();
            run_ = voiced ? run_ + 1 : 0;
            if (run_ >= kStartFrames) {
                open_ = true;
                cur_.clear();
                for (const auto& f : preroll_) cur_.insert(cur_.end(), f.begin(), f.end());
                preroll_.clear();
                voiced_frames_ = run_;
                silent_ = 0;
                frames_ = static_cast<int>(cur_.size()) / kFrame;
                run_ = 0;
            }
            return;
        }

        cur_.insert(cur_.end(), frame_.begin(), frame_.end());
        ++frames_;
        if (voiced) { ++voiced_frames_; silent_ = 0; }
        else ++silent_;
        if (silent_ >= kHangFrames || frames_ >= kMaxFrames) close(done);
    }

    void close(std::vector<std::vector<float>>& done) {
        if (voiced_frames_ >= kMinVoiced) {
            // Drop most of the trailing silence, keep 200 ms.
            const int keep_tail = std::max(0, silent_ - 10) * kFrame;
            if (keep_tail > 0 && static_cast<int>(cur_.size()) > keep_tail)
                cur_.resize(cur_.size() - static_cast<std::size_t>(keep_tail));
            done.push_back(std::move(cur_));
        }
        cur_ = {};
        open_ = false;
        voiced_frames_ = silent_ = frames_ = run_ = 0;
    }

    std::vector<float> frame_ = std::vector<float>(kFrame, 0.0f);
    int fill_ = 0;
    std::deque<std::vector<float>> preroll_;
    std::vector<float> cur_;
    bool open_ = false;
    int run_ = 0, voiced_frames_ = 0, silent_ = 0, frames_ = 0, warmup_ = 0;
    float floor_db_ = -70.0f, warm_acc_ = 0.0f;
};

} // namespace sw::model600::voice

#endif // SEVENTHWELL_MODEL600_VOICE_SEGMENTER_HPP
