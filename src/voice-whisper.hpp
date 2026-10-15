// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  voice-whisper.hpp  [Layer 2]  Embedded speech recognizer (whisper.cpp).
//
//  Replaces the OS speech services where they cannot be used from inside OBS
//  (macOS: TCC requires a usage key in OBS.app's Info.plist, which OBS does
//  not declare). The model runs fully on-device on the CPU; no microphone is
//  opened - audio comes from UNIT 01's clean pre-FX tap, exactly like the
//  Windows SAPI bridge.
//
//    WhisperEngine        one whisper.cpp context; transcribes a 16 kHz clip.
//    WhisperVoiceBridge   capture thread (pull -> 16 kHz -> utterances) +
//                         recognition thread (whisper -> IntentParser ->
//                         dispatch). Never touches the audio thread.
//
//  whisper.h stays inside voice-whisper.cpp, so including this header does
//  not pull ggml into other translation units.
// =============================================================================
#ifndef SEVENTHWELL_MODEL600_VOICE_WHISPER_HPP
#define SEVENTHWELL_MODEL600_VOICE_WHISPER_HPP

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "voice-command-multilingual.hpp"

namespace sw::model600::voice {

// Model shipped with the plugin (bundle Resources / OBS module data dir).
inline constexpr const char* kWhisperModelFile = "ggml-base-q5_1.bin";

class WhisperEngine {
public:
    WhisperEngine();
    ~WhisperEngine();
    WhisperEngine(const WhisperEngine&) = delete;
    WhisperEngine& operator=(const WhisperEngine&) = delete;

    // Loads the ggml model. n_threads <= 0 picks half the cores (1..4).
    bool load(const std::string& model_path, int n_threads = 0);
    bool loaded() const noexcept;

    // Transcribes one 16 kHz mono utterance. With hint == AUTO the language
    // is detected among JA / EN / KO / ZH / ID only. Returns UTF-8 text.
    std::string transcribe(const std::vector<float>& pcm16k, Lang hint, Lang* detected = nullptr);

    // While *keep_running is false, load()'s warm-up and transcribe() return
    // early (whisper's abort callback), so a filter can be removed promptly.
    void set_abort_flag(const std::atomic<bool>* keep_running) noexcept { keep_running_ = keep_running; }

    // whisper.cpp / ggml log lines at warning level and above, plus engine
    // errors (default: dropped).
    static void set_log_sink(std::function<void(const char*)> sink);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    const std::atomic<bool>* keep_running_ = nullptr;
};

class WhisperVoiceBridge {
public:
    using Pull     = std::function<int(float* out, int max)>;   // Pops source-rate mono samples.
    using Dispatch = std::function<void(const Command&)>;
    using Observer = std::function<void(const std::string& text, const Command& cmd)>;

    WhisperVoiceBridge(std::string model_path, double src_rate, Lang lang, Pull pull, Dispatch dispatch);
    ~WhisperVoiceBridge();
    WhisperVoiceBridge(const WhisperVoiceBridge&) = delete;
    WhisperVoiceBridge& operator=(const WhisperVoiceBridge&) = delete;

    void start();
    void stop();

    // Sees every transcription, fired or not (diagnostics / tests).
    void set_observer(Observer obs) { observer_ = std::move(obs); }

    // true once the model is loaded; false again if loading failed.
    bool ready() const noexcept { return ready_.load(std::memory_order_acquire); }
    bool failed() const noexcept { return failed_.load(std::memory_order_acquire); }
    // Utterances handed to whisper so far (tests poll this).
    int  processed() const noexcept { return processed_.load(std::memory_order_acquire); }
    // true while utterances wait in the queue or one is being decoded.
    bool busy() const;

private:
    void capture_loop();
    void recognize_loop();

    std::string model_path_;
    double src_rate_;
    Lang lang_;
    Pull pull_;
    Dispatch dispatch_;
    Observer observer_;

    std::atomic<bool> running_{false};
    std::atomic<bool> ready_{false};
    std::atomic<bool> failed_{false};
    std::atomic<int>  processed_{0};
    std::atomic<bool> decoding_{false};

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::vector<float>> queue_;

    std::thread capture_, recognize_;
};

} // namespace sw::model600::voice

#endif // SEVENTHWELL_MODEL600_VOICE_WHISPER_HPP
