// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  custom-ui-dialog.cpp  [Layer 2]  Studio console, UNIT 05 mixer, calibration.
// =============================================================================
#include "custom-ui-dialog.hpp"
#include "ui-theme.hpp"
#include "ui-widgets.hpp"

#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
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

constexpr int kEar = 18;   // Rack ear width (px).

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

// Mini strip readouts: LABEL over value, fixed 66 px columns.
class MiniValues : public QWidget {
public:
    explicit MiniValues(QWidget* parent = nullptr) : QWidget(parent) { setFixedHeight(44); }
    void set(const QVector<std::pair<QString, QString>>& v) {
        values_ = v;
        setFixedWidth(66 * static_cast<int>(v.size()));
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        for (int i = 0; i < values_.size(); ++i) {
            const qreal x = i * 66.0;
            p.setFont(font_label());
            p.setPen(Tok::t2);
            p.drawText(QRectF(x, 0, 66, 16), Qt::AlignLeft | Qt::AlignVCenter, values_[i].first);
            p.setFont(make_font(15, QFont::Medium));
            p.setPen(Tok::t1);
            draw_tabular(p, QRectF(x, 18, 66, 24), values_[i].second, Qt::AlignLeft);
        }
    }
private:
    QVector<std::pair<QString, QString>> values_;
};

QString fmt_unit(float v, const QString& unit) {
    return fmt_signed(v) + QStringLiteral(" ") + unit;
}

} // namespace

// =============================================================================
//  ChannelStrip
// =============================================================================
ChannelStrip::ChannelStrip(Mode mode, const QString& number, const QString& name, QWidget* parent)
    : QWidget(parent), mode_(mode), number_(number), name_(name) {
    setFixedWidth(168);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(12, 40, 12, 44);
    v->setSpacing(6);

    auto* labels = new QHBoxLayout();
    labels->setSpacing(8);
    auto* pk = new SectionLabel(QStringLiteral("PK"), this);
    pk->setFixedWidth(44);
    labels->addWidget(pk);
    labels->addWidget(new SectionLabel(QStringLiteral("DUCK"), this));
    labels->addStretch(1);
    v->addLayout(labels);

    auto* boxes = new QHBoxLayout();
    boxes->setSpacing(8);
    pk_box_ = new ValueBox(this);
    pk_box_->setFixedWidth(44);
    duck_box_ = new ValueBox(this);
    duck_box_->setFixedWidth(44);
    boxes->addWidget(pk_box_);
    boxes->addWidget(duck_box_);
    boxes->addStretch(1);
    v->addLayout(boxes);
    v->addSpacing(4);

    auto* meters = new QHBoxLayout();
    meters->setSpacing(3);
    level_ = new LedMeter(LedMeter::Kind::Level, Qt::Vertical, this);
    level_->setFixedWidth(16);
    duck_ = new LedMeter(LedMeter::Kind::GainReduction, Qt::Vertical, this);
    duck_->setFixedWidth(8);
    meters->addWidget(level_);
    meters->addWidget(duck_);
    meters->addWidget(new MeterScale(MeterScale::Kind::Level, this));
    meters->addStretch(1);
    fader_ = new RackFader(Qt::Vertical, -48.0f, 12.0f, RackFader::Law::Console, this);
    fader_->setFixedWidth(62);
    meters->addWidget(fader_);
    v->addLayout(meters, 1);

    auto* fv = new QHBoxLayout();
    fv->addStretch(1);
    fader_box_ = new ValueBox(this);
    fader_box_->setFixedWidth(52);
    fader_box_->set_text(QStringLiteral("0.0 dB"));
    fv->addWidget(fader_box_);
    v->addLayout(fv);

    connect(pk_box_, &ValueBox::clicked, this, [this] { hold_ = -120.0f; level_->reset_hold(); });
    connect(fader_, &RackFader::dbChanged, this, [this](float db) {
        fader_box_->set_text(fmt_signed(db) + QStringLiteral(" dB"));
        emit trimMoved(mode_, db);
    });
}

void ChannelStrip::update_from(float true_peak, float rms, float st_lufs, float gr, float duck, bool live) {
    live_ = live;
    st_lufs_ = st_lufs;
    hold_ = std::max(hold_, true_peak);
    level_->set_level(true_peak, rms, hold_);
    duck_->set_gr(std::min(gr, duck));
    pk_box_->set_text(fmt_db(hold_), hold_ > -1.0f ? Tok::red : QColor());
    duck_box_->set_text(fmt_db(duck));
    update();
}

