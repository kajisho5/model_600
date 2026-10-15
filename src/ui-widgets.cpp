// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  ui-widgets.cpp  [Layer 2]  Custom-painted widgets (see ui-widgets.hpp).
// =============================================================================
#include "ui-widgets.hpp"
#include "ui-theme.hpp"

#include <QFocusEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtMath>
#include <QtWidgets/qdrawutil.h>

#include <algorithm>
#include <cmath>

namespace sw::model600::ui {

// Vertical meters and their scales leave room for the end labels.
constexpr qreal kMeterPadV = 6.0;
// Horizontal bars with scales leave room for the edge labels.
constexpr qreal kBarPadH = 12.0;

static bool keyboard_reason(const QFocusEvent* e) {
    return e->reason() == Qt::TabFocusReason || e->reason() == Qt::BacktabFocusReason ||
           e->reason() == Qt::ShortcutFocusReason;
}

// =============================================================================
//  Helpers
// =============================================================================
static qreal digit_advance(const QFontMetricsF& fm) {
    qreal w = 0;
    for (QChar c : QStringLiteral("0123456789")) w = std::max(w, fm.horizontalAdvance(c));
    return w;
}

qreal tabular_width(const QFont& f, const QString& text) {
    const QFontMetricsF fm(f);
    const qreal dw = digit_advance(fm);
    qreal w = 0;
    for (QChar c : text) w += c.isDigit() ? dw : fm.horizontalAdvance(c);
    return w;
}

void draw_tabular(QPainter& p, const QRectF& r, const QString& text, Qt::Alignment align) {
    const QFontMetricsF fm(p.font());
    const qreal dw = digit_advance(fm);
    const qreal total = tabular_width(p.font(), text);
    qreal x = r.left();
    if (align & Qt::AlignHCenter) x = r.center().x() - total / 2;
    else if (align & Qt::AlignRight) x = r.right() - total;
    const qreal y = r.center().y() + (fm.ascent() - fm.descent()) / 2;
    for (QChar c : text) {
        const qreal cw = c.isDigit() ? dw : fm.horizontalAdvance(c);
        const qreal gx = c.isDigit() ? x + (dw - fm.horizontalAdvance(c)) / 2 : x;
        p.drawText(QPointF(gx, y), QString(c));
        x += cw;
    }
}

static void draw_led(QPainter& p, const QPointF& c, const QString& part_name, bool lit, qreal size = 10) {
    if (lit) {
        QColor glow = part_name == QLatin1String("led_red") ? Tok::red
                    : part_name == QLatin1String("led_green") ? Tok::green : Tok::accent;
        QRadialGradient g(c, size * 1.1);
        glow.setAlphaF(0.35);
        g.setColorAt(0, glow);
        glow.setAlphaF(0.0);
        g.setColorAt(1, glow);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(c, size * 1.1, size * 1.1);
    }
    const QPixmap& px = part(lit ? part_name : QStringLiteral("led_off"));
    p.drawPixmap(QRectF(c.x() - size / 2, c.y() - size / 2, size, size), px, QRectF(px.rect()));
}

static void stroke_icon(QPainter& p, RackKey::Icon icon, const QRectF& box, const QColor& col) {
    p.save();
    QPen pen(col, 1.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const qreal x = box.center().x() - 7, y = box.center().y() - 7;
    QPainterPath path;
    switch (icon) {
        case RackKey::Icon::Pin:
            path.moveTo(x + 4.5, y + 1.5); path.lineTo(x + 9.5, y + 1.5);
            path.moveTo(x + 5.5, y + 1.5); path.lineTo(x + 5, y + 6.5); path.lineTo(x + 3, y + 8.5);
            path.lineTo(x + 11, y + 8.5); path.lineTo(x + 9, y + 6.5); path.lineTo(x + 8.5, y + 1.5);
            path.moveTo(x + 7, y + 8.5); path.lineTo(x + 7, y + 12.5);
            break;
        case RackKey::Icon::Fold:
            path.moveTo(x + 2, y + 11.5); path.lineTo(x + 12, y + 11.5);
            path.moveTo(x + 4, y + 7); path.lineTo(x + 7, y + 4); path.lineTo(x + 10, y + 7);
            break;
        case RackKey::Icon::Unfold:
            path.moveTo(x + 2, y + 2.5); path.lineTo(x + 12, y + 2.5);
            path.moveTo(x + 4, y + 7); path.lineTo(x + 7, y + 10); path.lineTo(x + 10, y + 7);
            break;
        case RackKey::Icon::Reset: {
            const QPointF c(x + 7, y + 7.5);
            const qreal r = 4.6;
            const qreal a0 = -60.0, sweep = 280.0;
            QRectF arc(c.x() - r, c.y() - r, 2 * r, 2 * r);
            path.arcMoveTo(arc, -a0);
            path.arcTo(arc, -a0, -sweep);
            const QPointF s(c.x() + r * std::cos(qDegreesToRadians(a0)), c.y() + r * std::sin(qDegreesToRadians(a0)));
            path.moveTo(s.x() - 3.2, s.y() - 0.6); path.lineTo(s); path.lineTo(s.x() + 0.4, s.y() - 3.2);
            break;
        }
        case RackKey::Icon::None: break;
    }
    p.drawPath(path);
    p.restore();
}

// =============================================================================
//  RackKey
// =============================================================================
RackKey::RackKey(const QString& text, QWidget* parent) : QAbstractButton(parent) {
    setText(text);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFont(font_body());
}

RackKey::RackKey(Icon icon, QWidget* parent) : QAbstractButton(parent), icon_(icon) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(28, 28);
}

QSize RackKey::sizeHint() const {
    if (icon_ != Icon::None && text().isEmpty()) return QSize(28, 28);
    const QFontMetrics fm(font());
    return QSize(fm.horizontalAdvance(text()) + (led_.isEmpty() ? 32 : 48), 34);
}
QSize RackKey::minimumSizeHint() const { return QSize(icon_ != Icon::None ? 28 : 60, 28); }

void RackKey::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const bool on = isDown() || isChecked() || lit_;
    QString bg = QStringLiteral("key_default");
    if (style_ == Style::Primary) bg = QStringLiteral("key_primary");
    else if (style_ == Style::Danger) bg = QStringLiteral("key_danger");
    else if (on) bg = QStringLiteral("key_active");
    const QPixmap& px = part(bg);
    // 9-slice: 8 px logical corners (32 px in the 4x source).
    qDrawBorderPixmap(&p, rect(), QMargins(6, 6, 6, 6), px, px.rect(), QMargins(24, 24, 24, 24),
                      QTileRules(Qt::StretchTile));
    if (icon_ != Icon::None && isChecked()) {
        p.setPen(QPen(Tok::accent, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    }
    if (hover_ && isEnabled()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, style_ == Style::Primary ? 26 : 10));
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 4, 4);
    }
    if (hasFocus() && kb_focus_) {
        p.setPen(QPen(QColor(240, 163, 58, 110), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 3.5, 3.5);
    }
    if (icon_ != Icon::None) {
        stroke_icon(p, icon_, QRectF(rect()), isChecked() ? Tok::accent : Tok::t2);
        return;
    }
    qreal shift = 0;
    if (!led_.isEmpty()) {
        draw_led(p, QPointF(16, height() / 2.0), led_, on);
        shift = 8;
    }
    QColor tc = style_ == Style::Primary ? QColor(0x1A, 0x12, 0x06) : Tok::t1;
    if (!isEnabled()) tc = Tok::t3;
    QFont f = font();
    if (style_ == Style::Primary) f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(tc);
    p.drawText(QRectF(rect()).adjusted(shift * 2, 0, 0, 0), Qt::AlignCenter, text());
}

