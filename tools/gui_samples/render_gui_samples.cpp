// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  tools/gui_samples/render_gui_samples.cpp
//
//  Renders every GUI screen of the real StudioConsole / CalibrationDialog code
//  to PNG, with live meter data produced by running the actual DSP engines.
//  Runs headless (QT_QPA_PLATFORM=offscreen). Dev tool only; not shipped.
//
//    render_gui_samples <output-dir>
// =============================================================================
#include "custom-ui-dialog.hpp"
#include "dsp-core.hpp"
#include "shared-state.hpp"
#include "ui-theme.hpp"
#include "ui-widgets.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QThread>
#include <QVBoxLayout>

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

using namespace sw::model600;
using namespace sw::model600::dsp;
using namespace sw::model600::ui;

namespace {

QString g_out;
int g_count = 0;
constexpr int kExpected = 15;

void pump(int ms = 80) {
    for (int i = 0; i < ms / 10; ++i) { QApplication::processEvents(); QThread::msleep(10); }
    QApplication::processEvents();
}

void save(QWidget* w, const QString& name) {
    pump();
    const QPixmap px = w->grab();
    const QString path = QDir(g_out).filePath(name);
    if (px.save(path)) { ++g_count; std::printf("  wrote %s (%dx%d)\n", path.toUtf8().constData(), px.width(), px.height()); }
    else std::printf("  FAILED %s\n", path.toUtf8().constData());
}

struct Gen {
    std::mt19937 rng{7};
    std::normal_distribution<float> nd{0.0f, 1.0f};
    float ph = 0.0f;
    float voice(float amp) {   // Vowel-like: 160 Hz harmonics weighted into the formant band.
        float s = 0.0f;
        for (int h = 1; h <= 16; ++h) {
            const float w = (h >= 3 && h <= 12) ? 2.5f : 1.0f;
            s += w * std::sin(ph * static_cast<float>(h)) / static_cast<float>(h);
        }
        ph += kTwoPi * 160.0f / 48000.0f;
        if (ph > kTwoPi) ph -= kTwoPi;
        return s * amp + nd(rng) * amp * 0.08f;
    }
    float noise(float amp) { return nd(rng) * amp; }
};

template <typename Fn>
void run(Engine& e, int nch, float seconds, Fn fn) {
    const int N = 480;
    std::vector<std::vector<float>> buf(static_cast<std::size_t>(nch), std::vector<float>(N));
    std::vector<float*> ptr(static_cast<std::size_t>(nch));
    for (int c = 0; c < nch; ++c) ptr[static_cast<std::size_t>(c)] = buf[static_cast<std::size_t>(c)].data();
    const int blocks = static_cast<int>(seconds * 48000.0f / N);
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < N; ++i) {
            float l = 0.0f, r = 0.0f;
            fn(b * N + i, l, r);
            buf[0][static_cast<std::size_t>(i)] = l;
            if (nch > 1) buf[1][static_cast<std::size_t>(i)] = r;
        }
        e.process_block(ptr.data(), nch, N);
    }
}

template <typename T>
T* child(QWidget* root, const char* name) { return root->findChild<T*>(QString::fromLatin1(name)); }

void console_shot(Engine& e, SharedBus& bus, bool director, UiLang lang, const QString& name,
                  int w, const std::function<void(StudioConsole&)>& prep = {}) {
    StudioConsole con(&e, &bus, director, true);
    con.resize(w, 560);
    con.set_language(lang);
    con.show();
    if (prep) prep(con);
    QMetaObject::invokeMethod(&con, "refresh");
    save(&con, name);
    con.hide();
}