void ChannelStrip::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(Tok::line, 1));
    p.setBrush(Tok::panel);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    const QRectF badge(12.5, 12.5, 21, 17);
    p.setPen(QPen(live_ ? Tok::accent : Tok::line_strong, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(badge, 3, 3);
    p.setFont(make_font(10, QFont::DemiBold));
    p.drawText(badge, Qt::AlignCenter, number_);
    p.setFont(make_font(11, QFont::DemiBold, 1.2));
    p.setPen(live_ ? Tok::t1 : Tok::t3);
    p.drawText(QPointF(42, 25.5), name_);
    // Bottom: short-term loudness.
    p.setPen(QPen(Tok::line, 1));
    p.drawLine(QPointF(12, height() - 42.5), QPointF(width() - 12, height() - 42.5));
    p.setFont(font_label());
    p.setPen(Tok::t2);
    p.drawText(QRectF(12, height() - 34, 80, 20), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("S-LUFS"));
    p.setFont(make_font(14, QFont::Medium));
    p.setPen(live_ ? Tok::t1 : Tok::t3);
    draw_tabular(p, QRectF(12, height() - 36, width() - 24, 24), fmt_db(st_lufs_), Qt::AlignRight);
}

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
//  StudioConsole
// =============================================================================
StudioConsole::StudioConsole(dsp::Engine* engine, SharedBus* bus, bool director, bool pro, QWidget* parent)
    : QDialog(parent), engine_(engine), bus_(bus), director_(director && pro), pro_(pro) {
    install_fonts();
    setPalette(studio_palette());
    setFont(font_body());
    setWindowTitle(QStringLiteral("MODEL 600  -  %1 %2").arg(unit_number(), unit_name()));
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    build_ui();
    timer_ = new QTimer(this);
    timer_->setInterval(33);   // ~30 fps, only while visible.
    connect(timer_, &QTimer::timeout, this, &StudioConsole::refresh);
    if (director_) { setMinimumSize(1080, 120); resize(1120, 560); }
    else           { setMinimumSize(540, 120); resize(560, 560); }
    setMaximumHeight(560);
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

void StudioConsole::changed() { if (on_params_changed) on_params_changed(); }

void StudioConsole::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(kEar, 0, kEar, 0);
    root->setSpacing(0);

    header_ = new HeaderBar(this);
    header_->set_unit(unit_number(), unit_name());
    root->addWidget(header_);
    connect(header_->pin_key(), &RackKey::toggled, this, &StudioConsole::toggle_pin);
    connect(header_->fold_key(), &RackKey::toggled, this, &StudioConsole::toggle_mini);
    connect(header_->reset_key(), &RackKey::clicked, this, &StudioConsole::panic_reset);
    connect(header_->lang(), &SegmentedControl::currentChanged, this,
            [this](int i) { set_language(i == 0 ? UiLang::JA : UiLang::EN); });
    connect(header_, &HeaderBar::updateClicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.seventhwell.com/model600/download")));
    });

    body_ = director_ ? build_director_body() : build_unit_body();
    root->addWidget(body_, 1);
    mini_bar_ = build_mini_bar();
    mini_bar_->setVisible(false);
    root->addWidget(mini_bar_);

    status_ = new StatusLine(this);
    if (director_) {
        connect(status_, &StatusLine::linkClicked, this, [this] {
            QMenu menu(this);
            menu.setPalette(studio_palette());
            menu.setFont(font_body());
            QAction* deact = menu.addAction(lang_ == UiLang::JA ? QStringLiteral("このPCの認証を解除…")
                                                                : QStringLiteral("Deactivate this PC..."));
            if (menu.exec(QCursor::pos()) != deact) return;
            const auto answer = QMessageBox::question(this, QStringLiteral("MODEL 600"),
                lang_ == UiLang::JA ? QStringLiteral("このPCのライセンス認証を解除しますか？\n別のPCで使うときに行います。")
                                    : QStringLiteral("Deactivate the licence on this PC?\nDo this before moving to another PC."));
            if (answer == QMessageBox::Yes && on_deactivate_pc) on_deactivate_pc();
        });
    }
    root->addWidget(status_);
    relabel();
}

QWidget* StudioConsole::build_param_row(QWidget* parent, RackFader*& fader, ValueBox*& box, float lo, float hi,
                                        const QVector<float>& marks, float value, const QString& unit) {
    auto* row = new QWidget(parent);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(12);
    fader = new RackFader(Qt::Horizontal, lo, hi, RackFader::Law::Linear, row);
    fader->set_marks(marks);
    fader->set_default_db(value);
    fader->set_db(value);
    box = new ValueBox(row);
    box->setFixedWidth(84);
    box->set_text(fmt_unit(value, unit));
    h->addWidget(fader, 1);
    h->addWidget(box, 0, Qt::AlignTop);
    return row;
}