void RackKey::focusInEvent(QFocusEvent* e) { kb_focus_ = keyboard_reason(e); QAbstractButton::focusInEvent(e); update(); }
void RackKey::focusOutEvent(QFocusEvent* e) { kb_focus_ = false; QAbstractButton::focusOutEvent(e); update(); }

// =============================================================================
//  SegmentedControl
// =============================================================================
SegmentedControl::SegmentedControl(const QStringList& options, QWidget* parent)
    : QWidget(parent), options_(options) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(28);
}

void SegmentedControl::set_options(const QStringList& options) { options_ = options; update(); }
void SegmentedControl::set_current(int i) {
    if (i < 0 || i >= options_.size() || i == current_) { update(); return; }
    current_ = i;
    update();
    emit currentChanged(i);
}

QSize SegmentedControl::sizeHint() const {
    const QFontMetrics fm(make_font(text_px_, QFont::Medium));
    int w = 0;
    for (const QString& o : options_) w = std::max(w, fm.horizontalAdvance(o) + 24);
    return QSize(w * static_cast<int>(options_.size()), 30);
}

void SegmentedControl::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(Tok::line, 1));
    p.setBrush(Tok::panel);
    p.drawRoundedRect(r, 4, 4);
    const int n = static_cast<int>(options_.size());
    if (n == 0) return;
    const qreal sw = width() / static_cast<qreal>(n);
    for (int i = 0; i < n; ++i) {
        const QRectF seg(i * sw, 0, sw, height());
        if (i == current_) {
            p.setPen(Qt::NoPen);
            p.setBrush(Tok::raised_active);
            p.drawRoundedRect(seg.adjusted(2, 2, -2, -2), 3, 3);
            p.setBrush(Tok::accent);
            p.drawRoundedRect(QRectF(seg.left() + 8, height() - 4, sw - 16, 2), 1, 1);
        } else if (i > 0 && i != current_ + 1) {
            p.setPen(QPen(Tok::line, 1));
            p.drawLine(QPointF(seg.left(), 7), QPointF(seg.left(), height() - 7));
        }
        p.setFont(make_font(text_px_, i == current_ ? QFont::DemiBold : QFont::Medium));
        p.setPen(i == current_ ? Tok::t1 : Tok::t2);
        p.drawText(seg.adjusted(2, i == current_ ? -1 : 0, -2, 0), Qt::AlignCenter, options_[i]);
    }
    if (hasFocus() && kb_focus_) {
        p.setPen(QPen(QColor(240, 163, 58, 90), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 3.5, 3.5);
    }
}

void SegmentedControl::focusInEvent(QFocusEvent* e) { kb_focus_ = keyboard_reason(e); QWidget::focusInEvent(e); update(); }
void SegmentedControl::focusOutEvent(QFocusEvent* e) { kb_focus_ = false; QWidget::focusOutEvent(e); update(); }

