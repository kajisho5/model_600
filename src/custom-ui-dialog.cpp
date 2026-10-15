// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  custom-ui-dialog.cpp  [Layer 2]  Studio console, UNIT 05 mixer, calibration.
// =============================================================================
#include "custom-ui-dialog.hpp"
#include "hw-panel.hpp"
#include "ui-theme.hpp"
#include "ui-widgets.hpp"

#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <utility>

namespace sw::model600::ui {

namespace {


// Palette so native children (message boxes, menus) match the console.
QPalette studio_palette() {
    QPalette pal;
    pal.setColor(QPalette::Window, Tok::faceplate);
    pal.setColor(QPalette::WindowText, Tok::t1);
    pal.setColor(QPalette::Base, Tok::panel);
    pal.setColor(QPalette::AlternateBase, Tok::raised);
    pal.setColor(QPalette::Text, Tok::t1);
    pal.setColor(QPalette::Button, Tok::raised);
    pal.setColor(QPalette::ButtonText, Tok::t1);
    pal.setColor(QPalette::Highlight, Tok::accent);
    pal.setColor(QPalette::HighlightedText, QColor(0x1A, 0x12, 0x06));
    pal.setColor(QPalette::ToolTipBase, Tok::raised);
    pal.setColor(QPalette::ToolTipText, Tok::t1);
    pal.setColor(QPalette::PlaceholderText, Tok::t3);
    return pal;
}

} // namespace

// =============================================================================
//  CalibrationDialog ("INPUT CALIBRATOR", GUI v2)
// =============================================================================
namespace {

const QColor kCardInk(0x2A, 0x24, 0x1A);
const QColor kCardDim(0x6B, 0x5D, 0x45);

QPointF cal_point(const QJsonObject& o) {
    return {o.value(QStringLiteral("cx")).toDouble(), o.value(QStringLiteral("cy")).toDouble()};
}

// Knob guide position (percent of the sweep) for "aim for 1:30" and "turn it down".
constexpr double kGuideOk = 67.0;
constexpr double kGuideDown = 94.0;

void draw_lines(QPainter& p, double x, double y, const QStringList& lines, const QFont& f, const QColor& c, double lh) {
    p.setFont(f);
    p.setPen(c);
    const double step = QFontInfo(f).pixelSize() * lh;
    for (int i = 0; i < lines.size(); ++i) p.drawText(QPointF(x, y + i * step), lines.at(i));
}

} // namespace

CalibrationDialog::CalibrationDialog(dsp::Engine* engine, UiLang lang, QWidget* parent)
    : QDialog(parent), engine_(engine), lang_(lang) {
    install_fonts();
    setPalette(studio_palette());
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setModal(true);
    setWindowTitle(tr2("MODEL 600  -  Automatic Input Calibration", "MODEL 600  -  入力レベル自動測定"));

    const hw::Assets& a = hw::Assets::get();
    cal_ = a.calibrator();
    bg_ = a.background(QStringLiteral("cal"));
    const QJsonObject panel = cal_.value(QStringLiteral("panel")).toObject();
    setFixedSize(panel.value(QStringLiteral("w")).toInt(640), panel.value(QStringLiteral("h")).toInt(450));

    const QJsonObject st = cal_.value(QStringLiteral("steps")).toObject();
    const QJsonArray xs = st.value(QStringLiteral("xs")).toArray();
    for (int i = 0; i < 3; ++i) {   // Always three LEDs, even if the layout is missing.
        const double x = i < xs.size() ? xs.at(i).toDouble() : 458.0 + 64.0 * i;
        steps_[static_cast<std::size_t>(i)] = new hw::Led(QPointF(x, st.value(QStringLiteral("y")).toDouble(40.0)), this);
    }

    lamp_ = new hw::Lamp(cal_point(cal_.value(QStringLiteral("lamp")).toObject()), QStringLiteral("amber"), this);

    const QJsonObject m = cal_.value(QStringLiteral("meter")).toObject();
    const QPointF mc = cal_point(m);
    const double mw = m.value(QStringLiteral("w")).toDouble() - 28, mh = m.value(QStringLiteral("h")).toDouble() - 28;
    meter_ = new hw::DigitalMeter(QRectF(mc.x() - mw / 2, mc.y() - mh / 2, mw, mh), hw::DigitalMeter::Kind::Dbfs, this);
    meter_->setCursor(Qt::ArrowCursor);

    const QJsonObject g = cal_.value(QStringLiteral("seg")).toObject();
    const QPointF gc = cal_point(g);
    const double gw = g.value(QStringLiteral("w")).toDouble(), gh = g.value(QStringLiteral("h")).toDouble();
    seg_ = new hw::SegDisplay(QRectF(gc.x() - gw / 2, gc.y() - gh / 2, gw, gh), 4, this);

    const QJsonObject l = cal_.value(QStringLiteral("ladder")).toObject();
    ladder_ = new hw::Ladder(cal_point(l), l.value(QStringLiteral("n")).toInt(12), {{0.0, QStringLiteral("amber")}}, this);

    const QJsonObject k = cal_.value(QStringLiteral("guide_knob")).toObject();
    guide_ = new hw::Knob(QStringLiteral("knob_mid"), cal_point(k), k.value(QStringLiteral("r")).toDouble(), 0.0, 100.0,
                          kGuideOk, this);
    guide_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    guide_->setFocusPolicy(Qt::NoFocus);
    guide_->setVisible(false);

    const QJsonArray bs = cal_.value(QStringLiteral("buttons")).toArray();
    auto make_button = [this](const QJsonObject& b) {
        return new hw::Button(cal_point(b), b.value(QStringLiteral("w")).toInt(54), b.value(QStringLiteral("h")).toInt(34),
                              b.value(QStringLiteral("color")).toString(), this);
    };
    secondary_ = make_button(bs.at(0).toObject());
    primary_ = make_button(bs.at(1).toObject());

    timer_ = new QTimer(this);
    timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, &CalibrationDialog::tick);
    clock_.start();
}

QString CalibrationDialog::tr2(const char* en, const char* ja) const {
    return lang_ == UiLang::JA ? QString::fromUtf8(ja) : QString::fromUtf8(en);
}

void CalibrationDialog::set_buttons(const QString& secondary, const QString& primary, bool primary_enabled) {
    secondary_->disconnect();
    primary_->disconnect();
    secondary_text_ = secondary;
    primary_text_ = primary;
    primary_enabled_ = primary_enabled;
    primary_->setEnabled(primary_enabled);
    primary_->set_lit(primary_enabled);   // The default action is the lit button.
    secondary_->set_lit(false);
}

void CalibrationDialog::showEvent(QShowEvent* e) {
    QDialog::showEvent(e);
    if (!started_) start();
}