QWidget* StudioConsole::build_trim_row(QWidget* parent) {
    QWidget* row = build_param_row(parent, trim_, trim_box_, -24.0f, 24.0f, {-24, -12, 0, 12, 24},
                                   engine_->params().output_trim_db.load(), QStringLiteral("dB"));
    trim_->set_default_db(0.0f);
    connect(trim_, &RackFader::dbChanged, this, [this](float db) {
        engine_->params().output_trim_db.store(db);
        if (bus_) bus_->push_trim(engine_->mode(), db);
        trim_box_->set_text(fmt_unit(db, QStringLiteral("dB")));
        changed();
    });
    return row;
}

QWidget* StudioConsole::build_unit_body() {
    auto& prm = engine_->params();
    const Mode mode = engine_->mode();
    auto* body = new QWidget(this);
    auto* h = new QHBoxLayout(body);
    h->setContentsMargins(16, 16, 16, 10);
    h->setSpacing(18);

    // ---- Left: output + GR meters with scales -------------------------------
    auto* left = new QVBoxLayout();
    left->setSpacing(6);
    left->addWidget(new SectionLabel(QStringLiteral("OUTPUT"), body));
    auto* boxes = new QHBoxLayout();
    pk_box_ = new ValueBox(body);
    pk_box_->setFixedWidth(42);
    gr_box_ = new ValueBox(body);
    gr_box_->setFixedWidth(38);
    boxes->addWidget(pk_box_);
    boxes->addStretch(1);
    boxes->addWidget(gr_box_);
    left->addLayout(boxes);
    left->addSpacing(2);
    auto* meters = new QHBoxLayout();
    meters->setSpacing(4);
    auto col = [&](QWidget* w, int stretch_top, const QString& caption, int stretch_bottom) {
        auto* v = new QVBoxLayout();
        v->setSpacing(4);
        v->addWidget(w, stretch_top);
        auto* cap = new SectionLabel(caption, body);
        cap->set_centered(true);
        cap->setMinimumWidth(20);
        v->addWidget(cap);
        if (stretch_bottom > 0) v->addStretch(stretch_bottom);
        return v;
    };
    out_meter_ = new LedMeter(LedMeter::Kind::Level, Qt::Vertical, body);
    out_meter_->setFixedWidth(22);
    gr_meter_ = new LedMeter(LedMeter::Kind::GainReduction, Qt::Vertical, body);
    gr_meter_->setFixedWidth(14);
    meters->addLayout(col(out_meter_, 1, QStringLiteral("PK"), 0));
    meters->addLayout(col(new MeterScale(MeterScale::Kind::Level, body), 1, QString(), 0));
    meters->addLayout(col(gr_meter_, 42, QStringLiteral("GR"), 58));
    meters->addLayout(col(new MeterScale(MeterScale::Kind::GainReduction, body), 42, QString(), 58));
    left->addLayout(meters, 1);
    connect(pk_box_, &ValueBox::clicked, this, [this] { engine_->telemetry().reset_holds(); out_meter_->reset_hold(); });
    connect(out_meter_, &LedMeter::clicked, this, [this] { engine_->telemetry().reset_holds(); });
    auto* left_w = new QWidget(body);
    left_w->setLayout(left);
    left_w->setFixedWidth(116);
    h->addWidget(left_w);

    // ---- Right: readouts + unit-specific controls + trim ------------------------
    auto* right = new QVBoxLayout();
    right->setSpacing(6);
    auto* ro = new QHBoxLayout();
    ro->setSpacing(8);
    const QString fourth = mode == MODE_VOICE ? QStringLiteral("INPUT GAIN") : QStringLiteral("DUCK");
    const QString labels[4] = {QStringLiteral("TRUE PEAK"), QStringLiteral("SHORT-TERM"), QStringLiteral("INTEGRATED"), fourth};
    const QString units[4] = {QStringLiteral("dBTP"), QStringLiteral("LUFS"), QStringLiteral("LUFS"), QStringLiteral("dB")};
    for (int i = 0; i < 4; ++i) {
        readouts_[static_cast<std::size_t>(i)] = new Readout(labels[i], units[i], false, body);
        ro->addWidget(readouts_[static_cast<std::size_t>(i)], 1);
    }
    right->addLayout(ro);
    right->addSpacing(14);

    auto section = [&](int idx, const QString& text) {
        section_[static_cast<std::size_t>(idx)] = new SectionLabel(text, body);
        right->addWidget(section_[static_cast<std::size_t>(idx)]);
    };
    auto key_row = [&](RackKey* a, RackKey* b, int height) {
        auto* row = new QHBoxLayout();
        row->setSpacing(8);
        a->setFixedHeight(height);
        b->setFixedHeight(height);
        row->addWidget(a, 1);
        row->addWidget(b, 1);
        right->addLayout(row);
    };

    switch (mode) {
        case MODE_VOICE: {
            section(0, QStringLiteral("CHARACTER"));
            character_ = new SegmentedControl({QStringLiteral("SOFT / ASMR"), QStringLiteral("BROADCAST"), QStringLiteral("FPS / LOUD")}, body);
            character_->setFixedHeight(30);
            character_->set_current(prm.character.load());
            connect(character_, &SegmentedControl::currentChanged, this, [this](int i) { engine_->params().character.store(i); changed(); });
            right->addWidget(character_);
            right->addSpacing(12);
            section(1, QStringLiteral("SPACE FX"));
            space_fx_ = new SegmentedControl({QStringLiteral("DRY"), QStringLiteral("KARAOKE"), QStringLiteral("BATH"),
                                              QStringLiteral("HALL"), QStringLiteral("CAVE"), QStringLiteral("RADIO")}, body);
            space_fx_->set_text_px(10);
            space_fx_->setFixedHeight(30);
            space_fx_->set_current(prm.space_fx.load());
            connect(space_fx_, &SegmentedControl::currentChanged, this, [this](int i) { engine_->params().space_fx.store(i); changed(); });
            right->addWidget(space_fx_);
            right->addSpacing(12);
            section(2, QStringLiteral("CALIBRATION / MONITOR"));
            calib_key_ = new RackKey(QString(), body);
            audition_key_ = new RackKey(QString(), body);
            audition_key_->set_led(QStringLiteral("led_amber"));
            audition_key_->setCheckable(true);
            connect(calib_key_, &RackKey::clicked, this, &StudioConsole::run_calibration);
            connect(audition_key_, &RackKey::clicked, this, &StudioConsole::toggle_audition);
            key_row(calib_key_, audition_key_, 34);
            right->addSpacing(12);
            section(3, QStringLiteral("EMERGENCY"));
            bleep_key_ = new RackKey(QString(), body);
            bleep_key_->set_style(RackKey::Style::Danger);
            bleep_key_->set_led(QStringLiteral("led_red"));
            cough_key_ = new RackKey(QString(), body);
            cough_key_->set_led(QStringLiteral("led_amber"));
            connect(bleep_key_, &RackKey::pressed, this, [this] { engine_->bleep().trigger(3.0f); });
            connect(bleep_key_, &RackKey::released, this, [this] { engine_->bleep().stop(); });
            connect(cough_key_, &RackKey::pressed, this, [this] { engine_->cough().press(true); });
            connect(cough_key_, &RackKey::released, this, [this] { engine_->cough().release(); });
            key_row(bleep_key_, cough_key_, 40);
            break;
        }
        case MODE_COLLAB:
            section(0, QStringLiteral("TARGET LOUDNESS"));
            right->addWidget(build_param_row(body, param_a_, param_a_box_, -24.0f, -8.0f, {-24, -20, -16, -12, -8},
                                             prm.collab_target_lufs.load(), QStringLiteral("LUFS")));
            param_a_unit_ = QStringLiteral("LUFS");
            connect(param_a_, &RackFader::dbChanged, this, [this](float v) {
                engine_->params().collab_target_lufs.store(v); param_a_box_->set_text(fmt_unit(v, param_a_unit_)); changed(); });
            right->addSpacing(14);
            section(1, QStringLiteral("DUCK WHEN I TALK"));
            right->addWidget(build_param_row(body, param_b_, param_b_box_, -12.0f, 0.0f, {-12, -9, -6, -3, 0},
                                             prm.collab_duck_db.load(), QStringLiteral("dB")));
            connect(param_b_, &RackFader::dbChanged, this, [this](float v) {
                engine_->params().collab_duck_db.store(v); param_b_box_->set_text(fmt_unit(v, QStringLiteral("dB"))); changed(); });
            break;
        case MODE_GAME:
            section(0, QStringLiteral("IMPACT CEILING"));
            right->addWidget(build_param_row(body, param_a_, param_a_box_, -12.0f, 0.0f, {-12, -9, -6, -3, 0},
                                             prm.game_ceiling_db.load(), QStringLiteral("dB")));
            param_a_unit_ = QStringLiteral("dB");
            connect(param_a_, &RackFader::dbChanged, this, [this](float v) {
                engine_->params().game_ceiling_db.store(v); param_a_box_->set_text(fmt_unit(v, param_a_unit_)); changed(); });
            right->addSpacing(14);
            section(1, QStringLiteral("VOICE SCOOP"));
            right->addWidget(build_param_row(body, param_b_, param_b_box_, -12.0f, 0.0f, {-12, -9, -6, -3, 0},
                                             prm.game_scoop_db.load(), QStringLiteral("dB")));
            connect(param_b_, &RackFader::dbChanged, this, [this](float v) {
                engine_->params().game_scoop_db.store(v); param_b_box_->set_text(fmt_unit(v, QStringLiteral("dB"))); changed(); });
            break;
        case MODE_BGM:
            section(0, QStringLiteral("POCKET DEPTH"));
            right->addWidget(build_param_row(body, param_a_, param_a_box_, -12.0f, 0.0f, {-12, -9, -6, -3, 0},
                                             prm.bgm_pocket_db.load(), QStringLiteral("dB")));
            param_a_unit_ = QStringLiteral("dB");
            connect(param_a_, &RackFader::dbChanged, this, [this](float v) {
                engine_->params().bgm_pocket_db.store(v); param_a_box_->set_text(fmt_unit(v, param_a_unit_)); changed(); });
            right->addSpacing(14);
            section(1, QStringLiteral("BGM MODE"));
            safe_key_ = new RackKey(QString(), body);
            safe_key_->set_led(QStringLiteral("led_amber"));
            safe_key_->setCheckable(true);
            safe_key_->setChecked(prm.bgm_safe_mode.load() != 0);
            swell_key_ = new RackKey(QString(), body);
            swell_key_->set_led(QStringLiteral("led_green"));
            swell_key_->setCheckable(true);
            swell_key_->setChecked(prm.bgm_auto_swell.load() != 0);
            connect(safe_key_, &RackKey::toggled, this, [this](bool on) { engine_->params().bgm_safe_mode.store(on ? 1 : 0); changed(); });
            connect(swell_key_, &RackKey::toggled, this, [this](bool on) { engine_->params().bgm_auto_swell.store(on ? 1 : 0); changed(); });
            key_row(safe_key_, swell_key_, 34);
            break;
        default:
            break;
    }
    right->addStretch(1);
    section(5, QStringLiteral("OUTPUT TRIM"));
    right->addWidget(build_trim_row(body));
    h->addLayout(right, 1);
    return body;
}