void SegmentedControl::mousePressEvent(QMouseEvent* e) {
    if (options_.isEmpty()) return;
    const int i = std::clamp(static_cast<int>(e->position().x() / (width() / static_cast<qreal>(options_.size()))),
                             0, static_cast<int>(options_.size()) - 1);
    set_current(i);
}

void SegmentedControl::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Left) set_current(std::max(0, current_ - 1));
    else if (e->key() == Qt::Key_Right) set_current(std::min(static_cast<int>(options_.size()) - 1, current_ + 1));
    else QWidget::keyPressEvent(e);
}

// =============================================================================
//  Readout / ValueBox / SectionLabel
// =============================================================================
Readout::Readout(const QString& label, const QString& unit, bool big, QWidget* parent)
    : QWidget(parent), label_(label), unit_(unit), big_(big) {
    setMinimumHeight(big ? 64 : 56);
}

void Readout::set_value(const QString& v, const QColor& color) {
    if (v == value_ && color == color_) return;
    value_ = v;
    color_ = color;
    update();
}

QSize Readout::sizeHint() const { return QSize(big_ ? 100 : 90, big_ ? 64 : 56); }

void Readout::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(Tok::line, 1));
    p.setBrush(Tok::panel);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    p.setFont(make_font(9, QFont::DemiBold, 1.0));
    p.setPen(Tok::t2);
    p.drawText(QRectF(10, 7, width() - 14, 14), Qt::AlignLeft | Qt::AlignVCenter, label_.toUpper());
    const QFont vf = big_ ? font_display() : make_font(15, QFont::Medium);
    p.setFont(vf);
    p.setPen(color_.isValid() ? color_ : Tok::t1);
    const qreal base_h = big_ ? 30 : 24;
    const QRectF vr(10, height() - base_h - 6, width() - 14, base_h);
    draw_tabular(p, vr, value_, Qt::AlignLeft);
    if (!unit_.isEmpty()) {
        const qreal vw = tabular_width(vf, value_);
        p.setFont(make_font(10, QFont::Medium));
        p.setPen(Tok::t2);
        const QFontMetricsF vm(vf), um(p.font());
        const qreal base_y = vr.center().y() + (vm.ascent() - vm.descent()) / 2;
        p.drawText(QPointF(10 + vw + 3, base_y), unit_);
        Q_UNUSED(um);
    }
}

ValueBox::ValueBox(QWidget* parent) : QWidget(parent) { setFixedHeight(20); setMinimumWidth(40); }

void ValueBox::set_text(const QString& t, const QColor& c) {
    if (t == text_ && c == color_) return;
    text_ = t;
    color_ = c;
    update();
}

void ValueBox::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(Tok::line, 1));
    p.setBrush(Tok::panel);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    p.setFont(make_font(11, QFont::Medium));
    p.setPen(color_.isValid() ? color_ : Tok::t1);
    draw_tabular(p, QRectF(rect()), text_, Qt::AlignHCenter);
}

SectionLabel::SectionLabel(const QString& text, QWidget* parent) : QWidget(parent), text_(text) {
    setFixedHeight(16);
}

QSize SectionLabel::sizeHint() const {
    const QFontMetrics fm(font_label());
    return QSize(fm.horizontalAdvance(text_.toUpper()) + 8, 16);
}

void SectionLabel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setFont(font_label());
    p.setPen(Tok::t2);
    p.drawText(QRectF(rect()), (centered_ ? Qt::AlignHCenter : Qt::AlignLeft) | Qt::AlignVCenter, text_.toUpper());
    if (!right_.isEmpty()) {
        const QFontMetricsF fm(p.font());
        const qreal w = fm.horizontalAdvance(right_);
        p.setPen(right_color_.isValid() ? right_color_ : Tok::t1);
        p.drawText(QRectF(rect()), Qt::AlignRight | Qt::AlignVCenter, right_);
        if (right_led_) {
            p.setPen(Qt::NoPen);
            p.setBrush(right_color_);
            p.drawEllipse(QPointF(width() - w - 9, height() / 2.0), 3, 3);
        }
    }
}

// =============================================================================
//  LedMeter
// =============================================================================
LedMeter::LedMeter(Kind kind, Qt::Orientation o, QWidget* parent)
    : QWidget(parent), kind_(kind), orient_(o) {
    clock_.start();
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Click to reset the peak hold"));
}

QSize LedMeter::sizeHint() const {
    if (orient_ == Qt::Horizontal) return QSize(240, scale_ ? 32 : 14);
    return QSize(kind_ == Kind::Level ? 22 : 14, 200);
}
QSize LedMeter::minimumSizeHint() const {
    if (orient_ == Qt::Horizontal) return QSize(80, scale_ ? 30 : 10);
    return QSize(8, 80);
}

void LedMeter::set_level(float peak_db, float rms_db, float hold_db) {
    // Visual ballistics: rise instantly, fall at 20 dB/s.
    const float dt = static_cast<float>(clock_.restart()) / 1000.0f;
    const float fall = 20.0f * std::min(dt, 0.5f);
    shown_peak_ = std::max(peak_db, shown_peak_ - fall);
    shown_rms_ = std::max(rms_db, shown_rms_ - fall);
    peak_ = peak_db;
    rms_ = rms_db;
    hold_ = std::max(hold_, hold_db);
    update();
}

