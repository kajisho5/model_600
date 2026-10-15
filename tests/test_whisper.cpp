// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  test_whisper.cpp - end-to-end test of the embedded voice director:
//  WAV at its own rate -> Resampler16k -> UtteranceSegmenter -> whisper.cpp ->
//  IntentParser, then the same through the threaded WhisperVoiceBridge.
//
//    m600_test_whisper <model.bin> <tests/data/voice>
// =============================================================================
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "voice-segmenter.hpp"
#include "voice-whisper.hpp"

using namespace sw::model600::voice;

namespace {

struct Wav { int rate = 0; std::vector<float> x; };

bool read_wav(const std::string& path, Wav& w) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) return false;
    auto u16 = [&](std::size_t o) { return static_cast<int>(static_cast<unsigned char>(b[o]) | (static_cast<unsigned char>(b[o + 1]) << 8)); };
    auto u32 = [&](std::size_t o) {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(b[o])) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(b[o + 1])) << 8) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(b[o + 2])) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(b[o + 3])) << 24);
    };
    int channels = 0, bits = 0;
    std::size_t pos = 12;
    while (pos + 8 <= b.size()) {
        const std::uint32_t len = u32(pos + 4);
        if (std::memcmp(b.data() + pos, "fmt ", 4) == 0) {
            if (u16(pos + 8) != 1) return false;              // PCM only.
            channels = u16(pos + 10);
            w.rate = static_cast<int>(u32(pos + 12));
            bits = u16(pos + 22);
        } else if (std::memcmp(b.data() + pos, "data", 4) == 0) {
            if (bits != 16 || channels < 1) return false;
            const std::size_t n = std::min<std::size_t>(len, b.size() - pos - 8) / static_cast<std::size_t>(2 * channels);
            w.x.resize(n);
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t o = pos + 8 + i * static_cast<std::size_t>(2 * channels);
                w.x[i] = static_cast<float>(static_cast<std::int16_t>(u16(o))) / 32768.0f;
            }
            return true;
        }
        pos += 8 + len + (len & 1u);
    }
    return false;
}

// Utterance framed by room noise, as UNIT 01's tap would deliver it.
std::vector<float> with_room(const Wav& w, std::mt19937& rng, float pre_s = 0.8f, float post_s = 1.2f) {
    std::normal_distribution<float> n(0.0f, 0.0003f);     // ~ -70 dBFS hiss.
    std::vector<float> out;
    const auto pad = [&](float s) { for (int i = 0; i < static_cast<int>(s * static_cast<float>(w.rate)); ++i) out.push_back(n(rng)); };
    pad(pre_s);
    for (float s : w.x) out.push_back(s + n(rng));
    pad(post_s);
    return out;
}

struct Case { const char* file; bool fire; Intent intent; Target target; int sign; };

const Case kCases[] = {
    {"ja_bgm_down.wav",   true,  Intent::TRIM_DELTA,   Target::BGM,    -1},
    {"ja_game_up.wav",    true,  Intent::TRIM_DELTA,   Target::GAME,   +1},
    {"ja_mute.wav",       true,  Intent::MUTE_MIC,     Target::NONE,    0},
    {"ja_chapter.wav",    true,  Intent::CHAPTER_MARK, Target::NONE,    0},
    {"ja_undo.wav",       true,  Intent::UNDO,         Target::NONE,    0},
    {"ja_neg.wav",        false, Intent::NONE,         Target::NONE,    0},
    {"en_music_down.wav", true,  Intent::TRIM_DELTA,   Target::BGM,    -1},
    {"en_mute.wav",       true,  Intent::MUTE_MIC,     Target::NONE,    0},
    {"en_neg.wav",        false, Intent::NONE,         Target::NONE,    0},
};