QWidget* StudioConsole::build_director_body() {
    auto& prm = engine_->params();
    auto* body = new QWidget(this);
    auto* h = new QHBoxLayout(body);
    h->setContentsMargins(16, 14, 16, 12);
    h->setSpacing(10);
    const Mode modes[4] = {MODE_VOICE, MODE_COLLAB, MODE_GAME, MODE_BGM};
    const char* names[4] = {"VOCAL", "COLLAB", "GAME", "BGM"};
    for (int i = 0; i < 4; ++i) {
        auto* s = new ChannelStrip(modes[i], QStringLiteral("0%1").arg(i + 1), QString::fromLatin1(names[i]), body);
        strips_[static_cast<std::size_t>(i)] = s;
        connect(s, &ChannelStrip::trimMoved, this, [this](Mode m, float db) { if (bus_) bus_->push_trim(m, db); });
        h->addWidget(s);
    }
    h->addSpacing(6);

    auto* master = new PanelBox(body);
    auto* v = new QVBoxLayout(master);
    v->setContentsMargins(16, 16, 16, 14);
    v->setSpacing(6);
    master_label_ = new SectionLabel(QStringLiteral("VIRTUAL MASTER SUM  ·  LUFS"), master);
    v->addWidget(master_label_);
    auto* ro = new QHBoxLayout();
    ro->setSpacing(8);
    const QString ml[3] = {QStringLiteral("MOMENTARY"), QStringLiteral("SHORT-TERM"), QStringLiteral("INTEGRATED")};
    for (int i = 0; i < 3; ++i) {
        master_[static_cast<std::size_t>(i)] = new Readout(ml[i], QString(), true, master);
        ro->addWidget(master_[static_cast<std::size_t>(i)], 1);
    }
    v->addLayout(ro);
    v->addSpacing(14);
    target_label_ = new SectionLabel(QStringLiteral("YOUTUBE TARGET  -12 \u00B11.5"), master);
    v->addWidget(target_label_);
    target_ = new TargetBar(master);
    v->addWidget(target_);
    v->addSpacing(6);
    phase_label_ = new SectionLabel(QStringLiteral("PHASE CORRELATION"), master);
    v->addWidget(phase_label_);
    corr_ = new CorrelationBar(master);
    v->addWidget(corr_);
    v->addSpacing(6);
    auto* keys = new QHBoxLayout();
    keys->setSpacing(8);
    mono_key_ = new RackKey(QString(), master);
    mono_key_->set_led(QStringLiteral("led_amber"));
    mono_key_->setCheckable(true);
    mono_key_->setChecked(prm.mono_check.load() != 0);
    mono_key_->setFixedHeight(32);
    meter_reset_key_ = new RackKey(QString(), master);
    meter_reset_key_->setFixedHeight(32);
    keys->addWidget(mono_key_, 1);
    keys->addWidget(meter_reset_key_, 1);
    v->addLayout(keys);
    connect(mono_key_, &RackKey::toggled, this, [this](bool on) { engine_->params().mono_check.store(on ? 1 : 0); });
    connect(meter_reset_key_, &RackKey::clicked, this, [this] { engine_->reset_meters(); });
    v->addSpacing(12);
    scene_label_ = new SectionLabel(QStringLiteral("SCENE"), master);
    v->addWidget(scene_label_);
    scene_ = new SegmentedControl({}, master);
    scene_->setFixedHeight(32);
    v->addWidget(scene_);
    v->addSpacing(8);
    auto* exports = new QHBoxLayout();
    exports->setSpacing(8);
    chapter_key_ = new RackKey(QString(), master);
    karte_key_ = new RackKey(QString(), master);
    chapter_key_->setFixedHeight(32);
    karte_key_->setFixedHeight(32);
    exports->addWidget(chapter_key_, 1);
    exports->addWidget(karte_key_, 1);
    v->addLayout(exports);
    v->addStretch(1);
    connect(scene_, &SegmentedControl::currentChanged, this, [this](int i) {
        engine_->params().apply_scene(i);
        if (on_scene_macro) on_scene_macro(i);
        changed();
    });
    connect(chapter_key_, &RackKey::clicked, this, [this] { emit chapterRequested(); if (on_request_chapter) on_request_chapter(); });
    connect(karte_key_, &RackKey::clicked, this, [this] { if (on_export_karte) on_export_karte(); });
    h->addWidget(master, 1);
    return body;
}

