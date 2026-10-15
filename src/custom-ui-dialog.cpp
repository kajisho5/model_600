// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  custom-ui-dialog.cpp  [Layer 2]  Studio console + UNIT 05 mixer rendering.
// =============================================================================
#include "custom-ui-dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QHideEvent>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace sw::model600::ui {

// =============================================================================
//  LedMeter
// =============================================================================
LedMeter::LedMeter(Kind kind, const Skin* skin, QWidget* parent)
    : QWidget(parent), kind_(kind), skin_(skin) {
    setMinimumSize(18, 120);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setCursor(Qt::PointingHandCursor);
}

QSize LedMeter::sizeHint() const { return horizontal_ ? QSize(240, 14) : QSize(22, 160); }

void LedMeter::set_horizontal(bool horizontal) {
    horizontal_ = horizontal;
    if (horizontal_) { setMinimumSize(120, 12); setMaximumHeight(16); }
    else             { setMinimumSize(18, 120); setMaximumHeight(QWIDGETSIZE_MAX); }
    updateGeometry();
}

void LedMeter::set_values(float peak_db, float rms_db, float hold_db) noexcept {
    peak_db_ = peak_db; rms_db_ = rms_db; hold_db_ = hold_db;
}
void LedMeter::set_gr(float gr_db) noexcept { gr_db_ = gr_db; }
void LedMeter::reset_hold() noexcept { hold_db_ = kSilenceFloor; update(); }

void LedMeter::mousePressEvent(QMouseEvent*) { reset_hold(); }

void LedMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRect r = rect().adjusted(1, 1, -1, -1);
    p.fillRect(rect(), skin_->panel);

    const int segs = 24;
    const int gap = 1;
    const float seg_h = static_cast<float>(r.height()) / static_cast<float>(segs);

    auto db_to_frac = [](float db) {
        // -60..0 dBFS mapped to 0..1 with a broadcast-ish taper.
        const float f = (db + 60.0f) / 60.0f;
        return std::clamp(f, 0.0f, 1.0f);
    };

    if (kind_ == GainReduction) {
        // GR grows downward from the top; blue.
        const float gr = std::clamp(-gr_db_, 0.0f, 24.0f) / 24.0f;
        const int lit = static_cast<int>(gr * segs);
        for (int i = 0; i < segs; ++i) {
            const int y = r.top() + static_cast<int>(static_cast<float>(i) * seg_h);
            const QRect seg(r.left(), y, r.width(), static_cast<int>(seg_h) - gap);
            p.fillRect(seg, (i < lit) ? skin_->gr_blue : skin_->led_off);
        }
        return;
    }

    const int peak_lit = static_cast<int>(db_to_frac(peak_db_) * segs);
    const int rms_lit  = static_cast<int>(db_to_frac(rms_db_) * segs);
    const int hold_seg = static_cast<int>(db_to_frac(hold_db_) * segs);

    if (horizontal_) {
        // Left (quiet) to right (loud), same colour zones as the vertical bar.
        const int hsegs = 40;
        const float seg_w = static_cast<float>(r.width()) / static_cast<float>(hsegs);
        const int hp = static_cast<int>(db_to_frac(peak_db_) * hsegs);
        const int hr = static_cast<int>(db_to_frac(rms_db_) * hsegs);
        const int hh = static_cast<int>(db_to_frac(hold_db_) * hsegs);
        for (int i = 0; i < hsegs; ++i) {
            const int x = r.left() + static_cast<int>(static_cast<float>(i) * seg_w);
            const QRect seg(x, r.top(), static_cast<int>(seg_w) - gap, r.height());
            const float frac = static_cast<float>(i) / static_cast<float>(hsegs);
            const QColor on = frac > 0.9f ? skin_->led_red : (frac > 0.75f ? skin_->led_yellow : skin_->led_green);
            QColor c = skin_->led_off;
            if (i < hp) c = on;
            if (i < hr) c = on.lighter(130);
            if (i == hh && hold_db_ > kSilenceFloor) c = skin_->accent;
            p.fillRect(seg, c);
        }
        return;
    }

    for (int i = 0; i < segs; ++i) {
        const int from_bottom = segs - 1 - i;
        const int y = r.top() + static_cast<int>(static_cast<float>(i) * seg_h);
        const QRect seg(r.left(), y, r.width(), static_cast<int>(seg_h) - gap);
        QColor c = skin_->led_off;
        const float frac = static_cast<float>(from_bottom) / static_cast<float>(segs);
        QColor on = frac > 0.9f ? skin_->led_red : (frac > 0.75f ? skin_->led_yellow : skin_->led_green);
        if (from_bottom < peak_lit) c = on;
        if (from_bottom < rms_lit) c = on.lighter(130);
        if (from_bottom == hold_seg && hold_db_ > kSilenceFloor) c = skin_->accent; // Peak hold tick.
        p.fillRect(seg, c);
    }
}

// =============================================================================
//  TrimFader
// =============================================================================
TrimFader::TrimFader(QWidget* parent) : QSlider(Qt::Vertical, parent) {
    setRange(-240, 240);   // -24.0 .. +24.0 dB in 0.1 dB steps.
    setValue(0);
    setFocusPolicy(Qt::StrongFocus);
}
void TrimFader::set_db(float db) { setValue(static_cast<int>(std::lround(std::clamp(db, -24.0f, 24.0f) * 10.0f))); }
float TrimFader::db() const { return static_cast<float>(value()) / 10.0f; }

void TrimFader::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & Qt::ControlModifier) {
        const int dir = e->angleDelta().y() > 0 ? 1 : -1;
        setValue(value() + dir);          // 0.1 dB nudge.
        emit dbChanged(db());
        e->accept();
    } else {
        e->ignore();                      // Plain wheel disabled.
    }
}
void TrimFader::mouseDoubleClickEvent(QMouseEvent* e) {
    setValue(0);                          // Snap to 0.0 dB.
    emit dbChanged(0.0f);
    e->accept();
}