void LedMeter::set_gr(float gr_db) { gr_ = gr_db; update(); }
void LedMeter::reset_hold() { hold_ = -120.0f; update(); }

QRectF LedMeter::bar_rect() const {
    if (orient_ == Qt::Horizontal) {
        if (scale_) return QRectF(kBarPadH, 0, width() - 2 * kBarPadH, 14);
        return QRectF(rect());
    }
    return QRectF(0, kMeterPadV, width(), height() - 2 * kMeterPadV);
}

void LedMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QRectF b = bar_rect();
    p.setPen(QPen(Tok::line, 1));
    p.setBrush(Tok::panel);
    p.drawRect(b.adjusted(0.5, 0.5, -0.5, -0.5));
    p.setPen(Qt::NoPen);

    if (kind_ == Kind::GainReduction) {
        const qreal frac = std::clamp(-static_cast<qreal>(gr_) / 24.0, 0.0, 1.0);
        const qreal len = (b.height() - 4) * frac;
        const int pitch = 3;
        const int n = static_cast<int>((b.height() - 4) / pitch);
        for (int i = 0; i < n; ++i) {
            const qreal y = b.top() + 2 + i * pitch;
            const bool lit = (i + 1) * pitch <= len + 0.01;
            p.fillRect(QRectF(b.left() + 2, y, b.width() - 4, pitch - 1), lit ? Tok::gr : Tok::off);
        }
        return;
    }

    const bool vertical = orient_ == Qt::Vertical;
    const qreal len = vertical ? b.height() - 4 : b.width() - 4;
    const int pitch = vertical ? 3 : 4;
    const int n = static_cast<int>(len / pitch);
    const qreal pk = meter_pos(shown_peak_), rm = meter_pos(shown_rms_);
    for (int i = 0; i < n; ++i) {
        const qreal frac = (i + 0.5) / n;
        const qreal db = meter_db(frac);
        const bool lit_pk = frac <= pk, lit_rms = frac <= rm;
        QColor c = lit_pk ? zone_color(db) : Tok::off;
        if (lit_pk && !lit_rms) c.setAlphaF(0.55);
        if (vertical) {
            const qreal y = b.bottom() - 2 - (i + 1) * pitch;
            p.fillRect(QRectF(b.left() + 2, y + 1, b.width() - 4, pitch - 1), Tok::off);
            p.fillRect(QRectF(b.left() + 2, y + 1, b.width() - 4, pitch - 1), c);
        } else {
            const qreal x = b.left() + 2 + i * pitch;
            p.fillRect(QRectF(x, b.top() + 2, pitch - 1, b.height() - 4), Tok::off);
            p.fillRect(QRectF(x, b.top() + 2, pitch - 1, b.height() - 4), c);
        }
    }
    if (hold_ > -60.0f) {
        const qreal hp = meter_pos(hold_);
        if (vertical) p.fillRect(QRectF(b.left() + 2, b.bottom() - 2 - hp * len - 1, b.width() - 4, 2), Tok::t1);
        else p.fillRect(QRectF(b.left() + 2 + hp * len - 1, b.top() + 2, 2, b.height() - 4), Tok::t1);
    }
    if (!vertical && scale_) {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setFont(font_scale());
        for (float db : {-60.0f, -48.0f, -36.0f, -24.0f, -18.0f, -12.0f, -6.0f, -3.0f, 0.0f}) {
            const qreal x = b.left() + 2 + meter_pos(db) * len;
            p.setPen(QPen(Tok::t3, 1));
            p.drawLine(QPointF(x, b.bottom() + 2), QPointF(x, b.bottom() + 5));
            p.drawText(QRectF(x - 16, b.bottom() + 6, 32, 12), Qt::AlignCenter, QString::number(static_cast<int>(db)));
        }
    }
}

// =============================================================================
//  MeterScale
// =============================================================================
MeterScale::MeterScale(Kind kind, QWidget* parent) : QWidget(parent), kind_(kind) {
    setFixedWidth(28);
}

void MeterScale::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setFont(font_scale());
    p.setPen(Tok::t3);
    const qreal top = kMeterPadV + 2, len_full = height() - 2 * kMeterPadV - 4;
    if (kind_ == Kind::Level) {
        for (int db : {0, -3, -6, -9, -12, -18, -24, -36, -48, -60}) {
            const qreal y = top + len_full - meter_pos(db) * len_full;
            p.drawText(QRectF(0, y - 6, width(), 12), Qt::AlignCenter, QString::number(db));
        }
    } else {
        const qreal len = len_full * gr_span_;
        for (int db : {0, -3, -6, -12, -24}) {
            const qreal y = top + (-db / 24.0) * len;
            p.drawText(QRectF(0, y - 6, width(), 12), Qt::AlignCenter, QString::number(db));
        }
    }
}

// =============================================================================
//  RackFader
// =============================================================================
RackFader::RackFader(Qt::Orientation o, float min_db, float max_db, Law law, QWidget* parent)
    : QAbstractSlider(parent), min_db_(min_db), max_db_(max_db), law_(law) {
    setOrientation(o);
    setRange(static_cast<int>(std::lround(min_db * 10)), static_cast<int>(std::lround(max_db * 10)));
    setValue(0);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    connect(this, &QAbstractSlider::valueChanged, this, [this](int) { emit dbChanged(db()); update(); });
    if (law_ == Law::Console) marks_ = {12, 6, 0, -6, -12, -24, -48};
}