QWidget* StudioConsole::build_mini_bar() {
    auto* bar = new QWidget(this);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(16, 13, 16, 8);
    h->setSpacing(30);
    mini_meter_ = new LedMeter(LedMeter::Kind::Level, Qt::Horizontal, bar);
    mini_meter_->set_scale_visible(true);
    mini_meter_->setFixedHeight(32);
    h->addWidget(mini_meter_, 1, Qt::AlignTop);
    auto* values = new MiniValues(bar);
    mini_values_ = values;
    h->addWidget(values, 0, Qt::AlignTop);
    bar->setFixedHeight(75);
    return bar;
}

void StudioConsole::relabel() {
    const bool ja = lang_ == UiLang::JA;
    header_->lang()->blockSignals(true);
    header_->lang()->set_current(ja ? 0 : 1);
    header_->lang()->blockSignals(false);
    auto set = [](RackKey* k, const QString& en, const QString& j, bool jap) { if (k) k->setText(jap ? j : en); };
    set(calib_key_, QStringLiteral("10 s Auto Calibrate"), QStringLiteral("10秒 自動測定"), ja);
    set(audition_key_, QStringLiteral("A/B Audition"), QStringLiteral("A/B 試聴"), ja);
    set(bleep_key_, QStringLiteral("Bleep  1 kHz"), QStringLiteral("ピー音  1 kHz"), ja);
    set(cough_key_, QStringLiteral("Cough Mute"), QStringLiteral("咳ミュート"), ja);
    set(safe_key_, QStringLiteral("Singing / Rec Safe"), QStringLiteral("歌枠・収録セーフ"), ja);
    set(swell_key_, QStringLiteral("AFK Swell"), QStringLiteral("離席時スウェル"), ja);
    set(mono_key_, QStringLiteral("Mono Check"), QStringLiteral("モノラル確認"), ja);
    set(meter_reset_key_, QStringLiteral("Reset Meters"), QStringLiteral("メーターリセット"), ja);
    set(chapter_key_, QStringLiteral("Chapter Mark"), QStringLiteral("チャプター記録"), ja);
    set(karte_key_, QStringLiteral("Export Report"), QStringLiteral("配信カルテ出力"), ja);
    if (scene_) {
        const int cur = engine_->params().scene.load();
        scene_->blockSignals(true);
        scene_->set_options(ja ? QStringList{QStringLiteral("FPS・ゲーム"), QStringLiteral("雑談"), QStringLiteral("歌枠"), QStringLiteral("ASMR")}
                               : QStringList{QStringLiteral("FPS / GAME"), QStringLiteral("TALK"), QStringLiteral("SINGING"), QStringLiteral("ASMR")});
        scene_->set_current(cur);
        scene_->blockSignals(false);
    }
    if (status_ && director_) status_->set_link_text(ja ? QStringLiteral("ライセンス") : QStringLiteral("License"));
    update_status();
    update();
}