void CalibrationDialog::start() {
    started_ = true;
    state_ = State::Measuring;
    engine_->start_calibration(10.0f);
    engine_->telemetry().reset_holds();
    peak_.reset();
    last_ms_ = 0;

    steps_[0]->set_state(hw::Led::State::On);
    steps_[1]->set_state(hw::Led::State::Idle);
    steps_[2]->set_state(hw::Led::State::Off);
    lamp_->set_color(QStringLiteral("amber"));
    lamp_->set_lit(true);
    title_ = tr2("Measuring", "測定中");
    body_ = lang_ == UiLang::JA ? QStringList{QStringLiteral("普段の配信と同じ声の大きさで、"), QStringLiteral("カードを読み続けてください。")}
                                : QStringList{QStringLiteral("Keep reading the card at your"), QStringLiteral("normal streaming volume.")};
    card_head_ = tr2("READ ALOUD", "READ ALOUD  ／  読み上げ文");
    card_ = lang_ == UiLang::JA
        ? QStringList{QStringLiteral("「みなさんこんばんは、配信へようこそ。"), QStringLiteral("  今日も楽しい時間にしていきましょう。」")}
        : QStringList{QStringLiteral("“Hello everyone, welcome back to the stream."), QStringLiteral("  Let's make today a great one.”")};
    card_large_ = true;
    seg_label_ = QStringLiteral("SECONDS  /  10");
    hint_ = QStringList{tr2("Press Esc to cancel.", "Esc で中止できます。")};
    guide_->setVisible(false);
    ladder_->set_zones({{0.0, QStringLiteral("amber")}});
    ladder_->set_lit(0);
    meter_->set_kind(hw::DigitalMeter::Kind::Dbfs);
    set_buttons(tr2("Cancel", "中止"), tr2("Done", "完了"), false);
    connect(secondary_, &QAbstractButton::clicked, this, &CalibrationDialog::reject);
    timer_->start();
    tick();
    update();
}

void CalibrationDialog::reject() {
    if (state_ == State::Measuring && engine_->calibrator().running()) engine_->calibrator().reset();
    timer_->stop();
    QDialog::reject();
}

void CalibrationDialog::tick() {
    auto& c = engine_->calibrator();
    const auto& t = engine_->telemetry();
    const qint64 now = clock_.elapsed();
    const float dt = last_ms_ > 0 ? std::clamp(static_cast<float>(now - last_ms_) / 1000.0f, 0.0f, 0.25f) : 0.033f;
    last_ms_ = now;
    peak_.feed(t.sample_peak_db.load(), now, dt);
    const float progress = std::clamp(c.progress(), 0.0f, 1.0f);
    meter_->set_values(peak_.bar, peak_.hold,
                       peak_.hold > -99.0f ? QString::number(static_cast<double>(peak_.hold), 'f', 1) : QStringLiteral("----"),
                       QStringLiteral("PEAK"), QStringLiteral("dBFS"),
                       QStringLiteral("VOICE  %1 %").arg(static_cast<int>(std::lround(c.voice_ratio() * 100.0f))));
    seg_->set_text(QString::number(static_cast<double>(progress) * 10.0, 'f', 1));
    ladder_->set_lit(static_cast<int>(std::floor(progress * static_cast<float>(ladder_->segments()) + 1e-4f)));
    if (c.done()) { timer_->stop(); show_result(); }
}

void CalibrationDialog::show_result() {
    auto& c = engine_->calibrator();
    const bool ja = lang_ == UiLang::JA;
    steps_[0]->set_state(hw::Led::State::On);
    steps_[1]->set_state(hw::Led::State::On);
    steps_[2]->set_state(hw::Led::State::Idle);
    card_head_ = tr2("RESULT", "RESULT  ／  結果");
    card_large_ = false;
    seg_label_ = QStringLiteral("INPUT GAIN  dB");
    ladder_->set_lit(ladder_->segments());
    const float gain_now = engine_->params().input_gain_db.load();
    auto seg_gain = [](float g) { return std::abs(g) < 0.05f ? QStringLiteral("0.0") : QString::number(static_cast<double>(g), 'f', 1); };

    if (c.result_clipped()) {
        state_ = State::Clipped;
        lamp_->set_color(QStringLiteral("red"));
        title_ = tr2("The input is clipping", "入力が歪んでいます");
        body_ = ja ? QStringList{QStringLiteral("インターフェースの GAIN を下げてから、"), QStringLiteral("もう一度測定してください。")}
                   : QStringList{QStringLiteral("Turn down the GAIN on your interface,"), QStringLiteral("then measure again.")};
        const QString secs = QString::number(static_cast<double>(c.clipped_seconds()), 'f', 1);
        card_ = ja ? QStringList{QStringLiteral("%1 秒間、入力が 0 dBFS に張り付きました。").arg(secs), QStringLiteral("入力ゲインは変更していません。")}
                   : QStringList{QStringLiteral("The input sat at 0 dBFS for %1 s.").arg(secs), QStringLiteral("The input gain was not changed.")};
        meter_->set_values(0.0, 0.0, secs, QStringLiteral("CLIPPED"), QStringLiteral("s"),
                           QStringLiteral("INPUT GAIN %1 dB").arg(fmt_signed(gain_now)));
        seg_->set_text(seg_gain(gain_now));
        ladder_->set_zones({{0.0, QStringLiteral("red")}});
        guide_->set_value(kGuideDown);
        guide_->setVisible(true);
        hint_ = ja ? QStringList{QStringLiteral("つまみを左へ回して、"), QStringLiteral("クリップランプが消える位置にします。")}
                   : QStringList{QStringLiteral("Turn the knob left until"), QStringLiteral("the clip light stays off.")};
        set_buttons(tr2("Close", "閉じる"), tr2("Measure", "再測定"), true);
        connect(secondary_, &QAbstractButton::clicked, this, &CalibrationDialog::reject);
        connect(primary_, &QAbstractButton::clicked, this, &CalibrationDialog::start);
    } else if (c.result_silent() || !c.result_ok()) {
        state_ = State::NoVoice;
        lamp_->set_color(QStringLiteral("amber"));
        title_ = tr2("No voice was detected", "声を検出できませんでした");
        body_ = ja ? QStringList{QStringLiteral("マイクのミュートと、OBS の"), QStringLiteral("入力デバイスを確認してください。")}
                   : QStringList{QStringLiteral("Check the mic mute and the input"), QStringLiteral("device selected in OBS.")};
        const int pct = static_cast<int>(std::lround(c.voice_ratio() * 100.0f));
        card_ = ja ? QStringList{QStringLiteral("声として検出できたのは %1 %（20 % 以上が必要）。").arg(pct), QStringLiteral("入力ゲインは変更していません。")}
                   : QStringList{QStringLiteral("Voice was detected %1 % of the time (20 % needed).").arg(pct), QStringLiteral("The input gain was not changed.")};
        const float pk = engine_->telemetry().peak_hold_db.load();
        meter_->set_values(pk, pk, QString::number(pct), QStringLiteral("VOICE"), QStringLiteral("%"), QStringLiteral("REQUIRED 20 %"));
        seg_->set_text(seg_gain(gain_now));
        ladder_->set_zones({{0.0, QStringLiteral("amber")}});
        guide_->setVisible(false);
        hint_ = ja ? QStringList{QStringLiteral("マイクの物理ミュートスイッチも"), QStringLiteral("確認してください。")}
                   : QStringList{QStringLiteral("Also check the hardware mute"), QStringLiteral("switch on the mic.")};
        set_buttons(tr2("Close", "閉じる"), tr2("Measure", "再測定"), true);
        connect(secondary_, &QAbstractButton::clicked, this, &CalibrationDialog::reject);
        connect(primary_, &QAbstractButton::clicked, this, &CalibrationDialog::start);
    } else {
        state_ = State::Success;
        engine_->commit_calibration();
        lamp_->set_color(QStringLiteral("green"));
        title_ = tr2("Calibration complete", "測定が完了しました");
        body_ = ja ? QStringList{QStringLiteral("入力ゲインを自動で設定しました。"), QStringLiteral("このまま配信を始められます。")}
                   : QStringList{QStringLiteral("The input gain has been set"), QStringLiteral("automatically. You are ready to go live.")};
        const QString ref = fmt_db(c.result_reference_db()), target = fmt_db(c.target_peak_db()), gain = fmt_signed(c.result_gain_db());
        card_ = ja ? QStringList{QStringLiteral("基準ピーク %1 dBFS  →  目標 %2 dBFS").arg(ref, target), QStringLiteral("入力ゲインを %1 dB にしました。").arg(gain)}
                   : QStringList{QStringLiteral("Reference peak %1 dBFS  →  target %2 dBFS").arg(ref, target), QStringLiteral("Input gain set to %1 dB.").arg(gain)};
        meter_->set_values(c.result_reference_db(), c.result_reference_db(), ref, QStringLiteral("REF. PEAK"), QStringLiteral("dBFS"),
                           QStringLiteral("TARGET %1").arg(target));
        seg_->set_text(seg_gain(c.result_gain_db()));
        ladder_->set_zones({{0.0, QStringLiteral("green")}});
        guide_->set_value(kGuideOk);
        guide_->setVisible(true);
        const bool low = c.result_gain_db() > 12.0f;
        hint_ = low ? (ja ? QStringList{QStringLiteral("入力が小さめです。GAIN つまみを少し右へ"), QStringLiteral("回すとさらに安定します（目安は1時〜2時）。")}
                          : QStringList{QStringLiteral("Your input is low. Turn the GAIN knob slightly"), QStringLiteral("right for more headroom (1 to 2 o'clock).")})
                    : (ja ? QStringList{QStringLiteral("オーディオインターフェースの GAIN つまみは"), QStringLiteral("1時〜2時の位置が目安です。")}
                          : QStringList{QStringLiteral("Set the GAIN knob on your audio interface"), QStringLiteral("around the 1 to 2 o'clock position.")});
        set_buttons(tr2("Measure", "再測定"), tr2("Done", "完了"), true);
        connect(secondary_, &QAbstractButton::clicked, this, &CalibrationDialog::start);
        connect(primary_, &QAbstractButton::clicked, this, &CalibrationDialog::accept);
    }
    lamp_->set_lit(true);
    update();
}

void CalibrationDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (bg_.isNull()) p.fillRect(rect(), Tok::faceplate);
    else p.drawPixmap(QRectF(rect()), bg_, QRectF(bg_.rect()));
    const QColor ink(0xE9, 0xE3, 0xD3), ink_dim(0xA9, 0xA4, 0x97);

    // Status title + explanation next to the lamp.
    const QJsonObject t = cal_.value(QStringLiteral("title")).toObject();
    p.setFont(make_font(14, QFont::Bold, 0.4));
    p.setPen(ink);
    p.drawText(QPointF(t.value(QStringLiteral("x")).toDouble(), t.value(QStringLiteral("y")).toDouble()), title_);
    const QJsonObject b = cal_.value(QStringLiteral("body")).toObject();
    draw_lines(p, b.value(QStringLiteral("x")).toDouble(), b.value(QStringLiteral("y")).toDouble() + 10, body_,
               make_font(10, QFont::DemiBold, 0.2), ink_dim, 1.45);

    // Cue card.
    const QJsonObject c = cal_.value(QStringLiteral("card")).toObject();
    const double cx = c.value(QStringLiteral("x")).toDouble(), cy = c.value(QStringLiteral("y")).toDouble();
    p.setFont(make_font(8, QFont::Bold, 1.4));
    p.setPen(kCardDim);
    p.drawText(QPointF(cx + 14, cy + 22), card_head_);
    draw_lines(p, cx + 14, cy + 50, card_, make_font(card_large_ ? 13 : 11, QFont::Bold, 0.2), kCardInk, 1.6);

    // 7-segment window label.
    const QJsonObject g = cal_.value(QStringLiteral("seg")).toObject();
    const QFont lf = make_font(9, QFont::DemiBold, 2.0);
    p.setFont(lf);
    p.setPen(ink_dim);
    const double lw = QFontMetricsF(lf).horizontalAdvance(seg_label_);
    p.drawText(QPointF(g.value(QStringLiteral("cx")).toDouble() - lw / 2, g.value(QStringLiteral("label_y")).toDouble()), seg_label_);

    // Hint line (after the knob guide when it is shown).
    const QJsonObject h = cal_.value(QStringLiteral("hint")).toObject();
    const double hx = h.value(QStringLiteral("x")).toDouble() + (guide_->isVisible() ? 52.0 : 0.0);
    draw_lines(p, hx, h.value(QStringLiteral("y")).toDouble() - 6, hint_, make_font(10, QFont::DemiBold, 0.2), ink_dim, 1.45);

    // Button names under the caps.
    const QFont bf = make_font(9, QFont::Bold, 1.0);
    p.setFont(bf);
    const QFontMetricsF bm(bf);
    auto label = [&](const hw::Button* btn, const QString& text, bool enabled) {
        const QRectF r = btn->geometry();
        p.setPen(enabled ? ink : ink_dim);
        p.drawText(QPointF(r.center().x() - bm.horizontalAdvance(text) / 2, r.center().y() + 17 + 22), text);
    };
    label(secondary_, secondary_text_, true);
    label(primary_, primary_text_, primary_enabled_);
}

// =============================================================================
//  StudioConsole (GUI v2: hardware faceplates, docs/design/v2/DESIGN_V2.md)
// =============================================================================
namespace {

const char* const kDownloadUrl = "https://www.seventhwell.com/model600/download";

QPointF centre_of(const QJsonObject& o) {
    return {o.value(QStringLiteral("cx")).toDouble(), o.value(QStringLiteral("cy")).toDouble()};
}

QJsonArray items(const QJsonObject& layout, const char* key) { return layout.value(QLatin1String(key)).toArray(); }

// Number for a 7-segment field: one decimal, "----" for silence.
QString seg_db(float db) {
    if (!(db > -99.0f)) return QStringLiteral("----");
    if (std::abs(db) < 0.05f) return QStringLiteral("0.0");
    return QString::number(static_cast<double>(db), 'f', 1);
}

QString lufs_text(float lufs) {
    return lufs > -99.0f ? QString::number(static_cast<double>(lufs), 'f', 1) : QStringLiteral("--");
}

double knob_default(const QString& id) {
    if (id == QLatin1String("target")) return -14.5;
    if (id == QLatin1String("duck")) return -3.5;
    if (id == QLatin1String("ceiling")) return -6.0;
    if (id == QLatin1String("scoop")) return -5.0;
    if (id == QLatin1String("pocket")) return -8.5;
    return 0.0;   // input / output.
}

} // namespace

StudioConsole::StudioConsole(dsp::Engine* engine, SharedBus* bus, bool director, bool pro, QWidget* parent)
    : QDialog(parent), engine_(engine), bus_(bus), director_(director && pro), pro_(pro) {
#if !defined(MODEL600_PRO_EDITION)
    director_ = false;   // The Standard binary has no UNIT 05.
#endif
    install_fonts();
    setPalette(studio_palette());
    setFont(font_body());
    setWindowTitle(QStringLiteral("MODEL 600  -  %1 %2").arg(unit_number(), unit_name()));
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    build_ui();
    clock_.start();
    timer_ = new QTimer(this);
    timer_->setInterval(33);   // ~30 fps, only while visible.
    connect(timer_, &QTimer::timeout, this, &StudioConsole::refresh);
}