// =============================================================================
//  ChannelStrip
// =============================================================================
ChannelStrip::ChannelStrip(Mode mode, const L& title, const Skin* skin, QWidget* parent)
    : QWidget(parent), mode_(mode), title_(title), skin_(skin) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(3);

    name_ = new QLabel(title_.pick(lang_), this);
    QFont nf = name_->font(); nf.setBold(true); nf.setPointSizeF(nf.pointSizeF() * 0.9); name_->setFont(nf);
    name_->setAlignment(Qt::AlignHCenter);
    name_->setStyleSheet(QStringLiteral("color:#d6d8dc;"));

    auto* meters = new QHBoxLayout();
    meters->setSpacing(2);
    level_ = new LedMeter(LedMeter::Level, skin_, this);
    gr_ = new LedMeter(LedMeter::GainReduction, skin_, this);
    meters->addWidget(level_);
    meters->addWidget(gr_);

    readout_ = new QLabel(QStringLiteral("-∞"), this);
    readout_->setAlignment(Qt::AlignHCenter);
    readout_->setStyleSheet(QStringLiteral("color:#9a9da2;font-family:monospace;font-size:10px;"));

    fader_ = new TrimFader(this);
    fader_->setFixedHeight(90);
    connect(fader_, &TrimFader::dbChanged, this, [this](float db) { emit trimMoved(mode_, db); });
    connect(fader_, &QSlider::valueChanged, this, [this](int) { emit trimMoved(mode_, fader_->db()); });

    v->addWidget(name_);
    v->addLayout(meters, 1);
    v->addWidget(readout_);
    v->addWidget(fader_, 0, Qt::AlignHCenter);
}

void ChannelStrip::set_language(UiLang l) { lang_ = l; name_->setText(title_.pick(l)); }

void ChannelStrip::update_from(float true_peak, float peak_hold, float rms, float st_lufs,
                               float gr, float duck, bool live) {
    level_->set_values(true_peak, rms, peak_hold);
    gr_->set_gr(std::min(gr, duck)); // Show whichever reduction is deeper.
    level_->update();
    gr_->update();
    const QString tp = fmt_db(true_peak);
    const QString lf = fmt_db(st_lufs);
    readout_->setText(QStringLiteral("%1 dBTP\n%2 LUFS").arg(tp, lf));
    readout_->setStyleSheet(live ? QStringLiteral("color:#d6d8dc;font-family:monospace;font-size:10px;")
                                 : QStringLiteral("color:#5a5d62;font-family:monospace;font-size:10px;"));
}

// =============================================================================
//  CalibrationDialog
// =============================================================================
CalibrationDialog::CalibrationDialog(dsp::Engine* engine, const Skin* skin, UiLang lang, QWidget* parent)
    : QDialog(parent), engine_(engine), skin_(skin), lang_(lang) {
    setWindowTitle(lang_ == UiLang::JA ? QStringLiteral("10秒カンペ自動測定")
                                       : QStringLiteral("10-Second Cue-Card Calibration"));
    setModal(true);
    setMinimumSize(QSize(420, 260));
    // Same studio palette as the console (not the OS light theme).
    setStyleSheet(QStringLiteral(
        "QDialog{background:%1;}"
        "QLabel{color:%2;}"
        "QProgressBar{background:%3;border:1px solid #3a3c40;border-radius:4px;color:%2;text-align:center;height:18px;}"
        "QProgressBar::chunk{background:%4;border-radius:3px;}"
        "QPushButton{background:%5;color:%2;border:1px solid #3a3c40;border-radius:4px;padding:6px 18px;}"
        "QPushButton:disabled{color:#5a5d62;}")
        .arg(skin_->panel.name(), skin_->text.name(), skin_->led_off.name(),
             skin_->accent.name(), skin_->ear.name()));
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(22, 20, 22, 18);
    v->setSpacing(12);

    const L cue{
        QStringLiteral("Read this aloud at your normal streaming voice:\n\n"
                       "“Hello everyone, welcome back to the stream!\nToday we've got a great show lined up.”"),
        QStringLiteral("いつもの配信の声で読み上げてください：\n\n"
                       "「みなさんこんばんは、配信へようこそ！\n今日も楽しい時間にしていきましょう。」")};
    cue_ = new QLabel(cue.pick(lang_), this);
    cue_->setWordWrap(true);
    cue_->setAlignment(Qt::AlignCenter);
    QFont cf = cue_->font(); cf.setPointSizeF(cf.pointSizeF() * 1.1); cue_->setFont(cf);

    bar_ = new QProgressBar(this);
    bar_->setRange(0, 1000);
    bar_->setValue(0);
    bar_->setTextVisible(false);   // Percent is shown in the status line (legible on amber).

    status_ = new QLabel(QString(), this);
    status_->setWordWrap(true);
    status_->setAlignment(Qt::AlignCenter);

    close_btn_ = new QPushButton(lang_ == UiLang::JA ? QStringLiteral("閉じる") : QStringLiteral("Close"), this);
    close_btn_->setEnabled(false);
    connect(close_btn_, &QPushButton::clicked, this, &QDialog::accept);

    v->addWidget(cue_, 1);
    v->addWidget(bar_);
    v->addWidget(status_);
    v->addWidget(close_btn_, 0, Qt::AlignRight);

    timer_ = new QTimer(this);
    timer_->setInterval(50);
    connect(timer_, &QTimer::timeout, this, &CalibrationDialog::tick);
}

void CalibrationDialog::showEvent(QShowEvent* e) {
    QDialog::showEvent(e);
    if (running_) return;
    running_ = true;
    engine_->start_calibration(10.0f);
    status_->setText(lang_ == UiLang::JA ? QStringLiteral("測定中... 声を出し続けてください")
                                         : QStringLiteral("Measuring... keep talking"));
    timer_->start();
}

