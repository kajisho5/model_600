// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  plugin-main.cpp  [Layer 3]  Module entry point.
//
//  Boots the licence manager, registers the 5 filters, wires Stream-Deck-ready
//  global hotkeys, drives a frontend timer (trim persistence + update check),
//  writes YouTube chapters with a dual stream/record timestamp, exports the
//  post-stream "audio karte", and tears everything down cleanly on exit with
//  an immediate abort of cross-process comms.
// =============================================================================
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>

#include <cstdio>
#include <ctime>
#include <string>

#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QTimer>

#include "license-manager.hpp"
#include "update-checker.hpp"
#include "voice-command-multilingual.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("model600", "en-US")

// Adapters implemented in audio-filter.cpp.
extern "C" void m600_register_filters(bool pro_edition);
extern "C" void m600_persist_trims();
extern "C" void m600_set_license(void* lic);
extern "C" void m600_hotkey_bleep();
extern "C" void m600_hotkey_cough(bool pressed);
extern "C" void m600_hotkey_echo_toggle();

using namespace sw::model600;

namespace {

license::LicenseManager* g_license = nullptr;
update::UpdateChecker*   g_updater = nullptr;
QTimer*                  g_tick = nullptr;

obs_hotkey_id g_hk_bleep = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id g_hk_cough = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id g_hk_chapter = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id g_hk_echo = OBS_INVALID_HOTKEY_ID;

std::int64_t g_stream_start_ms = 0;   // os_gettime_ns-based wall anchors.
std::int64_t g_record_start_ms = 0;

std::int64_t now_ms() { return static_cast<std::int64_t>(os_gettime_ns() / 1000000ull); }

QString timecode(std::int64_t elapsed_ms) {
    if (elapsed_ms < 0) elapsed_ms = 0;
    const std::int64_t s = elapsed_ms / 1000;
    return QString::asprintf("%02lld:%02lld:%02lld", static_cast<long long>(s / 3600), static_cast<long long>((s % 3600) / 60), static_cast<long long>(s % 60));
}

QString chapter_file_path() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (dir.isEmpty() || !QDir(dir).exists()) {
        // Fall back to the module config folder when the desktop is blocked.
        char* cfg = obs_module_config_path("");
        dir = QString::fromUtf8(cfg ? cfg : "");
        bfree(cfg);
        QDir().mkpath(dir);
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd"));
    return QDir(dir).filePath(QStringLiteral("MODEL600_chapters_%1.txt").arg(stamp));
}

void ensure_chapter_header(const QString& path) {
    if (QFile::exists(path)) return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        // First line is always the opening marker, per spec.
        f.write("00:00:00 配信開始 (Opening)\n");
        f.close();
    }
}

void write_chapter_mark(const QString& label) {
    const QString path = chapter_file_path();
    ensure_chapter_header(path);
    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) {
        blog(LOG_WARNING, "[MODEL 600] could not write chapter file: %s", path.toUtf8().constData());
        return;
    }
    const std::int64_t now = now_ms();
    const QString st = (g_stream_start_ms > 0) ? timecode(now - g_stream_start_ms) : QStringLiteral("--:--:--");
    const QString rt = (g_record_start_ms > 0) ? timecode(now - g_record_start_ms) : QStringLiteral("--:--:--");
    const QByteArray line = QStringLiteral("%1 ★%2  (stream %1 / rec %3)\n")
        .arg(st, label.isEmpty() ? QStringLiteral("見どころ") : label, rt).toUtf8();
    f.write(line);
    f.close();
    blog(LOG_INFO, "[MODEL 600] chapter mark @ stream %s / rec %s", st.toUtf8().constData(), rt.toUtf8().constData());
}

// --- Hotkey callbacks -------------------------------------------------------
void hk_bleep(void*, obs_hotkey_id, obs_hotkey_t*, bool pressed) { if (pressed) m600_hotkey_bleep(); }
void hk_cough(void*, obs_hotkey_id, obs_hotkey_t*, bool pressed) { m600_hotkey_cough(pressed); }
void hk_chapter(void*, obs_hotkey_id, obs_hotkey_t*, bool pressed) { if (pressed) write_chapter_mark(QString()); }
void hk_echo(void*, obs_hotkey_id, obs_hotkey_t*, bool pressed) { if (pressed) m600_hotkey_echo_toggle(); }