void RackFader::set_db(float v) { setValue(static_cast<int>(std::lround(std::clamp(v, min_db_, max_db_) * 10))); }
float RackFader::db() const { return static_cast<float>(value()) / 10.0f; }

QSize RackFader::sizeHint() const { return orientation() == Qt::Vertical ? QSize(64, 240) : QSize(260, 36); }
QSize RackFader::minimumSizeHint() const { return orientation() == Qt::Vertical ? QSize(56, 120) : QSize(120, 34); }

qreal RackFader::pos_of(float v) const {
    v = std::clamp(v, min_db_, max_db_);
    if (law_ == Law::Console) {
        // +12 top .. -48 bottom; 0..-12 region gets the finest resolution.
        if (v >= -12.0f) return 0.35 + (v + 12.0f) / 24.0f * 0.65;
        return (v + 48.0f) / 36.0f * 0.35;
    }
    return (v - min_db_) / (max_db_ - min_db_);
}

float RackFader::db_at(qreal pos) const {
    pos = std::clamp(pos, 0.0, 1.0);
    if (law_ == Law::Console) {
        if (pos >= 0.35) return static_cast<float>(-12.0 + (pos - 0.35) / 0.65 * 24.0);
        return static_cast<float>(-48.0 + pos / 0.35 * 36.0);
    }
    return static_cast<float>(min_db_ + pos * (max_db_ - min_db_));
}

QRectF RackFader::track_rect() const {
    if (orientation() == Qt::Vertical) return QRectF(width() - 22, 9, 4, height() - 18);
    return QRectF(9, 12, width() - 18, 4);
}

void RackFader::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF t = track_rect();
    p.setPen(QPen(Tok::line, 1));
    p.setBrush(Tok::panel);
    p.drawRoundedRect(t, 2, 2);
    p.setFont(font_scale());
    const bool vertical = orientation() == Qt::Vertical;
    for (float m : marks_) {
        if (m < min_db_ || m > max_db_) continue;
        const qreal pp = pos_of(m);
        QString txt = m > 0 ? QStringLiteral("+%1").arg(static_cast<int>(m)) : QString::number(static_cast<int>(m));
        p.setPen(QPen(Tok::t3, 1));
        if (vertical) {
            const qreal y = t.bottom() - pp * t.height();
            p.drawLine(QPointF(t.left() - 10, y), QPointF(t.left() - 4, y));
            p.drawLine(QPointF(t.right() + 4, y), QPointF(t.right() + 10, y));
            p.drawText(QRectF(0, y - 6, t.left() - 13, 12), Qt::AlignRight | Qt::AlignVCenter, txt);
        } else {
            const qreal x = t.left() + pp * t.width();
            p.drawLine(QPointF(x, t.bottom() + 5), QPointF(x, t.bottom() + 9));
            p.drawText(QRectF(x - 18, t.bottom() + 10, 36, 12), Qt::AlignCenter, txt);
        }
    }
    // Blender-rendered cap.
    const qreal pv = pos_of(db());
    if (vertical) {
        const QPixmap& cap = part(QStringLiteral("fader_cap_v"));
        const qreal y = t.bottom() - pv * t.height();
        p.drawPixmap(QRectF(t.center().x() - 16, y - 7.5, 32, 15), cap, QRectF(cap.rect()));
    } else {
        const QPixmap& cap = part(QStringLiteral("fader_cap_h"));
        const qreal x = t.left() + pv * t.width();
        p.drawPixmap(QRectF(x - 7.5, t.center().y() - 9.5, 15, 19), cap, QRectF(cap.rect()));
    }
    if (hasFocus() && kb_focus_) {
        p.setPen(QPen(QColor(240, 163, 58, 70), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    }
}

void RackFader::set_from_point(const QPointF& pt) {
    const QRectF t = track_rect();
    const qreal pos = orientation() == Qt::Vertical ? (t.bottom() - pt.y()) / t.height()
                                                    : (pt.x() - t.left()) / t.width();
    set_db(db_at(pos));
}

void RackFader::mousePressEvent(QMouseEvent* e) { set_from_point(e->position()); setSliderDown(true); }
void RackFader::mouseMoveEvent(QMouseEvent* e) { if (e->buttons() & Qt::LeftButton) set_from_point(e->position()); }

void RackFader::wheelEvent(QWheelEvent* e) {
    if (!(e->modifiers() & Qt::ControlModifier)) { e->ignore(); return; }   // Plain wheel disabled.
    setValue(value() + (e->angleDelta().y() > 0 ? 1 : -1));                 // 0.1 dB nudge.
    e->accept();
}

void RackFader::mouseDoubleClickEvent(QMouseEvent* e) { set_db(default_db_); e->accept(); }
void RackFader::focusInEvent(QFocusEvent* e) { kb_focus_ = keyboard_reason(e); QAbstractSlider::focusInEvent(e); update(); }
void RackFader::focusOutEvent(QFocusEvent* e) { kb_focus_ = false; QAbstractSlider::focusOutEvent(e); update(); }

// =============================================================================
//  StatusLine
// =============================================================================
StatusLine::StatusLine(QWidget* parent) : QWidget(parent) { setFixedHeight(30); }

void StatusLine::set_message(Level level, const QString& msg) {
    if (level == level_ && msg == msg_) return;
    level_ = level;
    msg_ = msg;
    update();
}

void StatusLine::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setPen(QPen(Tok::line, 1));
    p.drawLine(QPointF(0, 0.5), QPointF(width(), 0.5));
    QString led = QStringLiteral("led_green");
    if (level_ == Level::Warn) led = QStringLiteral("led_amber");
    else if (level_ == Level::Error) led = QStringLiteral("led_red");
    if (level_ == Level::Info) {
        QRadialGradient g(QPointF(20, height() / 2.0), 9);
        QColor c = Tok::info; c.setAlphaF(0.3); g.setColorAt(0, c); c.setAlphaF(0); g.setColorAt(1, c);
        p.setPen(Qt::NoPen); p.setBrush(g); p.drawEllipse(QPointF(20, height() / 2.0), 9, 9);
        p.setBrush(Tok::info); p.drawEllipse(QPointF(20, height() / 2.0), 3.5, 3.5);
    } else {
        draw_led(p, QPointF(20, height() / 2.0), led, true, 8);
    }
    p.setFont(make_font(11, QFont::Medium));
    p.setPen(Tok::t2);
    const qreal right_w = QFontMetricsF(make_font(10, QFont::Medium)).horizontalAdvance(right_ + link_) + 40;
    p.drawText(QRectF(32, 0, width() - 32 - right_w, height()), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(p.font()).elidedText(msg_, Qt::ElideRight, static_cast<int>(width() - 32 - right_w)));
    p.setFont(make_font(10, QFont::Medium));
    qreal x = width() - 16;
    const QFontMetricsF fm(p.font());
    if (!right_.isEmpty()) {
        const qreal w = fm.horizontalAdvance(right_);
        p.setPen(Tok::t3);
        p.drawText(QRectF(x - w, 0, w, height()), Qt::AlignVCenter, right_);
        x -= w;
    }
    if (!link_.isEmpty()) {
        const QString sep = QStringLiteral("  ·  ");
        const qreal sw = fm.horizontalAdvance(sep);
        p.setPen(Tok::t3);
        p.drawText(QRectF(x - sw, 0, sw, height()), Qt::AlignVCenter, sep);
        x -= sw;
        const qreal lw = fm.horizontalAdvance(link_);
        link_rect_ = QRectF(x - lw, 0, lw, height());
        p.setPen(Tok::t2);
        p.drawText(link_rect_, Qt::AlignVCenter, link_);
        p.drawLine(QPointF(link_rect_.left(), height() / 2.0 + 7), QPointF(link_rect_.right(), height() / 2.0 + 7));
    }
}

