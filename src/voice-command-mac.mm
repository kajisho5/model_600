// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  voice-command-mac.mm  [Layer 3]  macOS SFSpeechRecognizer voice bridge.
//
//  Like the Windows bridge, this never opens the physical microphone. It feeds
//  SFSpeechRecognizer from UNIT 01's clean pre-FX tap (dsp::PreFxTap) via an
//  SFSpeechAudioBufferRecognitionRequest, so bath reverb, collaborators and
//  game audio cannot trigger it. TCC speech-recognition permission is checked
//  and requested up front; if denied, the bridge degrades silently.
//
//  Objective-C++. Compiled only on macOS (guarded + CMake-gated). Parsed intents
//  go through the portable IntentParser (Layer 1).
// =============================================================================
#if defined(__APPLE__)

#import <Foundation/Foundation.h>
#import <Speech/Speech.h>
#import <AVFoundation/AVFoundation.h>

#include <atomic>
#include <functional>
#include <string>

#include "dsp-core.hpp"
#include "voice-command-multilingual.hpp"

using namespace sw::model600;

// -----------------------------------------------------------------------------
//  Objective-C driver object. Pumps the float tap into an SFSpeech buffer
//  request on a timer and reports final transcriptions back to C++.
// -----------------------------------------------------------------------------
@interface M600SpeechDriver : NSObject
@property (nonatomic, assign) sw::model600::dsp::PreFxTap* tap;
@property (nonatomic, assign) float srcRate;
@property (nonatomic, strong) SFSpeechRecognizer* recognizer;
@property (nonatomic, strong) SFSpeechAudioBufferRecognitionRequest* request;
@property (nonatomic, strong) SFSpeechRecognitionTask* task;
@property (nonatomic, strong) dispatch_source_t timer;
@property (nonatomic, assign) std::function<void(const std::string&)>* sink;
@property (nonatomic, assign) BOOL running;
- (void)startWithLocale:(NSString*)localeId;
- (void)stop;
@end

@implementation M600SpeechDriver

- (void)startWithLocale:(NSString*)localeId {
    if (self.running) return;
    __weak M600SpeechDriver* weakSelf = self;
    // Request speech-recognition authorization (TCC) before doing anything.
    [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus status) {
        if (status != SFSpeechRecognizerAuthorizationStatusAuthorized) {
            NSLog(@"[MODEL 600] speech recognition not authorized (status=%ld); voice director disabled.", (long)status);
            return;
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakSelf beginSessionWithLocale:localeId];
        });
    }];
}

- (void)beginSessionWithLocale:(NSString*)localeId {
    NSLocale* locale = localeId.length ? [NSLocale localeWithLocaleIdentifier:localeId]
                                       : [NSLocale currentLocale];
    self.recognizer = [[SFSpeechRecognizer alloc] initWithLocale:locale];
    if (!self.recognizer || !self.recognizer.isAvailable) {
        NSLog(@"[MODEL 600] SFSpeechRecognizer unavailable for locale %@", localeId);
        return;
    }
    self.recognizer.supportsOnDeviceRecognition = YES;
    self.request = [[SFSpeechAudioBufferRecognitionRequest alloc] init];
    self.request.shouldReportPartialResults = NO;         // Only act on finals.
    if (@available(macOS 10.15, *)) self.request.requiresOnDeviceRecognition = YES;

    __weak M600SpeechDriver* weakSelf = self;
    self.task = [self.recognizer recognitionTaskWithRequest:self.request
        resultHandler:^(SFSpeechRecognitionResult* _Nullable result, NSError* _Nullable error) {
            if (error || !result) return;
            if (!result.isFinal) return;
            NSString* phrase = result.bestTranscription.formattedString;
            if (phrase.length == 0) return;
            std::string utf8 = std::string([phrase UTF8String] ? [phrase UTF8String] : "");
            if (weakSelf.sink && *weakSelf.sink) (*weakSelf.sink)(utf8);
        }];

    // Pull the pre-FX tap into the recognizer at ~50 Hz on a background queue.
    const double fs = self.srcRate > 0 ? self.srcRate : 48000.0;
    AVAudioFormat* fmt = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
                                                          sampleRate:fs channels:1 interleaved:NO];
    dispatch_queue_t q = dispatch_queue_create("com.seventhwell.model600.speech", DISPATCH_QUEUE_SERIAL);
    self.timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, q);
    dispatch_source_set_timer(self.timer, dispatch_time(DISPATCH_TIME_NOW, 0),
                              (uint64_t)(20 * NSEC_PER_MSEC), (uint64_t)(5 * NSEC_PER_MSEC));
    dispatch_source_set_event_handler(self.timer, ^{
        [weakSelf pumpWithFormat:fmt];
    });
    self.running = YES;
    dispatch_resume(self.timer);
}

