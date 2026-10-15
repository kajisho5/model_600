// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  audio-filter.cpp  [Layer 3]  OBS filter adapters for the 5 units.
//
//  This is the ~5% adapter layer. It owns no DSP maths of its own: it maps OBS
//  audio buffers onto dsp::Engine, exposes OBS properties, registers the
//  filters, wires global hotkeys, persists the remote-fader trim both ways, and
//  opens the Qt studio console. Everything portable lives in Layers 1-2.
// =============================================================================
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include <QApplication>
#include <QMessageBox>
#include <QMetaObject>
#include <QString>
#include <QPointer>
#include <QWidget>

#include "dsp-core.hpp"
#include "shared-state.hpp"
#include "custom-ui-dialog.hpp"
#include "license-manager.hpp"
#include "voice-command-multilingual.hpp"

using namespace sw::model600;

// Implemented in plugin-main.cpp.
QString m600_write_chapter(const QString& label);
QString m600_export_karte();

// -----------------------------------------------------------------------------
//  Setting keys (stable; part of the saved scene data).
// -----------------------------------------------------------------------------
namespace keys {
constexpr const char* input_gain   = "input_gain_db";
constexpr const char* output_trim  = "output_trim";
constexpr const char* ceiling      = "ceiling_db";
constexpr const char* zero_latency = "zero_latency";
constexpr const char* bypass       = "bypass";
constexpr const char* character    = "character";
constexpr const char* space_fx     = "space_fx";
constexpr const char* auto_mono    = "auto_mono";
constexpr const char* denoiser     = "upstream_denoiser";
constexpr const char* gate_db      = "gate_open_db";
constexpr const char* gate_on      = "gate_enabled";
constexpr const char* hpf_hz       = "hpf_hz";
constexpr const char* whisper      = "whisper_comp";
constexpr const char* presence     = "presence_eq";
constexpr const char* deess        = "deesser_depth_db";
constexpr const char* mic_muted    = "mic_muted";
constexpr const char* collab_lufs  = "collab_target_lufs";
constexpr const char* collab_duck  = "collab_duck_db";
constexpr const char* game_ceiling = "game_ceiling_db";
constexpr const char* game_scoop   = "game_scoop_db";
constexpr const char* bgm_pocket   = "bgm_pocket_db";
constexpr const char* bgm_safe     = "bgm_safe_mode";
constexpr const char* bgm_swell    = "bgm_auto_swell";
} // namespace keys

// -----------------------------------------------------------------------------
//  Per-instance state.
// -----------------------------------------------------------------------------
struct FilterInstance {
    Mode mode;
    obs_source_t* source = nullptr;          // The filter source (weak-held below).
    obs_weak_source_t* weak = nullptr;       // Avoids a strong cycle with the parent.
    dsp::Engine engine;
    SharedBus bus;
    std::uint32_t trim_rev_seen = 0;
    std::atomic<bool> settings_dirty{false};   // Main thread writes params -> scene data.
    std::atomic<bool> calib_autocommit{false}; // Voice-started calibration commits itself.
    int cached_channels = 2;
    float cached_rate = 48000.0f;
    QPointer<ui::StudioConsole> console;

    explicit FilterInstance(Mode m) : mode(m), engine(m) {}
};

// Global registry so the main-thread persistence sweep can reach every live
// instance. The audio thread never walks this list.
static std::mutex g_registry_mutex;
static std::vector<FilterInstance*> g_instances;

// Global licence (shared by all instances; decides Pro availability).
static license::LicenseManager* g_license = nullptr;

// Latest update version announced by the update checker (UI thread only).
static QString g_update_version;

static void start_voice_bridge(FilterInstance* f);
static void stop_voice_bridge(FilterInstance* f);

static void register_instance(FilterInstance* f) {
    std::lock_guard<std::mutex> lk(g_registry_mutex);
    g_instances.push_back(f);
}
static void unregister_instance(FilterInstance* f) {
    std::lock_guard<std::mutex> lk(g_registry_mutex);
    g_instances.erase(std::remove(g_instances.begin(), g_instances.end(), f), g_instances.end());
}

// -----------------------------------------------------------------------------
//  Display names (number + purpose, so beginners never mis-insert).
// -----------------------------------------------------------------------------
static const char* name_voice(void*)    { return "MODEL 600 - [01] マイク・声用 (VOCAL MASTER)"; }
static const char* name_collab(void*)   { return "MODEL 600 - [02] コラボ・通話用 (COLLAB RIDER)"; }
static const char* name_game(void*)     { return "MODEL 600 - [03] ゲーム音用 (GAME IMPACT)"; }
static const char* name_bgm(void*)      { return "MODEL 600 - [04] BGM・音楽用 (BGM POCKET)"; }
[[maybe_unused]] static const char* name_director(void*) { return "MODEL 600 - [05] 統合ミキサー＆声ディレクター (STREAM DIRECTOR)"; }