void StatusLine::mousePressEvent(QMouseEvent* e) {
    if (!link_.isEmpty() && link_rect_.contains(e->position())) emit linkClicked();
}

// =============================================================================
//  KnobView
// =============================================================================
KnobView::KnobView(QWidget* parent) : QWidget(parent) { setFixedSize(96, 96); }

void KnobView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QPointF c(width() / 2.0, height() / 2.0);
    const qreal r = 22.0;
    auto pt = [&](qreal deg, qreal rr) {
        const qreal t = qDegreesToRadians(deg - 90.0);
        return QPointF(c.x() + rr * std::cos(t), c.y() + rr * std::sin(t));
    };
    // Range arc 7 o'clock .. 5 o'clock.
    const QRectF ring(c.x() - r - 7, c.y() - r - 7, 2 * (r + 7), 2 * (r + 7));
    p.setPen(QPen(Tok::line_strong, 2));
    p.setBrush(Qt::NoBrush);
    // Qt arcs are counter-clockwise from 3 o'clock; convert clock degrees.
    auto qt_angle = [](qreal clock_deg) { return 90.0 - clock_deg; };
    p.drawArc(ring, static_cast<int>(qt_angle(150) * 16), static_cast<int>(300 * 16));
    if (show_ok_) {
        QPen pen(Tok::accent, 3);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawArc(ring, static_cast<int>(qt_angle(ok_to_) * 16), static_cast<int>((ok_to_ - ok_from_) * 16));
    }
    p.setPen(QPen(Tok::t3, 1));
    for (int h = 0; h < 12; ++h) {
        qreal a = h * 30.0;
        if (a > 180) a -= 360;
        if (std::abs(a) > 150) continue;
        p.drawLine(pt(a, r + 11), pt(a, r + 15));
    }
    const QPixmap& knob = part(QStringLiteral("knob"));
    p.drawPixmap(QRectF(c.x() - 26, c.y() - 26, 52, 52), knob, QRectF(knob.rect()));
    QPen ptr(Tok::t1, 2);
    ptr.setCapStyle(Qt::RoundCap);
    p.setPen(ptr);
    p.drawLine(pt(pointer_, 4), pt(pointer_, 15));
    if (turn_down_) {
        QPen ar(Tok::error, 2);
        ar.setCapStyle(Qt::RoundCap);
        p.setPen(ar);
        const QRectF outer(c.x() - r - 24, c.y() - r - 24, 2 * (r + 24), 2 * (r + 24));
        p.drawArc(outer, static_cast<int>(qt_angle(pointer_ - 10) * 16), static_cast<int>(40 * 16));
        p.setBrush(Tok::error);
        p.setPen(Qt::NoPen);
        p.drawEllipse(pt(pointer_ - 50, r + 24), 3, 3);
    }
}