- (void)pumpWithFormat:(AVAudioFormat*)fmt {
    if (!self.running || !self.tap || !self.request) return;
    const int kMax = 2048;
    static thread_local float scratch[2048];
    const int n = self.tap->pop(scratch, kMax);
    if (n <= 0) return;
    AVAudioPCMBuffer* buf = [[AVAudioPCMBuffer alloc] initWithPCMFormat:fmt
                                                         frameCapacity:(AVAudioFrameCount)n];
    if (!buf) return;
    buf.frameLength = (AVAudioFrameCount)n;
    float* dst = buf.floatChannelData[0];
    for (int i = 0; i < n; ++i) dst[i] = scratch[i];
    [self.request appendAudioPCMBuffer:buf];
}

- (void)stop {
    if (!self.running) return;
    self.running = NO;
    if (self.timer) { dispatch_source_cancel(self.timer); self.timer = nil; }
    [self.request endAudio];
    [self.task cancel];
    self.task = nil;
    self.request = nil;
    self.recognizer = nil;
}

@end

// -----------------------------------------------------------------------------
//  C++ bridge wrapping the ObjC driver + the portable intent parser.
// -----------------------------------------------------------------------------
namespace sw::model600::voice {

class MacVoiceBridge {
public:
    using Dispatch = std::function<void(const Command&)>;
    MacVoiceBridge(dsp::PreFxTap* tap, float rate, Dispatch dispatch)
        : dispatch_(std::move(dispatch)) {
        driver_ = [[M600SpeechDriver alloc] init];
        driver_.tap = tap;
        driver_.srcRate = rate;
        sink_ = [this](const std::string& s) {
            const Command c = parser_.parse(s);
            if (c.fires() && dispatch_) dispatch_(c);
        };
        driver_.sink = &sink_;
    }
    ~MacVoiceBridge() { stop(); driver_ = nil; }

    void set_language(Lang l) {
        parser_.set_language(l);
        switch (l) {
            case Lang::JA: locale_ = @"ja-JP"; break;
            case Lang::EN: locale_ = @"en-US"; break;
            case Lang::KO: locale_ = @"ko-KR"; break;
            case Lang::ZH: locale_ = @"zh-CN"; break;
            case Lang::ID: locale_ = @"id-ID"; break;
            default: locale_ = @""; break;
        }
    }
    void start() { [driver_ startWithLocale:locale_]; }
    void stop() { [driver_ stop]; }

private:
    Dispatch dispatch_;
    IntentParser parser_;
    M600SpeechDriver* driver_ = nil;
    std::function<void(const std::string&)> sink_;
    NSString* locale_ = @"";
};

extern "C" void* m600_mac_voice_start(void* tap, float rate, int lang, void (*cb)(const Command*)) {
    // TCC reads the speech-recognition prompt from the host app (OBS.app), not
    // from this plugin bundle. Without that key the Speech framework terminates
    // the whole process on the first authorization request, so stay disabled.
    NSString* why = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"NSSpeechRecognitionUsageDescription"];
    if (![why isKindOfClass:[NSString class]] || why.length == 0) return nullptr;
    auto dispatch = [cb](const Command& c) { if (cb) cb(&c); };
    auto* bridge = new MacVoiceBridge(static_cast<dsp::PreFxTap*>(tap), rate, dispatch);
    bridge->set_language(static_cast<Lang>(lang));
    bridge->start();
    return bridge;
}
extern "C" void m600_mac_voice_stop(void* bridge) {
    delete static_cast<MacVoiceBridge*>(bridge);
}

} // namespace sw::model600::voice

#endif // __APPLE__
