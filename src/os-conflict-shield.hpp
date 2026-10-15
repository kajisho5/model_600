// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  os-conflict-shield.hpp  [Layer 2]  OS "helpfulness" diagnostics & repair.
//
//  Detects OS features that silently fight a broadcast chain and offers a
//  one-click fix, all scoped to the current user's own audio preferences:
//
//    Windows:
//      * HKCU\...\Audio\UserDuckingPreference != 3  -> Windows ducks other
//        apps 80 % during "communications". Report + one-click set to 3.
//      * "Audio Enhancements" (per-device AI noise suppression) warning plus a
//        button that opens ms-settings:sound.
//    macOS:
//      * Mic mode "Voice Isolation" eats sung / screamed vocals -> guidance.
//      * Bluetooth HFP fallback drops the rate to 16/24 kHz -> live alert.
//
//  Everything is read-only until the user presses "repair". Qt6::Core only;
//  the platform calls are guarded so this header compiles on every target.
// =============================================================================
#ifndef SEVENTHWELL_MODEL600_OS_CONFLICT_SHIELD_HPP
#define SEVENTHWELL_MODEL600_OS_CONFLICT_SHIELD_HPP

#include <vector>

#include <QString>
#include <QStringList>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>
#endif

#if defined(__APPLE__)
#  include <QProcess>
#endif

namespace sw::model600::osshield {

enum class Severity : int { Info = 0, Warning = 1, Critical = 2 };
enum class IssueId : int {
    None = 0,
    WinDucking,        // UserDuckingPreference != 3
    WinAudioEnhance,   // "Audio Enhancements" likely on
    MacVoiceIsolation, // Mic mode = Voice Isolation
    MacBluetoothHfp    // BT device fell back to HFP 16/24 kHz
};

struct Issue {
    IssueId  id = IssueId::None;
    Severity severity = Severity::Info;
    QString  title;        // English
    QString  title_ja;     // Japanese
    QString  detail;
    QString  detail_ja;
    bool     repairable = false;      // A one-click fix exists.
    QString  action_label;            // e.g. "Fix now" / "Open sound settings".
    QString  action_label_ja;
};

// -----------------------------------------------------------------------------
//  Shield
// -----------------------------------------------------------------------------
class ConflictShield {
public:
    // Scan returns every detected issue (empty == all clear).
    std::vector<Issue> scan(float current_sample_rate = 48000.0f) {
        std::vector<Issue> out;
#if defined(_WIN32)
        scan_windows(out);
        (void)current_sample_rate;
#elif defined(__APPLE__)
        scan_macos(out, current_sample_rate);
#else
        (void)current_sample_rate; // Linux / other: nothing to police.
#endif
        return out;
    }

    // Perform the repair for a repairable issue. Returns true on success.
    bool repair(IssueId id) {
#if defined(_WIN32)
        switch (id) {
            case IssueId::WinDucking:      return win_set_ducking_preference(3);
            case IssueId::WinAudioEnhance: return win_open_sound_settings();
            default: return false;
        }
#elif defined(__APPLE__)
        switch (id) {
            case IssueId::MacVoiceIsolation: return mac_open_sound_settings();
            default: return false;
        }
#else
        (void)id; return false;
#endif
    }

#if defined(_WIN32)
private:
    static void scan_windows(std::vector<Issue>& out) {
        DWORD pref = 0;
        if (win_read_ducking_preference(pref) && pref != 3) {
            Issue i;
            i.id = IssueId::WinDucking; i.severity = Severity::Critical; i.repairable = true;
            i.title = QStringLiteral("Windows is auto-ducking your other apps by 80%%");
            i.title_ja = QStringLiteral("Windowsが通話時にゲーム音を勝手に80%%下げる設定です");
            i.detail = QStringLiteral("Communications ducking is set to \"%1\". During any call Windows "
                                      "lowers game/BGM volume, fighting MODEL 600. Recommended: Do nothing.").arg(pref);
            i.detail_ja = QStringLiteral("「通信時の動作」が「%1」になっています。通話中にWindowsがゲーム/BGMを"
                                         "勝手に下げてMODEL 600と干渉します。推奨：『何もしない』。").arg(pref);
            i.action_label = QStringLiteral("Fix now (set to \"Do nothing\")");
            i.action_label_ja = QStringLiteral("今すぐ修復（『何もしない』に設定）");
            out.push_back(i);
        }
        // Audio Enhancements: there is no reliable global HKCU flag (it is
        // per-endpoint under MMDevices with opaque property stores), so we
        // surface a non-destructive advisory with a shortcut to the panel.
        {
            Issue i;
            i.id = IssueId::WinAudioEnhance; i.severity = Severity::Warning; i.repairable = true;
            i.title = QStringLiteral("Check \"Audio Enhancements\" on your mic");
            i.title_ja = QStringLiteral("マイクの「オーディオの強化」を確認してください");
            i.detail = QStringLiteral("Windows 11 AI noise suppression can mute your voice when you shout. "
                                      "If your voice disappears on screams, turn enhancements off for the mic.");
            i.detail_ja = QStringLiteral("Windows 11のAIノイズ抑制は絶叫時に声を消すことがあります。"
                                         "叫ぶと声が消える場合はマイクの『オーディオの強化』をオフにしてください。");
            i.action_label = QStringLiteral("Open sound settings");
            i.action_label_ja = QStringLiteral("サウンド設定を開く");
            out.push_back(i);
        }
    }