void CalibrationDialog::tick() {
    auto& c = engine_->calibrator();
    bar_->setValue(static_cast<int>(c.progress() * 1000.0f));
    if (c.done()) { timer_->stop(); finish(); return; }
    const int pct = static_cast<int>(c.progress() * 100.0f);
    status_->setText(lang_ == UiLang::JA
        ? QStringLiteral("測定中... 声を出し続けてください（%1%）").arg(pct)
        : QStringLiteral("Measuring... keep talking (%1%)").arg(pct));
}

void CalibrationDialog::finish() {
    auto& c = engine_->calibrator();
    QString msg;
    if (c.result_clipped()) {
        msg = lang_ == UiLang::JA
            ? QStringLiteral("\U0001F6A8 オーディオIF本体の物理GAINツマミが割れ(赤ランプ)ています！ツマミを左に回してください。")
            : QStringLiteral("\U0001F6A8 Your audio interface input is clipping (red light)! Turn the physical GAIN knob down (left).");
    } else if (c.result_silent()) {
        msg = lang_ == UiLang::JA
            ? QStringLiteral("声が検出されませんでした（ミュート等を確認してください）。測定値は適用しません。")
            : QStringLiteral("No voice detected (check mute/input). No change applied.");
    } else if (c.result_ok()) {
        engine_->commit_calibration();
        const float g = c.result_gain_db();
        const QString knob = (g > 6.0f)
            ? (lang_ == UiLang::JA ? QStringLiteral("ツマミは12時〜1時方向が最適です。") : QStringLiteral("Aim the knob at the 12-1 o'clock position."))
            : (lang_ == UiLang::JA ? QStringLiteral("ツマミは1時〜2時方向が最適です。") : QStringLiteral("Aim the knob at the 1-2 o'clock position."));
        msg = (lang_ == UiLang::JA
            ? QStringLiteral("✅ 測定完了。入力ゲインを %1 dB に設定しました（基準ピーク %2 dBFS）。%3")
            : QStringLiteral("✅ Done. Input gain set to %1 dB (reference peak %2 dBFS). %3"))
            .arg(QString::number(static_cast<double>(g), 'f', 1),
                 QString::number(static_cast<double>(c.result_reference_db()), 'f', 1), knob);
    } else {
        msg = lang_ == UiLang::JA ? QStringLiteral("測定に失敗しました。もう一度お試しください。")
                                  : QStringLiteral("Measurement failed. Please try again.");
    }
    status_->setText(msg);
    close_btn_->setEnabled(true);
}

// =============================================================================
//  StudioConsole
// =============================================================================
StudioConsole::StudioConsole(dsp::Engine* engine, SharedBus* bus, bool director, bool pro, QWidget* parent)
    : QDialog(parent), engine_(engine), bus_(bus), director_(director && pro), pro_(pro) {
    setWindowTitle(QStringLiteral("MODEL 600 // BROADCAST SUITE  —  STUDIO CONSOLE"));
    setMaximumHeight(560);
    setMinimumWidth(director_ ? 900 : 520);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    build_ui();
    timer_ = new QTimer(this);
    timer_->setInterval(33); // ~30 fps.
    connect(timer_, &QTimer::timeout, this, &StudioConsole::refresh);
}

StudioConsole::~StudioConsole() = default;

void StudioConsole::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(30, 8, 30, 10);   // Clears the rack ears.
    root->setSpacing(6);

    header_box_ = new QWidget(this);
    auto* header = new QVBoxLayout(header_box_);
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(4);
    build_header(header);
    root->addWidget(header_box_);

    // UNIT 01 consoles show the voice controls; the UNIT 05 console is the
    // integrated mixer only (its own engine is the metering master, not a mic).
    body_box_ = new QWidget(this);
    auto* body = new QVBoxLayout(body_box_);
    body->setContentsMargins(0, 0, 0, 0);
    if (director_) build_unit05(body);
    else           build_unit01(body);
    root->addWidget(body_box_, 1);

    build_mini_bar(root);
    relabel();
}

void StudioConsole::build_mini_bar(QVBoxLayout* into) {
    mini_bar_ = new QWidget(this);
    auto* v = new QVBoxLayout(mini_bar_);
    v->setContentsMargins(10, 6, 10, 6);
    v->setSpacing(4);
    mini_meter_ = new LedMeter(LedMeter::Level, &skin_, mini_bar_);
    mini_meter_->set_horizontal(true);
    mini_readout_ = new QLabel(QString(), mini_bar_);
    mini_readout_->setStyleSheet(QStringLiteral("color:#d6d8dc;font-family:monospace;"));
    v->addWidget(mini_meter_);
    v->addWidget(mini_readout_);
    mini_bar_->setVisible(false);
    into->addWidget(mini_bar_);
}

static QPushButton* make_header_btn(const QString& text, const Skin& s) {
    auto* b = new QPushButton(text);
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:%2;border:1px solid #3a3c40;border-radius:4px;padding:3px 8px;}"
        "QPushButton:checked{background:%3;color:#1a1a1a;font-weight:bold;}")
        .arg(s.ear.name(), s.text.name(), s.accent.name()));
    return b;
}