StudioConsole::~StudioConsole() = default;

QString StudioConsole::unit_number() const {
    if (director_) return QStringLiteral("05");
    switch (engine_->mode()) {
        case MODE_COLLAB: return QStringLiteral("02");
        case MODE_GAME: return QStringLiteral("03");
        case MODE_BGM: return QStringLiteral("04");
        case MODE_DIRECTOR: return QStringLiteral("05");
        default: return QStringLiteral("01");
    }
}

QString StudioConsole::unit_name() const {
    if (director_) return QStringLiteral("STREAM DIRECTOR");
    switch (engine_->mode()) {
        case MODE_COLLAB: return QStringLiteral("COLLAB RIDER");
        case MODE_GAME: return QStringLiteral("GAME IMPACT");
        case MODE_BGM: return QStringLiteral("BGM POCKET");
        case MODE_DIRECTOR: return QStringLiteral("STREAM DIRECTOR");
        default: return QStringLiteral("VOCAL MASTER");
    }
}

QString StudioConsole::tr2(const char* en, const char* ja) const {
    return lang_ == UiLang::JA ? QString::fromUtf8(ja) : QString::fromUtf8(en);
}

void StudioConsole::changed() { if (on_params_changed) on_params_changed(); }

void StudioConsole::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    build_panel();
    root->addWidget(panel_);
    mini_bar_ = new hw::MiniStrip(this);
    mini_bar_->setVisible(false);
    root->addWidget(mini_bar_);
    setFixedSize(panel_->sizeHint());
    connect(panel_, &hw::Panel::contextRequested, this, &StudioConsole::show_menu);
    connect(panel_, &hw::Panel::statusClicked, this, [this] {
        if (!update_version_.isEmpty()) QDesktopServices::openUrl(QUrl(QString::fromLatin1(kDownloadUrl)));
    });
    relabel();
    sync_from_params();
}

std::atomic<float>* StudioConsole::param_for(const QString& id) const {
    auto& p = engine_->params();
    if (id == QLatin1String("input")) return &p.input_gain_db;
    if (id == QLatin1String("output")) return &p.output_trim_db;
    if (id == QLatin1String("target")) return &p.collab_target_lufs;
    if (id == QLatin1String("duck")) return &p.collab_duck_db;
    if (id == QLatin1String("ceiling")) return &p.game_ceiling_db;
    if (id == QLatin1String("scoop")) return &p.game_scoop_db;
    if (id == QLatin1String("pocket")) return &p.bgm_pocket_db;
    return nullptr;
}

