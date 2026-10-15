// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  voice-whisper.cpp  [Layer 2]  whisper.cpp engine + capture / recognition
//  threads for the voice director. See voice-whisper.hpp.
// =============================================================================
#include "voice-whisper.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#include <whisper.h>

#include "voice-segmenter.hpp"

namespace sw::model600::voice {

namespace {

// Vocabulary prompts: bias the decoder towards the wake words and command
// words of each language (whisper's initial_prompt).
const char* prompt_for(Lang l) {
    switch (l) {
        case Lang::JA:
            return "音響さん、ディレクター。BGM、マイク、ゲーム音、コラボ、全体。上げて、下げて、"
                   "ミュート、ミュート解除、ピー音、チャプター、元に戻して、リセット、エコー切って、測定。";
        case Lang::KO:
            return "감독님, 음향님. 배경음악, 마이크, 게임 소리, 통화. 올려, 내려, 음소거, 음소거 해제, "
                   "챕터, 되돌려, 초기화.";
        case Lang::ZH:
            return "导演，音响师。背景音乐，麦克风，游戏声音，连麦。调高，调低，静音，取消静音，章节，撤销，重置。";
        case Lang::ID:
            return "Pak direktur, sutradara. Musik latar, mikrofon, suara game, panggilan. Naikkan, turunkan, "
                   "bisukan, nyalakan mic, tandai ini, batalkan, atur ulang.";
        case Lang::EN:
        default:
            return "OK director, hey director. Turn up, turn down the music, BGM, mic, game audio, collab. "
                   "Mute, unmute, bleep, chapter, undo, reset, echo off, calibrate.";
    }
}

const char* code_for(Lang l) {
    switch (l) {
        case Lang::JA: return "ja";
        case Lang::KO: return "ko";
        case Lang::ZH: return "zh";
        case Lang::ID: return "id";
        case Lang::EN:
        default:       return "en";
    }
}

std::mutex g_log_mu;
std::function<void(const char*)> g_log_sink;

void log_line(const char* text) {
    std::lock_guard<std::mutex> lk(g_log_mu);
    if (g_log_sink) g_log_sink(text);
}

void whisper_log(enum ggml_log_level level, const char* text, void*) {
    if (level < GGML_LOG_LEVEL_WARN || !text) return;
    log_line(text);
}

// whisper_full_params::abort_callback: abort when the owner stops.
bool should_abort(void* keep_running) {
    return keep_running && !static_cast<const std::atomic<bool>*>(keep_running)->load(std::memory_order_acquire);
}

} // namespace

// -----------------------------------------------------------------------------
//  WhisperEngine
// -----------------------------------------------------------------------------
// Encoder window in 20 ms frames: 512 covers 10.2 s, enough for the 8 s
// utterance cap, and is ~2x faster than whisper's fixed 30 s window with the
// same transcripts on our test set (256 starts to degrade them).
constexpr int kAudioCtx = 512;

struct WhisperEngine::Impl {
    whisper_context* ctx = nullptr;
    int threads = 2;
    ~Impl() { if (ctx) whisper_free(ctx); }
};

WhisperEngine::WhisperEngine() : impl_(std::make_unique<Impl>()) {}
WhisperEngine::~WhisperEngine() = default;

void WhisperEngine::set_log_sink(std::function<void(const char*)> sink) {
    std::lock_guard<std::mutex> lk(g_log_mu);
    g_log_sink = std::move(sink);
}

bool WhisperEngine::load(const std::string& model_path, int n_threads) {
    whisper_log_set(whisper_log, nullptr);
    if (impl_->ctx) { whisper_free(impl_->ctx); impl_->ctx = nullptr; }
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = false;            // CPU only: no Metal / CUDA runtime inside OBS.
    impl_->ctx = whisper_init_from_file_with_params(model_path.c_str(), cp);
    const int hw = static_cast<int>(std::thread::hardware_concurrency());
    impl_->threads = n_threads > 0 ? n_threads : std::clamp(hw / 2, 2, 4);
    if (!impl_->ctx) return false;
    // Warm-up decode on silence: allocates the compute buffers now instead of
    // on the first command, and leaves the context's encoder window at
    // kAudioCtx, which whisper_lang_auto_detect() then reuses.
    std::vector<float> silence(static_cast<std::size_t>(kAsrRate * 1.2), 0.0f);
    whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    p.n_threads = impl_->threads;
    p.audio_ctx = kAudioCtx;
    p.language = "en";
    p.no_context = true;
    p.single_segment = true;
    p.print_progress = false;
    p.print_realtime = false;
    p.print_timestamps = false;
    p.abort_callback = should_abort;
    p.abort_callback_user_data = const_cast<std::atomic<bool>*>(keep_running_);
    whisper_full(impl_->ctx, p, silence.data(), static_cast<int>(silence.size()));
    return true;
}

bool WhisperEngine::loaded() const noexcept { return impl_->ctx != nullptr; }

std::string WhisperEngine::transcribe(const std::vector<float>& pcm16k, Lang hint, Lang* detected) {
    if (!impl_->ctx || pcm16k.empty()) return {};
    whisper_context* ctx = impl_->ctx;

    // whisper.cpp skips clips under 1 s: pad with silence (0.1 s lead, rest tail).
    std::vector<float> pcm;
    const std::size_t min_len = static_cast<std::size_t>(kAsrRate * 1.2);
    const std::size_t lead = static_cast<std::size_t>(kAsrRate / 10);
    pcm.reserve(std::max(min_len, pcm16k.size() + lead));
    pcm.assign(lead, 0.0f);
    pcm.insert(pcm.end(), pcm16k.begin(), pcm16k.end());
    if (pcm.size() < min_len) pcm.resize(min_len, 0.0f);

    Lang lang = hint;
    if (lang == Lang::AUTO) {
        // Language ID restricted to the five supported languages.
        lang = Lang::EN;
        if (whisper_pcm_to_mel(ctx, pcm.data(), static_cast<int>(pcm.size()), impl_->threads) == 0) {
            std::vector<float> probs(static_cast<std::size_t>(whisper_lang_max_id() + 1), 0.0f);
            if (whisper_lang_auto_detect(ctx, 0, impl_->threads, probs.data()) >= 0) {
                const Lang cands[] = {Lang::JA, Lang::EN, Lang::KO, Lang::ZH, Lang::ID};
                float best = -1.0f;
                for (Lang c : cands) {
                    const int id = whisper_lang_id(code_for(c));
                    if (id >= 0 && probs[static_cast<std::size_t>(id)] > best) {
                        best = probs[static_cast<std::size_t>(id)];
                        lang = c;
                    }
                }
            }
        }
    }
    if (detected) *detected = lang;
    // whisper checks the abort flag between encoder stages / decoder steps
    // only, so also skip the second encode outright when stopping.
    if (should_abort(const_cast<std::atomic<bool>*>(keep_running_))) return {};

    whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    p.n_threads = impl_->threads;
    p.audio_ctx = kAudioCtx;
    p.no_context = true;
    p.no_timestamps = true;
    p.single_segment = true;
    p.print_special = false;
    p.print_progress = false;
    p.print_realtime = false;
    p.print_timestamps = false;
    p.suppress_blank = true;
    p.language = code_for(lang);
    p.detect_language = false;
    p.initial_prompt = prompt_for(lang);
    p.abort_callback = should_abort;
    p.abort_callback_user_data = const_cast<std::atomic<bool>*>(keep_running_);
    if (whisper_full(ctx, p, pcm.data(), static_cast<int>(pcm.size())) != 0) return {};

    std::string text;
    const int n = whisper_full_n_segments(ctx);
    for (int i = 0; i < n; ++i) {
        const char* s = whisper_full_get_segment_text(ctx, i);
        if (s) text += s;
    }
    // Trim the leading space whisper emits.
    const auto b = text.find_first_not_of(' ');
    return b == std::string::npos ? std::string{} : text.substr(b);
}

// -----------------------------------------------------------------------------
//  WhisperVoiceBridge
// -----------------------------------------------------------------------------
WhisperVoiceBridge::WhisperVoiceBridge(std::string model_path, double src_rate, Lang lang,
                                       Pull pull, Dispatch dispatch)
    : model_path_(std::move(model_path)), src_rate_(src_rate), lang_(lang),
      pull_(std::move(pull)), dispatch_(std::move(dispatch)) {}

WhisperVoiceBridge::~WhisperVoiceBridge() { stop(); }

void WhisperVoiceBridge::start() {
    if (running_.exchange(true)) return;
    recognize_ = std::thread([this] { recognize_loop(); });
    capture_ = std::thread([this] { capture_loop(); });
}

void WhisperVoiceBridge::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    if (capture_.joinable()) capture_.join();
    if (recognize_.joinable()) recognize_.join();
    std::lock_guard<std::mutex> lk(mu_);
    queue_.clear();
}

