// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  voice-command-multilingual.hpp  [Layer 1]
//
//  Five-language (JA / EN / KO / ZH / ID) fuzzy intent parser for the
//  hands-free "Director" voice commands. Pure C++20, no Qt / OBS.
//
//  Safety design:
//    * A command fires ONLY when a wake word ("音響さん", "Director", ...)
//      AND an executable intent co-occur in the same utterance.
//    * Voice-driven trim deltas are clamped to the [-12.0, +6.0] dB window.
//    * Every applied command is pushed onto a fixed-size undo stack so
//      "音響さん、元に戻して" (Undo) reverts the previous operation.
//    * Matching is tolerant to recognizer variance: NFKC-style width folding,
//      Katakana -> Hiragana folding, ASCII case folding, punctuation stripping,
//      and an approximate-substring (Sellers) edit distance for longer tokens.
//
//  This file must stay UTF-8 (MSVC is compiled with /utf-8, so narrow string
//  literals below are UTF-8 byte sequences on every platform).
// =============================================================================
#ifndef SEVENTHWELL_MODEL600_VOICE_COMMAND_MULTILINGUAL_HPP
#define SEVENTHWELL_MODEL600_VOICE_COMMAND_MULTILINGUAL_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sw::model600::voice {

enum class Lang : int { AUTO = 0, JA = 1, EN = 2, KO = 3, ZH = 4, ID = 5 };

enum class Intent : int {
    NONE = 0,
    TRIM_DELTA,     // value_db = signed delta for `target`
    MUTE_MIC,
    UNMUTE_MIC,
    BLEEP,
    COUGH_MUTE,
    CHAPTER_MARK,
    FX_SET,         // fx_mode = 0..5 (see dsp::SpaceFxMode)
    FX_OFF,
    SCENE_SET,      // scene = 0..3 (see dsp::SceneMacro)
    UNDO,
    RESET,
    CALIBRATE
};

enum class Target : int { NONE = 0, VOICE = 1, COLLAB = 2, GAME = 3, BGM = 4, MASTER = 5 };

// Limits for voice-driven trim moves (dB).
inline constexpr float kVoiceTrimMinDb   = -12.0f;
inline constexpr float kVoiceTrimMaxDb   = 6.0f;
inline constexpr float kDefaultStepDb    = 2.0f;
inline constexpr float kMaxSingleStepDb  = 6.0f;
inline constexpr float kChimeLevelDb     = -24.0f;

struct Command {
    Intent intent   = Intent::NONE;
    Target target   = Target::NONE;
    float  value_db = 0.0f;   // Signed delta for TRIM_DELTA.
    int    fx_mode  = 0;      // For FX_SET.
    int    scene    = 0;      // For SCENE_SET.
    Lang   lang     = Lang::AUTO;
    bool   wake_detected = false;
    float  confidence = 0.0f; // 0..1, from match tightness.
    std::string matched_wake;
    std::string matched_phrase;
    bool fires() const noexcept { return wake_detected && intent != Intent::NONE; }
};

// -----------------------------------------------------------------------------
//  UTF-8 <-> UTF-32 helpers (lenient decoder: invalid bytes become U+FFFD).
// -----------------------------------------------------------------------------
inline std::u32string utf8_to_u32(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = 0xFFFD; std::size_t len = 1;
        if (c < 0x80) { cp = c; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) { cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu); len = 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) { cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 2]) & 0x3Fu); len = 3; }
        else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) { cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) | ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu); len = 4; }
        out.push_back(cp);
        i += len;
    }
    return out;
}