void StudioConsole::build_header(QVBoxLayout* into) {
    // Row 1: brand + (when available) the amber update lamp.
    auto* row1 = new QHBoxLayout();
    auto* brand = new QLabel(QStringLiteral("SEVENTHWELL  \u2502  MODEL 600"));
    QFont bf = brand->font(); bf.setBold(true); brand->setFont(bf);
    brand->setStyleSheet(QStringLiteral("color:%1;letter-spacing:1px;").arg(skin_.accent.name()));
    row1->addWidget(brand);
    row1->addStretch(1);

    // Row 2: LINK MATRIX lamps + window buttons.
    auto* row2 = new QHBoxLayout();
    link_matrix_ = new QLabel(QStringLiteral("LINK"));
    link_matrix_->setTextFormat(Qt::RichText);
    link_matrix_->setStyleSheet(QStringLiteral("color:#5a5d62;font-size:11px;"));
    row2->addWidget(link_matrix_);
    row2->addStretch(1);

    pin_btn_ = make_header_btn(QStringLiteral("\U0001F4CC PIN"), skin_);
    mini_btn_ = make_header_btn(QStringLiteral("\U0001F5D5 MINI"), skin_);
    lang_btn_ = make_header_btn(QStringLiteral("JP/EN"), skin_);
    reset_btn_ = make_header_btn(QStringLiteral("↺ RESET"), skin_);
    reset_btn_->setCheckable(false);
    update_btn_ = make_header_btn(QStringLiteral("⚡ UPDATE"), skin_);
    update_btn_->setCheckable(false);
    update_btn_->setVisible(false);

    connect(pin_btn_, &QPushButton::clicked, this, &StudioConsole::toggle_pin);
    connect(mini_btn_, &QPushButton::clicked, this, &StudioConsole::toggle_mini);
    connect(lang_btn_, &QPushButton::clicked, this, &StudioConsole::toggle_language);
    connect(reset_btn_, &QPushButton::clicked, this, &StudioConsole::panic_reset);

    row1->addWidget(update_btn_);
    row2->addWidget(pin_btn_);
    row2->addWidget(mini_btn_);
    row2->addWidget(lang_btn_);
    row2->addWidget(reset_btn_);
    into->addLayout(row1);
    into->addLayout(row2);
}

static QComboBox* make_combo(QWidget* p) {
    auto* c = new QComboBox(p);
    c->setStyleSheet(QStringLiteral("QComboBox{background:#2c2e32;color:#d6d8dc;border:1px solid #3a3c40;border-radius:3px;padding:2px 6px;}"));
    return c;
}

void StudioConsole::build_unit01(QVBoxLayout* into) {
    unit01_box_ = new QWidget(this);
    auto* box = new QVBoxLayout(unit01_box_);
    box->setContentsMargins(10, 8, 10, 10);
    box->setSpacing(6);

    auto* title = new QLabel(QStringLiteral("[01] VOCAL MASTER"));
    QFont tf = title->font(); tf.setBold(true); title->setFont(tf);
    title->setStyleSheet(QStringLiteral("color:%1;").arg(skin_.text.name()));
    box->addWidget(title);

    auto* meters = new QHBoxLayout();
    mic_level_ = new LedMeter(LedMeter::Level, &skin_, unit01_box_);
    mic_gr_ = new LedMeter(LedMeter::GainReduction, &skin_, unit01_box_);
    mic_level_->setFixedWidth(26);
    mic_gr_->setFixedWidth(26);
    auto* mcol = new QVBoxLayout();
    mic_readout_ = new QLabel(QStringLiteral("-∞ dBTP"), unit01_box_);
    mic_readout_->setStyleSheet(QStringLiteral("color:#d6d8dc;font-family:monospace;"));
    mic_hint_ = new QLabel(QString(), unit01_box_);
    mic_hint_->setWordWrap(true);
    mic_hint_->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(skin_.accent.name()));
    mcol->addWidget(mic_readout_);
    mcol->addWidget(mic_hint_, 1);
    meters->addWidget(mic_level_);
    meters->addWidget(mic_gr_);
    meters->addLayout(mcol, 1);
    box->addLayout(meters, 1);   // Meters absorb spare height (taller = finer reading).

    auto* grid = new QGridLayout();
    character_ = make_combo(unit01_box_);
    character_->addItems({QStringLiteral("SOFT / ASMR"), QStringLiteral("BROADCAST"), QStringLiteral("FPS / LOUD")});
    character_->setCurrentIndex(engine_->params().character.load());
    connect(character_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int i) { engine_->params().character.store(i); if (on_params_changed) on_params_changed(); });

    space_fx_ = make_combo(unit01_box_);
    space_fx_->addItems({QStringLiteral("DRY"), QStringLiteral("KARAOKE"), QStringLiteral("BATH"),
                         QStringLiteral("HALL"), QStringLiteral("CAVE"), QStringLiteral("RADIO")});
    space_fx_->setCurrentIndex(engine_->params().space_fx.load());
    connect(space_fx_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int i) { engine_->params().space_fx.store(i); if (on_params_changed) on_params_changed(); });

    char_lbl_ = new QLabel(unit01_box_);
    fx_lbl_ = new QLabel(unit01_box_);
    char_lbl_->setStyleSheet(QStringLiteral("color:#9a9da2;"));
    fx_lbl_->setStyleSheet(QStringLiteral("color:#9a9da2;"));
    grid->addWidget(char_lbl_, 0, 0); grid->addWidget(character_, 0, 1);
    grid->addWidget(fx_lbl_, 1, 0); grid->addWidget(space_fx_, 1, 1);
    grid->setColumnStretch(1, 1);
    box->addLayout(grid);

    auto* actions = new QHBoxLayout();
    calib_btn_ = new QPushButton(QStringLiteral("\U0001F3A4 10s CALIBRATE"), unit01_box_);
    audition_btn_ = new QPushButton(QStringLiteral("\U0001F501 A/B AUDITION"), unit01_box_);
    const QString action_style = QStringLiteral(
        "QPushButton{background:#2c2e32;color:#d6d8dc;border:1px solid #3a3c40;border-radius:4px;padding:6px;}"
        "QPushButton:checked{background:%1;color:#1a1a1a;}").arg(skin_.accent.name());
    calib_btn_->setStyleSheet(action_style);
    audition_btn_->setStyleSheet(action_style);
    audition_btn_->setCheckable(true);
    connect(calib_btn_, &QPushButton::clicked, this, &StudioConsole::run_calibration);
    connect(audition_btn_, &QPushButton::clicked, this, &StudioConsole::start_audition);
    actions->addWidget(calib_btn_);
    actions->addWidget(audition_btn_);
    box->addLayout(actions);

    auto* emergency = new QHBoxLayout();
    bleep_btn_ = new QPushButton(QStringLiteral("\U0001F92C 1kHz BLEEP"), unit01_box_);
    cough_btn_ = new QPushButton(QStringLiteral("\U0001F927 COUGH MUTE"), unit01_box_);
    bleep_btn_->setStyleSheet(QStringLiteral("QPushButton{background:#5a2c2c;color:#f0d0d0;border-radius:4px;padding:6px;}"));
    cough_btn_->setStyleSheet(QStringLiteral("QPushButton{background:#3a3c50;color:#d0d4f0;border-radius:4px;padding:6px;}"));
    connect(bleep_btn_, &QPushButton::pressed, this, &StudioConsole::fire_bleep);
    connect(bleep_btn_, &QPushButton::released, this, [this] { engine_->bleep().stop(); });
    connect(cough_btn_, &QPushButton::pressed, this, &StudioConsole::hold_cough);
    connect(cough_btn_, &QPushButton::released, this, [this] { engine_->cough().release(); });
    emergency->addWidget(bleep_btn_);
    emergency->addWidget(cough_btn_);
    box->addLayout(emergency);

    auto* trim_row = new QHBoxLayout();
    trim_lbl_ = new QLabel(unit01_box_);
    trim_lbl_->setStyleSheet(QStringLiteral("color:#9a9da2;"));
    mic_trim_ = new TrimFader(unit01_box_);
    mic_trim_->setOrientation(Qt::Horizontal);
    mic_trim_->set_db(engine_->params().output_trim_db.load());
    connect(mic_trim_, &TrimFader::valueChanged, this, [this](int) {
        const float db = mic_trim_->db();
        engine_->params().output_trim_db.store(db);
        if (bus_) bus_->push_trim(MODE_VOICE, db);
        if (on_params_changed) on_params_changed();
    });
    trim_row->addWidget(trim_lbl_);
    trim_row->addWidget(mic_trim_, 1);
    box->addLayout(trim_row);

    into->addWidget(unit01_box_, 1);
}

