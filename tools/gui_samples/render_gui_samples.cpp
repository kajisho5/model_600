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

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QThread>
#include <QVBoxLayout>

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace sw::model600;
using namespace sw::model600::dsp;
using namespace sw::model600::ui;

namespace {

QString g_out;
int g_count = 0;

void pump(int ms = 60) {
    // Let layouts settle, timers fire and the chassis pixmap regenerate.
    for (int i = 0; i < ms / 10; ++i) {
        QApplication::processEvents();
        QThread::msleep(10);
    }
    QApplication::processEvents();
}

void save(QWidget* w, const QString& name) {
    pump();
    const QPixmap px = w->grab();
    const QString path = QDir(g_out).filePath(name);
    if (px.save(path)) {
        ++g_count;
        std::printf("  wrote %s (%dx%d)\n", path.toUtf8().constData(), px.width(), px.height());
    } else {
        std::printf("  FAILED %s\n", path.toUtf8().constData());
    }
}

// ---- Test signals -----------------------------------------------------------
struct Gen {
    std::mt19937 rng{7};
    std::normal_distribution<float> nd{0.0f, 1.0f};
    float ph = 0.0f;
    // Vowel-like voice: 160 Hz harmonics weighted into the formant band.
    float voice(float amp) {
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

// Run `seconds` of audio through an engine. fn(i, L, R) fills one frame.
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

QPushButton* find_button(QWidget* root, const QString& contains) {
    for (QPushButton* b : root->findChildren<QPushButton*>())
        if (b->text().contains(contains)) return b;
    return nullptr;
}

// ---- Approximation of OBS's own filter dialog (rendered by OBS, not us) ----
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
    list->addItem(QStringLiteral("MODEL 600 - [01] マイク・声用 (VOCAL MASTER)"));
    list->addItem(QStringLiteral("MODEL 600 - [02] コラボ・通話用 (COLLAB RIDER)"));
    list->addItem(QStringLiteral("MODEL 600 - [03] ゲーム音用 (GAME IMPACT)"));
    list->addItem(QStringLiteral("MODEL 600 - [04] BGM・音楽用 (BGM POCKET)"));
    list->addItem(QStringLiteral("MODEL 600 - [05] 統合ミキサー＆声ディレクター (STREAM DIRECTOR)"));
    list->setCurrentRow(0);
    list->setWordWrap(true);
    body->addWidget(list);

    auto* props = new QVBoxLayout();
    auto* open = new QPushButton(QStringLiteral(
        "\U0001F39B️ OPEN STUDIO CONSOLE (専用コンソール画面を開く)"));
    open->setMinimumHeight(44);
    QFont of = open->font(); of.setBold(true); of.setPointSizeF(of.pointSizeF() * 1.15); open->setFont(of);
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
    QFont base(QStringLiteral("IPAPGothic"));
    base.setPointSize(10);
    QApplication::setFont(base);

    g_out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("gui-samples");
    QDir().mkpath(g_out);
    Gen gen;

    // ---- Units 01-04 with live audio so the bus carries real telemetry ----
    SharedBus bus_v, bus_c, bus_g, bus_b, bus_d;
    bus_v.open(MODE_VOICE); bus_c.open(MODE_COLLAB); bus_g.open(MODE_GAME);
    bus_b.open(MODE_BGM); bus_d.open(MODE_DIRECTOR);
    Engine voice(MODE_VOICE), collab(MODE_COLLAB), game(MODE_GAME), bgm(MODE_BGM), director(MODE_DIRECTOR);
    voice.attach_bus(&bus_v); collab.attach_bus(&bus_c); game.attach_bus(&bus_g);
    bgm.attach_bus(&bus_b); director.attach_bus(&bus_d);
    for (Engine* e : {&voice, &collab, &game, &bgm, &director}) e->prepare(48000.0f, 2);
    voice.params().input_gain_db.store(3.5f);

    run(voice, 2, 4.0f, [&](int, float& l, float& r) { l = gen.voice(0.11f); r = l; });
    run(collab, 2, 4.0f, [&](int, float& l, float& r) { l = gen.voice(0.06f); r = l; });
    run(game, 2, 4.0f, [&](int i, float& l, float& r) {
        const float burst = (i % 24000) < 900 ? 0.8f : 0.05f;   // Periodic gunshots.
        l = gen.noise(burst); r = gen.noise(burst);
    });
    run(bgm, 2, 4.0f, [&](int i, float& l, float& r) {
        const float t = static_cast<float>(i) / 48000.0f;
        l = 0.22f * std::sin(kTwoPi * 220.0f * t) + 0.12f * std::sin(kTwoPi * 330.0f * t);
        r = 0.22f * std::sin(kTwoPi * 220.0f * t + 0.6f) + 0.12f * std::sin(kTwoPi * 440.0f * t);
    });
    // Director hears the summed programme; level chosen to land near -12 LUFS.
    run(director, 2, 6.0f, [&](int i, float& l, float& r) {
        const float t = static_cast<float>(i) / 48000.0f;
        const float v = gen.voice(0.143f);
        l = v + 0.072f * std::sin(kTwoPi * 220.0f * t);
        r = v + 0.072f * std::sin(kTwoPi * 220.0f * t + 0.6f);
    });
    std::printf("director integrated LUFS = %.1f\n", static_cast<double>(director.telemetry().lufs_i.load()));
    // Re-run UNIT 01 last so its bus-derived flags reflect the full mix
    // (game + BGM active -> no "solo chat" hint on the normal screenshot).
    run(voice, 2, 1.0f, [&](int, float& l, float& r) { l = gen.voice(0.11f); r = l; });

    std::printf("Rendering GUI samples into %s\n", g_out.toUtf8().constData());

    // 01/02: UNIT 01 console, JA and EN.
    {
        StudioConsole con(&voice, &bus_v, false, false);
        con.resize(520, 560);
        con.set_language(UiLang::JA);
        con.show();
        QMetaObject::invokeMethod(&con, "refresh");
        save(&con, QStringLiteral("01_console_unit01_ja.png"));
        con.set_language(UiLang::EN);
        QMetaObject::invokeMethod(&con, "refresh");
        save(&con, QStringLiteral("02_console_unit01_en.png"));
        con.hide();
    }

    // 03: UNIT 01 with a hint + the amber update lamp lit + pinned.
    {
        StudioConsole con(&voice, &bus_v, false, false);
        con.resize(520, 560);
        con.set_language(UiLang::JA);
        con.set_update_available(QStringLiteral("1.1.0"));
        con.show();
        if (QPushButton* pin = find_button(&con, QStringLiteral("PIN"))) pin->setChecked(true);
        voice.telemetry().hint_bgm_misplug.store(1);
        QMetaObject::invokeMethod(&con, "refresh");
        save(&con, QStringLiteral("03_console_unit01_hint_update.png"));
        voice.telemetry().hint_bgm_misplug.store(0);
        con.hide();
    }

    // 04/05: UNIT 05 director console with the mixer, JA and EN.
    {
        StudioConsole con(&director, &bus_d, true, true);
        con.resize(1180, 560);
        con.set_language(UiLang::JA);
        con.show();
        QMetaObject::invokeMethod(&con, "refresh");
        save(&con, QStringLiteral("04_console_director_ja.png"));
        con.set_language(UiLang::EN);
        QMetaObject::invokeMethod(&con, "refresh");
        save(&con, QStringLiteral("05_console_director_en.png"));
        con.hide();
    }

    // 06/07: MINI meter-bar mode (UNIT 01 and UNIT 05).
    {
        StudioConsole con(&voice, &bus_v, false, false);
        con.resize(520, 560);
        con.set_language(UiLang::JA);
        con.show();
        if (QPushButton* mini = find_button(&con, QStringLiteral("MINI"))) mini->click();
        QMetaObject::invokeMethod(&con, "refresh");
        save(&con, QStringLiteral("06_console_mini_unit01.png"));
        con.hide();
    }
    {
        StudioConsole con(&director, &bus_d, true, true);
        con.resize(1180, 560);
        con.set_language(UiLang::JA);
        con.show();
        if (QPushButton* mini = find_button(&con, QStringLiteral("MINI"))) mini->click();
        QMetaObject::invokeMethod(&con, "refresh");
        save(&con, QStringLiteral("07_console_mini_director.png"));
        con.hide();
    }

    // 08-11: calibration modal states (driven by real calibrator runs).
    auto calib_shot = [&](const QString& name, float seconds, auto signal) {
        Engine e(MODE_VOICE);
        e.prepare(48000.0f, 2);
        Skin skin;
        CalibrationDialog dlg(&e, &skin, UiLang::JA);
        dlg.resize(520, 300);
        dlg.show();                                  // Starts the 10 s measurement.
        pump();
        run(e, 2, seconds, signal);
        QMetaObject::invokeMethod(&dlg, "tick");
        save(&dlg, name);
        dlg.hide();
    };
    calib_shot(QStringLiteral("08_calibration_measuring.png"), 4.0f,
               [&](int, float& l, float& r) { l = gen.voice(0.10f); r = l; });
    calib_shot(QStringLiteral("09_calibration_success.png"), 10.2f,
               [&](int, float& l, float& r) { l = gen.voice(0.10f); r = l; });
    calib_shot(QStringLiteral("10_calibration_clip_warning.png"), 10.2f,
               [&](int i, float& l, float& r) { l = ((i / 40) % 2) ? 1.0f : -1.0f; r = l; }); // Flat-top square.
    calib_shot(QStringLiteral("11_calibration_no_voice.png"), 10.2f,
               [&](int, float& l, float& r) { l = gen.noise(0.0005f); r = l; });

    // 12: OBS filter panel approximation.
    {
        QWidget* panel = make_obs_filter_panel();
        panel->show();
        save(panel, QStringLiteral("12_obs_filter_panel_approx.png"));
        delete panel;
    }

    std::printf("Done: %d images.\n", g_count);
    return g_count == 12 ? 0 : 1;
}