bool matches(const Case& c, const Command& cmd) {
    if (cmd.fires() != c.fire) return false;
    if (!c.fire) return true;
    if (cmd.intent != c.intent) return false;
    if (c.target != Target::NONE && cmd.target != c.target) return false;
    if (c.sign != 0 && (cmd.value_db * static_cast<float>(c.sign)) <= 0.0f) return false;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s <model.bin> <voice-data-dir>\n", argv[0]); return 2; }
    const std::string model = argv[1], dir = argv[2];
    WhisperEngine::set_log_sink([](const char* s) { std::fprintf(stderr, "[whisper] %s", s); });

    WhisperEngine engine;
    if (!engine.load(model)) { std::fprintf(stderr, "cannot load model %s\n", model.c_str()); return 1; }
    IntentParser parser;
    std::mt19937 rng(600);
    int fails = 0;
    // Safety is strict, recall is scored: a missed command is a nuisance, a
    // wrong or spurious one changes the live mix.
    int positives = 0, recognized = 0, wrong_fires = 0;

    // 1. Segment + transcribe + parse each clip.
    for (const Case& c : kCases) {
        Wav w;
        if (!read_wav(dir + "/" + c.file, w)) { std::printf("FAIL %-18s unreadable\n", c.file); ++fails; continue; }
        const std::vector<float> stream = with_room(w, rng);
        Resampler16k rs(w.rate);
        UtteranceSegmenter seg;
        std::vector<float> s16;
        std::vector<std::vector<float>> utts;
        for (std::size_t i = 0; i < stream.size(); i += 480) {
            const int n = static_cast<int>(std::min<std::size_t>(480, stream.size() - i));
            s16.clear();
            rs.process(stream.data() + i, n, s16);
            seg.push(s16.data(), static_cast<int>(s16.size()), utts);
        }
        seg.flush(utts);
        if (utts.size() != 1) {
            std::printf("FAIL %-18s %zu utterances (expected 1)\n", c.file, utts.size());
            ++fails;
            continue;
        }
        const auto t0 = std::chrono::steady_clock::now();
        Lang lang = Lang::AUTO;
        const std::string text = engine.transcribe(utts[0], Lang::AUTO, &lang);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const Command cmd = parser.parse(text);
        const bool ok = matches(c, cmd);
        if (c.fire) { ++positives; if (ok) ++recognized; }
        if (cmd.fires() && !ok) ++wrong_fires;
        std::printf("%s %-18s %.2fs utt, lang=%s, %5.0f ms, \"%s\" -> %s/%s %+.1f%s\n", ok ? "ok  " : (c.fire && !cmd.fires() ? "miss" : "FAIL"), c.file,
                    static_cast<double>(utts[0].size()) / kAsrRate, lang == Lang::JA ? "ja" : lang == Lang::EN ? "en" : "other",
                    ms, text.c_str(), intent_name(cmd.intent), target_name(cmd.target),
                    static_cast<double>(cmd.value_db), cmd.fires() ? " FIRE" : "");
    }

    std::printf("recognized %d/%d commands, %d wrong or spurious fire%s\n", recognized, positives, wrong_fires,
                wrong_fires == 1 ? "" : "s");
    if (wrong_fires > 0) ++fails;
    if (recognized * 10 < positives * 7) ++fails;     // Recall floor: 70 %.

    // 2. Threaded bridge: two commands in one 48 kHz stream, pulled like the tap.
    {
        Wav a, b;
        if (!read_wav(dir + "/ja_bgm_down.wav", a) || !read_wav(dir + "/ja_mute.wav", b) || a.rate != 48000 || b.rate != 48000) {
            std::printf("FAIL bridge: test clips must be 48 kHz\n");
            ++fails;
        } else {
            std::vector<float> stream = with_room(a, rng);
            const std::vector<float> second = with_room(b, rng, 0.2f, 1.2f);
            stream.insert(stream.end(), second.begin(), second.end());
            std::atomic<std::size_t> cursor{0};
            std::mutex m;
            std::vector<Command> fired;
            WhisperVoiceBridge bridge(model, 48000.0, Lang::AUTO,
                [&](float* out, int max) {
                    // Real-time-ish: at most 20 ms per pull call.
                    const std::size_t at = cursor.load();
                    const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(std::min(max, 960)), stream.size() - at));
                    std::copy_n(stream.data() + at, n, out);
                    cursor.store(at + static_cast<std::size_t>(n));
                    return n;
                },
                [&](const Command& c) { std::lock_guard<std::mutex> lk(m); fired.push_back(c); });
            bridge.start();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
            while (std::chrono::steady_clock::now() < deadline &&
                   !(bridge.failed() || (cursor >= stream.size() && bridge.processed() >= 2 && !bridge.busy())))
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            bridge.stop();
            std::lock_guard<std::mutex> lk(m);
            const bool ok = !bridge.failed() && fired.size() == 2 &&
                            fired[0].intent == Intent::TRIM_DELTA && fired[0].target == Target::BGM &&
                            fired[1].intent == Intent::MUTE_MIC;
            if (!ok) ++fails;
            std::printf("%s bridge: %d utterances, %zu commands fired\n", ok ? "ok  " : "FAIL",
                        bridge.processed(), fired.size());
        }
    }

    // 3. Removing the filter while a decode runs must return promptly (abort
    //    callback) and must not fire anything afterwards.
    {
        Wav a;
        if (read_wav(dir + "/ja_bgm_down.wav", a)) {
            const std::vector<float> stream = with_room(a, rng);
            std::atomic<std::size_t> cursor{0};
            std::atomic<int> fired{0};
            WhisperVoiceBridge bridge(model, static_cast<double>(a.rate), Lang::AUTO,
                [&](float* out, int max) {
                    const std::size_t at = cursor.load();
                    const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(max), stream.size() - at));
                    std::copy_n(stream.data() + at, n, out);
                    cursor.store(at + static_cast<std::size_t>(n));
                    return n;
                },
                [&](const Command&) { fired.fetch_add(1); });
            bridge.start();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            // Model loaded first, then wait for the utterance to be in decode.
            while (std::chrono::steady_clock::now() < deadline && !bridge.ready() && !bridge.failed())
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            while (std::chrono::steady_clock::now() < deadline && !bridge.busy() && !bridge.failed())
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const bool was_busy = bridge.busy();
            const auto t0 = std::chrono::steady_clock::now();
            bridge.stop();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            const bool ok = was_busy && fired.load() == 0;
            if (!ok) ++fails;
            std::printf("%s stop during decode: %.0f ms, %d fired\n", ok ? "ok  " : "FAIL", ms, fired.load());
        }
    }

    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