void StudioConsole::attach_strip(ChannelStrip* strip) {
    connect(strip, &ChannelStrip::trimMoved, this, [this](Mode m, float db) {
        if (bus_) bus_->push_trim(m, db);
    });
}

void StudioConsole::build_unit05(QVBoxLayout* into) {
    unit05_box_ = new QWidget(this);
    auto* box = new QVBoxLayout(unit05_box_);
    box->setContentsMargins(10, 8, 10, 10);
    box->setSpacing(6);

    unit05_title_ = new QLabel(unit05_box_);
    QFont tf = unit05_title_->font(); tf.setBold(true); unit05_title_->setFont(tf);
    unit05_title_->setStyleSheet(QStringLiteral("color:%1;").arg(skin_.text.name()));
    box->addWidget(unit05_title_);

    auto* strips = new QHBoxLayout();
    strips->setSpacing(4);
    const L names[4] = {
        {QStringLiteral("01 VOCAL"), QStringLiteral("01 声")},
        {QStringLiteral("02 COLLAB"), QStringLiteral("02 通話")},
        {QStringLiteral("03 GAME"), QStringLiteral("03 ゲーム")},
        {QStringLiteral("04 BGM"), QStringLiteral("04 BGM")}};
    const Mode modes[4] = {MODE_VOICE, MODE_COLLAB, MODE_GAME, MODE_BGM};
    for (int i = 0; i < 4; ++i) {
        strips_[static_cast<std::size_t>(i)] = new ChannelStrip(modes[i], names[i], &skin_, unit05_box_);
        attach_strip(strips_[static_cast<std::size_t>(i)]);
        strips->addWidget(strips_[static_cast<std::size_t>(i)]);
    }

    // Virtual master sum column.
    auto* master = new QVBoxLayout();
    master_title_ = new QLabel(unit05_box_);
    master_title_->setStyleSheet(QStringLiteral("color:#9a9da2;font-size:11px;"));
    master_title_->setMinimumWidth(190);
    master_lufs_ = new QLabel(QStringLiteral("M -∞  S -∞\nI -∞ LUFS"), unit05_box_);
    master_lufs_->setStyleSheet(QStringLiteral("color:#d6d8dc;font-family:monospace;"));
    golden_zone_ = new QLabel(QStringLiteral("—"), unit05_box_);
    golden_zone_->setStyleSheet(QStringLiteral("color:#5a5d62;font-weight:bold;"));
    phase_corr_ = new QLabel(QStringLiteral("Phase +1.00"), unit05_box_);
    phase_corr_->setStyleSheet(QStringLiteral("color:#d6d8dc;font-family:monospace;"));
    mono_check_ = new QCheckBox(QStringLiteral("Mono Check"), unit05_box_);
    mono_check_->setStyleSheet(QStringLiteral("color:#d6d8dc;"));
    mono_check_->setChecked(engine_->params().mono_check.load() != 0);
    connect(mono_check_, &QCheckBox::toggled, this, [this](bool on) {
        engine_->params().mono_check.store(on ? 1 : 0);
    });
    master->addWidget(master_title_);
    master->addWidget(master_lufs_);
    master->addWidget(golden_zone_);
    master->addWidget(phase_corr_);
    master->addWidget(mono_check_);
    master->addStretch(1);
    strips->addLayout(master);
    box->addLayout(strips, 1);

    // Scene macros.
    auto* macros = new QHBoxLayout();
    const char* macro_labels[4] = {"FPS/GAME", "TALK", "SINGING", "ASMR"};
    for (int i = 0; i < 4; ++i) {
        scene_btns_[i] = new QPushButton(QString::fromLatin1(macro_labels[i]), unit05_box_);
        scene_btns_[i]->setStyleSheet(QStringLiteral("QPushButton{background:#2c2e32;color:#d6d8dc;border:1px solid #3a3c40;border-radius:4px;padding:5px;}"));
        connect(scene_btns_[i], &QPushButton::clicked, this, [this, i] {
            engine_->params().apply_scene(i);
            if (character_) character_->setCurrentIndex(engine_->params().character.load());
            if (space_fx_) space_fx_->setCurrentIndex(engine_->params().space_fx.load());
            if (on_scene_macro) on_scene_macro(i);
            if (on_params_changed) on_params_changed();
        });
        macros->addWidget(scene_btns_[i]);
    }
    box->addLayout(macros);

    auto* exports = new QHBoxLayout();
    chapter_btn_ = new QPushButton(QStringLiteral("\U0001F4D1 CHAPTER MARK"), unit05_box_);
    karte_btn_ = new QPushButton(QStringLiteral("\U0001F4CA EXPORT KARTE"), unit05_box_);
    const QString export_style = QStringLiteral(
        "QPushButton{background:#2c2e32;color:#d6d8dc;border:1px solid #3a3c40;border-radius:4px;padding:6px;}");
    chapter_btn_->setStyleSheet(export_style);
    karte_btn_->setStyleSheet(export_style);
    connect(chapter_btn_, &QPushButton::clicked, this, [this] { emit chapterRequested(); if (on_request_chapter) on_request_chapter(); });
    connect(karte_btn_, &QPushButton::clicked, this, [this] { if (on_export_karte) on_export_karte(); });
    exports->addWidget(chapter_btn_);
    exports->addWidget(karte_btn_);
    box->addLayout(exports);

    deactivate_btn_ = new QPushButton(QStringLiteral("DEACTIVATE THIS PC"), unit05_box_);
    deactivate_btn_->setStyleSheet(QStringLiteral("QPushButton{background:#3a2c2c;color:#c09090;border-radius:4px;padding:4px;}"));
    connect(deactivate_btn_, &QPushButton::clicked, this, [this] { if (on_deactivate_pc) on_deactivate_pc(); });
    box->addWidget(deactivate_btn_, 0, Qt::AlignRight);

    into->addWidget(unit05_box_);
}