void StudioConsole::build_panel() {
    panel_ = new hw::Panel(unit_number(), this);
    const QJsonObject& L = panel_->layout();

    // Continuous knobs.
    for (const QJsonValue v : items(L, "knobs_big")) {
        const QJsonObject k = v.toObject();
        const QString id = k.value(QStringLiteral("id")).toString();
        const double r = k.value(QStringLiteral("r")).toDouble();
        auto* knob = new hw::Knob(r >= 50 ? QStringLiteral("knob_big") : QStringLiteral("knob_mid"), centre_of(k), r,
                                  k.value(QStringLiteral("min")).toDouble(), k.value(QStringLiteral("max")).toDouble(),
                                  knob_default(id), panel_);
        const QString unit = k.value(QStringLiteral("unit")).toString();
        knob->set_step(0.5);
        knob->set_formatter([unit](double d) {
            return (unit == QLatin1String("LUFS") ? QString::number(d, 'f', 1) : fmt_signed(static_cast<float>(d)))
                   + QStringLiteral(" ") + unit;
        });
        knob->setObjectName(QStringLiteral("knob_") + id);
        knobs_.insert(id, knob);
        connect(knob, &hw::Knob::valueChanged, this, [this, id](double d) {
            if (auto* p = param_for(id)) p->store(static_cast<float>(d));
        });
        connect(knob, &hw::Knob::released, this, &StudioConsole::changed);
    }

    // Stepped selectors.
    for (const QJsonValue v : items(L, "knobs_step")) {
        const QJsonObject k = v.toObject();
        const QString id = k.value(QStringLiteral("id")).toString();
        const QJsonArray pos = k.value(QStringLiteral("positions")).toArray();
        const double spread = k.value(QStringLiteral("spread")).toDouble();
        QVector<double> angles;
        QStringList names;
        const int n = static_cast<int>(pos.size());
        for (int i = 0; i < n; ++i) {
            angles.push_back(n > 1 ? -spread + 2.0 * spread * i / (n - 1) : 0.0);
            names.push_back(pos.at(i).toString());
        }
        auto* sel = new hw::Selector(centre_of(k), k.value(QStringLiteral("r")).toDouble(), angles, names, panel_);
        sel->setObjectName(QStringLiteral("selector_") + id);
        selectors_.insert(id, sel);
        connect(sel, &hw::Selector::indexChanged, this, [this, id](int i) {
            auto& p = engine_->params();
            if (id == QLatin1String("character")) p.character.store(i);
            else if (id == QLatin1String("space")) p.space_fx.store(i);
            else if (id == QLatin1String("scene")) {
                p.apply_scene(i);
                if (on_scene_macro) on_scene_macro(i);
            }
            changed();
        });
    }

    // UNIT 05 channel trims, pushed to UNITs 01-04 over the shared bus.
    int ch = 0;
    for (const QJsonValue v : items(L, "knobs_trim")) {
        const QJsonObject k = v.toObject();
        auto* t = new hw::Knob(QStringLiteral("knob_trim"), centre_of(k), k.value(QStringLiteral("r")).toDouble(),
                               k.value(QStringLiteral("min")).toDouble(), k.value(QStringLiteral("max")).toDouble(), 0.0,
                               panel_);
        t->set_step(0.5);
        t->set_formatter([](double d) { return fmt_signed(static_cast<float>(d)) + QStringLiteral(" dB"); });
        const Mode m = static_cast<Mode>(ch);
        connect(t, &hw::Knob::valueChanged, this, [this, m](double d) {
            if (bus_) bus_->push_trim(m, static_cast<float>(d));
        });
        trims_.push_back(t);
        ++ch;
    }

    // Toggle switches.
    for (const QJsonValue v : items(L, "toggles")) {
        const QJsonObject t = v.toObject();
        const QString id = t.value(QStringLiteral("id")).toString();
        auto* tg = new hw::Toggle(centre_of(t), t.value(QStringLiteral("horizontal")).toBool(), panel_);
        tg->setObjectName(QStringLiteral("toggle_") + id);
        toggles_.insert(id, tg);
        connect(tg, &QAbstractButton::clicked, this, [this, id](bool on) {
            auto& p = engine_->params();
            if (id == QLatin1String("mono")) p.auto_mono.store(on ? 1 : 0);
            else if (id == QLatin1String("latency")) p.zero_latency.store(on ? 0 : 1);   // Up = 1.5 ms look-ahead.
            else if (id == QLatin1String("safe")) p.bgm_safe_mode.store(on ? 1 : 0);
            else if (id == QLatin1String("swell")) p.bgm_auto_swell.store(on ? 1 : 0);
            if (id != QLatin1String("meter")) changed();   // OUT / GR only switches the display.
            refresh();
        });
    }

    // Illuminated push buttons.
    for (const QJsonValue v : items(L, "buttons")) {
        const QJsonObject b = v.toObject();
        const QString id = b.value(QStringLiteral("id")).toString();
        auto* btn = new hw::Button(centre_of(b), b.value(QStringLiteral("w")).toInt(), b.value(QStringLiteral("h")).toInt(),
                                   b.value(QStringLiteral("color")).toString(), panel_);
        btn->setObjectName(QStringLiteral("button_") + id);
        buttons_.insert(id, btn);
        if (id == QLatin1String("cal")) connect(btn, &QAbstractButton::clicked, this, &StudioConsole::run_calibration);
        else if (id == QLatin1String("ab")) connect(btn, &QAbstractButton::clicked, this, &StudioConsole::toggle_audition);
        else if (id == QLatin1String("bleep")) {
            connect(btn, &QAbstractButton::pressed, this, [this] { engine_->bleep().trigger(3.0f); });
            connect(btn, &QAbstractButton::released, this, [this] { engine_->bleep().stop(); });
        } else if (id == QLatin1String("cough")) {
            connect(btn, &QAbstractButton::pressed, this, [this] { engine_->cough().press(true); });
            connect(btn, &QAbstractButton::released, this, [this] { engine_->cough().release(); });
        } else if (id == QLatin1String("mono")) {
            connect(btn, &QAbstractButton::clicked, this, [this] {
                auto& p = engine_->params();
                p.mono_check.store(p.mono_check.load() ? 0 : 1);
            });
        } else if (id == QLatin1String("reset")) {
            connect(btn, &QAbstractButton::clicked, this, [this] {
                engine_->reset_meters();
                for (auto& pk : peaks_) pk.reset();
            });
        } else if (id == QLatin1String("chapter")) {
            connect(btn, &QAbstractButton::clicked, this, [this] {
                flash_chapter_until_ = clock_.elapsed() + 1500;
                emit chapterRequested();
                if (on_request_chapter) on_request_chapter();
            });
        } else if (id == QLatin1String("report")) {
            connect(btn, &QAbstractButton::clicked, this, [this] {
                flash_report_until_ = clock_.elapsed() + 1500;
                if (on_export_karte) on_export_karte();
            });
        }
    }

    // Pilot lamps.
    for (const QJsonValue v : items(L, "lamps")) {
        const QJsonObject l = v.toObject();
        lamps_.insert(l.value(QStringLiteral("label")).toString(),
                      new hw::Lamp(centre_of(l), l.value(QStringLiteral("color")).toString(), panel_));
    }

    // Digital meter windows.
    for (const QJsonValue v : items(L, "vus")) {
        const QJsonObject m = v.toObject();
        const QJsonObject d = m.value(QStringLiteral("digi")).toObject();
        const double ww = d.value(QStringLiteral("win_w")).toDouble(), wh = d.value(QStringLiteral("win_h")).toDouble();
        const QPointF c = centre_of(m);
        const QString kind = d.value(QStringLiteral("kind")).toString();
        const hw::DigitalMeter::Kind k = kind == QLatin1String("ride")    ? hw::DigitalMeter::Kind::Ride
                                         : kind == QLatin1String("phase") ? hw::DigitalMeter::Kind::Phase
                                                                          : hw::DigitalMeter::Kind::Dbfs;
        auto* meter = new hw::DigitalMeter(QRectF(c.x() - ww / 2, c.y() - wh / 2, ww, wh), k, panel_);
        connect(meter, &hw::DigitalMeter::clicked, this, [this] {   // Click a meter to clear the peak hold.
            engine_->telemetry().reset_holds();
            for (auto& pk : peaks_) pk.reset();
        });
        meters_.push_back(meter);
    }

    // LED ladder (LIMITING / POCKET).
    const QJsonArray ladders = items(L, "ladders");
    if (!ladders.isEmpty()) {
        const QJsonObject l = ladders.at(0).toObject();
        QVector<QPair<double, QString>> zones;
        for (const QJsonValue z : l.value(QStringLiteral("colors")).toArray()) {
            const QJsonArray pair = z.toArray();
            zones.push_back({pair.at(0).toDouble(), pair.at(1).toString()});
        }
        ladder_ = new hw::Ladder(centre_of(l), l.value(QStringLiteral("n")).toInt(16), zones, panel_);
    }

    // 7-segment master read-outs (UNIT 05).
    for (const QJsonValue v : items(L, "displays")) {
        const QJsonObject d = v.toObject();
        const QPointF c = centre_of(d);
        const double w = d.value(QStringLiteral("w")).toDouble(), h = d.value(QStringLiteral("h")).toDouble();
        segs_.insert(d.value(QStringLiteral("id")).toString(),
                     new hw::SegDisplay(QRectF(c.x() - w / 2, c.y() - h / 2, w, h), 4, panel_));
    }

    // LINK row.
    const QJsonObject lk = L.value(QStringLiteral("links")).toObject();
    links_ = new hw::LinkRow(lk.value(QStringLiteral("x")).toDouble(), lk.value(QStringLiteral("y")).toDouble(),
                             lk.value(QStringLiteral("dx")).toDouble(), panel_);
}

void StudioConsole::sync_from_params() {
    auto& p = engine_->params();
    for (auto it = knobs_.begin(); it != knobs_.end(); ++it) {
        if (it.value()->dragging()) continue;
        if (auto* a = param_for(it.key())) it.value()->set_value(static_cast<double>(a->load()));
    }
    if (auto* s = selectors_.value(QStringLiteral("character"))) s->set_index(p.character.load());
    if (auto* s = selectors_.value(QStringLiteral("space"))) s->set_index(p.space_fx.load());
    if (auto* s = selectors_.value(QStringLiteral("scene"))) s->set_index(p.scene.load());
    if (auto* t = toggles_.value(QStringLiteral("mono"))) t->setChecked(p.auto_mono.load() != 0);
    if (auto* t = toggles_.value(QStringLiteral("latency"))) t->setChecked(p.zero_latency.load() == 0);
    if (auto* t = toggles_.value(QStringLiteral("safe"))) t->setChecked(p.bgm_safe_mode.load() != 0);
    if (auto* t = toggles_.value(QStringLiteral("swell"))) t->setChecked(p.bgm_auto_swell.load() != 0);
    if (bus_ && bus_->raw()) {
        for (int i = 0; i < trims_.size(); ++i) {
            if (trims_[i]->dragging()) continue;
            trims_[i]->set_value(static_cast<double>(bus_->raw()->trim_value_db[i].load(std::memory_order_acquire)));
        }
    }
}