// -----------------------------------------------------------------------------
//  Settings <-> engine params
// -----------------------------------------------------------------------------
static void apply_settings(FilterInstance* f, obs_data_t* s) {
    auto& p = f->engine.params();
    p.input_gain_db.store(static_cast<float>(obs_data_get_double(s, keys::input_gain)));
    p.output_trim_db.store(static_cast<float>(obs_data_get_double(s, keys::output_trim)));
    p.ceiling_db.store(static_cast<float>(obs_data_get_double(s, keys::ceiling)));
    p.zero_latency.store(obs_data_get_bool(s, keys::zero_latency) ? 1 : 0);
    p.bypass.store(obs_data_get_bool(s, keys::bypass) ? 1 : 0);
    p.character.store(static_cast<int>(obs_data_get_int(s, keys::character)));
    p.space_fx.store(static_cast<int>(obs_data_get_int(s, keys::space_fx)));
    p.auto_mono.store(obs_data_get_bool(s, keys::auto_mono) ? 1 : 0);
    p.upstream_denoiser.store(obs_data_get_bool(s, keys::denoiser) ? 1 : 0);
    p.gate_open_db.store(static_cast<float>(obs_data_get_double(s, keys::gate_db)));
    p.gate_enabled.store(obs_data_get_bool(s, keys::gate_on) ? 1 : 0);
    p.hpf_hz.store(static_cast<float>(obs_data_get_double(s, keys::hpf_hz)));
    p.whisper_comp.store(obs_data_get_bool(s, keys::whisper) ? 1 : 0);
    p.presence_eq.store(obs_data_get_bool(s, keys::presence) ? 1 : 0);
    p.deesser_depth_db.store(static_cast<float>(obs_data_get_double(s, keys::deess)));
    p.mic_muted.store(obs_data_get_bool(s, keys::mic_muted) ? 1 : 0);
    p.collab_target_lufs.store(static_cast<float>(obs_data_get_double(s, keys::collab_lufs)));
    p.collab_duck_db.store(static_cast<float>(obs_data_get_double(s, keys::collab_duck)));
    p.game_ceiling_db.store(static_cast<float>(obs_data_get_double(s, keys::game_ceiling)));
    p.game_scoop_db.store(static_cast<float>(obs_data_get_double(s, keys::game_scoop)));
    p.bgm_pocket_db.store(static_cast<float>(obs_data_get_double(s, keys::bgm_pocket)));
    p.bgm_safe_mode.store(obs_data_get_bool(s, keys::bgm_safe) ? 1 : 0);
    p.bgm_auto_swell.store(obs_data_get_bool(s, keys::bgm_swell) ? 1 : 0);
    p.pro_scale.store(g_license && g_license->is_pro() ? 1.0f : (f->mode == MODE_VOICE ? 1.0f : 1.0f));
    // Standard-tier units 01-04 still run fully; pro_scale only truly gates the
    // Pro-only DIRECTOR behaviours, so keep DSP identical and let the physical
    // binary split (MODEL600_PRO_EDITION) remove Pro code entirely.
}

static void defaults(obs_data_t* s) {
    obs_data_set_default_double(s, keys::input_gain, 0.0);
    obs_data_set_default_double(s, keys::output_trim, 0.0);
    obs_data_set_default_double(s, keys::ceiling, -1.0);
    obs_data_set_default_bool(s, keys::zero_latency, false);
    obs_data_set_default_bool(s, keys::bypass, false);
    obs_data_set_default_int(s, keys::character, dsp::CHAR_BROADCAST);
    obs_data_set_default_int(s, keys::space_fx, dsp::FX_DRY);
    obs_data_set_default_bool(s, keys::auto_mono, true);
    obs_data_set_default_bool(s, keys::denoiser, false);
    obs_data_set_default_double(s, keys::gate_db, -42.0);
    obs_data_set_default_bool(s, keys::gate_on, true);
    obs_data_set_default_double(s, keys::hpf_hz, 100.0);
    obs_data_set_default_bool(s, keys::whisper, true);
    obs_data_set_default_bool(s, keys::presence, true);
    obs_data_set_default_double(s, keys::deess, 8.0);
    obs_data_set_default_bool(s, keys::mic_muted, false);
    obs_data_set_default_double(s, keys::collab_lufs, -14.5);
    obs_data_set_default_double(s, keys::collab_duck, -3.5);
    obs_data_set_default_double(s, keys::game_ceiling, -6.0);
    obs_data_set_default_double(s, keys::game_scoop, -5.0);
    obs_data_set_default_double(s, keys::bgm_pocket, -8.5);
    obs_data_set_default_bool(s, keys::bgm_safe, false);
    obs_data_set_default_bool(s, keys::bgm_swell, true);
}