inline std::string u32_to_utf8(const std::u32string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (char32_t cp : s) {
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else { out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    }
    return out;
}

// -----------------------------------------------------------------------------
//  Normalization: width folding, kana folding, case folding, punctuation drop.
// -----------------------------------------------------------------------------
inline char32_t fold_codepoint(char32_t c) noexcept {
    // Full-width ASCII (U+FF01..U+FF5E) -> ASCII.
    if (c >= 0xFF01 && c <= 0xFF5E) c = c - 0xFF01 + 0x21;
    // Ideographic space -> space.
    if (c == 0x3000) c = 0x20;
    // Katakana (U+30A1..U+30F6) -> Hiragana (U+3041..U+3096).
    if (c >= 0x30A1 && c <= 0x30F6) c = c - 0x30A1 + 0x3041;
    // Half-width Katakana block (U+FF66..U+FF9D) -> approximate Hiragana.
    if (c >= 0xFF66 && c <= 0xFF9D) {
        static constexpr char32_t tbl[] = {
            0x3092,0x3041,0x3043,0x3045,0x3047,0x3049,0x3083,0x3085,0x3087,0x3063,0x30FC,
            0x3042,0x3044,0x3046,0x3048,0x304A,0x304B,0x304D,0x304F,0x3051,0x3053,0x3055,0x3057,0x3059,0x305B,0x305D,
            0x305F,0x3061,0x3064,0x3066,0x3068,0x306A,0x306B,0x306C,0x306D,0x306E,0x306F,0x3072,0x3075,0x3078,0x307B,
            0x307E,0x307F,0x3080,0x3081,0x3082,0x3084,0x3086,0x3088,0x3089,0x308A,0x308B,0x308C,0x308D,0x308F,0x3093};
        const std::size_t idx = static_cast<std::size_t>(c - 0xFF66);
        if (idx < sizeof(tbl) / sizeof(tbl[0])) c = tbl[idx];
    }
    // ASCII upper -> lower.
    if (c >= 'A' && c <= 'Z') c = c + 32;
    // Prolonged sound mark variants.
    if (c == 0x2015 || c == 0x2014 || c == 0xFF70) c = 0x30FC;
    return c;
}

inline bool is_droppable(char32_t c) noexcept {
    // Whitespace, ASCII punctuation, common CJK punctuation.
    if (c == 0x20 || c == 0x09 || c == 0x0A || c == 0x0D) return true;
    if ((c >= 0x21 && c <= 0x2F && c != '+' && c != '-' && c != '.') || (c >= 0x3A && c <= 0x40) || (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E)) return true;
    if (c == 0x3001 || c == 0x3002 || c == 0xFF0C || c == 0xFF0E || c == 0xFF01 || c == 0xFF1F || c == 0x300C || c == 0x300D) return true;
    return false;
}

inline std::u32string normalize(std::string_view utf8) {
    std::u32string raw = utf8_to_u32(utf8);
    std::u32string out; out.reserve(raw.size());
    for (char32_t c : raw) {
        c = fold_codepoint(c);
        if (is_droppable(c)) continue;
        out.push_back(c);
    }
    return out;
}

// -----------------------------------------------------------------------------
//  Approximate substring distance (Sellers): minimum Levenshtein distance from
//  `needle` to any substring of `hay`. O(n*m) with a two-row DP, no heap after
//  the first call per size thanks to a thread-local scratch vector.
// -----------------------------------------------------------------------------
inline int approx_substring_distance(const std::u32string& hay, const std::u32string& needle) {
    const std::size_t n = hay.size(), m = needle.size();
    if (m == 0) return 0;
    if (n == 0) return static_cast<int>(m);
    thread_local std::vector<int> prev, cur;
    prev.assign(n + 1, 0); cur.assign(n + 1, 0);
    for (std::size_t i = 1; i <= m; ++i) {
        cur[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= n; ++j) {
            const int cost = (needle[i - 1] == hay[j - 1]) ? 0 : 1;
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
        }
        std::swap(prev, cur);
    }
    int best = prev[0];
    for (std::size_t j = 1; j <= n; ++j) best = std::min(best, prev[j]);
    return best;
}

// Allowed edit distance by needle length (in code points).
inline int tolerance_for(std::size_t len) noexcept {
    if (len <= 3) return 0;
    if (len <= 6) return 1;
    return 2;
}

struct MatchResult { bool hit = false; int distance = 0; float score = 0.0f; };

inline MatchResult fuzzy_contains(const std::u32string& hay, const std::u32string& needle, bool allow_fuzzy = true) {
    MatchResult r;
    if (needle.empty()) return r;
    if (hay.find(needle) != std::u32string::npos) { r.hit = true; r.distance = 0; r.score = 1.0f; return r; }
    if (!allow_fuzzy) return r;
    const int d = approx_substring_distance(hay, needle);
    const int tol = tolerance_for(needle.size());
    if (d <= tol) { r.hit = true; r.distance = d; r.score = 1.0f - static_cast<float>(d) / static_cast<float>(needle.size() + 1); }
    return r;
}

// -----------------------------------------------------------------------------
//  Dictionaries. Every phrase is stored as UTF-8 and normalized lazily once.
// -----------------------------------------------------------------------------
struct Phrase { const char* utf8; Lang lang; };

// Wake words. Variants cover recognizer spellings (kanji / kana / romaji).
inline constexpr Phrase kWakeWords[] = {
    {"音響さん", Lang::JA}, {"おんきょうさん", Lang::JA}, {"音響", Lang::JA}, {"ディレクター", Lang::JA},
    {"でぃれくたー", Lang::JA}, {"ディレクターさん", Lang::JA}, {"監督", Lang::JA},
    {"director", Lang::EN}, {"hey director", Lang::EN}, {"ok director", Lang::EN}, {"sound guy", Lang::EN}, {"audio", Lang::EN},
    {"감독님", Lang::KO}, {"감독", Lang::KO}, {"음향님", Lang::KO}, {"음향", Lang::KO}, {"디렉터", Lang::KO},
    {"导演", Lang::ZH}, {"導演", Lang::ZH}, {"音响", Lang::ZH}, {"音響師", Lang::ZH}, {"音响师", Lang::ZH},
    {"sutradara", Lang::ID}, {"direktur", Lang::ID}, {"pak direktur", Lang::ID}, {"mas audio", Lang::ID},
};

inline constexpr Phrase kUp[] = {
    {"上げて", Lang::JA}, {"あげて", Lang::JA}, {"大きく", Lang::JA}, {"おおきく", Lang::JA}, {"アップ", Lang::JA}, {"あっぷ", Lang::JA}, {"上げ", Lang::JA},
    {"up", Lang::EN}, {"raise", Lang::EN}, {"louder", Lang::EN}, {"increase", Lang::EN}, {"boost", Lang::EN}, {"turn up", Lang::EN},
    {"올려", Lang::KO}, {"올려줘", Lang::KO}, {"크게", Lang::KO}, {"키워", Lang::KO}, {"높여", Lang::KO},
    {"调高", Lang::ZH}, {"調高", Lang::ZH}, {"大声", Lang::ZH}, {"大一点", Lang::ZH}, {"提高", Lang::ZH}, {"升高", Lang::ZH},
    {"naikkan", Lang::ID}, {"keraskan", Lang::ID}, {"besarkan", Lang::ID}, {"tambah", Lang::ID},
};
inline constexpr Phrase kDown[] = {
    {"下げて", Lang::JA}, {"さげて", Lang::JA}, {"小さく", Lang::JA}, {"ちいさく", Lang::JA}, {"ダウン", Lang::JA}, {"だうん", Lang::JA}, {"下げ", Lang::JA}, {"絞って", Lang::JA},
    {"down", Lang::EN}, {"lower", Lang::EN}, {"quieter", Lang::EN}, {"decrease", Lang::EN}, {"reduce", Lang::EN}, {"turn down", Lang::EN}, {"softer", Lang::EN},
    {"내려", Lang::KO}, {"내려줘", Lang::KO}, {"작게", Lang::KO}, {"줄여", Lang::KO}, {"낮춰", Lang::KO},
    {"调低", Lang::ZH}, {"調低", Lang::ZH}, {"小声", Lang::ZH}, {"小一点", Lang::ZH}, {"降低", Lang::ZH}, {"减小", Lang::ZH},
    {"turunkan", Lang::ID}, {"kecilkan", Lang::ID}, {"pelankan", Lang::ID}, {"kurangi", Lang::ID},
};

inline constexpr Phrase kTargetVoice[] = {
    {"マイク", Lang::JA}, {"まいく", Lang::JA}, {"声", Lang::JA}, {"こえ", Lang::JA}, {"自分", Lang::JA}, {"ボーカル", Lang::JA},
    {"mic", Lang::EN}, {"microphone", Lang::EN}, {"voice", Lang::EN}, {"my voice", Lang::EN}, {"vocal", Lang::EN},
    {"마이크", Lang::KO}, {"목소리", Lang::KO}, {"내 목소리", Lang::KO},
    {"麦克风", Lang::ZH}, {"麥克風", Lang::ZH}, {"声音", Lang::ZH}, {"我的声音", Lang::ZH}, {"人声", Lang::ZH},
    {"mikrofon", Lang::ID}, {"mic", Lang::ID}, {"suara saya", Lang::ID}, {"vokal", Lang::ID},
};
inline constexpr Phrase kTargetCollab[] = {
    {"コラボ", Lang::JA}, {"こらぼ", Lang::JA}, {"通話", Lang::JA}, {"つうわ", Lang::JA}, {"相手", Lang::JA}, {"あいて", Lang::JA}, {"ディスコード", Lang::JA},
    {"collab", Lang::EN}, {"call", Lang::EN}, {"discord", Lang::EN}, {"guest", Lang::EN}, {"partner", Lang::EN}, {"other person", Lang::EN},
    {"콜라보", Lang::KO}, {"통화", Lang::KO}, {"상대", Lang::KO}, {"디스코드", Lang::KO},
    {"通话", Lang::ZH}, {"通話", Lang::ZH}, {"连麦", Lang::ZH}, {"嘉宾", Lang::ZH}, {"对方", Lang::ZH},
    {"lawan bicara", Lang::ID}, {"teman", Lang::ID}, {"panggilan", Lang::ID}, {"kolab", Lang::ID}, {"discord", Lang::ID},
};
inline constexpr Phrase kTargetGame[] = {
    {"ゲーム", Lang::JA}, {"げーむ", Lang::JA}, {"ゲーム音", Lang::JA}, {"効果音", Lang::JA},
    {"game", Lang::EN}, {"game audio", Lang::EN}, {"game sound", Lang::EN}, {"sfx", Lang::EN},
    {"게임", Lang::KO}, {"게임 소리", Lang::KO}, {"효과음", Lang::KO},
    {"游戏", Lang::ZH}, {"遊戲", Lang::ZH}, {"游戏声音", Lang::ZH}, {"音效", Lang::ZH},
    {"game", Lang::ID}, {"suara game", Lang::ID}, {"efek suara", Lang::ID},
};
inline constexpr Phrase kTargetBgm[] = {
    {"BGM", Lang::JA}, {"bgm", Lang::JA}, {"ビージーエム", Lang::JA}, {"音楽", Lang::JA}, {"おんがく", Lang::JA}, {"ミュージック", Lang::JA}, {"曲", Lang::JA},
    {"bgm", Lang::EN}, {"music", Lang::EN}, {"background music", Lang::EN}, {"song", Lang::EN}, {"track", Lang::EN},
    {"배경음악", Lang::KO}, {"음악", Lang::KO}, {"브금", Lang::KO}, {"노래", Lang::KO},
    {"背景音乐", Lang::ZH}, {"背景音樂", Lang::ZH}, {"音乐", Lang::ZH}, {"音樂", Lang::ZH}, {"歌", Lang::ZH},
    {"musik", Lang::ID}, {"lagu", Lang::ID}, {"musik latar", Lang::ID}, {"bgm", Lang::ID},
};
inline constexpr Phrase kTargetMaster[] = {
    {"全体", Lang::JA}, {"ぜんたい", Lang::JA}, {"マスター", Lang::JA}, {"ますたー", Lang::JA}, {"全部", Lang::JA},
    {"master", Lang::EN}, {"everything", Lang::EN}, {"overall", Lang::EN}, {"all", Lang::EN},
    {"전체", Lang::KO}, {"마스터", Lang::KO}, {"전부", Lang::KO},
    {"总音量", Lang::ZH}, {"總音量", Lang::ZH}, {"整体", Lang::ZH}, {"全部", Lang::ZH}, {"主输出", Lang::ZH},
    {"semua", Lang::ID}, {"master", Lang::ID}, {"keseluruhan", Lang::ID},
};

inline constexpr Phrase kMute[] = {
    {"ミュート", Lang::JA}, {"みゅーと", Lang::JA}, {"マイク切って", Lang::JA}, {"マイクオフ", Lang::JA}, {"消音", Lang::JA}, {"マイク止めて", Lang::JA},
    {"mute", Lang::EN}, {"mic off", Lang::EN}, {"mute me", Lang::EN}, {"silence me", Lang::EN},
    {"음소거", Lang::KO}, {"마이크 꺼", Lang::KO}, {"마이크 끄기", Lang::KO},
    {"静音", Lang::ZH}, {"靜音", Lang::ZH}, {"关麦", Lang::ZH}, {"關麥", Lang::ZH}, {"闭麦", Lang::ZH},
    {"bisukan", Lang::ID}, {"mute", Lang::ID}, {"matikan mic", Lang::ID}, {"matikan mikrofon", Lang::ID},
};
inline constexpr Phrase kUnmute[] = {
    {"ミュート解除", Lang::JA}, {"みゅーとかいじょ", Lang::JA}, {"マイクオン", Lang::JA}, {"マイク入れて", Lang::JA}, {"マイク戻して", Lang::JA},
    {"unmute", Lang::EN}, {"mic on", Lang::EN}, {"unmute me", Lang::EN},
    {"음소거 해제", Lang::KO}, {"마이크 켜", Lang::KO}, {"마이크 켜기", Lang::KO},
    {"取消静音", Lang::ZH}, {"取消靜音", Lang::ZH}, {"开麦", Lang::ZH}, {"開麥", Lang::ZH},
    {"nyalakan mic", Lang::ID}, {"unmute", Lang::ID}, {"nyalakan mikrofon", Lang::ID}, {"buka suara", Lang::ID},
};
inline constexpr Phrase kBleep[] = {
    {"ピー音", Lang::JA}, {"ぴーおん", Lang::JA}, {"ピー", Lang::JA}, {"ぴー", Lang::JA}, {"自主規制", Lang::JA}, {"じしゅきせい", Lang::JA}, {"放送禁止", Lang::JA},
    {"bleep", Lang::EN}, {"beep", Lang::EN}, {"censor", Lang::EN}, {"censor that", Lang::EN},
    {"삐", Lang::KO}, {"삐소리", Lang::KO}, {"검열", Lang::KO}, {"묵음처리", Lang::KO},
    {"哔", Lang::ZH}, {"哔声", Lang::ZH}, {"消音处理", Lang::ZH}, {"打码", Lang::ZH},
    {"bip", Lang::ID}, {"sensor", Lang::ID}, {"sensor itu", Lang::ID}, {"bunyi bip", Lang::ID},
};
inline constexpr Phrase kCough[] = {
    {"咳", Lang::JA}, {"せき", Lang::JA}, {"咳払い", Lang::JA}, {"せきばらい", Lang::JA}, {"くしゃみ", Lang::JA}, {"咳ミュート", Lang::JA},
    {"cough", Lang::EN}, {"cough mute", Lang::EN}, {"sneeze", Lang::EN}, {"clear my throat", Lang::EN},
    {"기침", Lang::KO}, {"재채기", Lang::KO}, {"헛기침", Lang::KO},
    {"咳嗽", Lang::ZH}, {"打喷嚏", Lang::ZH}, {"清嗓", Lang::ZH},
    {"batuk", Lang::ID}, {"bersin", Lang::ID}, {"mute batuk", Lang::ID},
};
inline constexpr Phrase kChapter[] = {
    {"チャプター", Lang::JA}, {"ちゃぷたー", Lang::JA}, {"見どころ", Lang::JA}, {"みどころ", Lang::JA}, {"ハイライト", Lang::JA}, {"はいらいと", Lang::JA}, {"ここ記録", Lang::JA}, {"マーク", Lang::JA},
    {"chapter", Lang::EN}, {"mark this", Lang::EN}, {"highlight", Lang::EN}, {"bookmark", Lang::EN}, {"clip that", Lang::EN},
    {"챕터", Lang::KO}, {"하이라이트", Lang::KO}, {"여기 기록", Lang::KO}, {"북마크", Lang::KO},
    {"章节", Lang::ZH}, {"章節", Lang::ZH}, {"精彩", Lang::ZH}, {"标记", Lang::ZH}, {"標記", Lang::ZH}, {"记录这里", Lang::ZH},
    {"bab", Lang::ID}, {"sorotan", Lang::ID}, {"tandai", Lang::ID}, {"tandai ini", Lang::ID}, {"highlight", Lang::ID},
};
inline constexpr Phrase kUndo[] = {
    {"元に戻して", Lang::JA}, {"もとにもどして", Lang::JA}, {"戻して", Lang::JA}, {"もどして", Lang::JA}, {"取り消し", Lang::JA}, {"とりけし", Lang::JA}, {"やっぱなし", Lang::JA}, {"キャンセル", Lang::JA},
    {"undo", Lang::EN}, {"revert", Lang::EN}, {"go back", Lang::EN}, {"cancel that", Lang::EN}, {"never mind", Lang::EN},
    {"되돌려", Lang::KO}, {"되돌려줘", Lang::KO}, {"취소", Lang::KO}, {"원래대로", Lang::KO},
    {"撤销", Lang::ZH}, {"撤銷", Lang::ZH}, {"恢复", Lang::ZH}, {"还原", Lang::ZH}, {"取消", Lang::ZH},
    {"batalkan", Lang::ID}, {"kembalikan", Lang::ID}, {"undo", Lang::ID}, {"balikkan", Lang::ID},
};
inline constexpr Phrase kReset[] = {
    {"リセット", Lang::JA}, {"りせっと", Lang::JA}, {"初期化", Lang::JA}, {"しょきか", Lang::JA}, {"全部戻して", Lang::JA},
    {"reset", Lang::EN}, {"reset everything", Lang::EN}, {"factory reset", Lang::EN}, {"default", Lang::EN},
    {"초기화", Lang::KO}, {"리셋", Lang::KO},
    {"重置", Lang::ZH}, {"初始化", Lang::ZH}, {"恢复默认", Lang::ZH},
    {"atur ulang", Lang::ID}, {"reset", Lang::ID}, {"kembali ke awal", Lang::ID},
};
inline constexpr Phrase kCalibrate[] = {
    {"測定", Lang::JA}, {"そくてい", Lang::JA}, {"キャリブレーション", Lang::JA}, {"きゃりぶれーしょん", Lang::JA}, {"カンペ", Lang::JA}, {"自動調整", Lang::JA}, {"調整して", Lang::JA},
    {"calibrate", Lang::EN}, {"calibration", Lang::EN}, {"measure", Lang::EN}, {"auto adjust", Lang::EN}, {"auto level", Lang::EN},
    {"측정", Lang::KO}, {"캘리브레이션", Lang::KO}, {"자동 조정", Lang::KO},
    {"校准", Lang::ZH}, {"校準", Lang::ZH}, {"测量", Lang::ZH}, {"自动调整", Lang::ZH},
    {"kalibrasi", Lang::ID}, {"ukur", Lang::ID}, {"atur otomatis", Lang::ID},
};
inline constexpr Phrase kFxOff[] = {
    {"エコー切って", Lang::JA}, {"えこーきって", Lang::JA}, {"エコーオフ", Lang::JA}, {"えこーおふ", Lang::JA}, {"エコー消して", Lang::JA}, {"ドライ", Lang::JA}, {"どらい", Lang::JA}, {"エフェクト切って", Lang::JA}, {"エフェクトオフ", Lang::JA}, {"お風呂やめて", Lang::JA},
    {"echo off", Lang::EN}, {"dry", Lang::EN}, {"no echo", Lang::EN}, {"effect off", Lang::EN}, {"reverb off", Lang::EN}, {"turn off echo", Lang::EN},
    {"에코 꺼", Lang::KO}, {"에코 끄기", Lang::KO}, {"이펙트 꺼", Lang::KO}, {"드라이", Lang::KO},
    {"关闭回声", Lang::ZH}, {"關閉回聲", Lang::ZH}, {"回声关", Lang::ZH}, {"干声", Lang::ZH}, {"关掉效果", Lang::ZH},
    {"matikan echo", Lang::ID}, {"matikan gema", Lang::ID}, {"tanpa echo", Lang::ID}, {"kering", Lang::ID}, {"matikan efek", Lang::ID},
};
struct FxPhrase { const char* utf8; Lang lang; int fx_mode; };
inline constexpr FxPhrase kFxOn[] = {
    {"お風呂", Lang::JA, 2}, {"おふろ", Lang::JA, 2}, {"風呂", Lang::JA, 2}, {"ふろ", Lang::JA, 2}, {"バス", Lang::JA, 2},
    {"カラオケ", Lang::JA, 1}, {"からおけ", Lang::JA, 1},
    {"ホール", Lang::JA, 3}, {"ほーる", Lang::JA, 3}, {"コンサート", Lang::JA, 3},
    {"洞窟", Lang::JA, 4}, {"どうくつ", Lang::JA, 4}, {"ケイブ", Lang::JA, 4},
    {"無線", Lang::JA, 5}, {"むせん", Lang::JA, 5}, {"ラジオ", Lang::JA, 5}, {"らじお", Lang::JA, 5}, {"トランシーバー", Lang::JA, 5},
    {"エコー", Lang::JA, 2}, {"えこー", Lang::JA, 2}, {"リバーブ", Lang::JA, 3},
    {"bath", Lang::EN, 2}, {"bathroom", Lang::EN, 2}, {"karaoke", Lang::EN, 1}, {"hall", Lang::EN, 3}, {"concert", Lang::EN, 3},
    {"cave", Lang::EN, 4}, {"radio", Lang::EN, 5}, {"walkie", Lang::EN, 5}, {"echo", Lang::EN, 2}, {"reverb", Lang::EN, 3},
    {"욕실", Lang::KO, 2}, {"목욕탕", Lang::KO, 2}, {"노래방", Lang::KO, 1}, {"홀", Lang::KO, 3}, {"동굴", Lang::KO, 4}, {"무전기", Lang::KO, 5}, {"라디오", Lang::KO, 5}, {"에코", Lang::KO, 2},
    {"浴室", Lang::ZH, 2}, {"卡拉OK", Lang::ZH, 1}, {"卡拉ok", Lang::ZH, 1}, {"大厅", Lang::ZH, 3}, {"音乐厅", Lang::ZH, 3}, {"洞穴", Lang::ZH, 4}, {"对讲机", Lang::ZH, 5}, {"收音机", Lang::ZH, 5}, {"回声", Lang::ZH, 2}, {"回聲", Lang::ZH, 2},
    {"kamar mandi", Lang::ID, 2}, {"karaoke", Lang::ID, 1}, {"aula", Lang::ID, 3}, {"gua", Lang::ID, 4}, {"radio", Lang::ID, 5}, {"walkie", Lang::ID, 5}, {"gema", Lang::ID, 2}, {"echo", Lang::ID, 2},
};
struct ScenePhrase { const char* utf8; Lang lang; int scene; };
inline constexpr ScenePhrase kScenes[] = {
    {"FPS", Lang::JA, 0}, {"fps", Lang::JA, 0}, {"ゲームモード", Lang::JA, 0}, {"げーむもーど", Lang::JA, 0}, {"ゲーム配信", Lang::JA, 0},
    {"雑談", Lang::JA, 1}, {"ざつだん", Lang::JA, 1}, {"トークモード", Lang::JA, 1}, {"とーくもーど", Lang::JA, 1}, {"トーク", Lang::JA, 1},
    {"歌枠", Lang::JA, 2}, {"うたわく", Lang::JA, 2}, {"歌モード", Lang::JA, 2}, {"うたもーど", Lang::JA, 2}, {"歌います", Lang::JA, 2}, {"シンギング", Lang::JA, 2},
    {"ASMR", Lang::JA, 3}, {"asmr", Lang::JA, 3}, {"えーえすえむあーる", Lang::JA, 3}, {"囁き", Lang::JA, 3}, {"ささやき", Lang::JA, 3},
    {"fps mode", Lang::EN, 0}, {"game mode", Lang::EN, 0}, {"gaming mode", Lang::EN, 0}, {"fps", Lang::EN, 0},
    {"talk mode", Lang::EN, 1}, {"chat mode", Lang::EN, 1}, {"just chatting", Lang::EN, 1}, {"talk", Lang::EN, 1},
    {"singing mode", Lang::EN, 2}, {"singing", Lang::EN, 2}, {"karaoke mode", Lang::EN, 2}, {"sing", Lang::EN, 2},
    {"asmr mode", Lang::EN, 3}, {"asmr", Lang::EN, 3}, {"whisper mode", Lang::EN, 3},
    {"게임 모드", Lang::KO, 0}, {"에프피에스", Lang::KO, 0}, {"fps", Lang::KO, 0},
    {"토크 모드", Lang::KO, 1}, {"잡담", Lang::KO, 1}, {"수다", Lang::KO, 1},
    {"노래 모드", Lang::KO, 2}, {"노래방송", Lang::KO, 2}, {"노래", Lang::KO, 2},
    {"asmr", Lang::KO, 3}, {"속삭임", Lang::KO, 3},
    {"游戏模式", Lang::ZH, 0}, {"遊戲模式", Lang::ZH, 0}, {"fps", Lang::ZH, 0},
    {"聊天模式", Lang::ZH, 1}, {"杂谈", Lang::ZH, 1}, {"雜談", Lang::ZH, 1}, {"聊天", Lang::ZH, 1},
    {"唱歌模式", Lang::ZH, 2}, {"唱歌", Lang::ZH, 2}, {"歌回", Lang::ZH, 2},
    {"asmr", Lang::ZH, 3}, {"耳语", Lang::ZH, 3}, {"轻声", Lang::ZH, 3},
    {"mode game", Lang::ID, 0}, {"mode fps", Lang::ID, 0}, {"fps", Lang::ID, 0},
    {"mode ngobrol", Lang::ID, 1}, {"ngobrol", Lang::ID, 1}, {"mode bicara", Lang::ID, 1},
    {"mode nyanyi", Lang::ID, 2}, {"nyanyi", Lang::ID, 2}, {"bernyanyi", Lang::ID, 2},
    {"asmr", Lang::ID, 3}, {"mode bisik", Lang::ID, 3}, {"berbisik", Lang::ID, 3},
};

// Number words for "3dB上げて" style phrases.
struct NumberWord { const char* utf8; float value; };
inline constexpr NumberWord kNumberWords[] = {
    {"一", 1}, {"二", 2}, {"三", 3}, {"四", 4}, {"五", 5}, {"六", 6}, {"七", 7}, {"八", 8}, {"九", 9}, {"十", 10},
    {"いち", 1}, {"に", 2}, {"さん", 3}, {"よん", 4}, {"ご", 5}, {"ろく", 6}, {"なな", 7}, {"はち", 8}, {"きゅう", 9}, {"じゅう", 10},
    {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}, {"five", 5}, {"six", 6}, {"seven", 7}, {"eight", 8}, {"nine", 9}, {"ten", 10}, {"twelve", 12},
    {"하나", 1}, {"둘", 2}, {"셋", 3}, {"넷", 4}, {"다섯", 5}, {"여섯", 6}, {"일곱", 7}, {"여덟", 8}, {"아홉", 9}, {"열", 10},
    {"일", 1}, {"이", 2}, {"삼", 3}, {"사", 4}, {"오", 5}, {"육", 6}, {"칠", 7}, {"팔", 8}, {"구", 9}, {"십", 10},
    {"两", 2}, {"兩", 2}, {"俩", 2},
    {"satu", 1}, {"dua", 2}, {"tiga", 3}, {"empat", 4}, {"lima", 5}, {"enam", 6}, {"tujuh", 7}, {"delapan", 8}, {"sembilan", 9}, {"sepuluh", 10},
};

// -----------------------------------------------------------------------------
//  Parser
// -----------------------------------------------------------------------------
class IntentParser {
public:
    IntentParser() { build_cache(); }

    // Restrict matching to one language (AUTO = all five).
    void set_language(Lang l) noexcept { lang_ = l; }
    Lang language() const noexcept { return lang_; }

    Command parse(std::string_view utf8_transcript) const {
        Command cmd;
        const std::u32string text = normalize(utf8_transcript);
        if (text.empty()) return cmd;

        // 1. Wake word (mandatory). Exact first, then fuzzy.
        float best_wake = 0.0f; std::u32string wake_norm;
        for (int pass = 0; pass < 2 && best_wake <= 0.0f; ++pass) {
            for (const auto& w : wake_cache_) {
                if (!lang_ok(w.lang)) continue;
                const MatchResult m = fuzzy_contains(text, w.norm, pass == 1);
                if (m.hit && m.score > best_wake) { best_wake = m.score; cmd.matched_wake = w.utf8; cmd.lang = w.lang; wake_norm = w.norm; }
            }
        }
        cmd.wake_detected = best_wake > 0.0f;
        if (!cmd.wake_detected) return cmd;

        // Text with the wake word removed, so "おんきょう**さん**" never reads as "3".
        std::u32string body = text;
        if (!wake_norm.empty()) {
            const std::size_t pos = body.find(wake_norm);
            if (pos != std::u32string::npos) body.erase(pos, wake_norm.size());
        }

        // 2. Intents: an exact pass over every dictionary wins over any fuzzy
        //    hit, so "mute me" can never be absorbed by a fuzzy "unmute me".
        float conf = 0.0f;
        for (int pass = 0; pass < 2 && cmd.intent == Intent::NONE; ++pass) {
            const bool fz = (pass == 1);
            classify(body, fz, cmd, conf, utf8_transcript);
        }
        if (cmd.intent == Intent::MUTE_MIC || cmd.intent == Intent::UNMUTE_MIC || cmd.intent == Intent::BLEEP ||
            cmd.intent == Intent::COUGH_MUTE || cmd.intent == Intent::CALIBRATE)
            cmd.target = Target::VOICE;
        cmd.confidence = cmd.intent == Intent::NONE ? 0.0f : (best_wake * 0.4f + conf * 0.6f);
        return cmd;
    }

private:
    struct Entry { std::string utf8; std::u32string norm; Lang lang; int extra; };

    bool lang_ok(Lang l) const noexcept { return lang_ == Lang::AUTO || lang_ == l; }

    bool match_any(const std::u32string& text, const std::vector<Entry>& dict, float& conf, std::string& phrase, bool fuzzy) const {
        bool hit = false;
        for (const auto& e : dict) {
            if (!lang_ok(e.lang)) continue;
            const MatchResult m = fuzzy_contains(text, e.norm, fuzzy);
            if (m.hit && m.score > conf) { conf = m.score; phrase = e.utf8; hit = true; }
        }
        return hit;
    }

    // One classification pass (exact or fuzzy). Order = specificity.
    void classify(const std::u32string& body, bool fz, Command& cmd, float& conf, std::string_view raw) const {
        if (match_any(body, unmute_cache_, conf, cmd.matched_phrase, fz)) { cmd.intent = Intent::UNMUTE_MIC; return; }
        if (match_any(body, undo_cache_, conf, cmd.matched_phrase, fz)) { cmd.intent = Intent::UNDO; return; }
        if (match_any(body, reset_cache_, conf, cmd.matched_phrase, fz)) { cmd.intent = Intent::RESET; return; }
        if (match_any(body, calibrate_cache_, conf, cmd.matched_phrase, fz)) { cmd.intent = Intent::CALIBRATE; return; }
        if (match_any(body, fxoff_cache_, conf, cmd.matched_phrase, fz)) { cmd.intent = Intent::FX_OFF; return; }
        if (match_any(body, chapter_cache_, conf, cmd.matched_phrase, fz)) { cmd.intent = Intent::CHAPTER_MARK; return; }
        if (match_any(body, bleep_cache_, conf, cmd.matched_phrase, fz)) { cmd.intent = Intent::BLEEP; return; }
        if (match_any(body, cough_cache_, conf, cmd.matched_phrase, fz)) { cmd.intent = Intent::COUGH_MUTE; return; }
        if (match_any(body, mute_cache_, conf, cmd.matched_phrase, fz)) { cmd.intent = Intent::MUTE_MIC; return; }

        int scene = -1; float sc = 0.0f; std::string sp;
        for (const auto& s : scene_cache_) {
            if (!lang_ok(s.lang)) continue;
            const MatchResult m = fuzzy_contains(body, s.norm, fz);
            if (m.hit && m.score > sc) { sc = m.score; scene = s.extra; sp = s.utf8; }
        }
        float up_c = 0.0f, dn_c = 0.0f; std::string up_p, dn_p;
        match_any(body, up_cache_, up_c, up_p, fz);
        match_any(body, down_cache_, dn_c, dn_p, fz);
        int fx = -1; float fc = 0.0f; std::string fp;
        for (const auto& f : fxon_cache_) {
            if (!lang_ok(f.lang)) continue;
            const MatchResult m = fuzzy_contains(body, f.norm, fz);
            if (m.hit && m.score > fc) { fc = m.score; fx = f.extra; fp = f.utf8; }
        }
        if (up_c > 0.0f || dn_c > 0.0f) {
            cmd.intent = Intent::TRIM_DELTA;
            const bool up = up_c >= dn_c;
            conf = up ? up_c : dn_c;
            cmd.matched_phrase = up ? up_p : dn_p;
            float step = extract_number(body, raw);
            if (step <= 0.0f) step = kDefaultStepDb;
            if (step > kMaxSingleStepDb) step = kMaxSingleStepDb;
            cmd.value_db = up ? step : -step;
            cmd.target = detect_target(body, fz);
            if (cmd.target == Target::NONE) cmd.target = Target::VOICE;
        } else if (scene >= 0 && sc >= fc) {
            cmd.intent = Intent::SCENE_SET; cmd.scene = scene; conf = sc; cmd.matched_phrase = sp;
        } else if (fx >= 0) {
            cmd.intent = Intent::FX_SET; cmd.fx_mode = fx; conf = fc; cmd.matched_phrase = fp;
        }
    }

    Target detect_target(const std::u32string& text, bool fuzzy) const {
        float c[6] = {0, 0, 0, 0, 0, 0}; std::string p;
        // Exact first; only fall back to fuzzy when nothing matched exactly.
        for (int pass = 0; pass < (fuzzy ? 2 : 1); ++pass) {
            const bool fz = pass == 1;
            match_any(text, tvoice_cache_, c[1], p, fz);
            match_any(text, tcollab_cache_, c[2], p, fz);
            match_any(text, tgame_cache_, c[3], p, fz);
            match_any(text, tbgm_cache_, c[4], p, fz);
            match_any(text, tmaster_cache_, c[5], p, fz);
            bool any = false; for (int i = 1; i < 6; ++i) any = any || c[i] > 0.0f;
            if (any) break;
        }
        int best = 0; float bc = 0.0f;
        for (int i = 1; i < 6; ++i) if (c[i] > bc) { bc = c[i]; best = i; }
        return static_cast<Target>(best);
    }

    // True when a dB unit immediately follows position `pos` (after folding).
    static bool unit_follows(const std::u32string& t, std::size_t pos) {
        static const std::u32string units[] = { U"db", U"でしべる", U"데시벨", U"디비", U"分贝", U"分貝", U"desibel", U"dezibel" };
        for (const auto& u : units) if (t.compare(pos, u.size(), u) == 0) return true;
        return false;
    }

    // Finds the first ASCII number (after width folding) or a number word.
    // The wake word has already been removed from `norm` by the caller.
    float extract_number(const std::u32string& norm, std::string_view raw) const {
        (void)raw;
        for (std::size_t i = 0; i < norm.size(); ++i) {
            if (norm[i] >= '0' && norm[i] <= '9') {
                float v = 0.0f; std::size_t j = i;
                while (j < norm.size() && norm[j] >= '0' && norm[j] <= '9') { v = v * 10.0f + static_cast<float>(norm[j] - '0'); ++j; }
                if (j < norm.size() && norm[j] == '.' && j + 1 < norm.size() && norm[j + 1] >= '0' && norm[j + 1] <= '9') {
                    v += static_cast<float>(norm[j + 1] - '0') * 0.1f;
                }
                return v;
            }
        }
        // Number words. Short kana / hangul numerals ("に", "さん", "이", "오")
        // double as particles, so they only count when a dB unit follows.
        float best = 0.0f; std::size_t best_len = 0;
        for (const auto& nw : number_cache_) {
            const std::size_t pos = norm.find(nw.norm);
            if (pos == std::u32string::npos) continue;
            if (nw.ambiguous && !unit_follows(norm, pos + nw.norm.size())) continue;
            if (nw.norm.size() > best_len) { best = nw.value; best_len = nw.norm.size(); }
        }
        return best;
    }

    template <std::size_t N>
    static void fill(std::vector<Entry>& out, const Phrase (&src)[N]) {
        out.reserve(N);
        for (const auto& p : src) out.push_back({p.utf8, normalize(p.utf8), p.lang, 0});
    }
    void build_cache() {
        fill(wake_cache_, kWakeWords);
        fill(up_cache_, kUp); fill(down_cache_, kDown);
        fill(tvoice_cache_, kTargetVoice); fill(tcollab_cache_, kTargetCollab);
        fill(tgame_cache_, kTargetGame); fill(tbgm_cache_, kTargetBgm); fill(tmaster_cache_, kTargetMaster);
        fill(mute_cache_, kMute); fill(unmute_cache_, kUnmute); fill(bleep_cache_, kBleep); fill(cough_cache_, kCough);
        fill(chapter_cache_, kChapter); fill(undo_cache_, kUndo); fill(reset_cache_, kReset); fill(calibrate_cache_, kCalibrate);
        fill(fxoff_cache_, kFxOff);
        for (const auto& f : kFxOn) fxon_cache_.push_back({f.utf8, normalize(f.utf8), f.lang, f.fx_mode});
        for (const auto& s : kScenes) scene_cache_.push_back({s.utf8, normalize(s.utf8), s.lang, s.scene});
        for (const auto& n : kNumberWords) {
            const std::u32string nn = normalize(n.utf8);
            // Ambiguous: 1-2 code points of kana or hangul (homophones of particles).
            bool amb = false;
            if (nn.size() <= 2) {
                for (char32_t c : nn) if ((c >= 0x3041 && c <= 0x3096) || (c >= 0xAC00 && c <= 0xD7A3)) amb = true;
            }
            number_cache_.push_back({nn, n.value, amb});
        }
    }

    struct NumEntry { std::u32string norm; float value; bool ambiguous; };
    Lang lang_ = Lang::AUTO;
    std::vector<Entry> wake_cache_, up_cache_, down_cache_;
    std::vector<Entry> tvoice_cache_, tcollab_cache_, tgame_cache_, tbgm_cache_, tmaster_cache_;
    std::vector<Entry> mute_cache_, unmute_cache_, bleep_cache_, cough_cache_, chapter_cache_;
    std::vector<Entry> undo_cache_, reset_cache_, calibrate_cache_, fxoff_cache_, fxon_cache_, scene_cache_;
    std::vector<NumEntry> number_cache_;
};

// -----------------------------------------------------------------------------
//  Undo stack (fixed capacity, no heap after construction).
// -----------------------------------------------------------------------------
struct UndoRecord {
    Intent intent = Intent::NONE;
    Target target = Target::NONE;
    float  previous_value = 0.0f;  // Trim before the change, or previous fx/scene as float.
    float  new_value = 0.0f;
    bool   valid = false;
};

class UndoStack {
public:
    static constexpr int kDepth = 8;
    void push(const UndoRecord& r) noexcept {
        ring_[static_cast<std::size_t>(top_)] = r;
        ring_[static_cast<std::size_t>(top_)].valid = true;
        top_ = (top_ + 1) % kDepth;
        if (count_ < kDepth) ++count_;
    }
    bool pop(UndoRecord& out) noexcept {
        if (count_ == 0) return false;
        top_ = (top_ - 1 + kDepth) % kDepth;
        out = ring_[static_cast<std::size_t>(top_)];
        ring_[static_cast<std::size_t>(top_)].valid = false;
        --count_;
        return out.valid;
    }
    int size() const noexcept { return count_; }
    void clear() noexcept { count_ = 0; top_ = 0; for (auto& r : ring_) r.valid = false; }
private:
    std::array<UndoRecord, kDepth> ring_{};
    int top_ = 0, count_ = 0;
};

// Clamp helper shared by every applier: voice trims live in [-12, +6] dB.
inline float apply_voice_trim(float current_db, float delta_db) noexcept {
    float v = current_db + delta_db;
    if (v < kVoiceTrimMinDb) v = kVoiceTrimMinDb;
    if (v > kVoiceTrimMaxDb) v = kVoiceTrimMaxDb;
    return v;
}

// Human-readable intent name for logs / UI (ASCII only, safe anywhere).
inline const char* intent_name(Intent i) noexcept {
    switch (i) {
        case Intent::NONE: return "NONE";
        case Intent::TRIM_DELTA: return "TRIM_DELTA";
        case Intent::MUTE_MIC: return "MUTE_MIC";
        case Intent::UNMUTE_MIC: return "UNMUTE_MIC";
        case Intent::BLEEP: return "BLEEP";
        case Intent::COUGH_MUTE: return "COUGH_MUTE";
        case Intent::CHAPTER_MARK: return "CHAPTER_MARK";
        case Intent::FX_SET: return "FX_SET";
        case Intent::FX_OFF: return "FX_OFF";
        case Intent::SCENE_SET: return "SCENE_SET";
        case Intent::UNDO: return "UNDO";
        case Intent::RESET: return "RESET";
        case Intent::CALIBRATE: return "CALIBRATE";
    }
    return "NONE";
}
inline const char* target_name(Target t) noexcept {
    switch (t) {
        case Target::NONE: return "NONE";
        case Target::VOICE: return "VOICE";
        case Target::COLLAB: return "COLLAB";
        case Target::GAME: return "GAME";
        case Target::BGM: return "BGM";
        case Target::MASTER: return "MASTER";
    }
    return "NONE";
}

// -----------------------------------------------------------------------------
//  Self-test (returns number of failures). Used by the test harness and can be
//  run at plugin load in debug builds.
// -----------------------------------------------------------------------------
inline int run_selftest() {
    IntentParser p;
    int fails = 0;
    auto expect = [&](const char* text, Intent in, Target tg, float val, bool fire) {
        const Command c = p.parse(text);
        const bool ok = (c.fires() == fire) && (!fire || (c.intent == in && (tg == Target::NONE || c.target == tg) &&
                        (in != Intent::TRIM_DELTA || (c.value_db > val - 0.01f && c.value_db < val + 0.01f))));
        if (!ok) ++fails;
    };
    expect("音響さん、BGM3dB下げて", Intent::TRIM_DELTA, Target::BGM, -3.0f, true);
    expect("おんきょうさん ゲーム音 上げて", Intent::TRIM_DELTA, Target::GAME, 2.0f, true);
    expect("ディレクター、元に戻して", Intent::UNDO, Target::NONE, 0.0f, true);
    expect("音響さんお風呂にして", Intent::FX_SET, Target::NONE, 0.0f, true);
    expect("音響さん、エコー切って", Intent::FX_OFF, Target::NONE, 0.0f, true);
    expect("音響さんピー音", Intent::BLEEP, Target::VOICE, 0.0f, true);
    expect("音響さん、ここ見どころ", Intent::CHAPTER_MARK, Target::NONE, 0.0f, true);
    expect("音響さん歌枠モード", Intent::SCENE_SET, Target::NONE, 0.0f, true);
    expect("BGM下げて", Intent::NONE, Target::NONE, 0.0f, false);         // No wake word -> never fires.
    expect("hey director, turn the music down 4 db", Intent::TRIM_DELTA, Target::BGM, -4.0f, true);
    expect("director undo", Intent::UNDO, Target::NONE, 0.0f, true);
    expect("director mute me", Intent::MUTE_MIC, Target::VOICE, 0.0f, true);
    expect("director unmute", Intent::UNMUTE_MIC, Target::VOICE, 0.0f, true);
    expect("감독님 게임 소리 내려줘", Intent::TRIM_DELTA, Target::GAME, -2.0f, true);
    expect("导演 背景音乐 调低 三", Intent::TRIM_DELTA, Target::BGM, -3.0f, true);
    expect("sutradara, kecilkan musik dua", Intent::TRIM_DELTA, Target::BGM, -2.0f, true);
    expect("音響さん　ＢＧＭ　１０ｄＢ上げて", Intent::TRIM_DELTA, Target::BGM, 6.0f, true); // Clamped to +6 per step.
    expect("おんきょさん ミュート解除", Intent::UNMUTE_MIC, Target::VOICE, 0.0f, true);   // Fuzzy wake ("おんきょう"→"おんきょ").
    return fails;
}

} // namespace sw::model600::voice

#endif // SEVENTHWELL_MODEL600_VOICE_COMMAND_MULTILINGUAL_HPP