void StudioConsole::relabel() {
    struct Names { const char* id; QStringList en, ja; };
    const Names sel[] = {
        {"character", {QStringLiteral("SOFT / ASMR"), QStringLiteral("BROADCAST"), QStringLiteral("FPS / LOUD")},
                      {QStringLiteral("ソフト・ASMR"), QStringLiteral("放送"), QStringLiteral("FPS・大声")}},
        {"space", {QStringLiteral("DRY"), QStringLiteral("KARAOKE"), QStringLiteral("BATH"), QStringLiteral("HALL"),
                   QStringLiteral("CAVE"), QStringLiteral("RADIO")},
                  {QStringLiteral("ドライ"), QStringLiteral("カラオケ"), QStringLiteral("お風呂"), QStringLiteral("ホール"),
                   QStringLiteral("洞窟"), QStringLiteral("ラジオ")}},
        {"scene", {QStringLiteral("FPS / GAME"), QStringLiteral("TALK"), QStringLiteral("SINGING"), QStringLiteral("ASMR")},
                  {QStringLiteral("FPS・ゲーム"), QStringLiteral("雑談"), QStringLiteral("歌枠"), QStringLiteral("ASMR")}},
    };
    const bool ja = lang_ == UiLang::JA;
    for (const Names& n : sel)
        if (auto* s = selectors_.value(QString::fromLatin1(n.id))) s->set_names(ja ? n.ja : n.en);

    struct Tip { const char* id; const char* en; const char* ja; };
    const Tip knob_tips[] = {
        {"input", "Input gain", "入力ゲイン"}, {"output", "Output trim", "出力トリム"},
        {"target", "Target loudness", "目標ラウドネス"}, {"duck", "Duck when I talk", "自分が話すと下げる量"},
        {"ceiling", "Impact ceiling", "音割れ防止の天井"}, {"scoop", "Voice scoop", "声の帯域を空ける量"},
        {"pocket", "Pocket depth", "BGM を下げる深さ"}};
    for (const Tip& t : knob_tips)
        if (auto* k = knobs_.value(QString::fromLatin1(t.id))) k->set_label(tr2(t.en, t.ja));
    const char* const ch_names[] = {"01 VOCAL", "02 COLLAB", "03 GAME", "04 BGM"};
    for (int i = 0; i < trims_.size() && i < 4; ++i)
        trims_[i]->set_label(QString::fromLatin1(ch_names[i]) + QStringLiteral("  ") + tr2("trim", "トリム"));

    const Tip toggle_tips[] = {
        {"meter", "Meter: output peak / gain reduction", "メーター：出力ピーク ／ リミッター減衰量"},
        {"mono", "Auto mono (one-sided mic rescue)", "片耳マイク救済（自動モノラル）"},
        {"latency", "Look-ahead: 1.5 ms (up) / 0 ms zero latency (down)", "先読み：1.5 ms（上）／ 0 ms 低遅延（下）"},
        {"safe", "Singing / recording safe", "歌枠・収録セーフ"},
        {"swell", "AFK swell", "離席時スウェル"}};
    for (const Tip& t : toggle_tips)
        if (auto* w = toggles_.value(QString::fromLatin1(t.id))) w->setToolTip(tr2(t.en, t.ja));
    const Tip button_tips[] = {
        {"cal", "10 s auto calibrate", "10秒 自動測定"}, {"ab", "A/B audition", "A/B 試聴"},
        {"bleep", "Bleep 1 kHz (hold)", "ピー音 1 kHz（押している間）"},
        {"cough", "Cough mute (hold)", "咳ミュート（押している間）"},
        {"mono", "Mono check", "モノラル確認"}, {"reset", "Reset meters", "メーターリセット"},
        {"chapter", "Chapter mark", "チャプター記録"}, {"report", "Export stream report", "配信カルテ出力"}};
    for (const Tip& t : button_tips)
        if (auto* w = buttons_.value(QString::fromLatin1(t.id))) w->setToolTip(tr2(t.en, t.ja));
    for (auto* m : meters_) m->setToolTip(tr2("Click to clear the peak hold", "クリックでピークホールドを解除"));
    panel_->setToolTip(tr2("Right-click for settings", "右クリックで設定"));
    update_status();
    update();
}

void StudioConsole::set_language(UiLang lang) {
    lang_ = lang;
    relabel();
}

void StudioConsole::set_update_available(const QString& version) {
    update_version_ = version;
    update_status();
}

void StudioConsole::update_status() {
    if (!panel_) return;
    const bool ja = lang_ == UiLang::JA;
    const auto& t = engine_->telemetry();
    const float fs = t.sample_rate.load();
    const double lat = fs > 0 ? engine_->latency_samples() * 1000.0 / fs : 0.0;
    const bool guard = bus_ && bus_->raw() && bus_->raw()->guard_active.load() != 0;
    const QString right = QStringLiteral("%1 kHz  ·  %2 ch  ·  v1.0.0")
                              .arg(QString::number(fs / 1000.0f, 'g', 3)).arg(t.channels.load());
    using Lv = hw::Panel::Level;
    if (t.hint_bgm_misplug.load()) {
        panel_->set_status(Lv::Warn, ja ? QStringLiteral("音楽を検出：BGM は 04 BGM POCKET へ")
                                        : QStringLiteral("Music detected: use 04 BGM POCKET"), right);
    } else if (t.bridge_mode.load()) {
        panel_->set_status(Lv::Info, ja ? QStringLiteral("VC Edition と連携中（二重処理を停止）")
                                        : QStringLiteral("Linked to VC Edition (no double processing)"), right);
    } else if (!update_version_.isEmpty()) {
        panel_->set_status(Lv::Info, ja ? QStringLiteral("アップデート %1 があります（クリックで入手）").arg(update_version_)
                                        : QStringLiteral("Update %1 available (click to download)").arg(update_version_), right);
    } else if (t.acapella_pad.load()) {
        panel_->set_status(Lv::Ok, ja ? QStringLiteral("雑談ソロ：天井を -1.5 dB 下げています")
                                      : QStringLiteral("Solo talk: ceiling lowered 1.5 dB"), right);
    } else {
        const QString g = guard ? (ja ? QStringLiteral("作動中") : QStringLiteral("active"))
                                : (ja ? QStringLiteral("待機") : QStringLiteral("standby"));
        if (director_) {
            int live = 0;
            if (bus_ && bus_->raw()) {
                const std::int64_t now = now_us();
                for (std::size_t i = 0; i < kMaxSlots; ++i) {
                    const auto& s = bus_->raw()->unit_slots[i];
                    if (s.in_use.load() && now - s.heartbeat_us.load() <= SharedBus::kHeartbeatGcUs) ++live;
                }
            }
            panel_->set_status(Lv::Ok,
                ja ? QStringLiteral("%1 ユニット リンク中  ·  Summing Guard %2  ·  先読み %3 ms").arg(live).arg(g).arg(lat, 0, 'f', 1)
                   : QStringLiteral("%1 units linked  ·  Summing Guard %2  ·  Look-ahead %3 ms").arg(live).arg(g).arg(lat, 0, 'f', 1),
                right);
        } else {
            panel_->set_status(Lv::Ok,
                ja ? QStringLiteral("動作中  ·  先読み %1 ms  ·  Summing Guard %2").arg(lat, 0, 'f', 1).arg(g)
                   : QStringLiteral("Running  ·  Look-ahead %1 ms  ·  Summing Guard %2").arg(lat, 0, 'f', 1).arg(g),
                right);
        }
    }
}