void StudioConsole::rebuild_chassis_pixmap() {
    const qreal dpr = devicePixelRatioF();
    const QSize sz = size();
    const int panel_top = header_box_ ? header_box_->geometry().bottom() + 6 : 40;
    if (dpr == chassis_dpr_ && sz == chassis_size_ && panel_top == chassis_panel_top_ && !chassis_.isNull()) return;
    chassis_dpr_ = dpr; chassis_size_ = sz; chassis_panel_top_ = panel_top;
    chassis_ = QPixmap(sz * dpr);
    chassis_.setDevicePixelRatio(dpr);
    chassis_.fill(Qt::transparent);

    QPainter p(&chassis_);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(0, 0, sz.width(), sz.height());

    // Brushed-metal body.
    QLinearGradient grad(r.topLeft(), r.bottomLeft());
    grad.setColorAt(0.0, skin_.chassis_top);
    grad.setColorAt(0.5, skin_.chassis_bottom.lighter(108));
    grad.setColorAt(1.0, skin_.chassis_bottom);
    p.fillRect(r, grad);

    // Rack ears + screws.
    const qreal ear_w = 22.0;
    p.fillRect(QRectF(0, 0, ear_w, r.height()), skin_.ear);
    p.fillRect(QRectF(r.width() - ear_w, 0, ear_w, r.height()), skin_.ear);
    p.setPen(Qt::NoPen);
    auto screw = [&](qreal x, qreal y) {
        QRadialGradient g(QPointF(x, y), 5);
        g.setColorAt(0.0, skin_.screw.lighter(140));
        g.setColorAt(1.0, skin_.screw.darker(160));
        p.setBrush(g);
        p.drawEllipse(QPointF(x, y), 4.5, 4.5);
        p.setPen(QPen(skin_.chassis_bottom.darker(130), 1));
        p.drawLine(QPointF(x - 3, y), QPointF(x + 3, y));
        p.setPen(Qt::NoPen);
    };
    screw(ear_w / 2, 12); screw(ear_w / 2, r.height() - 12);
    screw(r.width() - ear_w / 2, 12); screw(r.width() - ear_w / 2, r.height() - 12);

    // Inner recessed panel.
    const QRectF panel = r.adjusted(ear_w + 4, panel_top, -(ear_w + 4), -6);
    p.setBrush(skin_.panel);
    p.setPen(QPen(skin_.chassis_bottom.darker(140), 1));
    p.drawRoundedRect(panel, 6, 6);
}

void StudioConsole::paintEvent(QPaintEvent*) {
    rebuild_chassis_pixmap();
    QPainter p(this);
    p.drawPixmap(0, 0, chassis_);
}