void StudioConsole::set_language(UiLang lang) {
    lang_ = lang;
    relabel();
}

void StudioConsole::set_update_available(const QString& version) {
    update_version_ = version;
    header_->set_update(version);
}

void StudioConsole::update_status() {
    if (!status_) return;
    const bool ja = lang_ == UiLang::JA;
    const auto& t = engine_->telemetry();
    const float fs = t.sample_rate.load();
    const double lat = fs > 0 ? engine_->latency_samples() * 1000.0 / fs : 0.0;
    const bool guard = bus_ && bus_->raw() && bus_->raw()->guard_active.load() != 0;
    status_->set_right(QStringLiteral("%1 kHz  ·  %2 ch  ·  v1.0.0")
                           .arg(QString::number(fs / 1000.0f, 'g', 3)).arg(t.channels.load()));
    if (t.hint_bgm_misplug.load()) {
        status_->set_message(StatusLine::Level::Warn, ja ? QStringLiteral("音楽を検出：BGM は 04 BGM POCKET へ")
                                                          : QStringLiteral("Music detected: use 04 BGM POCKET"));
    } else if (t.bridge_mode.load()) {
        status_->set_message(StatusLine::Level::Info, ja ? QStringLiteral("VC Edition と連携中（二重処理を停止）")
                                                          : QStringLiteral("Linked to VC Edition (no double processing)"));
    } else if (t.acapella_pad.load()) {
        status_->set_message(StatusLine::Level::Ok, ja ? QStringLiteral("雑談ソロ：天井を -1.5 dB 下げています")
                                                        : QStringLiteral("Solo talk: ceiling lowered 1.5 dB"));
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
            status_->set_message(StatusLine::Level::Ok,
                ja ? QStringLiteral("%1 ユニット リンク中  ·  Summing Guard %2  ·  先読み %3 ms").arg(live).arg(g).arg(lat, 0, 'f', 1)
                   : QStringLiteral("%1 units linked  ·  Summing Guard %2  ·  Look-ahead %3 ms").arg(live).arg(g).arg(lat, 0, 'f', 1));
        } else {
            status_->set_message(StatusLine::Level::Ok,
                ja ? QStringLiteral("動作中  ·  先読み %1 ms  ·  Summing Guard %2").arg(lat, 0, 'f', 1).arg(g)
                   : QStringLiteral("Running  ·  Look-ahead %1 ms  ·  Summing Guard %2").arg(lat, 0, 'f', 1).arg(g));
        }
    }
}

