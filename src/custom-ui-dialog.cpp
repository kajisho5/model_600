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
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QScreen>
#include <QShowEvent>
#include <QStackedLayout>
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

QLabel* make_text(const QString& text, int px, int weight, const QColor& color, QWidget* parent,
                  bool wrap = false) {
    auto* l = new QLabel(text, parent);
    l->setFont(make_font(px, weight));
    QPalette pal = l->palette();
    pal.setColor(QPalette::WindowText, color);
    l->setPalette(pal);
    l->setWordWrap(wrap);
    return l;
}

// Rounded panel background (strips, master section, script card).
class PanelBox : public QWidget {
public:
    explicit PanelBox(QWidget* parent = nullptr, qreal radius = 6) : QWidget(parent), radius_(radius) {}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(Tok::line, 1));
        p.setBrush(Tok::panel);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius_, radius_);
    }
private:
    qreal radius_;
};

// 4 px status colour bar on the left of a calibration result.
class ColorBar : public QWidget {
public:
    explicit ColorBar(QWidget* parent = nullptr) : QWidget(parent) { setFixedWidth(4); }
    void set_color(const QColor& c) { color_ = c; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(color_);
        p.drawRoundedRect(QRectF(rect()), 2, 2);
    }
private:
    QColor color_ = Tok::ok;
};

// Amber progress track.
class Progress : public QWidget {
public:
    explicit Progress(QWidget* parent = nullptr) : QWidget(parent) { setFixedHeight(10); }
    void set_value(qreal v) { if (std::abs(v - value_) > 1e-3) { value_ = v; update(); } }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(Tok::line, 1));
        p.setBrush(Tok::off);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
        p.setPen(Qt::NoPen);
        p.setBrush(Tok::accent);
        p.drawRoundedRect(QRectF(0, 0, width() * std::clamp(value_, 0.0, 1.0), height()), 5, 5);
    }
private:
    qreal value_ = 0;
};

} // namespace

// =============================================================================
//  CalibrationDialog
// =============================================================================
CalibrationDialog::CalibrationDialog(dsp::Engine* engine, UiLang lang, QWidget* parent)
    : QDialog(parent), engine_(engine), lang_(lang) {
    install_fonts();
    setPalette(studio_palette());
    setAutoFillBackground(false);
    setModal(true);
    setFixedSize(560, 400);
    setWindowTitle(L{QStringLiteral("MODEL 600  -  Automatic Input Calibration"),
                     QStringLiteral("MODEL 600  -  入力レベル自動測定")}.pick(lang_));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 78, 24, 24);
    pages_ = new QStackedLayout();
    root->addLayout(pages_);
    build_measuring_page();
    build_result_page();
    timer_ = new QTimer(this);
    timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, &CalibrationDialog::tick);
}

void CalibrationDialog::build_measuring_page() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto* card = new PanelBox(page);
    card->setFixedHeight(110);
    auto* cv = new QVBoxLayout(card);
    cv->setContentsMargins(16, 12, 16, 12);
    cv->addWidget(new SectionLabel(L{QStringLiteral("Read aloud"), QStringLiteral("読み上げ文")}.pick(lang_), card));
    cv->addSpacing(6);
    const QString script = L{QStringLiteral("“Hello everyone, welcome back to the stream.\n Let's make today a great one.”"),
                             QStringLiteral("「みなさんこんばんは、配信へようこそ。\n　今日も楽しい時間にしていきましょう。」")}.pick(lang_);
    cv->addWidget(make_text(script, 16, QFont::Medium, Tok::t1, card));
    cv->addStretch(1);
    v->addWidget(card);
    v->addSpacing(20);

    auto* in_row = new QHBoxLayout();
    auto* in_label = new SectionLabel(QStringLiteral("INPUT"), page);
    in_label->setFixedWidth(72);
    in_row->addWidget(in_label);
    input_meter_ = new LedMeter(LedMeter::Kind::Level, Qt::Horizontal, page);
    input_meter_->setFixedHeight(12);
    in_row->addWidget(input_meter_, 1);
    v->addLayout(in_row);
    v->addSpacing(22);

    auto* pr_row = new QHBoxLayout();
    auto* pr_label = new SectionLabel(QStringLiteral("PROGRESS"), page);
    pr_label->setFixedWidth(72);
    pr_row->addWidget(pr_label);
    progress_ = new Progress(page);
    pr_row->addWidget(progress_, 1);
    pr_row->addSpacing(16);
    time_label_ = make_text(QString(), 11, QFont::Medium, Tok::t1, page);
    time_label_->setFixedWidth(96);
    time_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pr_row->addWidget(time_label_);
    v->addLayout(pr_row);
    v->addSpacing(28);

    v->addWidget(make_text(L{QStringLiteral("Keep reading at your normal streaming volume."),
                             QStringLiteral("普段の配信と同じ声の大きさで、そのまま読み続けてください。")}.pick(lang_),
                           12, QFont::Medium, Tok::t2, page, true));
    v->addStretch(1);
    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* cancel = new RackKey(L{QStringLiteral("Cancel"), QStringLiteral("中止")}.pick(lang_), page);
    cancel->setFixedSize(96, 34);
    connect(cancel, &RackKey::clicked, this, &CalibrationDialog::reject);
    buttons->addWidget(cancel);
    v->addLayout(buttons);
    pages_->addWidget(page);
}

