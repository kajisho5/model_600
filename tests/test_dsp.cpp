// Headless functional test for the DSP core + shared bus (Layers 1).
// Exercises calibration, ducking, the summing guard, sample-rate changes and
// the NaN/Inf sanitiser, and fails (non-zero) on any invariant violation.
#include "dsp-core.hpp"
#include "shared-state.hpp"
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
using namespace sw::model600;
using namespace sw::model600::dsp;

int main() {
    int fails = 0;
    SharedBus busV, busB;
    busV.open(MODE_VOICE);
    busB.open(MODE_BGM);

    Engine voice(MODE_VOICE), bgm(MODE_BGM), game(MODE_GAME), collab(MODE_COLLAB);
    voice.attach_bus(&busV);
    bgm.attach_bus(&busB);
    voice.prepare(48000.f, 2);
    bgm.prepare(48000.f, 2);
    game.prepare(48000.f, 6);
    collab.prepare(48000.f, 1);
    voice.params().space_fx.store(FX_BATH);
    voice.start_calibration(1.0f);

    const int N = 1024;
    std::vector<float> L(N), R(N);
    float* ch[8] = {L.data(), R.data(), L.data(), R.data(), L.data(), R.data(), L.data(), R.data()};
    std::mt19937 rng(1);
    std::normal_distribution<float> nd(0.f, 0.1f);
    float ph = 0.f;

    for (int blk = 0; blk < 300; ++blk) {
        for (int i = 0; i < N; ++i) {
            float s = 0.f;
            for (int h = 1; h <= 14; ++h) {
                const float w = (h >= 3 && h <= 12) ? 2.5f : 1.f;
                s += w * std::sin(ph * static_cast<float>(h)) / static_cast<float>(h);
            }
            ph += 2.f * kPi * 180.f / 48000.f;
            if (ph > kTwoPi) ph -= kTwoPi;
            float x = s * 0.08f + nd(rng) * 0.02f;
            if (blk == 150) x *= 20.f;   // Scream -> limiter must engage.
            L[i] = x; R[i] = x * 0.9f;
        }
        voice.process_block(ch, 2, N);
        for (int i = 0; i < N; ++i) { L[i] = std::sin(static_cast<float>(i) * 0.05f) * 0.5f; R[i] = std::cos(static_cast<float>(i) * 0.05f) * 0.5f; }
        bgm.process_block(ch, 2, N);
        game.process_block(ch, 6, N);
        collab.process_block(ch, 1, N);
        if (blk == 60) voice.commit_calibration();
        if (blk == 120) voice.notify_sample_rate(44100.f);
    }

    auto& tv = voice.telemetry();
    if (!voice.calibrator().done() || !voice.calibrator().result_ok()) { std::printf("FAIL: calibration did not converge\n"); ++fails; }
    const float g = voice.calibrator().result_gain_db();
    if (g < -18.f || g > 24.f) { std::printf("FAIL: calibration gain out of range: %.2f\n", g); ++fails; }
    if (bgm.telemetry().duck_db.load() > -1.0f) { std::printf("FAIL: BGM did not duck under voice: %.2f\n", bgm.telemetry().duck_db.load()); ++fails; }
    if (tv.scream_defense_count.load() == 0) { std::printf("FAIL: scream was not caught\n"); ++fails; }

    // NaN/Inf sanitiser: poison the input, output must stay finite and bounded.
    L[0] = NAN; R[1] = INFINITY; L[5] = 1e30f;
    voice.process_block(ch, 2, N);
    for (int i = 0; i < N; ++i) {
        if (!std::isfinite(L[i]) || !std::isfinite(R[i]) || std::fabs(L[i]) > 1.0f || std::fabs(R[i]) > 1.0f) {
            std::printf("FAIL: non-finite/over-unity output at %d\n", i); ++fails; break;
        }
    }

    std::printf("[dsp] gain=%.2f ref=%.2f screams=%u ducked=%.2f -> %s\n",
                g, voice.calibrator().result_reference_db(), tv.scream_defense_count.load(),
                bgm.telemetry().duck_db.load(), fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
