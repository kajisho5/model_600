// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  voice-command-win.cpp  [Layer 3]  Windows internal-PCM-tap voice bridge.
//
//  Does NOT grab the physical microphone (that would collide with OBS's
//  exclusive-mode capture). Instead it consumes UNIT 01's clean pre-FX tap
//  (dsp::PreFxTap) through SAPI's in-process recognizer fed from an in-memory
//  stream, so recognition works even through heavy bath reverb and never mis-
//  fires on a collaborator's or the game's speech.
//
//  Compiled only on Windows (the CMake list guards it). The recognizer runs on
//  its own thread; results are parsed by the portable IntentParser (Layer 1)
//  and dispatched through a callback the host installs.
// =============================================================================
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sapi.h>
#include <sphelper.h>
#include <atlbase.h>

#include <atomic>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

#include "dsp-core.hpp"
#include "voice-command-multilingual.hpp"

namespace sw::model600::voice {

// -----------------------------------------------------------------------------
//  A minimal ISpStream-compatible in-memory PCM source. SAPI pulls 16-bit mono
//  PCM from here; we fill it from the float pre-FX tap, resampling 48k->16k.
// -----------------------------------------------------------------------------
class TapAudioStream : public ISpStreamFormat {
public:
    explicit TapAudioStream(dsp::PreFxTap* tap, float src_rate)
        : tap_(tap), src_rate_(src_rate) {
        decim_ = src_rate_ / 16000.0f;
        if (decim_ < 1.0f) decim_ = 1.0f;
        wfx_.wFormatTag = WAVE_FORMAT_PCM;
        wfx_.nChannels = 1;
        wfx_.nSamplesPerSec = 16000;
        wfx_.wBitsPerSample = 16;
        wfx_.nBlockAlign = 2;
        wfx_.nAvgBytesPerSec = 32000;
        wfx_.cbSize = 0;
    }
    virtual ~TapAudioStream() = default;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ISequentialStream ||
            riid == IID_IStream || riid == __uuidof(ISpStreamFormat)) {
            *ppv = static_cast<ISpStreamFormat*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    // ISpStreamFormat
    STDMETHODIMP GetFormat(GUID* guid, WAVEFORMATEX** ppwfx) override {
        if (!guid || !ppwfx) return E_POINTER;
        *guid = SPDFID_WaveFormatEx;
        *ppwfx = static_cast<WAVEFORMATEX*>(::CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
        if (!*ppwfx) return E_OUTOFMEMORY;
        std::memcpy(*ppwfx, &wfx_, sizeof(WAVEFORMATEX));
        return S_OK;
    }

    // ISequentialStream::Read - the hot path SAPI calls.
    STDMETHODIMP Read(void* pv, ULONG cb, ULONG* pcbRead) override {
        auto* out = static_cast<std::int16_t*>(pv);
        const ULONG want = cb / 2;
        ULONG got = 0;
        float f[512];
        while (got < want && running_.load(std::memory_order_acquire)) {
            const int n = tap_->pop(f, 512);
            if (n <= 0) { ::Sleep(5); continue; }
            for (int i = 0; i < n && got < want; ++i) {
                acc_ += 1.0f;
                if (acc_ >= decim_) {
                    acc_ -= decim_;
                    float v = f[i];
                    if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f;
                    out[got++] = static_cast<std::int16_t>(v * 32767.0f);
                }
            }
        }
        if (pcbRead) *pcbRead = got * 2;
        return got > 0 ? S_OK : S_FALSE;
    }
    STDMETHODIMP Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }

    // IStream (mostly unsupported for a live capture stream).
    STDMETHODIMP Seek(LARGE_INTEGER, DWORD, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    STDMETHODIMP SetSize(ULARGE_INTEGER) override { return E_NOTIMPL; }
    STDMETHODIMP CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    STDMETHODIMP Commit(DWORD) override { return S_OK; }
    STDMETHODIMP Revert() override { return S_OK; }
    STDMETHODIMP LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP Stat(STATSTG* p, DWORD) override { if (p) std::memset(p, 0, sizeof(*p)); return S_OK; }
    STDMETHODIMP Clone(IStream**) override { return E_NOTIMPL; }

    void stop() { running_.store(false, std::memory_order_release); }

private:
    LONG ref_ = 1;
    dsp::PreFxTap* tap_;
    float src_rate_, decim_ = 3.0f, acc_ = 0.0f;
    std::atomic<bool> running_{true};
    WAVEFORMATEX wfx_{};
};

// -----------------------------------------------------------------------------
//  The bridge: owns the recognizer thread and dispatches parsed commands.
// -----------------------------------------------------------------------------
class WindowsVoiceBridge {
public:
    using Dispatch = std::function<void(const Command&)>;

    WindowsVoiceBridge(dsp::PreFxTap* tap, float src_rate, Dispatch dispatch)
        : tap_(tap), src_rate_(src_rate), dispatch_(std::move(dispatch)) {}
    ~WindowsVoiceBridge() { stop(); }

    void set_language(Lang l) { parser_.set_language(l); }

    bool start() {
        if (running_.exchange(true)) return true;
        thread_ = std::thread([this] { run(); });
        return true;
    }
    void stop() {
        if (!running_.exchange(false)) return;
        if (stream_) stream_->stop();
        if (thread_.joinable()) thread_.join();
    }

private:
    void run() {
        const HRESULT hrCo = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool need_uninit = SUCCEEDED(hrCo);

        CComPtr<ISpRecognizer> recognizer;
        CComPtr<ISpRecoContext> context;
        CComPtr<ISpRecoGrammar> grammar;
        stream_ = new TapAudioStream(tap_, src_rate_);

        bool ok = SUCCEEDED(recognizer.CoCreateInstance(CLSID_SpInprocRecognizer));
        if (ok) ok = SUCCEEDED(recognizer->SetInput(stream_, TRUE));
        if (ok) ok = SUCCEEDED(recognizer->CreateRecoContext(&context));
        if (ok) ok = SUCCEEDED(context->CreateGrammar(1, &grammar));
        if (ok) ok = SUCCEEDED(grammar->LoadDictation(nullptr, SPLO_STATIC));
        if (ok) ok = SUCCEEDED(grammar->SetDictationState(SPRS_ACTIVE));
        if (ok) context->SetNotifyWin32Event();

        while (ok && running_.load(std::memory_order_acquire)) {
            if (context->WaitForNotifyEvent(200) != S_OK) continue;
            CSpEvent ev;
            while (ev.GetFrom(context) == S_OK) {
                if (ev.eEventId != SPEI_RECOGNITION) continue;
                CComPtr<ISpRecoResult> result = ev.RecoResult();
                if (!result) continue;
                CSpDynamicString text;
                if (FAILED(result->GetText(static_cast<ULONG>(SP_GETWHOLEPHRASE), static_cast<ULONG>(SP_GETWHOLEPHRASE), TRUE, &text, nullptr))) continue;
                const int len = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
                if (len <= 1) continue;
                std::vector<char> utf8(static_cast<std::size_t>(len));
                ::WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), len, nullptr, nullptr);
                const Command cmd = parser_.parse(std::string_view(utf8.data()));
                if (cmd.fires() && dispatch_) dispatch_(cmd);
            }
        }

        if (grammar) grammar->SetDictationState(SPRS_INACTIVE);
        if (stream_) { stream_->Release(); stream_ = nullptr; }
        if (need_uninit) ::CoUninitialize();
    }

    dsp::PreFxTap* tap_;
    float src_rate_;
    Dispatch dispatch_;
    IntentParser parser_;
    TapAudioStream* stream_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

// C bridge for the adapter layer.
extern "C" void* m600_win_voice_start(void* tap, float rate, void (*cb)(const Command*)) {
    auto dispatch = [cb](const Command& c) { if (cb) cb(&c); };
    auto* bridge = new WindowsVoiceBridge(static_cast<dsp::PreFxTap*>(tap), rate, dispatch);
    bridge->start();
    return bridge;
}
extern "C" void m600_win_voice_stop(void* bridge) {
    delete static_cast<WindowsVoiceBridge*>(bridge);
}

} // namespace sw::model600::voice

#endif // _WIN32