// -----------------------------------------------------------------------------
//  Lifecycle
// -----------------------------------------------------------------------------
static void* create(obs_data_t* settings, obs_source_t* source) {
    const char* id = obs_source_get_id(source);
    Mode mode = MODE_VOICE;
    if (id) {
        const std::string sid = id;
        if (sid.find("collab") != std::string::npos) mode = MODE_COLLAB;
        else if (sid.find("game") != std::string::npos) mode = MODE_GAME;
        else if (sid.find("bgm") != std::string::npos) mode = MODE_BGM;
        else if (sid.find("director") != std::string::npos) mode = MODE_DIRECTOR;
    }
    auto* f = new FilterInstance(mode);
    f->source = source;
    f->weak = obs_source_get_weak_source(source);

    audio_t* audio = obs_get_audio();
    f->cached_rate = audio ? static_cast<float>(audio_output_get_sample_rate(audio)) : 48000.0f;
    f->cached_channels = audio ? static_cast<int>(audio_output_get_channels(audio)) : 2;

    f->engine.attach_bus(&f->bus);
    f->bus.open(mode);
    f->engine.prepare(f->cached_rate, f->cached_channels);
    apply_settings(f, settings);
    f->trim_rev_seen = 0;

    register_instance(f);
    start_voice_bridge(f);
    blog(LOG_INFO, "[MODEL 600] created unit %d @ %.0f Hz / %d ch", static_cast<int>(mode), static_cast<double>(f->cached_rate), f->cached_channels);
    return f;
}

static void destroy(void* data) {
    auto* f = static_cast<FilterInstance*>(data);
    stop_voice_bridge(f);              // Must stop before the tap it reads is freed.
    unregister_instance(f);
    if (f->console) { QMetaObject::invokeMethod(f->console, "close", Qt::QueuedConnection); }
    f->bus.shutdown();                 // Immediate abort of cross-process comms.
    if (f->weak) obs_weak_source_release(f->weak);
    delete f;
}

static void update(void* data, obs_data_t* settings) {
    auto* f = static_cast<FilterInstance*>(data);
    apply_settings(f, settings);
}

// -----------------------------------------------------------------------------
//  Audio processing (RT thread).
// -----------------------------------------------------------------------------
static struct obs_audio_data* filter_audio(void* data, struct obs_audio_data* audio) {
    auto* f = static_cast<FilterInstance*>(data);

    // Follow host rate / channel changes.
    audio_t* ao = obs_get_audio();
    if (ao) {
        const float rate = static_cast<float>(audio_output_get_sample_rate(ao));
        if (rate > 0.0f && rate != f->cached_rate) { f->cached_rate = rate; f->engine.notify_sample_rate(rate); }
    }
    int nch = 0;
    for (int c = 0; c < MAX_AV_PLANES; ++c) if (audio->data[c]) ++nch; else break;
    if (nch < 1) nch = 1;
    f->cached_channels = nch;

    // Pull a bus-driven remote trim (UNIT 05 fader) into the engine instantly.
    float trim_db = 0.0f;
    if (f->bus.poll_trim(f->mode, f->trim_rev_seen, trim_db)) {
        f->engine.params().output_trim_db.store(trim_db);
        f->settings_dirty.store(true);   // Main thread persists to scene data.
    }

    float* planar[dsp::kMaxChannels] = {nullptr};
    for (int c = 0; c < nch && c < dsp::kMaxChannels; ++c)
        planar[c] = reinterpret_cast<float*>(audio->data[c]);
    f->engine.process_block(planar, nch, static_cast<int>(audio->frames));
    return audio;
}