void StudioConsole::refresh() {
    auto& t = engine_->telemetry();
    if (mini_ && mini_meter_) {
        mini_meter_->set_values(t.true_peak_db.load(), t.rms_db.load(), t.peak_hold_db.load());
        mini_meter_->update();
        const QString unit = director_ ? QStringLiteral("MASTER") : QStringLiteral("[01]");
        mini_readout_->setText(QStringLiteral("%1  %2 dBTP   S %3 LUFS   GR %4 dB")
            .arg(unit, fmt_db(t.true_peak_db.load()), fmt_db(t.lufs_s.load()), fmt_db(t.gr_db.load())));
    }
    // UNIT 01 meters.
    if (mic_level_) {
        mic_level_->set_values(t.true_peak_db.load(), t.rms_db.load(), t.peak_hold_db.load());
        mic_level_->update();
    }
    if (mic_gr_) { mic_gr_->set_gr(t.gr_db.load()); mic_gr_->update(); }
    if (mic_readout_) {
        mic_readout_->setText(QStringLiteral("%1 dBTP  %2 LUFS").arg(fmt_db(t.true_peak_db.load()), fmt_db(t.lufs_s.load())));
    }
    if (mic_hint_) {
        QString hint;
        if (t.hint_bgm_misplug.load()) hint = (lang_ == UiLang::JA)
            ? QStringLiteral("\U0001F4A1 このトラックはBGMではありませんか？ [04] BGM POCKET をお使いください。")
            : QStringLiteral("\U0001F4A1 Is this actually BGM? Use [04] BGM POCKET for music.");
        else if (t.bridge_mode.load()) hint = (lang_ == UiLang::JA)
            ? QStringLiteral("\U0001F517 Link Bridge Mode: 単体VCアプリと連携中（二重処理を自動回避）。")
            : QStringLiteral("\U0001F517 Link Bridge Mode: linked to standalone VC app (double-processing bypassed).");
        else if (t.acapella_pad.load()) hint = (lang_ == UiLang::JA)
            ? QStringLiteral("\U0001F3A7 雑談ソロ検出：天井を自動で -1.5 dB 落ち着かせています。")
            : QStringLiteral("\U0001F3A7 Solo chat detected: ceiling auto-eased by -1.5 dB.");
        mic_hint_->setText(hint);
    }

    // LINK MATRIX lamp from the bus.
    if (link_matrix_ && bus_ && bus_->raw()) {
        const auto* b = bus_->raw();
        const std::int64_t now = now_us();
        bool live[5] = {false, false, false, false, false};
        for (std::size_t i = 0; i < kMaxSlots; ++i) {
            const auto& s = b->unit_slots[i];
            if (s.in_use.load() == 0) continue;
            if (now - s.heartbeat_us.load() > SharedBus::kHeartbeatGcUs) continue;
            const int m = s.mode.load();
            if (m >= 0 && m < 5) live[m] = true;
        }
        const char* tags[5] = {"01", "02", "03", "04", "DIR"};
        QString s = QStringLiteral("<span style='color:#7a7d82'>LINK</span>&nbsp;");
        for (int i = 0; i < 5; ++i) {
            s += live[i] ? QStringLiteral("<span style='color:%1'>\u25CF%2</span>&nbsp;").arg(skin_.led_green.name(), QString::fromLatin1(tags[i]))
                         : QStringLiteral("<span style='color:#4a4d52'>\u25CB%1</span>&nbsp;").arg(QString::fromLatin1(tags[i]));
        }
        link_matrix_->setText(s);
    }

    // UNIT 05 strips pull their peers' telemetry from the bus.
    if (director_ && bus_ && bus_->raw()) {
        const auto* b = bus_->raw();
        const std::int64_t now = now_us();
        const Mode modes[4] = {MODE_VOICE, MODE_COLLAB, MODE_GAME, MODE_BGM};
        for (int k = 0; k < 4; ++k) {
            float tp = -120, rms = -120, lufs = -120, gr = 0, duck = 0; bool live = false;
            for (std::size_t i = 0; i < kMaxSlots; ++i) {
                const auto& s = b->unit_slots[i];
                if (s.in_use.load() == 0 || s.mode.load() != modes[k]) continue;
                if (now - s.heartbeat_us.load() > SharedBus::kHeartbeatGcUs) continue;
                tp = s.true_peak_db.load(); rms = s.rms_db.load(); lufs = s.short_term_lufs.load();
                gr = s.gr_db.load(); duck = s.duck_db.load(); live = true; break;
            }
            strips_[static_cast<std::size_t>(k)]->update_from(tp, tp, rms, lufs, gr, duck, live);
        }
        // Master section uses UNIT 05's own loudness meter (it sees Track-1 sum).
        master_lufs_->setText(QStringLiteral("M %1  S %2\nI %3 LUFS")
            .arg(fmt_db(t.lufs_m.load()), fmt_db(t.lufs_s.load()), fmt_db(t.lufs_i.load())));
        const float li = t.lufs_i.load();
        if (in_youtube_golden_zone(li)) {
            golden_zone_->setText(QStringLiteral("✓ YOUTUBE LIVE GOLDEN ZONE"));
            golden_zone_->setStyleSheet(QStringLiteral("color:%1;font-weight:bold;").arg(skin_.led_green.name()));
        } else {
            golden_zone_->setText(li <= -119.0f ? QStringLiteral("—") : QStringLiteral("%1 LUFS").arg(fmt_db(li)));
            golden_zone_->setStyleSheet(QStringLiteral("color:#9a9da2;"));
        }
        const float pc = t.phase_corr.load();
        phase_corr_->setText(QStringLiteral("Phase %1%2").arg(pc >= 0 ? QStringLiteral("+") : QString()).arg(QString::number(static_cast<double>(pc), 'f', 2)));
        phase_corr_->setStyleSheet(pc < 0.0f ? QStringLiteral("color:%1;font-family:monospace;").arg(skin_.led_red.name())
                                             : QStringLiteral("color:#d6d8dc;font-family:monospace;"));
    }
}