// =============================================================================
//  TargetBar / CorrelationBar
// =============================================================================
TargetBar::TargetBar(QWidget* parent) : QWidget(parent) { setFixedHeight(34); }

void TargetBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal lo = -24.0, hi = -6.0;
    const QRectF bar(kBarPadH, 3, width() - 2 * kBarPadH, 14);
    auto x_of = [&](qreal l) { return bar.left() + (std::clamp(l, lo, hi) - lo) / (hi - lo) * bar.width(); };
    p.setPen(QPen(Tok::line, 1));
    p.setBrush(Tok::off);
    p.drawRoundedRect(bar, 2, 2);
    const qreal b0 = x_of(center_ - tol_), b1 = x_of(center_ + tol_);
    p.setPen(Qt::NoPen);
    p.setBrush(Tok::band);
    p.drawRect(QRectF(b0, bar.top() + 1, b1 - b0, bar.height() - 2));
    if (value_ > -60.0f) {
        p.setBrush(Tok::band_fill);
        p.drawRoundedRect(QRectF(bar.left() + 1, bar.top() + 4, x_of(value_) - bar.left() - 1, bar.height() - 8), 1, 1);
    }
    p.setPen(QPen(Tok::ok, 1));
    p.drawLine(QPointF(b0, bar.top()), QPointF(b0, bar.bottom()));
    p.drawLine(QPointF(b1, bar.top()), QPointF(b1, bar.bottom()));
    if (value_ > -60.0f) {
        p.setPen(Qt::NoPen);
        p.setBrush(Tok::t1);
        p.drawRect(QRectF(x_of(value_) - 1, bar.top() - 3, 2, bar.height() + 6));
    }
    p.setFont(font_scale());
    p.setPen(Tok::t3);
    for (int l : {-24, -18, -12, -6})
        p.drawText(QRectF(x_of(l) - 16, bar.bottom() + 3, 32, 12), Qt::AlignCenter, QString::number(l));
}

CorrelationBar::CorrelationBar(QWidget* parent) : QWidget(parent) { setFixedHeight(32); }

void CorrelationBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF bar(kBarPadH, 3, width() - 2 * kBarPadH, 12);
    p.setPen(QPen(Tok::line, 1));
    p.setBrush(Tok::off);
    p.drawRoundedRect(bar, 2, 2);
    const qreal mid = bar.center().x();
    const qreal v = std::clamp(static_cast<qreal>(value_), -1.0, 1.0);
    const qreal vx = mid + v * bar.width() / 2;
    p.setPen(Qt::NoPen);
    p.setBrush(v >= 0 ? Tok::band_fill : QColor(0x8A, 0x3A, 0x36));
    p.drawRoundedRect(QRectF(std::min(mid, vx), bar.top() + 3, std::abs(vx - mid), bar.height() - 6), 1, 1);
    p.setPen(QPen(Tok::t3, 1));
    p.drawLine(QPointF(mid, bar.top() - 2), QPointF(mid, bar.bottom() + 2));
    p.setPen(Qt::NoPen);
    p.setBrush(Tok::t1);
    p.drawRect(QRectF(vx - 1, bar.top() - 2, 2, bar.height() + 4));
    p.setFont(font_scale());
    p.setPen(Tok::t3);
    p.drawText(QRectF(bar.left() - 12, bar.bottom() + 3, 24, 12), Qt::AlignCenter, QStringLiteral("-1"));
    p.drawText(QRectF(mid - 12, bar.bottom() + 3, 24, 12), Qt::AlignCenter, QStringLiteral("0"));
    p.drawText(QRectF(bar.right() - 12, bar.bottom() + 3, 24, 12), Qt::AlignCenter, QStringLiteral("+1"));
}

// =============================================================================
//  HeaderBar
// =============================================================================
HeaderBar::HeaderBar(QWidget* parent) : QWidget(parent) {
    pin_ = new RackKey(RackKey::Icon::Pin, this);
    pin_->setCheckable(true);
    pin_->setObjectName(QStringLiteral("pin"));
    pin_->setToolTip(QStringLiteral("Keep on top"));
    fold_ = new RackKey(RackKey::Icon::Fold, this);
    fold_->setCheckable(true);
    fold_->setObjectName(QStringLiteral("fold"));
    fold_->setToolTip(QStringLiteral("Mini view"));
    connect(fold_, &RackKey::toggled, this, [this](bool on) { fold_->set_icon(on ? RackKey::Icon::Unfold : RackKey::Icon::Fold); });
    reset_ = new RackKey(RackKey::Icon::Reset, this);
    reset_->setObjectName(QStringLiteral("reset"));
    reset_->setToolTip(QStringLiteral("Reset to factory (keeps calibration)"));
    lang_ = new SegmentedControl({QStringLiteral("JA"), QStringLiteral("EN")}, this);
    lang_->setObjectName(QStringLiteral("lang"));
    lang_->set_text_px(10);
    setFixedHeight(63);
}

QSize HeaderBar::sizeHint() const { return QSize(520, compact_ ? 45 : 63); }