// --- Frontend events: capture stream/record start anchors -------------------
void on_frontend_event(enum obs_frontend_event ev, void*) {
    switch (ev) {
        case OBS_FRONTEND_EVENT_STREAMING_STARTED: g_stream_start_ms = now_ms(); break;
        case OBS_FRONTEND_EVENT_STREAMING_STOPPED: g_stream_start_ms = 0; break;
        case OBS_FRONTEND_EVENT_RECORDING_STARTED: g_record_start_ms = now_ms(); break;
        case OBS_FRONTEND_EVENT_RECORDING_STOPPED: g_record_start_ms = 0; break;
        case OBS_FRONTEND_EVENT_EXIT:
            // Immediate, deterministic teardown before Qt/libobs unwind.
            if (g_updater) g_updater->stop();
            if (g_tick) g_tick->stop();
            break;
        default: break;
    }
}

bool is_pro_edition() {
#if defined(MODEL600_PRO_EDITION)
    return true;
#else
    return false;
#endif
}

} // namespace

MODULE_EXPORT const char* obs_module_name(void) { return "MODEL 600 // BROADCAST SUITE"; }
MODULE_EXPORT const char* obs_module_description(void) {
    return "SEVENTHWELL MODEL 600 - broadcast-grade auto mixing suite (5 cooperating units).";
}

bool obs_module_load(void) {
    blog(LOG_INFO, "[MODEL 600] loading %s edition", is_pro_edition() ? "PRO" : "STANDARD");

    // Licence (config path doubles as the offline-token mirror location).
    char* cfg = obs_module_config_path("license.ini");
    g_license = new license::LicenseManager(QString::fromUtf8(cfg ? cfg : ""));
    bfree(cfg);
    m600_set_license(g_license);
    const license::Result lr = g_license->validate_offline();
    blog(LOG_INFO, "[MODEL 600] licence: %s", lr.message.toUtf8().constData());

    // Register the 5 filters (DIRECTOR only in the Pro binary).
    m600_register_filters(is_pro_edition());

    // Stream-Deck-ready global hotkeys.
    g_hk_bleep = obs_hotkey_register_frontend("model600.bleep",
        "[MODEL 600] 自主規制ピー音 (1kHz Bleep)", hk_bleep, nullptr);
    g_hk_cough = obs_hotkey_register_frontend("model600.cough",
        "[MODEL 600] 咳払いミュート (Cough Mute)", hk_cough, nullptr);
    g_hk_chapter = obs_hotkey_register_frontend("model600.chapter",
        "[MODEL 600] YouTubeチャプター記録", hk_chapter, nullptr);
    g_hk_echo = obs_hotkey_register_frontend("model600.echo",
        "[MODEL 600] お風呂エコー ON/OFF", hk_echo, nullptr);

    obs_frontend_add_event_callback(on_frontend_event, nullptr);

    // Update checker (amber header ramp is lit by the console via this signal).
    g_updater = new update::UpdateChecker(QStringLiteral("1.0.0"));
    g_updater->start();

    // Frontend heartbeat: persist bus-driven trims to scene data at 4 Hz.
    g_tick = new QTimer();
    g_tick->setInterval(250);
    QObject::connect(g_tick, &QTimer::timeout, []() { m600_persist_trims(); });
    g_tick->start();

#ifndef NDEBUG
    const int vf = voice::run_selftest();
    blog(vf == 0 ? LOG_INFO : LOG_WARNING, "[MODEL 600] voice intent self-test failures: %d", vf);
#endif
    return true;
}

void obs_module_unload(void) {
    if (g_updater) g_updater->stop();
    if (g_tick) { g_tick->stop(); delete g_tick; g_tick = nullptr; }
    obs_frontend_remove_event_callback(on_frontend_event, nullptr);
    if (g_hk_bleep != OBS_INVALID_HOTKEY_ID) obs_hotkey_unregister(g_hk_bleep);
    if (g_hk_cough != OBS_INVALID_HOTKEY_ID) obs_hotkey_unregister(g_hk_cough);
    if (g_hk_chapter != OBS_INVALID_HOTKEY_ID) obs_hotkey_unregister(g_hk_chapter);
    if (g_hk_echo != OBS_INVALID_HOTKEY_ID) obs_hotkey_unregister(g_hk_echo);
    delete g_updater; g_updater = nullptr;
    m600_set_license(nullptr);
    delete g_license; g_license = nullptr;
    blog(LOG_INFO, "[MODEL 600] unloaded");
}