void StudioConsole::showEvent(QShowEvent* e) {
    QDialog::showEvent(e);
    clamp_to_screen();
    if (timer_) timer_->start(); // Resume 30 fps only while visible.
}
void StudioConsole::hideEvent(QHideEvent* e) {
    if (timer_) timer_->stop();  // 0.00% CPU when hidden.
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

void StudioConsole::toggle_pin() {
    pinned_ = pin_btn_->isChecked();
    Qt::WindowFlags f = windowFlags();
    f.setFlag(Qt::WindowStaysOnTopHint, pinned_);
    setWindowFlags(f);
    show(); // Re-apply flags.
}

void StudioConsole::toggle_mini() {
    // MINI folds the console into a ~120 px strip: header + one horizontal
    // meter + a one-line readout. The full body is hidden, not destroyed.
    mini_ = mini_btn_->isChecked();
    body_box_->setVisible(!mini_);
    mini_bar_->setVisible(mini_);
    if (mini_) {
        setMinimumHeight(0);
        setMaximumHeight(130);
        resize(width(), minimumSizeHint().height());
    } else {
        setMaximumHeight(560);
        resize(width(), 560);
    }
    refresh();
}

void StudioConsole::toggle_language() {
    lang_ = (lang_ == UiLang::JA) ? UiLang::EN : UiLang::JA;
    relabel();
}

void StudioConsole::set_language(UiLang lang) { lang_ = lang; relabel(); }

void StudioConsole::relabel() {
    for (int i = 0; i < 4; ++i) if (strips_[static_cast<std::size_t>(i)]) strips_[static_cast<std::size_t>(i)]->set_language(lang_);
    if (calib_btn_) calib_btn_->setText(lang_ == UiLang::JA ? QStringLiteral("\U0001F3A4 10秒カンペ測定") : QStringLiteral("\U0001F3A4 10s CALIBRATE"));
    if (audition_btn_) audition_btn_->setText(lang_ == UiLang::JA ? QStringLiteral("\U0001F501 加工前/後 試聴") : QStringLiteral("\U0001F501 A/B AUDITION"));
    if (bleep_btn_) bleep_btn_->setText(lang_ == UiLang::JA ? QStringLiteral("\U0001F92C ピー音") : QStringLiteral("\U0001F92C 1kHz BLEEP"));
    if (cough_btn_) cough_btn_->setText(lang_ == UiLang::JA ? QStringLiteral("\U0001F927 咳ミュート") : QStringLiteral("\U0001F927 COUGH MUTE"));
    if (chapter_btn_) chapter_btn_->setText(lang_ == UiLang::JA ? QStringLiteral("\U0001F4D1 チャプター記録") : QStringLiteral("\U0001F4D1 CHAPTER MARK"));
    if (karte_btn_) karte_btn_->setText(lang_ == UiLang::JA ? QStringLiteral("\U0001F4CA カルテ出力") : QStringLiteral("\U0001F4CA EXPORT KARTE"));
    if (deactivate_btn_) deactivate_btn_->setText(lang_ == UiLang::JA ? QStringLiteral("このPCの認証を解除") : QStringLiteral("DEACTIVATE THIS PC"));
    const bool ja = (lang_ == UiLang::JA);
    if (char_lbl_) char_lbl_->setText(ja ? QStringLiteral("キャラクター") : QStringLiteral("Character"));
    if (fx_lbl_) fx_lbl_->setText(ja ? QStringLiteral("空間エフェクト") : QStringLiteral("Space FX"));
    if (trim_lbl_) trim_lbl_->setText(ja ? QStringLiteral("出力トリム") : QStringLiteral("OUTPUT TRIM"));
    if (unit05_title_) unit05_title_->setText(ja ? QStringLiteral("[05] 統合ミキサー＆声ディレクター")
                                                 : QStringLiteral("[05] STREAM DIRECTOR  \u2014  MASTER TELEMETRY"));
    if (master_title_) master_title_->setText(ja ? QStringLiteral("仮想マスター合算") : QStringLiteral("VIRTUAL MASTER SUM"));
    if (mono_check_) mono_check_->setText(ja ? QStringLiteral("モノラル確認") : QStringLiteral("Mono Check"));
    static const char* scene_en[4] = {"FPS/GAME", "TALK", "SINGING", "ASMR"};
    const QString scene_ja[4] = {QStringLiteral("FPS/ゲーム"), QStringLiteral("雑談"), QStringLiteral("歌枠"), QStringLiteral("ASMR")};
    for (int i = 0; i < 4; ++i)
        if (scene_btns_[i]) scene_btns_[i]->setText(ja ? scene_ja[i] : QString::fromLatin1(scene_en[i]));
    for (auto& hook : relabel_hooks_) hook();
    update();
}

void StudioConsole::panic_reset() {
    engine_->params().factory_reset_keep_calibration();
    if (character_) character_->setCurrentIndex(engine_->params().character.load());
    if (space_fx_) space_fx_->setCurrentIndex(engine_->params().space_fx.load());
    if (mic_trim_) mic_trim_->set_db(engine_->params().output_trim_db.load());
    if (mono_check_) mono_check_->setChecked(false);
    engine_->telemetry().reset_holds();
    engine_->reset_meters();
    if (on_params_changed) on_params_changed();
}

void StudioConsole::run_calibration() {
    if (engine_->calibrator().running()) return;
    CalibrationDialog dlg(engine_, &skin_, lang_, this);
    dlg.exec();
    if (on_params_changed) on_params_changed(); // Persist a committed input gain.
}

void StudioConsole::start_audition() {
    auto& a = engine_->audition();
    if (a.playing() || a.capturing()) { a.stop(); audition_btn_->setChecked(false); return; }
    a.start_record(engine_->telemetry().sample_rate.load());
    // After 3 s the recorder flips to IDLE; a short timer then starts the loop.
    QTimer::singleShot(static_cast<int>(dsp::AuditionRecorder::kSeconds * 1000 + 100), this, [this] {
        engine_->audition().start_loop();
    });
}

void StudioConsole::fire_bleep() { engine_->bleep().trigger(3.0f); }
// UNIT 01 publishes the cough state on the bus; UNIT 04 lifts BGM +2 dB itself.
void StudioConsole::hold_cough() { engine_->cough().press(true); }

void StudioConsole::set_update_available(const QString& version) {
    update_available_ = true;
    update_version_ = version;
    if (update_btn_) {
        update_btn_->setVisible(true);
        update_btn_->setText(QStringLiteral("⚡ UPDATE v%1 READY").arg(version));
        update_btn_->setStyleSheet(QStringLiteral(
            "QPushButton{background:%1;color:#1a1a1a;border-radius:4px;padding:3px 8px;font-weight:bold;}")
            .arg(skin_.accent.name()));
    }
}

} // namespace sw::model600::ui