void CalibrationDialog::build_result_page() {
    auto* page = new QWidget(this);
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* h = new QHBoxLayout();
    h->setSpacing(14);
    auto* bar = new ColorBar(page);
    result_bar_ = bar;
    h->addWidget(bar);
    auto* v = new QVBoxLayout();
    v->setSpacing(4);
    title_ = make_text(QString(), 15, QFont::DemiBold, Tok::t1, page);
    body_ = make_text(QString(), 12, QFont::Medium, Tok::t2, page, true);
    v->addWidget(title_);
    v->addWidget(body_);
    v->addSpacing(12);
    auto* readouts = new QHBoxLayout();
    readouts->setSpacing(8);
    r1_ = new Readout(QString(), QString(), false, page);
    r2_ = new Readout(QString(), QString(), false, page);
    r3_ = new Readout(QString(), QString(), false, page);
    for (Readout* r : {r1_, r2_, r3_}) { r->setFixedSize(148, 60); readouts->addWidget(r); }
    readouts->addStretch(1);
    v->addLayout(readouts);
    v->addSpacing(10);
    auto* knob_row = new QHBoxLayout();
    knob_ = new KnobView(page);
    knob_row->addWidget(knob_);
    knob_row->addSpacing(8);
    knob_text_ = make_text(QString(), 12, QFont::Medium, Tok::t1, page, true);
    knob_row->addWidget(knob_text_, 1);
    v->addLayout(knob_row);
    measured_meter_ = new LedMeter(LedMeter::Kind::Level, Qt::Horizontal, page);
    measured_meter_->set_scale_visible(true);
    measured_meter_->setFixedHeight(32);
    v->addWidget(measured_meter_);
    v->addStretch(1);
    h->addLayout(v, 1);
    outer->addLayout(h, 1);
    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    secondary_ = new RackKey(QString(), page);
    primary_ = new RackKey(QString(), page);
    primary_->set_style(RackKey::Style::Primary);
    secondary_->setFixedSize(96, 34);
    primary_->setFixedSize(96, 34);
    buttons->addWidget(secondary_);
    buttons->addSpacing(8);
    buttons->addWidget(primary_);
    outer->addLayout(buttons);
    pages_->addWidget(page);
}

void CalibrationDialog::showEvent(QShowEvent* e) {
    QDialog::showEvent(e);
    if (!started_) start();
}

void CalibrationDialog::start() {
    started_ = true;
    state_ = State::Measuring;
    pages_->setCurrentIndex(0);
    engine_->start_calibration(10.0f);
    engine_->telemetry().reset_holds();
    input_meter_->reset_hold();
    timer_->start();
    update();
}

void CalibrationDialog::reject() {
    if (state_ == State::Measuring && engine_->calibrator().running()) engine_->calibrator().reset();
    timer_->stop();
    QDialog::reject();
}