    static bool win_read_ducking_preference(DWORD& value) {
        HKEY key = nullptr;
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
                "Software\\Microsoft\\Multimedia\\Audio",
                0, KEY_READ, &key) != ERROR_SUCCESS)
            return false;
        DWORD type = 0, data = 0, size = sizeof(data);
        const LONG rc = RegQueryValueExA(key, "UserDuckingPreference", nullptr, &type,
                                         reinterpret_cast<LPBYTE>(&data), &size);
        RegCloseKey(key);
        if (rc != ERROR_SUCCESS || type != REG_DWORD) { value = 3; return false; }
        value = data; return true;
    }

    static bool win_set_ducking_preference(DWORD value) {
        HKEY key = nullptr;
        if (RegCreateKeyExA(HKEY_CURRENT_USER,
                "Software\\Microsoft\\Multimedia\\Audio",
                0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            return false;
        const LONG rc = RegSetValueExA(key, "UserDuckingPreference", 0, REG_DWORD,
                                       reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
        return rc == ERROR_SUCCESS;
    }

    static bool win_open_sound_settings() {
        const HINSTANCE h = ShellExecuteA(nullptr, "open", "ms-settings:sound", nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(h) > 32;
    }

#elif defined(__APPLE__)
private:
    static void scan_macos(std::vector<Issue>& out, float fs) {
        // Bluetooth HFP fallback: any active rate <= 24 kHz means the headset
        // switched to the phone-call profile and the stream now sounds muffled.
        if (fs > 0.0f && fs <= 24000.5f) {
            Issue i;
            i.id = IssueId::MacBluetoothHfp; i.severity = Severity::Critical; i.repairable = false;
            i.title = QStringLiteral("Bluetooth dropped to call quality (%1 kHz)").arg(static_cast<int>(fs / 1000.0f));
            i.title_ja = QStringLiteral("Bluetoothが電話音質（%1kHz）に低下しています").arg(static_cast<int>(fs / 1000.0f));
            i.detail = QStringLiteral("Your AirPods/headset fell back to the HFP hands-free profile. Audio is "
                                      "narrow-band. Use a wired mic, or set input to the Mac mic while streaming.");
            i.detail_ja = QStringLiteral("AirPods/ヘッドセットがHFP(ハンズフリー)プロファイルに切り替わり、"
                                         "狭帯域になっています。配信中は有線マイクかMac内蔵マイクを使用してください。");
            out.push_back(i);
        }
        // Voice Isolation advisory (there is no public API to read the mic mode;
        // surface guidance with a shortcut to Sound settings).
        {
            Issue i;
            i.id = IssueId::MacVoiceIsolation; i.severity = Severity::Warning; i.repairable = true;
            i.title = QStringLiteral("Check the menubar \"Mic Mode\"");
            i.title_ja = QStringLiteral("メニューバーの「マイクモード」を確認してください");
            i.detail = QStringLiteral("If Mic Mode = Voice Isolation, macOS removes singing and screams as "
                                      "\"noise\". Set it to Standard for music/loud content.");
            i.detail_ja = QStringLiteral("マイクモードが『声を分離』だと、macOSが歌声や絶叫を『ノイズ』として"
                                         "消します。音楽・大音量配信では『標準』にしてください。");
            i.action_label = QStringLiteral("Open sound settings");
            i.action_label_ja = QStringLiteral("サウンド設定を開く");
            out.push_back(i);
        }
    }

    static bool mac_open_sound_settings() {
        return QProcess::startDetached(QStringLiteral("open"),
            {QStringLiteral("x-apple.systempreferences:com.apple.Sound-Settings.extension")});
    }
#else
private:
#endif
};

} // namespace sw::model600::osshield

#endif // SEVENTHWELL_MODEL600_OS_CONFLICT_SHIELD_HPP