// OBS draws its own filter dialog; this is an approximation for documentation.
QWidget* make_obs_filter_panel() {
    auto* w = new QWidget();
    w->setFixedSize(980, 600);
    w->setStyleSheet(QStringLiteral(
        "QWidget{background:#1e1e1e;color:#e0e0e0;font-size:12px;}"
        "QListWidget{background:#272727;border:1px solid #3a3a3a;}"
        "QListWidget::item{padding:6px;}"
        "QListWidget::item:selected{background:#3c4f73;}"
        "QPushButton{background:#3a3a3a;border:1px solid #4a4a4a;border-radius:3px;padding:6px;}"
        "QSlider::groove:horizontal{height:4px;background:#4a4a4a;}"
        "QSlider::handle:horizontal{width:12px;margin:-5px 0;background:#d0d0d0;border-radius:6px;}"
        "QComboBox{background:#2c2c2c;border:1px solid #4a4a4a;padding:3px;}"));
    auto* root = new QVBoxLayout(w);
    auto* caption = new QLabel(QStringLiteral(
        "※ OBS側が描画するフィルタ画面の再現（近似）。"
        "実際の配色はOBSのテーマに依存します。"));
    caption->setStyleSheet(QStringLiteral("color:#ffb020;"));
    root->addWidget(caption);
    auto* body = new QHBoxLayout();
    auto* list = new QListWidget();
    list->setFixedWidth(330);
    list->setWordWrap(true);
    const QStringList names = {
        QStringLiteral("[01] マイク・声用 (VOCAL MASTER)"),
        QStringLiteral("[02] コラボ・通話用 (COLLAB RIDER)"),
        QStringLiteral("[03] ゲーム音用 (GAME IMPACT)"),
        QStringLiteral("[04] BGM・音楽用 (BGM POCKET)"),
        QStringLiteral("[05] 統合ミキサー＆声ディレクター (STREAM DIRECTOR)")};
    for (const QString& n : names) list->addItem(QStringLiteral("MODEL 600 - ") + n);
    list->setCurrentRow(0);
    body->addWidget(list);
    auto* props = new QVBoxLayout();
    auto* open = new QPushButton(QStringLiteral("OPEN STUDIO CONSOLE  (専用コンソール画面を開く)"));
    open->setMinimumHeight(44);
    props->addWidget(open);
    auto add_check = [&](const QString& t, bool on) { auto* c = new QCheckBox(t); c->setChecked(on); props->addWidget(c); };
    auto add_slider = [&](const QString& t, int lo, int hi, int v, const QString& val) {
        auto* row = new QHBoxLayout();
        auto* l = new QLabel(t); l->setFixedWidth(170); row->addWidget(l);
        auto* s = new QSlider(Qt::Horizontal); s->setRange(lo, hi); s->setValue(v); row->addWidget(s, 1);
        auto* vl = new QLabel(val); vl->setFixedWidth(52); row->addWidget(vl);
        props->addLayout(row);
    };
    auto add_combo = [&](const QString& t, const QStringList& items) {
        auto* row = new QHBoxLayout();
        auto* l = new QLabel(t); l->setFixedWidth(170); row->addWidget(l);
        auto* c = new QComboBox(); c->addItems(items); c->setCurrentIndex(1); row->addWidget(c, 1);
        props->addLayout(row);
    };
    add_check(QStringLiteral("Bypass (バイパス)"), false);
    add_check(QStringLiteral("0.0ms ZERO-LATENCY MODE"), false);
    add_slider(QStringLiteral("Input Gain (dB)"), -180, 240, 35, QStringLiteral("3.5"));
    add_slider(QStringLiteral("Output Trim (dB)"), -240, 240, 0, QStringLiteral("0.0"));
    add_combo(QStringLiteral("Character"), {QStringLiteral("SOFT / ASMR"), QStringLiteral("BROADCAST"), QStringLiteral("FPS / LOUD")});
    add_combo(QStringLiteral("Space FX"), {QStringLiteral("DRY"), QStringLiteral("KARAOKE"), QStringLiteral("BATH"), QStringLiteral("HALL"), QStringLiteral("CAVE"), QStringLiteral("RADIO")});
    add_check(QStringLiteral("Auto Mono (片耳マイク救済)"), true);
    add_check(QStringLiteral("Noise Gate"), true);
    add_slider(QStringLiteral("Gate Threshold (dB)"), -140, -40, -84, QStringLiteral("-42.0"));
    add_slider(QStringLiteral("De-Plosive HPF (Hz)"), 85, 130, 100, QStringLiteral("100"));
    add_check(QStringLiteral("Whisper Comp (小声救済)"), true);
    add_check(QStringLiteral("Presence EQ"), true);
    add_slider(QStringLiteral("De-Esser Depth (dB)"), 0, 24, 16, QStringLiteral("8.0"));
    add_slider(QStringLiteral("Limiter Ceiling (dB)"), -60, 0, -10, QStringLiteral("-1.0"));
    props->addStretch(1);
    body->addLayout(props, 1);
    root->addLayout(body, 1);
    return w;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    install_fonts();
    g_out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("gui-samples");
    QDir().mkpath(g_out);
    Gen gen;

    // ---- Units 01-05 with live audio so the bus carries real telemetry ----
    SharedBus bus_v, bus_c, bus_g, bus_b, bus_d;
    bus_v.open(MODE_VOICE); bus_c.open(MODE_COLLAB); bus_g.open(MODE_GAME);
    bus_b.open(MODE_BGM); bus_d.open(MODE_DIRECTOR);
    Engine voice(MODE_VOICE), collab(MODE_COLLAB), game(MODE_GAME), bgm(MODE_BGM), director(MODE_DIRECTOR);
    voice.attach_bus(&bus_v); collab.attach_bus(&bus_c); game.attach_bus(&bus_g);
    bgm.attach_bus(&bus_b); director.attach_bus(&bus_d);
    for (Engine* e : {&voice, &collab, &game, &bgm, &director}) e->prepare(48000.0f, 2);
    voice.params().input_gain_db.store(3.5f);

    auto music = [](int i, float& l, float& r) {
        const float t = static_cast<float>(i) / 48000.0f;
        l = 0.22f * std::sin(kTwoPi * 220.0f * t) + 0.12f * std::sin(kTwoPi * 330.0f * t);
        r = 0.22f * std::sin(kTwoPi * 220.0f * t + 0.6f) + 0.12f * std::sin(kTwoPi * 440.0f * t);
    };
    run(voice, 2, 4.0f, [&](int, float& l, float& r) { l = gen.voice(0.11f); r = l; });
    run(collab, 2, 4.0f, [&](int, float& l, float& r) { l = gen.voice(0.06f); r = l; });
    run(game, 2, 4.0f, [&](int i, float& l, float& r) {
        const float burst = (i % 24000) < 900 ? 0.8f : 0.05f;
        l = gen.noise(burst); r = gen.noise(burst);
    });
    run(bgm, 2, 4.0f, music);
    run(director, 2, 6.0f, [&](int i, float& l, float& r) {
        const float t = static_cast<float>(i) / 48000.0f;
        const float v = gen.voice(0.143f);
        l = v + 0.072f * std::sin(kTwoPi * 220.0f * t);
        r = v + 0.072f * std::sin(kTwoPi * 220.0f * t + 0.6f);
    });
    // Re-run voice last so its bus-derived flags reflect the full mix.
    run(voice, 2, 1.0f, [&](int, float& l, float& r) { l = gen.voice(0.11f); r = l; });
    std::printf("director integrated LUFS = %.1f\n", static_cast<double>(director.telemetry().lufs_i.load()));
    std::printf("Rendering GUI samples into %s\n", g_out.toUtf8().constData());

    console_shot(voice, bus_v, false, UiLang::JA, QStringLiteral("01_console_unit01_ja.png"), 560);
    console_shot(voice, bus_v, false, UiLang::EN, QStringLiteral("02_console_unit01_en.png"), 560);
    voice.telemetry().hint_bgm_misplug.store(1);
    console_shot(voice, bus_v, false, UiLang::JA, QStringLiteral("03_console_unit01_states.png"), 560, [](StudioConsole& c) {
        c.set_update_available(QStringLiteral("1.1.0"));
        if (auto* pin = child<RackKey>(&c, "pin")) pin->setChecked(true);
    });
    voice.telemetry().hint_bgm_misplug.store(0);
    console_shot(collab, bus_c, false, UiLang::JA, QStringLiteral("04_console_unit02_collab.png"), 560);
    console_shot(game, bus_g, false, UiLang::JA, QStringLiteral("05_console_unit03_game.png"), 560);
    console_shot(bgm, bus_b, false, UiLang::JA, QStringLiteral("06_console_unit04_bgm.png"), 560);
    console_shot(director, bus_d, true, UiLang::JA, QStringLiteral("07_console_director_ja.png"), 1120);
    console_shot(director, bus_d, true, UiLang::EN, QStringLiteral("08_console_director_en.png"), 1120);
    console_shot(voice, bus_v, false, UiLang::JA, QStringLiteral("09_mini_unit01.png"), 560, [](StudioConsole& c) {
        if (auto* fold = child<RackKey>(&c, "fold")) fold->click();
    });
    console_shot(director, bus_d, true, UiLang::JA, QStringLiteral("10_mini_director.png"), 1120, [](StudioConsole& c) {
        if (auto* fold = child<RackKey>(&c, "fold")) fold->click();
    });

    // Calibration modal states (driven by real calibrator runs).
    auto calib_shot = [&](const QString& name, float seconds, auto signal) {
        Engine e(MODE_VOICE);
        e.prepare(48000.0f, 2);
        CalibrationDialog dlg(&e, UiLang::JA);
        dlg.show();                                  // Starts the 10 s measurement.
        pump();
        run(e, 2, seconds, signal);
        QMetaObject::invokeMethod(&dlg, "tick");
        save(&dlg, name);
        dlg.hide();
    };
    calib_shot(QStringLiteral("11_calibration_measuring.png"), 4.0f,
               [&](int, float& l, float& r) { l = gen.voice(0.10f); r = l; });
    calib_shot(QStringLiteral("12_calibration_success.png"), 10.2f,
               [&](int, float& l, float& r) { l = gen.voice(0.10f); r = l; });
    calib_shot(QStringLiteral("13_calibration_clip_warning.png"), 10.2f,
               [&](int i, float& l, float& r) { l = ((i / 40) % 2) ? 1.0f : -1.0f; r = l; });
    calib_shot(QStringLiteral("14_calibration_no_voice.png"), 10.2f,
               [&](int, float& l, float& r) { l = gen.noise(0.0005f); r = l; });

    {
        QWidget* panel = make_obs_filter_panel();
        panel->show();
        save(panel, QStringLiteral("15_obs_filter_panel_approx.png"));
        delete panel;
    }

    std::printf("Done: %d images.\n", g_count);
    return g_count == kExpected ? 0 : 1;
}