void CalibrationDialog::tick() {
    auto& c = engine_->calibrator();
    auto& t = engine_->telemetry();
    input_meter_->set_level(t.true_peak_db.load(), t.rms_db.load(), t.peak_hold_db.load());
    static_cast<Progress*>(progress_)->set_value(c.progress());
    time_label_->setText(QStringLiteral("%1 / 10.0 %2")
        .arg(QString::number(static_cast<double>(c.progress()) * 10.0, 'f', 1).rightJustified(4, QLatin1Char('0')),
             L{QStringLiteral("s"), QStringLiteral("秒")}.pick(lang_)));
    if (c.done()) { timer_->stop(); show_result(); }
}

void CalibrationDialog::show_result() {
    auto& c = engine_->calibrator();
    const bool ja = lang_ == UiLang::JA;
    secondary_->disconnect();
    primary_->disconnect();
    r3_->setVisible(true);
    knob_->setVisible(true);
    knob_text_->setVisible(true);
    measured_meter_->setVisible(false);
    if (c.result_clipped()) {
        state_ = State::Clipped;
        static_cast<ColorBar*>(result_bar_)->set_color(Tok::error);
        title_->setText(ja ? QStringLiteral("入力が歪んでいます") : QStringLiteral("The input is clipping"));
        body_->setText(ja ? QStringLiteral("オーディオインターフェース本体の GAIN つまみを下げてから、\nもう一度測定してください。クリップランプが点かない位置が目安です。")
                          : QStringLiteral("Turn down the GAIN knob on your audio interface, then measure again. Aim for a position where the clip light stays off."));
        r1_->set_label(QStringLiteral("CLIPPED"));
        r1_->set_unit(ja ? QStringLiteral("秒") : QStringLiteral("s"));
        r1_->set_value(QString::number(static_cast<double>(c.clipped_seconds()), 'f', 1), Tok::error);
        r2_->set_label(QStringLiteral("INPUT GAIN"));
        r2_->set_unit(ja ? QStringLiteral("dB（変更なし）") : QStringLiteral("dB unchanged"));
        r2_->set_value(fmt_signed(engine_->params().input_gain_db.load()));
        r3_->setVisible(false);
        knob_->set_pointer(120);
        knob_->set_turn_down_hint(true);
        knob_text_->setText(ja ? QStringLiteral("つまみを左へ回して\nクリップランプを消してください。")
                               : QStringLiteral("Turn the knob left until\nthe clip light stays off."));
        secondary_->setText(ja ? QStringLiteral("閉じる") : QStringLiteral("Close"));
        primary_->setText(ja ? QStringLiteral("再測定") : QStringLiteral("Measure"));
        connect(secondary_, &RackKey::clicked, this, &CalibrationDialog::reject);
        connect(primary_, &RackKey::clicked, this, &CalibrationDialog::start);
    } else if (c.result_silent() || !c.result_ok()) {
        state_ = State::NoVoice;
        static_cast<ColorBar*>(result_bar_)->set_color(Tok::warn);
        title_->setText(ja ? QStringLiteral("声を検出できませんでした") : QStringLiteral("No voice was detected"));
        body_->setText(ja ? QStringLiteral("マイクのミュートと、OBS の入力デバイスの選択を確認してください。\n入力ゲインは変更していません。")
                          : QStringLiteral("Check that your mic is not muted and that OBS uses the right input device. The input gain was not changed."));
        r1_->set_label(QStringLiteral("VOICE DETECTED"));
        r1_->set_unit(QStringLiteral("%"));
        r1_->set_value(QString::number(static_cast<int>(std::lround(c.voice_ratio() * 100.0f))), Tok::warn);
        r2_->set_label(QStringLiteral("REQUIRED"));
        r2_->set_unit(ja ? QStringLiteral("% 以上") : QStringLiteral("% min"));
        r2_->set_value(QStringLiteral("20"));
        r3_->setVisible(false);
        knob_->setVisible(false);
        knob_text_->setVisible(false);
        const auto& t = engine_->telemetry();
        measured_meter_->set_level(t.peak_hold_db.load(), t.rms_db.load(), t.peak_hold_db.load());
        measured_meter_->setVisible(true);
        secondary_->setText(ja ? QStringLiteral("閉じる") : QStringLiteral("Close"));
        primary_->setText(ja ? QStringLiteral("再測定") : QStringLiteral("Measure"));
        connect(secondary_, &RackKey::clicked, this, &CalibrationDialog::reject);
        connect(primary_, &RackKey::clicked, this, &CalibrationDialog::start);
    } else {
        state_ = State::Success;
        engine_->commit_calibration();
        static_cast<ColorBar*>(result_bar_)->set_color(Tok::ok);
        title_->setText(ja ? QStringLiteral("測定が完了しました") : QStringLiteral("Calibration complete"));
        body_->setText(ja ? QStringLiteral("入力ゲインを自動で設定しました。") : QStringLiteral("The input gain has been set automatically."));
        r1_->set_label(QStringLiteral("INPUT GAIN"));
        r1_->set_unit(QStringLiteral("dB"));
        r1_->set_value(fmt_signed(c.result_gain_db()));
        r2_->set_label(QStringLiteral("REF. PEAK"));
        r2_->set_unit(QStringLiteral("dBFS"));
        r2_->set_value(fmt_db(c.result_reference_db()));
        r3_->set_label(QStringLiteral("TARGET PEAK"));
        r3_->set_unit(QStringLiteral("dBFS"));
        r3_->set_value(fmt_db(c.target_peak_db()));
        knob_->set_turn_down_hint(false);
        knob_->set_ok_range(30, 60);
        knob_->set_pointer(45);
        const bool low = c.result_gain_db() > 12.0f;
        knob_text_->setText(low
            ? (ja ? QStringLiteral("入力が小さめです。GAIN つまみを少し右へ回すと\nさらに安定します（目安は1時〜2時）。")
                  : QStringLiteral("Your input is low. Turn the GAIN knob slightly right\nfor more headroom (aim for 1 to 2 o'clock)."))
            : (ja ? QStringLiteral("オーディオインターフェースの GAIN つまみは\n1時〜2時の位置が目安です。")
                  : QStringLiteral("Set the GAIN knob on your audio interface\naround the 1 to 2 o'clock position.")));
        secondary_->setText(ja ? QStringLiteral("再測定") : QStringLiteral("Measure"));
        primary_->setText(ja ? QStringLiteral("完了") : QStringLiteral("Done"));
        connect(secondary_, &RackKey::clicked, this, &CalibrationDialog::start);
        connect(primary_, &RackKey::clicked, this, &CalibrationDialog::accept);
    }
    pages_->setCurrentIndex(1);
    update();
}

void CalibrationDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), Tok::faceplate);
    p.setFont(make_font(14, QFont::DemiBold));
    p.setPen(Tok::t1);
    p.drawText(QPointF(24, 34), L{QStringLiteral("Automatic Input Calibration"), QStringLiteral("入力レベル自動測定")}.pick(lang_));
    const QStringList steps = lang_ == UiLang::JA
        ? QStringList{QStringLiteral("準備"), QStringLiteral("測定"), QStringLiteral("結果")}
        : QStringList{QStringLiteral("Prepare"), QStringLiteral("Measure"), QStringLiteral("Result")};
    const int cur = state_ == State::Measuring ? 1 : 2;
    for (int i = 0; i < 3; ++i) {
        const qreal x = width() - 24 - (3 - i) * 72;
        const QColor col = i == cur ? Tok::accent : (i < cur ? Tok::t2 : Tok::t3);
        p.setPen(Qt::NoPen);
        p.setBrush(i <= cur ? col : Tok::line);
        p.drawRoundedRect(QRectF(x, 24, 64, 3), 1.5, 1.5);
        p.setFont(make_font(10, i == cur ? QFont::DemiBold : QFont::Medium));
        p.setPen(col);
        p.drawText(QPointF(x, 42), QStringLiteral("%1  %2").arg(i + 1).arg(steps[i]));
    }
    p.setPen(QPen(Tok::line, 1));
    p.drawLine(QPointF(0, 58.5), QPointF(width(), 58.5));
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

void StudioConsole::Peak::feed(float db, qint64 now_ms, float dt_s) {
    if (!(db > -120.0f)) db = -120.0f;
    bar = std::max(db, bar - 20.0f * dt_s);
    if (db >= hold) { hold = db; hold_ms = now_ms; }
    else if (now_ms - hold_ms > 1500) hold = std::max(bar, hold - 20.0f * dt_s);
}

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
    Peak& pk = peaks_[0];
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
    Peak& master = peaks_[4];
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
        Peak& pk = peaks_[static_cast<std::size_t>(k)];
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
        const Peak& pk = peaks_[director_ ? 4 : 0];
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