// -----------------------------------------------------------------------------
//  Studio console button
// -----------------------------------------------------------------------------
static bool open_console_clicked(obs_properties_t*, obs_property_t*, void* data) {
    auto* f = static_cast<FilterInstance*>(data);
    // Must build/show Qt widgets on the UI thread.
    QMetaObject::invokeMethod(qApp, [f]() {
        if (f->console) { f->console->show(); f->console->raise(); f->console->activateWindow(); return; }
        auto* parent = static_cast<QWidget*>(obs_frontend_get_main_window());
        const bool pro = g_license && g_license->is_pro();
        const bool director = (f->mode == MODE_DIRECTOR) && pro;
        auto* con = new ui::StudioConsole(&f->engine, &f->bus, director, pro, parent);
        con->setAttribute(Qt::WA_DeleteOnClose, true);
        f->console = con;
        con->on_params_changed = [f]() { f->settings_dirty.store(true); };
        con->on_request_chapter = []() { m600_write_chapter(QString()); };
        con->on_export_karte = [con]() {
            const QString path = m600_export_karte();
            if (path.isEmpty())
                QMessageBox::warning(con, QStringLiteral("MODEL 600"),
                    QStringLiteral("\u30AB\u30EB\u30C6\u3092\u66F8\u304D\u51FA\u305B\u307E\u305B\u3093\u3067\u3057\u305F\u3002 / Could not write the karte."));
            else
                QMessageBox::information(con, QStringLiteral("MODEL 600"),
                    QStringLiteral("\u30AB\u30EB\u30C6\u3092\u4FDD\u5B58\u3057\u307E\u3057\u305F / Karte saved:\n%1").arg(path));
        };
        if (!g_update_version.isEmpty()) con->set_update_available(g_update_version);
        con->show();
    }, Qt::QueuedConnection);
    return false;
}