bool WhisperVoiceBridge::busy() const {
    std::lock_guard<std::mutex> lk(mu_);
    return !queue_.empty() || decoding_.load(std::memory_order_acquire);
}

void WhisperVoiceBridge::capture_loop() {
    Resampler16k rs(src_rate_);
    UtteranceSegmenter seg;
    std::vector<float> in(8192), out16;
    std::vector<std::vector<float>> done;
    while (running_.load(std::memory_order_acquire) && !failed_.load(std::memory_order_acquire)) {
        int n = 0;
        while ((n = pull_ ? pull_(in.data(), static_cast<int>(in.size())) : 0) > 0) {
            out16.clear();
            rs.process(in.data(), n, out16);
            seg.push(out16.data(), static_cast<int>(out16.size()), done);
        }
        if (!done.empty()) {
            {
                std::lock_guard<std::mutex> lk(mu_);
                for (auto& u : done) {
                    queue_.push_back(std::move(u));
                    while (queue_.size() > 3) queue_.pop_front();   // Never fall behind the talker.
                }
            }
            done.clear();
            cv_.notify_one();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void WhisperVoiceBridge::recognize_loop() {
    WhisperEngine engine;
    engine.set_abort_flag(&running_);
    if (!engine.load(model_path_)) {
        const std::string msg = "voice director disabled: cannot load speech model " + model_path_ + "\n";
        log_line(msg.c_str());
        failed_.store(true, std::memory_order_release);
        return;
    }
    ready_.store(true, std::memory_order_release);
    IntentParser parser;
    parser.set_language(lang_);

    while (true) {
        std::vector<float> utt;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return !running_.load(std::memory_order_acquire) || !queue_.empty(); });
            if (!running_.load(std::memory_order_acquire)) return;
            utt = std::move(queue_.front());
            queue_.pop_front();
            decoding_.store(true, std::memory_order_release);
        }
        const std::string text = engine.transcribe(utt, lang_);
        if (!running_.load(std::memory_order_acquire)) return;   // Aborted mid-decode.
        const Command cmd = text.empty() ? Command{} : parser.parse(text);
        if (observer_) observer_(text, cmd);
        if (cmd.fires() && dispatch_) dispatch_(cmd);
        processed_.fetch_add(1, std::memory_order_acq_rel);
        decoding_.store(false, std::memory_order_release);
    }
}

} // namespace sw::model600::voice