void StudioConsole::refresh_links() {
    if (!links_) return;
    std::array<hw::LinkRow::State, 5> st;
    st.fill(hw::LinkRow::State::Off);
    std::array<int, 5> count{};
    count.fill(0);
    if (bus_ && bus_->raw()) {
        const std::int64_t now = now_us();
        for (std::size_t i = 0; i < kMaxSlots; ++i) {
            const auto& s = bus_->raw()->unit_slots[i];
            if (!s.in_use.load() || now - s.heartbeat_us.load() > SharedBus::kHeartbeatGcUs) continue;
            const int m = s.mode.load();
            if (m < 0 || m >= 5) continue;
            const auto k = static_cast<std::size_t>(m);
            ++count[k];
            // Green when any instance of this unit carries audio, dim amber when all are silent
            // (e.g. their sources are in a scene that is not on air).
            if (s.signal_present.load()) st[k] = hw::LinkRow::State::On;
            else if (st[k] == hw::LinkRow::State::Off) st[k] = hw::LinkRow::State::Idle;
        }
    }
    links_->set(st, count);
}

void StudioConsole::refresh_unit(qint64 now, float dt) {
    const auto& t = engine_->telemetry();
    hw::PeakHold& pk = peaks_[0];
    pk.feed(t.sample_peak_db.load(), now, dt);
    const float lufs_s = t.lufs_s.load();
    if (!meters_.isEmpty()) {
        hw::DigitalMeter* m = meters_[0];
        const bool narrow = m->width() < 250;
        const QString aux_lufs = (narrow ? QStringLiteral("S-TERM %1 LUFS") : QStringLiteral("SHORT-TERM  %1 LUFS")).arg(lufs_text(lufs_s));
        const auto* meter_sw = toggles_.value(QStringLiteral("meter"));
        if (meter_sw && meter_sw->isChecked()) {
            const float gr = t.gr_db.load();
            m->set_kind(hw::DigitalMeter::Kind::Gr);
            m->set_values(gr, 0.0, seg_db(gr), QStringLiteral("GAIN REDUCTION"), QStringLiteral("dB"),
                          QStringLiteral("PEAK  %1 dBFS").arg(lufs_text(pk.hold)));
        } else {
            m->set_kind(hw::DigitalMeter::Kind::Dbfs);
            m->set_values(pk.bar, pk.hold, seg_db(pk.hold), QStringLiteral("PEAK"), QStringLiteral("dBFS"), aux_lufs);
        }
    }
    if (meters_.size() > 1) {   // UNIT 02 auto-ride gain.
        const float ride = t.ride_db.load();
        meters_[1]->set_values(ride, 0.0, seg_db(ride), QStringLiteral("CUT  /  BOOST"), QStringLiteral("dB"), QString());
    }
    const Mode mode = engine_->mode();
    if (ladder_) {
        // 16 LEDs over 0..-12 dB (0.8 dB per LED), as printed above the ladder.
        const float v = mode == MODE_GAME ? t.gr_db.load() : t.duck_db.load();
        ladder_->set_lit(v < -0.2f ? std::min(ladder_->segments(), static_cast<int>(-v / 0.8f) + 1) : 0);
    }
    auto lamp = [this](const char* label, bool on) {
        if (auto* l = lamps_.value(QString::fromLatin1(label))) l->set_lit(on);
    };
    float voice_act = 0.0f;
    if (bus_) bus_->read_voice_center(&voice_act);
    lamp("RIDING", std::abs(t.ride_db.load()) > 0.5f);
    lamp("YOU TALK", voice_act > 0.5f);
    lamp("LIMIT", t.gr_db.load() < -0.5f);
    lamp("SCOOPING", t.duck_db.load() < -0.5f);
    const bool swelling = t.bgm_swelling.load() != 0;
    lamp("VOICE DUCK", t.duck_db.load() < -0.5f && !swelling);
    lamp("AFK SWELL", swelling);
    lamp("COUGH LIFT", t.bgm_cough_lift.load() != 0);
    if (auto* b = buttons_.value(QStringLiteral("cal"))) b->set_lit(engine_->calibrator().running());
    if (auto* b = buttons_.value(QStringLiteral("ab"))) b->set_lit(engine_->audition().playing() || engine_->audition().capturing());
    if (auto* b = buttons_.value(QStringLiteral("bleep"))) b->set_lit(engine_->bleep().active());
    if (auto* b = buttons_.value(QStringLiteral("cough"))) b->set_lit(engine_->cough().active());
}

#if defined(MODEL600_PRO_EDITION)
void StudioConsole::refresh_director(qint64 now, float dt) {
    const auto& t = engine_->telemetry();
    hw::PeakHold& master = peaks_[4];
    master.feed(t.sample_peak_db.load(), now, dt);
    if (auto* s = segs_.value(QStringLiteral("p")))
        s->set_text(seg_db(master.hold), master.hold > -1.0f ? hw::Disp::red : QColor());
    if (auto* s = segs_.value(QStringLiteral("s"))) s->set_text(seg_db(t.lufs_s.load()));
    if (auto* s = segs_.value(QStringLiteral("i"))) s->set_text(seg_db(t.lufs_i.load()));

    // Channel meters 01-04 from the shared bus.
    const std::int64_t now_bus = now_us();
    for (int k = 0; k < 4 && k < meters_.size(); ++k) {
        float sp = -120.0f, lufs = -120.0f;
        bool live = false;
        if (bus_ && bus_->raw()) {
            for (std::size_t i = 0; i < kMaxSlots; ++i) {
                const auto& s = bus_->raw()->unit_slots[i];
                if (!s.in_use.load() || s.mode.load() != k) continue;
                if (now_bus - s.heartbeat_us.load() > SharedBus::kHeartbeatGcUs) continue;
                sp = std::max(sp, s.sample_peak_db.load());
                lufs = std::max(lufs, s.short_term_lufs.load());
                live = true;
            }
        }
        hw::PeakHold& pk = peaks_[static_cast<std::size_t>(k)];
        pk.feed(sp, now, dt);
        if (live)
            meters_[k]->set_values(pk.bar, pk.hold, seg_db(pk.hold), QStringLiteral("PEAK"), QStringLiteral("dBFS"),
                                   QStringLiteral("S %1 LUFS").arg(lufs_text(lufs)));
        else
            meters_[k]->set_values(-120.0, -120.0, QStringLiteral("----"), QStringLiteral("PEAK"), QStringLiteral("dBFS"),
                                   QStringLiteral("NO LINK"));
    }
    if (meters_.size() > 4) {
        const float pc = t.phase_corr.load();
        meters_[4]->set_values(pc, 0.0, QString::number(static_cast<double>(pc), 'f', 2), QStringLiteral("CORRELATION"),
                               QString(), QString());
    }
    const float i = t.lufs_i.load();
    const bool has = i > -119.0f;
    auto lamp = [this](const char* label, bool on) {
        if (auto* l = lamps_.value(QString::fromLatin1(label))) l->set_lit(on);
    };
    lamp("IN RANGE", has && in_youtube_golden_zone(i));
    lamp("HIGH", has && i > -10.5f);
    lamp("LOW", has && i < -13.5f);
    if (auto* b = buttons_.value(QStringLiteral("mono"))) b->set_lit(engine_->params().mono_check.load() != 0);
    if (auto* b = buttons_.value(QStringLiteral("chapter"))) b->set_lit(now < flash_chapter_until_);
    if (auto* b = buttons_.value(QStringLiteral("report"))) b->set_lit(now < flash_report_until_);
}
#endif