void StudioConsole::refresh() {
    const auto& t = engine_->telemetry();
    // LINK lamps.
    if (bus_ && bus_->raw()) {
        bool live[5] = {false, false, false, false, false};
        const std::int64_t now = now_us();
        for (std::size_t i = 0; i < kMaxSlots; ++i) {
            const auto& s = bus_->raw()->unit_slots[i];
            if (!s.in_use.load() || now - s.heartbeat_us.load() > SharedBus::kHeartbeatGcUs) continue;
            const int m = s.mode.load();
            if (m >= 0 && m < 5) live[m] = true;
        }
        header_->set_links(live);
    }
    const float tp = t.true_peak_db.load(), hold = t.peak_hold_db.load();
    if (out_meter_) {
        out_meter_->set_level(tp, t.rms_db.load(), hold);
        gr_meter_->set_gr(t.gr_db.load());
        pk_box_->set_text(fmt_db(hold), hold > -1.0f ? Tok::red : QColor());
        gr_box_->set_text(fmt_db(t.gr_db.load()));
        readouts_[0]->set_value(fmt_db(tp), tp > -1.0f ? Tok::red : QColor());
        readouts_[1]->set_value(fmt_db(t.lufs_s.load()));
        readouts_[2]->set_value(fmt_db(t.lufs_i.load()));
        readouts_[3]->set_value(engine_->mode() == MODE_VOICE ? fmt_signed(engine_->params().input_gain_db.load())
                                                             : fmt_db(t.duck_db.load()));
        if (audition_key_) audition_key_->setChecked(engine_->audition().playing() || engine_->audition().capturing());
        if (cough_key_) cough_key_->set_lit(engine_->cough().active());
        if (bleep_key_) bleep_key_->set_lit(engine_->bleep().active());
    }
    if (director_ && bus_ && bus_->raw()) {
        const std::int64_t now = now_us();
        const Mode modes[4] = {MODE_VOICE, MODE_COLLAB, MODE_GAME, MODE_BGM};
        for (int k = 0; k < 4; ++k) {
            float stp = -120, rms = -120, lufs = -120, gr = 0, duck = 0;
            bool live = false;
            for (std::size_t i = 0; i < kMaxSlots; ++i) {
                const auto& s = bus_->raw()->unit_slots[i];
                if (!s.in_use.load() || s.mode.load() != modes[k]) continue;
                if (now - s.heartbeat_us.load() > SharedBus::kHeartbeatGcUs) continue;
                stp = s.true_peak_db.load(); rms = s.rms_db.load(); lufs = s.short_term_lufs.load();
                gr = s.gr_db.load(); duck = s.duck_db.load(); live = true;
                break;
            }
            strips_[static_cast<std::size_t>(k)]->update_from(stp, rms, lufs, gr, duck, live);
        }
        const float m = t.lufs_m.load(), s = t.lufs_s.load(), i = t.lufs_i.load();
        master_[0]->set_value(fmt_db(m));
        master_[1]->set_value(fmt_db(s));
        master_[2]->set_value(fmt_db(i));
        target_->set_value(i);
        if (i <= -119.0f) target_label_->set_right(QStringLiteral("--"), Tok::t3);
        else if (in_youtube_golden_zone(i)) target_label_->set_right(QStringLiteral("IN RANGE"), Tok::ok, true);
        else if (i > -10.5f) target_label_->set_right(QStringLiteral("TOO LOUD"), Tok::warn, true);
        else target_label_->set_right(QStringLiteral("TOO QUIET"), Tok::warn, true);
        const float pc = t.phase_corr.load();
        phase_label_->set_right(fmt_signed(pc, 2), pc < 0 ? Tok::red : Tok::t1);
        corr_->set_value(pc);
        mono_key_->setChecked(engine_->params().mono_check.load() != 0);
    }
    if (mini_) {
        mini_meter_->set_level(tp, t.rms_db.load(), hold);
        auto* mv = static_cast<MiniValues*>(mini_values_);
        if (director_) mv->set({{QStringLiteral("M"), fmt_db(t.lufs_m.load())}, {QStringLiteral("S"), fmt_db(t.lufs_s.load())},
                                {QStringLiteral("I"), fmt_db(t.lufs_i.load())}, {QStringLiteral("TP"), fmt_db(tp)}});
        else mv->set({{QStringLiteral("TP"), fmt_db(tp)}, {QStringLiteral("S"), fmt_db(t.lufs_s.load())},
                      {QStringLiteral("GR"), fmt_db(t.gr_db.load())}});
    }
    update_status();
}