// -----------------------------------------------------------------------------
//  Properties (compact OBS panel: status + big console button + quick presets).
// -----------------------------------------------------------------------------
static obs_properties_t* get_properties(void* data) {
    auto* f = static_cast<FilterInstance*>(data);
    obs_properties_t* props = obs_properties_create();

    obs_properties_add_button(props, "open_console",
        "OPEN STUDIO CONSOLE  (専用コンソール画面を開く)",
        open_console_clicked);

    obs_properties_add_bool(props, keys::bypass, "Bypass (バイパス)");
    obs_properties_add_bool(props, keys::zero_latency, "0.0ms ZERO-LATENCY MODE");
    obs_properties_add_float_slider(props, keys::input_gain, "Input Gain (dB)", -18.0, 24.0, 0.1);
    obs_properties_add_float_slider(props, keys::output_trim, "Output Trim (dB)", -24.0, 24.0, 0.1);

    switch (f->mode) {
        case MODE_VOICE: {
            obs_property_t* ch = obs_properties_add_list(props, keys::character, "Character",
                OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
            obs_property_list_add_int(ch, "SOFT / ASMR", dsp::CHAR_SOFT_ASMR);
            obs_property_list_add_int(ch, "BROADCAST", dsp::CHAR_BROADCAST);
            obs_property_list_add_int(ch, "FPS / LOUD", dsp::CHAR_FPS_LOUD);
            obs_property_t* fx = obs_properties_add_list(props, keys::space_fx, "Space FX",
                OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
            obs_property_list_add_int(fx, "DRY", dsp::FX_DRY);
            obs_property_list_add_int(fx, "KARAOKE", dsp::FX_KARAOKE);
            obs_property_list_add_int(fx, "BATH", dsp::FX_BATH);
            obs_property_list_add_int(fx, "HALL", dsp::FX_HALL);
            obs_property_list_add_int(fx, "CAVE", dsp::FX_CAVE);
            obs_property_list_add_int(fx, "RADIO", dsp::FX_RADIO);
            obs_properties_add_bool(props, keys::auto_mono, "Auto Mono (片耳マイク救済)");
            obs_properties_add_bool(props, keys::denoiser, "Upstream RNNoise present");
            obs_properties_add_bool(props, keys::gate_on, "Noise Gate");
            obs_properties_add_float_slider(props, keys::gate_db, "Gate Threshold (dB)", -70.0, -20.0, 0.5);
            obs_properties_add_float_slider(props, keys::hpf_hz, "De-Plosive HPF (Hz)", 85.0, 130.0, 1.0);
            obs_properties_add_bool(props, keys::whisper, "Whisper Comp (小声救済)");
            obs_properties_add_bool(props, keys::presence, "Presence EQ");
            obs_properties_add_float_slider(props, keys::deess, "De-Esser Depth (dB)", 0.0, 12.0, 0.5);
            obs_properties_add_bool(props, keys::mic_muted, "Mic Mute");
            obs_properties_add_float_slider(props, keys::ceiling, "Limiter Ceiling (dB)", -6.0, 0.0, 0.1);
            break;
        }
        case MODE_COLLAB:
            obs_properties_add_float_slider(props, keys::collab_lufs, "Target Loudness (LUFS)", -24.0, -8.0, 0.5);
            obs_properties_add_float_slider(props, keys::collab_duck, "Duck When I Talk (dB)", -12.0, 0.0, 0.5);
            obs_properties_add_float_slider(props, keys::ceiling, "Limiter Ceiling (dB)", -6.0, 0.0, 0.1);
            break;
        case MODE_GAME:
            obs_properties_add_float_slider(props, keys::game_ceiling, "Impact Ceiling (dB)", -12.0, 0.0, 0.1);
            obs_properties_add_float_slider(props, keys::game_scoop, "Voice Scoop (dB)", -12.0, 0.0, 0.5);
            break;
        case MODE_BGM:
            obs_properties_add_float_slider(props, keys::bgm_pocket, "M/S Pocket Depth (dB)", -12.0, 0.0, 0.5);
            obs_properties_add_bool(props, keys::bgm_safe, "Singing / Recording Safe (-3dB)");
            obs_properties_add_bool(props, keys::bgm_swell, "Auto Swell when AFK");
            obs_properties_add_float_slider(props, keys::ceiling, "Limiter Ceiling (dB)", -6.0, 0.0, 0.1);
            break;
        case MODE_DIRECTOR:
            obs_properties_add_float_slider(props, keys::ceiling, "Master Ceiling (dB)", -6.0, 0.0, 0.1);
            break;
    }
    return props;
}

// -----------------------------------------------------------------------------
//  Main-thread sweep: persist bus-driven trims into scene data (called by the
//  plugin's frontend timer, never from the audio thread).
// -----------------------------------------------------------------------------
// Write the engine's user-facing params back into the source settings so a
// change made from the console, a voice command or UNIT 05 survives a restart.
static void write_params_to_settings(FilterInstance* f) {
    if (!f->source) return;
    obs_data_t* s = obs_source_get_settings(f->source);
    if (!s) return;
    const auto& p = f->engine.params();
    obs_data_set_double(s, keys::input_gain, static_cast<double>(p.input_gain_db.load()));
    obs_data_set_double(s, keys::output_trim, static_cast<double>(p.output_trim_db.load()));
    obs_data_set_int(s, keys::character, p.character.load());
    obs_data_set_int(s, keys::space_fx, p.space_fx.load());
    obs_data_set_bool(s, keys::auto_mono, p.auto_mono.load() != 0);
    obs_data_set_double(s, keys::gate_db, static_cast<double>(p.gate_open_db.load()));
    obs_data_set_bool(s, keys::mic_muted, p.mic_muted.load() != 0);
    obs_data_set_double(s, keys::collab_duck, static_cast<double>(p.collab_duck_db.load()));
    obs_data_set_double(s, keys::game_scoop, static_cast<double>(p.game_scoop_db.load()));
    obs_data_set_double(s, keys::bgm_pocket, static_cast<double>(p.bgm_pocket_db.load()));
    obs_data_set_bool(s, keys::bgm_safe, p.bgm_safe_mode.load() != 0);
    obs_data_release(s);
}

extern "C" void m600_persist_trims() {
    std::lock_guard<std::mutex> lk(g_registry_mutex);
    for (FilterInstance* f : g_instances) {
        // A calibration started by voice has no modal to commit it.
        if (f->calib_autocommit.load() && f->engine.calibrator().done()) {
            f->calib_autocommit.store(false);
            if (f->engine.commit_calibration()) {
                f->settings_dirty.store(true);
                blog(LOG_INFO, "[MODEL 600] voice calibration committed: %.1f dB",
                     static_cast<double>(f->engine.calibrator().result_gain_db()));
            } else {
                blog(LOG_WARNING, "[MODEL 600] voice calibration not applied (silent=%d clipped=%d)",
                     f->engine.calibrator().result_silent() ? 1 : 0, f->engine.calibrator().result_clipped() ? 1 : 0);
            }
        }
        if (f->settings_dirty.exchange(false)) write_params_to_settings(f);
    }
}

// Update checker -> amber header lamp on every open console (UI thread).
void m600_notify_update(const QString& version) {
    g_update_version = version;
    std::lock_guard<std::mutex> lk(g_registry_mutex);
    for (FilterInstance* f : g_instances)
        if (f->console) f->console->set_update_available(version);
}

// Per-unit session stats for the audio karte (main thread).
QString m600_collect_karte_body() {
    static const char* names[5] = {"[01] VOCAL MASTER", "[02] COLLAB RIDER", "[03] GAME IMPACT",
                                   "[04] BGM POCKET", "[05] STREAM DIRECTOR"};
    auto fmt = [](float db) {
        return db <= -119.0f ? QStringLiteral("-inf") : QString::number(static_cast<double>(db), 'f', 1);
    };
    QString out;
    float master_lufs = -120.0f;
    std::uint32_t screams = 0;
    std::lock_guard<std::mutex> lk(g_registry_mutex);
    for (FilterInstance* f : g_instances) {
        const auto& t = f->engine.telemetry();
        const int m = static_cast<int>(f->mode);
        const float li = t.lufs_i.load();
        const float pk = t.session_peak_db.load();
        const std::uint32_t sc = t.scream_defense_count.load();
        out += QStringLiteral("%1\n  \u5E73\u5747\u30E9\u30A6\u30C9\u30CD\u30B9 / Integrated : %2 LUFS\n"
                              "  \u6700\u5927\u30D4\u30FC\u30AF / Max peak     : %3 dBTP\n"
                              "  \u7D76\u53EB\u9632\u885B / Scream saves  : %4\n")
                   .arg(QString::fromLatin1(names[(m >= 0 && m < 5) ? m : 0]), fmt(li), fmt(pk))
                   .arg(sc);
        if (f->mode == MODE_DIRECTOR || (f->mode == MODE_VOICE && master_lufs <= -119.0f)) master_lufs = li;
        if (f->mode == MODE_VOICE) screams += sc;
    }
    if (out.isEmpty()) return QStringLiteral("(\u7A3C\u50CD\u4E2D\u306E\u30E6\u30CB\u30C3\u30C8\u304C\u3042\u308A\u307E\u305B\u3093 / no active units)\n");
    const bool golden = master_lufs >= -13.5f && master_lufs <= -10.5f;
    out += QStringLiteral("\n\u7DCF\u5408 / Summary: %1 LUFS%2, \u7D76\u53EB\u9632\u885B %3 \u56DE\n")
               .arg(fmt(master_lufs),
                    golden ? QStringLiteral("  (YouTube Live 目標帯 -12.0 ±1.5 LUFS 内)") : QString())
               .arg(screams);
    return out;
}

// Set the shared licence pointer (owned by plugin-main).
extern "C" void m600_set_license(void* lic) {
    g_license = static_cast<license::LicenseManager*>(lic);
}

// -----------------------------------------------------------------------------
//  Emergency / chapter hotkeys fire on whichever UNIT 01 instance is live.
// -----------------------------------------------------------------------------
extern "C" void m600_hotkey_bleep() {
    std::lock_guard<std::mutex> lk(g_registry_mutex);
    for (FilterInstance* f : g_instances) if (f->mode == MODE_VOICE) f->engine.bleep().trigger(3.0f);
}
extern "C" void m600_hotkey_cough(bool pressed) {
    std::lock_guard<std::mutex> lk(g_registry_mutex);
    for (FilterInstance* f : g_instances) if (f->mode == MODE_VOICE) { if (pressed) f->engine.cough().press(true); else f->engine.cough().release(); }
}
extern "C" void m600_hotkey_echo_toggle() {
    std::lock_guard<std::mutex> lk(g_registry_mutex);
    for (FilterInstance* f : g_instances) if (f->mode == MODE_VOICE) {
        auto& p = f->engine.params();
        p.space_fx.store(p.space_fx.load() == dsp::FX_DRY ? dsp::FX_BATH : dsp::FX_DRY);
    }
}


// -----------------------------------------------------------------------------
//  Voice director (Pro). The platform bridge recognises speech from UNIT 01's
//  pre-FX tap on its own thread and calls m600_apply_voice_command(); every
//  change is then applied on the UI thread under the registry lock.
// -----------------------------------------------------------------------------
#if defined(MODEL600_PRO_EDITION)
namespace {

// Exact undo: a snapshot of every voice-reachable parameter before a change.
struct VoiceSnapshot {
    float trim_db[kModeCount] = {0, 0, 0, 0, 0};
    bool  has_mode[kModeCount] = {false, false, false, false, false};
    int   space_fx = dsp::FX_DRY;
    int   mic_muted = 0;
    int   character = dsp::CHAR_BROADCAST;
    float bgm_pocket = -8.5f, game_scoop = -5.0f, collab_duck = -3.5f;
    int   bgm_safe = 0;
};
constexpr int kUndoDepth = 8;
VoiceSnapshot g_undo[kUndoDepth];
int g_undo_top = 0, g_undo_count = 0;

VoiceSnapshot take_snapshot() {
    VoiceSnapshot snap;
    for (FilterInstance* f : g_instances) {
        const int m = static_cast<int>(f->mode);
        if (m < 0 || m >= kModeCount) continue;
        const auto& p = f->engine.params();
        snap.trim_db[m] = p.output_trim_db.load();
        snap.has_mode[m] = true;
        if (f->mode == MODE_VOICE) {
            snap.space_fx = p.space_fx.load();
            snap.mic_muted = p.mic_muted.load();
            snap.character = p.character.load();
        }
        if (f->mode == MODE_BGM) { snap.bgm_pocket = p.bgm_pocket_db.load(); snap.bgm_safe = p.bgm_safe_mode.load(); }
        if (f->mode == MODE_GAME) snap.game_scoop = p.game_scoop_db.load();
        if (f->mode == MODE_COLLAB) snap.collab_duck = p.collab_duck_db.load();
    }
    return snap;
}

void push_undo() {
    g_undo[g_undo_top] = take_snapshot();
    g_undo_top = (g_undo_top + 1) % kUndoDepth;
    if (g_undo_count < kUndoDepth) ++g_undo_count;
}

bool pop_undo() {
    if (g_undo_count == 0) return false;
    g_undo_top = (g_undo_top - 1 + kUndoDepth) % kUndoDepth;
    --g_undo_count;
    const VoiceSnapshot& snap = g_undo[g_undo_top];
    for (FilterInstance* f : g_instances) {
        const int m = static_cast<int>(f->mode);
        if (m < 0 || m >= kModeCount || !snap.has_mode[m]) continue;
        auto& p = f->engine.params();
        p.output_trim_db.store(snap.trim_db[m]);
        if (f->mode == MODE_VOICE) {
            p.space_fx.store(snap.space_fx);
            p.mic_muted.store(snap.mic_muted);
            p.character.store(snap.character);
        }
        if (f->mode == MODE_BGM) { p.bgm_pocket_db.store(snap.bgm_pocket); p.bgm_safe_mode.store(snap.bgm_safe); }
        if (f->mode == MODE_GAME) p.game_scoop_db.store(snap.game_scoop);
        if (f->mode == MODE_COLLAB) p.collab_duck_db.store(snap.collab_duck);
        f->settings_dirty.store(true);
    }
    return true;
}

bool target_matches(voice::Target t, Mode m) {
    switch (t) {
        case voice::Target::VOICE:  return m == MODE_VOICE;
        case voice::Target::COLLAB: return m == MODE_COLLAB;
        case voice::Target::GAME:   return m == MODE_GAME;
        case voice::Target::BGM:    return m == MODE_BGM;
        case voice::Target::MASTER: return m != MODE_DIRECTOR; // Every audio unit.
        default: return false;
    }
}

// UI thread. Caller does not hold the registry lock.
void apply_command_main(const voice::Command& c) {
    bool want_chapter = false;
    {
        std::lock_guard<std::mutex> lk(g_registry_mutex);
        const bool mutates = c.intent != voice::Intent::UNDO && c.intent != voice::Intent::CHAPTER_MARK &&
                             c.intent != voice::Intent::BLEEP && c.intent != voice::Intent::COUGH_MUTE &&
                             c.intent != voice::Intent::CALIBRATE;
        if (mutates) push_undo();

        switch (c.intent) {
            case voice::Intent::TRIM_DELTA:
                for (FilterInstance* f : g_instances) {
                    if (!target_matches(c.target, f->mode)) continue;
                    auto& p = f->engine.params();
                    p.output_trim_db.store(voice::apply_voice_trim(p.output_trim_db.load(), c.value_db));
                    f->settings_dirty.store(true);
                }
                break;
            case voice::Intent::MUTE_MIC:
            case voice::Intent::UNMUTE_MIC:
                for (FilterInstance* f : g_instances) if (f->mode == MODE_VOICE) {
                    f->engine.params().mic_muted.store(c.intent == voice::Intent::MUTE_MIC ? 1 : 0);
                    f->settings_dirty.store(true);
                }
                break;
            case voice::Intent::BLEEP:
                for (FilterInstance* f : g_instances) if (f->mode == MODE_VOICE) f->engine.bleep().trigger(1.0f);
                break;
            case voice::Intent::COUGH_MUTE:
                for (FilterInstance* f : g_instances) if (f->mode == MODE_VOICE) f->engine.cough().press(false, 3.0f);
                break;
            case voice::Intent::CHAPTER_MARK:
                want_chapter = true;
                break;
            case voice::Intent::FX_SET:
            case voice::Intent::FX_OFF:
                for (FilterInstance* f : g_instances) if (f->mode == MODE_VOICE) {
                    f->engine.params().space_fx.store(c.intent == voice::Intent::FX_OFF ? dsp::FX_DRY : c.fx_mode);
                    f->settings_dirty.store(true);
                }
                break;
            case voice::Intent::SCENE_SET:
                for (FilterInstance* f : g_instances) {
                    f->engine.params().apply_scene(c.scene);
                    f->settings_dirty.store(true);
                }
                break;
            case voice::Intent::UNDO:
                if (!pop_undo()) blog(LOG_INFO, "[MODEL 600] voice undo: nothing to undo");
                break;
            case voice::Intent::RESET:
                for (FilterInstance* f : g_instances) {
                    f->engine.params().factory_reset_keep_calibration();
                    f->settings_dirty.store(true);
                }
                break;
            case voice::Intent::CALIBRATE:
                for (FilterInstance* f : g_instances) if (f->mode == MODE_VOICE && !f->engine.calibrator().running()) {
                    f->engine.start_calibration(10.0f);
                    f->calib_autocommit.store(true);
                }
                break;
            case voice::Intent::NONE:
                break;
        }
    }
    // Outside the lock: the chapter writer does file I/O.
    if (want_chapter) m600_write_chapter(QStringLiteral("見どころ"));
    blog(LOG_INFO, "[MODEL 600] voice command: %s target=%s value=%.1f conf=%.2f",
         voice::intent_name(c.intent), voice::target_name(c.target),
         static_cast<double>(c.value_db), static_cast<double>(c.confidence));
}

void* g_voice_bridge = nullptr;
FilterInstance* g_voice_bridge_owner = nullptr;

} // namespace

// Called by the platform bridge on its recognizer thread.
extern "C" void m600_apply_voice_command(const voice::Command* c) {
    if (!c || !c->fires()) return;
    const voice::Command copy = *c;
    QMetaObject::invokeMethod(qApp, [copy]() { apply_command_main(copy); }, Qt::QueuedConnection);
}

#if defined(_WIN32)
extern "C" void* m600_win_voice_start(void* tap, float rate, void (*cb)(const voice::Command*));
extern "C" void m600_win_voice_stop(void* bridge);
#elif defined(__APPLE__)
extern "C" void* m600_mac_voice_start(void* tap, float rate, int lang, void (*cb)(const voice::Command*));
extern "C" void m600_mac_voice_stop(void* bridge);
#endif

static void start_voice_bridge(FilterInstance* f) {
    if (g_voice_bridge || f->mode != MODE_VOICE) return;
#if defined(_WIN32)
    g_voice_bridge = m600_win_voice_start(&f->engine.prefx_tap(), f->cached_rate, m600_apply_voice_command);
#elif defined(__APPLE__)
    g_voice_bridge = m600_mac_voice_start(&f->engine.prefx_tap(), f->cached_rate,
                                          static_cast<int>(voice::Lang::AUTO), m600_apply_voice_command);
    if (!g_voice_bridge)
        blog(LOG_WARNING, "[MODEL 600] voice director unavailable: the OBS app bundle does not declare "
                          "NSSpeechRecognitionUsageDescription, so macOS speech recognition cannot be used.");
#endif
    if (g_voice_bridge) g_voice_bridge_owner = f;
}

static void stop_voice_bridge(FilterInstance* f) {
    if (!g_voice_bridge || g_voice_bridge_owner != f) return;
#if defined(_WIN32)
    m600_win_voice_stop(g_voice_bridge);
#elif defined(__APPLE__)
    m600_mac_voice_stop(g_voice_bridge);
#endif
    g_voice_bridge = nullptr;
    g_voice_bridge_owner = nullptr;
}
#else
static void start_voice_bridge(FilterInstance*) {}
static void stop_voice_bridge(FilterInstance*) {}
#endif

// -----------------------------------------------------------------------------
//  obs_source_info builders
// -----------------------------------------------------------------------------
static obs_source_info make_info(const char* id, const char* (*namer)(void*)) {
    obs_source_info info = {};
    info.id = id;
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = namer;
    info.create = create;
    info.destroy = destroy;
    info.update = update;
    info.get_defaults = defaults;
    info.get_properties = get_properties;
    info.filter_audio = filter_audio;
    return info;
}

extern "C" void m600_register_filters(bool pro_edition) {
    static obs_source_info voice  = make_info("model600_vocal_master", name_voice);
    static obs_source_info collab = make_info("model600_collab_rider", name_collab);
    static obs_source_info game   = make_info("model600_game_impact", name_game);
    static obs_source_info bgm    = make_info("model600_bgm_pocket", name_bgm);
    obs_register_source(&voice);
    obs_register_source(&collab);
    obs_register_source(&game);
    obs_register_source(&bgm);
#if defined(MODEL600_PRO_EDITION)
    if (pro_edition) {
        static obs_source_info director = make_info("model600_stream_director", name_director);
        obs_register_source(&director);
    }
#else
    (void)pro_edition;
#endif
}