void HeaderBar::set_links(const bool live[5]) {
    bool changed = false;
    for (int i = 0; i < 5; ++i) if (links_[i] != live[i]) { links_[i] = live[i]; changed = true; }
    if (changed) update();
}

void HeaderBar::set_compact(bool compact) {
    compact_ = compact;
    setFixedHeight(compact ? 45 : 63);
    layout_keys();
    update();
}

void HeaderBar::resizeEvent(QResizeEvent*) { layout_keys(); }

void HeaderBar::layout_keys() {
    const int s = compact_ ? 24 : 28, gap = compact_ ? 6 : 8, top = compact_ ? 10 : 14;
    int x = width() - 16;
    x -= s; reset_->setGeometry(x, top, s, s); reset_->setFixedSize(s, s);
    x -= gap + 64; lang_->setGeometry(x, top, 64, s);
    x -= gap + s; fold_->setGeometry(x, top, s, s); fold_->setFixedSize(s, s);
    x -= gap + s; pin_->setGeometry(x, top, s, s); pin_->setFixedSize(s, s);
}

void HeaderBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    const qreal L = 16;
    if (compact_) {
        p.setFont(make_font(13, QFont::Bold, 1.0));
        p.setPen(Tok::t1);
        p.drawText(QPointF(L, 28), QStringLiteral("MODEL 600"));
        p.setPen(QPen(Tok::line, 1));
        p.drawLine(QPointF(L + 96, 14), QPointF(L + 96, 34));
        const QRectF badge(L + 108.5, 13.5, 21, 17);
        p.setPen(QPen(Tok::accent, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(badge, 3, 3);
        p.setFont(make_font(10, QFont::DemiBold));
        p.drawText(badge, Qt::AlignCenter, num_);
        p.setFont(make_font(11, QFont::DemiBold, 1.2));
        p.setPen(Tok::t1);
        p.drawText(QPointF(L + 138, 26.5), name_);
    } else {
        p.setFont(make_font(9, QFont::DemiBold, 2.6));
        p.setPen(Tok::t3);
        p.drawText(QPointF(L, 22), QStringLiteral("SEVENTHWELL"));
        p.setFont(font_title());
        p.setPen(Tok::t1);
        p.drawText(QPointF(L, 41), QStringLiteral("MODEL 600"));
        p.setPen(QPen(Tok::line, 1));
        p.drawLine(QPointF(L + 104, 14), QPointF(L + 104, 44));
        const QRectF badge(L + 120.5, 11.5, 25, 21);
        p.setPen(QPen(Tok::accent, 1));
        p.drawRoundedRect(badge, 3, 3);
        p.setFont(make_font(11, QFont::DemiBold));
        p.drawText(badge, Qt::AlignCenter, num_);
        p.setFont(make_font(12, QFont::DemiBold, 1.3));
        p.setPen(Tok::t1);
        p.drawText(QPointF(L + 156, 26.5), name_);
        // LINK lamps.
        p.setFont(make_font(9, QFont::DemiBold, 1.4));
        p.setPen(Tok::t3);
        p.drawText(QPointF(L + 120, 48.5), QStringLiteral("LINK"));
        qreal x = L + 154;
        for (int i = 0; i < 5; ++i) {
            const QPointF c(x, 45);
            if (links_[i]) {
                QRadialGradient g(c, 6);
                QColor gc = Tok::green; gc.setAlphaF(0.3); g.setColorAt(0, gc); gc.setAlphaF(0); g.setColorAt(1, gc);
                p.setPen(Qt::NoPen); p.setBrush(g); p.drawEllipse(c, 6, 6);
            }
            p.setPen(Qt::NoPen);
            p.setBrush(links_[i] ? Tok::green : Tok::line_strong);
            p.drawEllipse(c, 2.6, 2.6);
            p.setFont(make_font(9, QFont::Medium));
            p.setPen(links_[i] ? Tok::t2 : Tok::t3);
            p.drawText(QPointF(x + 6, 48.5), QStringLiteral("0%1").arg(i + 1));
            x += 30;
        }
        // Update pill under the window keys.
        if (!update_.isEmpty()) {
            const qreal uw = 104;
            update_rect_ = QRectF(width() - 16 - uw + 0.5, 45.5, uw - 1, 13);
            p.setPen(QPen(Tok::accent, 1));
            p.setBrush(Tok::pill_bg);
            p.drawRoundedRect(update_rect_, 6.5, 6.5);
            p.setPen(Qt::NoPen);
            p.setBrush(Tok::accent);
            p.drawEllipse(QPointF(update_rect_.left() + 10, 52), 2.2, 2.2);
            p.setFont(make_font(8, QFont::DemiBold, 0.8));
            p.setPen(Tok::accent);
            p.drawText(update_rect_.adjusted(12, 0, 0, 0), Qt::AlignCenter, QStringLiteral("UPDATE %1").arg(update_));
        } else {
            update_rect_ = QRectF();
        }
    }
    p.setPen(QPen(Tok::line, 1));
    p.drawLine(QPointF(0, height() - 0.5), QPointF(width(), height() - 0.5));
}

void HeaderBar::mousePressEvent(QMouseEvent* e) {
    if (update_rect_.isValid() && update_rect_.contains(e->position())) emit updateClicked();
}

} // namespace sw::model600::ui