void StudioConsole::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.fillRect(rect(), Tok::chassis);
    p.fillRect(QRect(kEar, 0, width() - 2 * kEar, height()), Tok::faceplate);
    p.setPen(QPen(QColor(0x24, 0x28, 0x2D), 1));
    p.drawLine(QPointF(kEar, 0.5), QPointF(width() - kEar, 0.5));
    const QPixmap& screw = part(QStringLiteral("screw"));
    auto draw_screw = [&](qreal cx, qreal cy) { p.drawPixmap(QRectF(cx - 6, cy - 6, 12, 12), screw, QRectF(screw.rect())); };
    for (qreal cx : {kEar / 2.0, width() - kEar / 2.0}) {
        if (height() > 200) { draw_screw(cx, 26); draw_screw(cx, height() - 26); }
        else draw_screw(cx, height() / 2.0);
    }
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
    Qt::WindowFlags f = windowFlags();
    f.setFlag(Qt::WindowStaysOnTopHint, on);
    setWindowFlags(f);
    show();   // Re-applying flags hides the window.
}

void StudioConsole::toggle_mini(bool on) {
    mini_ = on;
    header_->set_compact(on);
    body_->setVisible(!on);
    status_->setVisible(!on);
    mini_bar_->setVisible(on);
    if (on) {
        setMinimumHeight(120);
        setMaximumHeight(120);
        resize(width(), 120);
    } else {
        setMinimumHeight(120);
        setMaximumHeight(560);
        resize(width(), 560);
    }
    refresh();
}

void StudioConsole::panic_reset() {
    auto& prm = engine_->params();
    prm.factory_reset_keep_calibration();
    if (character_) character_->set_current(prm.character.load());
    if (space_fx_) space_fx_->set_current(prm.space_fx.load());
    if (trim_) trim_->set_db(prm.output_trim_db.load());
    if (param_a_) {
        switch (engine_->mode()) {
            case MODE_COLLAB: param_a_->set_db(prm.collab_target_lufs.load()); param_b_->set_db(prm.collab_duck_db.load()); break;
            case MODE_GAME: param_a_->set_db(prm.game_ceiling_db.load()); param_b_->set_db(prm.game_scoop_db.load()); break;
            case MODE_BGM: param_a_->set_db(prm.bgm_pocket_db.load()); break;
            default: break;
        }
    }
    if (safe_key_) safe_key_->setChecked(prm.bgm_safe_mode.load() != 0);
    if (swell_key_) swell_key_->setChecked(prm.bgm_auto_swell.load() != 0);
    if (mono_key_) mono_key_->setChecked(false);
    if (scene_) { scene_->blockSignals(true); scene_->set_current(prm.scene.load()); scene_->blockSignals(false); }
    engine_->telemetry().reset_holds();
    engine_->reset_meters();
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