void StudioConsole::refresh() {
    const qint64 now = clock_.elapsed();
    const float dt = last_ms_ > 0 ? std::clamp(static_cast<float>(now - last_ms_) / 1000.0f, 0.0f, 0.25f) : 0.033f;
    last_ms_ = now;
    sync_from_params();
    refresh_links();
#if defined(MODEL600_PRO_EDITION)
    if (director_) refresh_director(now, dt);
    else refresh_unit(now, dt);
#else
    refresh_unit(now, dt);
#endif
    update_status();
    if (mini_) {
        const auto& t = engine_->telemetry();
        const hw::PeakHold& pk = peaks_[director_ ? 4 : 0];
        if (director_)
            mini_bar_->set(pk.bar, pk.hold, {{QStringLiteral("PEAK"), seg_db(pk.hold)}, {QStringLiteral("S LUFS"), seg_db(t.lufs_s.load())},
                                             {QStringLiteral("I LUFS"), seg_db(t.lufs_i.load())}});
        else
            mini_bar_->set(pk.bar, pk.hold, {{QStringLiteral("PEAK"), seg_db(pk.hold)}, {QStringLiteral("S LUFS"), seg_db(t.lufs_s.load())},
                                             {QStringLiteral("GR dB"), seg_db(t.gr_db.load())}});
    }
}

void StudioConsole::show_menu(const QPoint& global) {
    QMenu menu(this);
    menu.setPalette(studio_palette());
    menu.setFont(font_body());
    QAction* ja = menu.addAction(QStringLiteral("日本語"));
    QAction* en = menu.addAction(QStringLiteral("English"));
    ja->setCheckable(true);
    en->setCheckable(true);
    ja->setChecked(lang_ == UiLang::JA);
    en->setChecked(lang_ == UiLang::EN);
    menu.addSeparator();
    QAction* pin = menu.addAction(tr2("Always on top", "常に手前に表示"));
    pin->setCheckable(true);
    pin->setChecked(pinned_);
    QAction* mini = menu.addAction(tr2("Compact view", "コンパクト表示"));
    mini->setCheckable(true);
    mini->setChecked(mini_);
    menu.addSeparator();
    QAction* cal = nullptr;
    if (!director_ && engine_->mode() == MODE_VOICE) cal = menu.addAction(tr2("10 s auto calibrate...", "10秒 自動測定…"));
    QAction* reset = menu.addAction(tr2("Reset to defaults", "初期設定に戻す"));
    QAction* upd = nullptr;
    if (!update_version_.isEmpty())
        upd = menu.addAction(tr2("Download update %1", "アップデート %1 を入手").arg(update_version_));
    QAction* deact = nullptr;
    if (director_) {
        menu.addSeparator();
        deact = menu.addAction(tr2("Deactivate this PC...", "このPCの認証を解除…"));
    }
    QAction* chosen = menu.exec(global);
    if (!chosen) return;
    if (chosen == ja) set_language(UiLang::JA);
    else if (chosen == en) set_language(UiLang::EN);
    else if (chosen == pin) toggle_pin(!pinned_);
    else if (chosen == mini) toggle_mini(!mini_);
    else if (cal && chosen == cal) run_calibration();
    else if (chosen == reset) {
        const auto answer = QMessageBox::question(this, QStringLiteral("MODEL 600"),
            tr2("Reset every control to its default?\nThe calibrated input gain is kept.",
                "すべての操作を初期設定に戻しますか？\n測定済みの入力ゲインはそのまま残ります。"));
        if (answer == QMessageBox::Yes) panic_reset();
    } else if (upd && chosen == upd) {
        QDesktopServices::openUrl(QUrl(QString::fromLatin1(kDownloadUrl)));
    } else if (deact && chosen == deact) {
        const auto answer = QMessageBox::question(this, QStringLiteral("MODEL 600"),
            tr2("Deactivate the licence on this PC?\nDo this before moving to another PC.",
                "このPCのライセンス認証を解除しますか？\n別のPCで使うときに行います。"));
        if (answer == QMessageBox::Yes && on_deactivate_pc) on_deactivate_pc();
    }
}

void StudioConsole::contextMenuEvent(QContextMenuEvent* e) {
    show_menu(e->globalPos());   // Reached in compact view; the panel sends its own requests.
    e->accept();
}

void StudioConsole::mouseDoubleClickEvent(QMouseEvent* e) {
    if (mini_) toggle_mini(false);   // Double-click the compact strip to open the full panel.
    QDialog::mouseDoubleClickEvent(e);
}

void StudioConsole::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Tok::chassis);
}

void StudioConsole::showEvent(QShowEvent* e) {
    QDialog::showEvent(e);
    clamp_to_screen();
    refresh();
    timer_->start();
}

void StudioConsole::hideEvent(QHideEvent* e) {
    timer_->stop();   // 0.00 % CPU while hidden.
    QDialog::hideEvent(e);
}

void StudioConsole::clamp_to_screen() {
    QScreen* scr = screen();
    if (!scr && windowHandle()) scr = windowHandle()->screen();
    if (!scr) scr = QGuiApplication::primaryScreen();
    if (!scr) return;
    const QRect avail = scr->availableGeometry();
    QRect g = frameGeometry();
    if (g.right() > avail.right()) g.moveRight(avail.right() - 4);
    if (g.bottom() > avail.bottom()) g.moveBottom(avail.bottom() - 4);
    if (g.left() < avail.left()) g.moveLeft(avail.left() + 4);
    if (g.top() < avail.top()) g.moveTop(avail.top() + 4);
    move(g.topLeft());
}

void StudioConsole::toggle_pin(bool on) {
    pinned_ = on;
    Qt::WindowFlags f = windowFlags();
    f.setFlag(Qt::WindowStaysOnTopHint, on);
    setWindowFlags(f);
    show();   // Re-applying flags hides the window.
}

void StudioConsole::toggle_mini(bool on) {
    mini_ = on;
    panel_->setVisible(!on);
    mini_bar_->setVisible(on);
    setFixedSize(on ? QSize(panel_->sizeHint().width(), mini_bar_->height()) : panel_->sizeHint());
    refresh();
}

void StudioConsole::panic_reset() {
    engine_->params().factory_reset_keep_calibration();
    engine_->telemetry().reset_holds();
    engine_->reset_meters();
    for (auto& pk : peaks_) pk.reset();
    sync_from_params();
    changed();
}

void StudioConsole::run_calibration() {
    if (engine_->calibrator().running()) return;
    if (std::abs(engine_->params().input_gain_db.load()) > 0.05f) {
        const auto answer = QMessageBox::question(this, QStringLiteral("MODEL 600"),
            lang_ == UiLang::JA ? QStringLiteral("測定済みの入力ゲイン（%1 dB）を上書きしますか？").arg(fmt_signed(engine_->params().input_gain_db.load()))
                                : QStringLiteral("Overwrite the calibrated input gain (%1 dB)?").arg(fmt_signed(engine_->params().input_gain_db.load())));
        if (answer != QMessageBox::Yes) return;
    }
    CalibrationDialog dlg(engine_, lang_, this);
    dlg.exec();
    changed();   // Persist a committed input gain.
}

void StudioConsole::toggle_audition() {
    auto& a = engine_->audition();
    if (a.playing() || a.capturing()) { a.stop(); return; }
    a.start_record(engine_->telemetry().sample_rate.load());
    // The recorder flips to IDLE after 3 s; then loop DRY / processed.
    QTimer::singleShot(dsp::AuditionRecorder::kSeconds * 1000 + 100, this, [this] { engine_->audition().start_loop(); });
}

} // namespace sw::model600::ui
