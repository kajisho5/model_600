// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  hw-panel.cpp  [Layer 2]  Hardware-style faceplate widgets (GUI v2).
// =============================================================================
#include "hw-panel.hpp"
#include "ui-theme.hpp"

#include <QContextMenuEvent>
#include <QFile>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace sw::model600::ui::hw {

namespace {

const QString kRoot = QStringLiteral(":/m600/hw/");
constexpr double kPi = 3.14159265358979323846;

QRect centred_rect(QPointF c, double w, double h) {
    return QRect(static_cast<int>(std::lround(c.x() - w / 2.0)), static_cast<int>(std::lround(c.y() - h / 2.0)),
                 static_cast<int>(std::lround(w)), static_cast<int>(std::lround(h)));
}

void smooth(QPainter& p) {
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
}

// Draw a whole pixmap into a logical rectangle (assets are 2x; Qt scales to the screen).
void blit(QPainter& p, const QRectF& target, const QPixmap& pm) {
    if (!pm.isNull()) p.drawPixmap(target, pm, QRectF(pm.rect()));
}

QFont disp_font(double px, int weight = QFont::Bold, double spacing = 0.0) {
    return make_font(std::max(6, static_cast<int>(std::lround(px))), weight, spacing);
}

// 7-segment glyph table: segments a..g lit per character.
const char* segments_for(QChar c) {
    switch (c.unicode()) {
        case '0': return "abcdef";
        case '1': return "bc";
        case '2': return "abged";
        case '3': return "abgcd";
        case '4': return "fgbc";
        case '5': return "afgcd";
        case '6': return "afgedc";
        case '7': return "abc";
        case '8': return "abcdefg";
        case '9': return "abcfgd";
        case '-': return "g";
        default: return "";
    }
}

struct Cell { QChar c; bool dot; };

QVector<Cell> split_cells(const QString& text) {
    QVector<Cell> cells;
    for (QChar ch : text) {
        if (ch == QLatin1Char('.') && !cells.isEmpty()) cells.back().dot = true;
        else cells.push_back({ch, false});
    }
    return cells;
}

QColor zone_on(const QString& z) {
    if (z == QLatin1String("green")) return Disp::green;
    if (z == QLatin1String("red")) return Disp::red;
    return Disp::amber;
}

QColor zone_off(const QString& z) {
    if (z == QLatin1String("green")) return Disp::green_off;
    if (z == QLatin1String("red")) return Disp::red_off;
    return Disp::amber_off;
}

// Soft bloom around a lit shape: a wide translucent stroke under the fill.
void glow_fill(QPainter& p, const QPainterPath& path, const QColor& c, double width) {
    QColor g = c;
    g.setAlphaF(0.28);
    p.strokePath(path, QPen(g, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.fillPath(path, c);
}

} // namespace

// =============================================================================
//  Assets
// =============================================================================
int Strip::nearest(double angle) const {
    int best = 0;
    double d = 1e9;
    for (int i = 0; i < angles.size(); ++i) {
        const double e = std::abs(angles[i] - angle);
        if (e < d) { d = e; best = i; }
    }
    return best;
}

QRectF Strip::source(int frame) const {
    const int c = cols > 0 ? cols : 1;
    return QRectF((frame % c) * frame_px, (frame / c) * frame_px, frame_px, frame_px);
}

const Assets& Assets::get() {
    static const Assets instance;
    return instance;
}

Assets::Assets() {
    QFile f(kRoot + QStringLiteral("hw.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    scale_ = root.value(QStringLiteral("scale")).toDouble(2.0);
    units_ = root.value(QStringLiteral("units")).toObject();
    calibrator_ = root.value(QStringLiteral("calibrator")).toObject();
    const QJsonObject bgs = root.value(QStringLiteral("backgrounds")).toObject();
    for (auto it = bgs.begin(); it != bgs.end(); ++it) bg_files_.insert(it.key(), it.value().toString());
    const QJsonObject sprites = root.value(QStringLiteral("sprites")).toObject();
    for (auto it = sprites.begin(); it != sprites.end(); ++it) {
        const QJsonObject s = it.value().toObject();
        const QString file = s.value(QStringLiteral("file")).toString();
        if (s.contains(QStringLiteral("angles"))) {
            Strip st;
            st.sheet = QPixmap(kRoot + file);
            st.logical = s.value(QStringLiteral("size")).toDouble();
            st.radius = s.value(QStringLiteral("radius")).toDouble();
            st.frame_px = static_cast<int>(std::lround(st.logical * scale_));
            st.cols = s.value(QStringLiteral("cols")).toInt(1);
            for (const QJsonValue v : s.value(QStringLiteral("angles")).toArray()) st.angles.push_back(v.toDouble());
            strips_.insert(it.key(), st);
        } else {
            files_.insert(it.key(), file);
            sizes_.insert(it.key(), QSizeF(s.value(QStringLiteral("w")).toDouble(), s.value(QStringLiteral("h")).toDouble()));
        }
    }
    ok_ = !units_.isEmpty();
}

QJsonObject Assets::unit(const QString& number) const { return units_.value(number).toObject(); }

QPixmap Assets::background(const QString& number) const {
    const QString key = QStringLiteral("bg:") + number;
    auto it = cache_.find(key);
    if (it != cache_.end()) return it.value();
    QPixmap pm(kRoot + bg_files_.value(number));
    cache_.insert(key, pm);
    return pm;
}

const Strip* Assets::strip(const QString& name) const {
    auto it = strips_.find(name);
    return it == strips_.end() ? nullptr : &it.value();
}

QPixmap Assets::sprite(const QString& name) const {
    auto it = cache_.find(name);
    if (it != cache_.end()) return it.value();
    QPixmap pm;
    if (files_.contains(name)) pm = QPixmap(kRoot + files_.value(name));
    cache_.insert(name, pm);
    return pm;
}

QSizeF Assets::sprite_size(const QString& name) const { return sizes_.value(name); }

// =============================================================================
//  PeakHold
// =============================================================================
void PeakHold::feed(float db, qint64 now_ms, float dt_s) {
    if (!(db > -120.0f)) db = -120.0f;
    bar = std::max(db, bar - 20.0f * dt_s);
    if (db >= hold) { hold = db; hold_ms = now_ms; }
    else if (now_ms - hold_ms > 1500) hold = std::max(bar, hold - 20.0f * dt_s);
}

// =============================================================================
//  7-segment text
// =============================================================================
double seg_text_cells(const QString& text) { return split_cells(text).size(); }

void draw_seg_text(QPainter& p, const QString& text, QPointF o, double cw, double ch,
                   const QColor& lit, const QColor& ghost) {
    const QVector<Cell> cells = split_cells(text);
    const double t = std::max(1.2, cw * 0.16);     // Segment thickness.
    const double sw = cw * 0.68;                    // Glyph width.
    const double sk = 0.10;                         // Italic skew.
    auto poly = [&](double x, double y, char name) {
        const double hw = sw - t;
        const double hh = (ch - t) / 2.0;
        const double e = t * 0.12;
        QPolygonF pts;
        if (name == 'a' || name == 'g' || name == 'd') {
            const double yy = name == 'a' ? y : (name == 'g' ? y + hh : y + 2 * hh);
            pts << QPointF(x + t / 2 + e, yy + t / 2) << QPointF(x + t + e, yy) << QPointF(x + hw - e, yy)
                << QPointF(x + hw + t / 2 - e, yy + t / 2) << QPointF(x + hw - e, yy + t) << QPointF(x + t + e, yy + t);
        } else {
            const double xx = (name == 'f' || name == 'e') ? x : x + hw;
            const double yy = (name == 'f' || name == 'b') ? y : y + hh;
            pts << QPointF(xx + t / 2, yy + t / 2 + e) << QPointF(xx + t, yy + t + e) << QPointF(xx + t, yy + hh - e)
                << QPointF(xx + t / 2, yy + hh + t / 2 - e) << QPointF(xx, yy + hh - e) << QPointF(xx, yy + t + e);
        }
        const double base = y + ch;
        for (QPointF& q : pts) q.setX(q.x() + (base - q.y()) * sk);
        return pts;
    };
    QPainterPath ghost_path, lit_path;
    for (int i = 0; i < cells.size(); ++i) {
        const double x = o.x() + i * cw + cw * 0.12;
        const double y = o.y();
        const char* on = segments_for(cells[i].c);
        for (const char s : {'a', 'b', 'c', 'd', 'e', 'f', 'g'}) {
            const QPolygonF pg = poly(x, y, s);
            ghost_path.addPolygon(pg);
            ghost_path.closeSubpath();
            if (std::strchr(on, s) != nullptr) { lit_path.addPolygon(pg); lit_path.closeSubpath(); }
        }
        const double r = t * 0.55;
        const QPointF dc(x + sw + t * 0.6, y + ch - r);
        ghost_path.addEllipse(dc, r, r);
        if (cells[i].dot) lit_path.addEllipse(dc, r, r);
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (ghost.isValid()) p.fillPath(ghost_path, ghost);
    glow_fill(p, lit_path, lit, t * 0.9);
    p.restore();
}

// =============================================================================
//  Panel
// =============================================================================
Panel::Panel(const QString& unit_number, QWidget* parent) : QWidget(parent), number_(unit_number) {
    const Assets& a = Assets::get();
    layout_ = a.unit(unit_number);
    const QJsonObject panel = layout_.value(QStringLiteral("panel")).toObject();
    size_ = QSize(panel.value(QStringLiteral("w")).toInt(1100), panel.value(QStringLiteral("h")).toInt(480));
    setFixedSize(size_);
    bg_ = a.background(unit_number);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void Panel::set_status(Level level, const QString& text, const QString& right) {
    if (level == level_ && text == status_ && right == right_) return;
    level_ = level;
    status_ = text;
    right_ = right;
    update(QRect(0, height() - 34, width(), 34));
}

void Panel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    smooth(p);
    if (bg_.isNull()) p.fillRect(rect(), QColor(0x1B, 0x1D, 0x21));
    else blit(p, QRectF(rect()), bg_);
    // Live status where the printed serial line sits on the sample renders.
    const double y = height() - 18.0;
    const QFont f = make_font(9, QFont::DemiBold, 1.4);
    p.setFont(f);
    const QFontMetricsF fm(f);
    QColor c = Disp::ink_dim;
    QColor dot = QColor(0x57, 0xE0, 0x8A);
    switch (level_) {
        case Level::Warn: c = QColor(0xF0, 0xA3, 0x3A); dot = c; break;
        case Level::Error: c = QColor(0xFF, 0x5A, 0x4A); dot = c; break;
        case Level::Info: c = Disp::ink; dot = QColor(0x8F, 0xC3, 0xF0); break;
        default: break;
    }
    const double tw = fm.horizontalAdvance(status_);
    const double x0 = width() / 2.0 - tw / 2.0;
    status_rect_ = QRectF(x0 - 14, y - 12, tw + 18, 18);
    if (!status_.isEmpty()) {
        p.setPen(Qt::NoPen);
        p.setBrush(dot);
        p.drawEllipse(QPointF(x0 - 8, y - 3.5), 2.6, 2.6);
        p.setPen(c);
        p.drawText(QPointF(x0, y), status_);
    }
    if (!right_.isEmpty()) {
        p.setFont(make_font(8, QFont::DemiBold, 1.0));
        p.setPen(Disp::ink_dim);
        const double rw = QFontMetricsF(p.font()).horizontalAdvance(right_);
        p.drawText(QPointF(width() - 56.0 - rw, y), right_);
    }
}

void Panel::contextMenuEvent(QContextMenuEvent* e) {
    emit contextRequested(e->globalPos());
    e->accept();
}

void Panel::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && status_rect_.contains(e->position())) emit statusClicked();
    QWidget::mousePressEvent(e);
}

// =============================================================================
//  Knob
// =============================================================================
Knob::Knob(const QString& strip, QPointF centre, double radius, double lo, double hi, double def, QWidget* parent)
    : QWidget(parent), strip_(Assets::get().strip(strip)), radius_(radius), lo_(lo), hi_(hi), def_(def),
      value_(def), step_((hi - lo) / 48.0) {
    const double side = strip_ ? strip_->logical * radius / strip_->radius : 2 * radius + 16;
    setGeometry(centred_rect(centre, side, side));
    const int r = static_cast<int>(std::lround(radius + 2));
    setMask(QRegion(width() / 2 - r, height() / 2 - r, 2 * r, 2 * r, QRegion::Ellipse));
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::SizeVerCursor);
    fmt_ = [](double v) { return QString::number(v, 'f', 1); };
    update_tip();
}

double Knob::angle() const { return -135.0 + 270.0 * (value_ - lo_) / (hi_ - lo_); }

void Knob::set_value(double v, bool notify) {
    v = std::clamp(std::round(v * 10.0) / 10.0, lo_, hi_);
    if (std::abs(v - value_) < 1e-9) return;
    value_ = v;
    update();
    update_tip();
    if (notify) emit valueChanged(value_);
}

void Knob::update_tip() { setToolTip(label_.isEmpty() ? fmt_(value_) : label_ + QStringLiteral("  ") + fmt_(value_)); }

void Knob::show_value(const QPoint& global) {
    QToolTip::showText(global + QPoint(14, 10), label_.isEmpty() ? fmt_(value_) : label_ + QStringLiteral("  ") + fmt_(value_), this);
}

void Knob::paintEvent(QPaintEvent*) {
    QPainter p(this);
    smooth(p);
    if (strip_ && !strip_->sheet.isNull()) {
        p.drawPixmap(QRectF(rect()), strip_->sheet, strip_->source(strip_->nearest(angle())));
    } else {   // Fallback if assets are missing: a plain disc with an index.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x15, 0x16, 0x18));
        p.drawEllipse(QRectF(rect()).center(), radius_, radius_);
        p.setPen(QPen(Disp::ink, 2));
        const double a = (angle() - 90.0) * kPi / 180.0;
        const QPointF c = QRectF(rect()).center();
        p.drawLine(c + QPointF(std::cos(a), std::sin(a)) * radius_ * 0.4, c + QPointF(std::cos(a), std::sin(a)) * radius_ * 0.9);
    }
    if (hasFocus() && kb_focus_) {
        p.setPen(QPen(QColor(0xF0, 0xA3, 0x3A, 160), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(rect()).center(), radius_ + 1, radius_ + 1);
    }
}

void Knob::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) { e->ignore(); return; }
    kb_focus_ = false;
    dragging_ = true;
    drag_y_ = static_cast<int>(e->globalPosition().y());
    drag_value_ = value_;
    show_value(e->globalPosition().toPoint());
}

void Knob::mouseMoveEvent(QMouseEvent* e) {
    if (!dragging_) return;
    const double dy = drag_y_ - e->globalPosition().y();
    const double fine = (e->modifiers() & Qt::ShiftModifier) ? 0.2 : 1.0;
    set_value(drag_value_ + dy / 220.0 * (hi_ - lo_) * fine, true);
    if (e->modifiers() & Qt::ShiftModifier) {   // Re-anchor so switching to fine mode never jumps.
        drag_y_ = static_cast<int>(e->globalPosition().y());
        drag_value_ = value_;
    }
    show_value(e->globalPosition().toPoint());
}

void Knob::mouseReleaseEvent(QMouseEvent*) {
    if (dragging_) { dragging_ = false; emit released(); }
}

void Knob::mouseDoubleClickEvent(QMouseEvent* e) {
    set_value(def_, true);
    show_value(e->globalPosition().toPoint());
    emit released();
}

void Knob::wheelEvent(QWheelEvent* e) {
    const double notches = e->angleDelta().y() / 120.0;
    const double fine = (e->modifiers() & Qt::ShiftModifier) ? 0.25 : 1.0;
    set_value(value_ + notches * step_ * fine, true);
    show_value(e->globalPosition().toPoint());
    emit released();
    e->accept();
}

void Knob::keyPressEvent(QKeyEvent* e) {
    kb_focus_ = true;
    double v = value_;
    switch (e->key()) {
        case Qt::Key_Up: case Qt::Key_Right: v += step_; break;
        case Qt::Key_Down: case Qt::Key_Left: v -= step_; break;
        case Qt::Key_PageUp: v += step_ * 4; break;
        case Qt::Key_PageDown: v -= step_ * 4; break;
        case Qt::Key_Home: v = lo_; break;
        case Qt::Key_End: v = hi_; break;
        default: QWidget::keyPressEvent(e); return;
    }
    set_value(v, true);
    emit released();
    update();
}

// =============================================================================
//  Selector
// =============================================================================
Selector::Selector(QPointF centre, double radius, const QVector<double>& angles, const QStringList& names, QWidget* parent)
    : QWidget(parent), strip_(Assets::get().strip(QStringLiteral("selector"))), radius_(radius), angles_(angles),
      names_(names) {
    const double side = strip_ ? strip_->logical * radius / strip_->radius : 3.2 * radius;
    setGeometry(centred_rect(centre, side, side));
    const int r = static_cast<int>(std::lround(radius * 1.2));
    setMask(QRegion(width() / 2 - r, height() / 2 - r, 2 * r, 2 * r, QRegion::Ellipse));
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    update_tip();
}

void Selector::set_index(int i, bool notify) {
    i = std::clamp(i, 0, static_cast<int>(angles_.size()) - 1);
    if (i == index_) return;
    index_ = i;
    update();
    update_tip();
    if (notify) emit indexChanged(index_);
}

void Selector::update_tip() {
    if (index_ >= 0 && index_ < names_.size()) setToolTip(names_.at(index_));
}

void Selector::paintEvent(QPaintEvent*) {
    QPainter p(this);
    smooth(p);
    const double a = angles_.isEmpty() ? 0.0 : angles_.at(index_);
    if (strip_ && !strip_->sheet.isNull()) {
        p.drawPixmap(QRectF(rect()), strip_->sheet, strip_->source(strip_->nearest(a)));
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x0A, 0x0A, 0x0B));
        p.drawEllipse(QRectF(rect()).center(), radius_, radius_);
    }
}

void Selector::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) { e->ignore(); return; }
    // Click the left half to turn anticlockwise, the right half to turn clockwise.
    set_index(index_ + (e->position().x() < width() / 2.0 ? -1 : 1), true);
}

void Selector::contextMenuEvent(QContextMenuEvent* e) { e->ignore(); }

void Selector::wheelEvent(QWheelEvent* e) {
    set_index(index_ + (e->angleDelta().y() > 0 ? 1 : -1), true);
    e->accept();
}

void Selector::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
        case Qt::Key_Right: case Qt::Key_Up: set_index(index_ + 1, true); break;
        case Qt::Key_Left: case Qt::Key_Down: set_index(index_ - 1, true); break;
        default: QWidget::keyPressEvent(e);
    }
}

// =============================================================================
//  Toggle
// =============================================================================
Toggle::Toggle(QPointF centre, bool horizontal, QWidget* parent) : QAbstractButton(parent), horizontal_(horizontal) {
    setCheckable(true);
    setGeometry(centred_rect(centre, 64, 64));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

bool Toggle::hitButton(const QPoint& pos) const {
    const QPointF d = QPointF(pos) - QRectF(rect()).center();
    return std::hypot(d.x(), d.y()) <= 22.0;
}

void Toggle::paintEvent(QPaintEvent*) {
    QPainter p(this);
    smooth(p);
    const QString name = horizontal_ ? (isChecked() ? QStringLiteral("toggle_right") : QStringLiteral("toggle_left"))
                                     : (isChecked() ? QStringLiteral("toggle_up") : QStringLiteral("toggle_down"));
    blit(p, QRectF(rect()), Assets::get().sprite(name));
}

// =============================================================================
//  Button
// =============================================================================
Button::Button(QPointF centre, int w, int h, const QString& color, QWidget* parent)
    : QAbstractButton(parent), color_(color), w_(w), h_(h) {
    setGeometry(centred_rect(centre, w + 10, h + 10));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void Button::paintEvent(QPaintEvent*) {
    QPainter p(this);
    smooth(p);
    const QString name = QStringLiteral("cap_%1_%2%3_%4x%5")
                             .arg(color_, lit_ ? QStringLiteral("on") : QStringLiteral("off"),
                                  isDown() ? QStringLiteral("_down") : QString())
                             .arg(w_)
                             .arg(h_);
    blit(p, QRectF(rect()), Assets::get().sprite(name));
    if (hasFocus()) {
        p.setPen(QPen(QColor(0xF0, 0xA3, 0x3A, 140), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(3, 3, -3, -3), 3, 3);
    }
}

// =============================================================================
//  Lamp
// =============================================================================
Lamp::Lamp(QPointF centre, const QString& color, QWidget* parent) : QWidget(parent), color_(color) {
    setGeometry(centred_rect(centre, 28, 28));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void Lamp::paintEvent(QPaintEvent*) {
    QPainter p(this);
    smooth(p);
    blit(p, QRectF(rect()), Assets::get().sprite(QStringLiteral("lamp_%1_%2").arg(color_, lit_ ? QStringLiteral("on") : QStringLiteral("off"))));
}

// =============================================================================
//  Led
// =============================================================================
Led::Led(QPointF centre, QWidget* parent) : QWidget(parent) {
    setGeometry(centred_rect(centre, 16, 16));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void Led::paintEvent(QPaintEvent*) {
    QPainter p(this);
    smooth(p);
    const QString name = state_ == State::On ? QStringLiteral("led_on")
                         : state_ == State::Idle ? QStringLiteral("led_idle") : QStringLiteral("led_off");
    blit(p, QRectF(rect()), Assets::get().sprite(name));
}

// =============================================================================
//  LinkRow
// =============================================================================
LinkRow::LinkRow(double x, double y, double dx, QWidget* parent) : QWidget(parent), x_(x), y_(y), dx_(dx) {
    setGeometry(QRect(static_cast<int>(x - 12), static_cast<int>(y - 18), static_cast<int>(dx * 4 + 36), 30));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    states_.fill(State::Off);
    counts_.fill(0);
}

void LinkRow::set(const std::array<State, 5>& states, const std::array<int, 5>& counts) {
    if (states == states_ && counts == counts_) return;
    states_ = states;
    counts_ = counts;
    update();
}

void LinkRow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    smooth(p);
    const Assets& a = Assets::get();
    const QPointF org(x_ - geometry().x(), y_ - geometry().y());
    p.setFont(make_font(8, QFont::Bold, 0.2));
    for (int i = 0; i < 5; ++i) {
        const QPointF c = org + QPointF(i * dx_, 0);
        const QString name = states_[static_cast<std::size_t>(i)] == State::On   ? QStringLiteral("led_on")
                             : states_[static_cast<std::size_t>(i)] == State::Idle ? QStringLiteral("led_idle")
                                                                                   : QStringLiteral("led_off");
        blit(p, QRectF(c.x() - 8, c.y() - 8, 16, 16), a.sprite(name));
        const int n = counts_[static_cast<std::size_t>(i)];
        if (n > 1) {
            p.setPen(QColor(0xF0, 0xA3, 0x3A));
            p.drawText(QPointF(c.x() + 6, c.y() - 7), QStringLiteral("×%1").arg(n));
        }
    }
}

// =============================================================================
//  DigitalMeter
// =============================================================================
DigitalMeter::DigitalMeter(const QRectF& window, Kind kind, QWidget* parent) : QWidget(parent), kind_(kind) {
    setGeometry(window.toAlignedRect());
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setCursor(Qt::PointingHandCursor);
}

void DigitalMeter::set_values(double bar, double hold, const QString& text, const QString& label,
                              const QString& unit, const QString& aux) {
    if (std::abs(bar - bar_) < 0.05 && std::abs(hold - hold_) < 0.05 && text == text_ && label == label_ &&
        unit == unit_ && aux == aux_)
        return;
    bar_ = bar; hold_ = hold; text_ = text; label_ = label; unit_ = unit; aux_ = aux;
    update();
}

void DigitalMeter::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) emit clicked();
    else e->ignore();
}

void DigitalMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    smooth(p);
    const double W = width(), H = height();
    p.fillRect(rect(), Disp::bg);
    const double pad = std::max(7.0, H * 0.07);
    const double fs = std::max(6.0, H * 0.078);

    // Top line: what the number is (left) and the auxiliary value (right).
    const QFont lf = disp_font(fs, QFont::Bold, fs * 0.14);
    p.setFont(lf);
    p.setPen(Disp::dim);
    p.drawText(QPointF(pad, pad + fs * 0.75), label_);
    if (!aux_.isEmpty()) {
        const double aw = QFontMetricsF(lf).horizontalAdvance(aux_);
        p.drawText(QPointF(W - pad - aw, pad + fs * 0.75), aux_);
    }

    // Big value + unit, centred as a group over a fixed 4-cell field.
    const double ch = H * 0.36;
    const double cw = ch * 0.62;
    const QFont uf = disp_font(H * 0.095, QFont::Bold, H * 0.0095);
    const double uw = unit_.isEmpty() ? 0.0 : QFontMetricsF(uf).horizontalAdvance(unit_) + H * 0.095 * 0.5;
    const int cells = 4;
    const double group = cells * cw + uw;
    const double x0 = (W - group) / 2.0;
    const double y0 = pad + fs + H * 0.07;
    QString shown = text_;
    while (seg_text_cells(shown) < cells) shown.prepend(QLatin1Char(' '));
    draw_seg_text(p, shown, QPointF(x0, y0), cw, ch, Disp::lit, Disp::ghost);
    if (!unit_.isEmpty()) {
        p.setFont(uf);
        p.setPen(Disp::lit);
        p.drawText(QPointF(x0 + cells * cw + H * 0.095 * 0.5, y0 + ch), unit_);
    }

    // Segment bar.
    double lo = -60, hi = 0;
    int n = 48;
    switch (kind_) {
        case Kind::Ride: lo = -12; hi = 12; n = 24; break;
        case Kind::Phase: lo = -1; hi = 1; n = 20; break;
        case Kind::Gr: lo = -24; hi = 0; n = 24; break;
        default: break;
    }
    const double bx0 = pad, bx1 = W - pad;
    const double by = H * 0.70;
    const double bh = std::max(3.0, H * 0.085);
    const double pitch = (bx1 - bx0) / n;
    const double val = std::clamp(bar_, lo, hi);
    for (int i = 0; i < n; ++i) {
        const double a = lo + (hi - lo) * i / n, b = lo + (hi - lo) * (i + 1) / n;
        const double c = (a + b) / 2.0;
        bool lit = false;
        QString zone;
        switch (kind_) {
            case Kind::Dbfs:
                // OBS defaults (frontend/components/VolumeMeter.cpp): warning -20 dB, error -9 dB.
                zone = c < -20 ? QStringLiteral("green") : (c < -9 ? QStringLiteral("amber") : QStringLiteral("red"));
                lit = c <= val || (hold_ > lo && a <= hold_ && hold_ < b);
                break;
            case Kind::Gr: {
                // Gain reduction grows to the right from 0 dB at the left edge.
                const double g = hi - (c - lo);
                zone = g > -6 ? QStringLiteral("amber") : QStringLiteral("red");
                lit = g >= val && val < -0.05;
                break;
            }
            case Kind::Ride:
                zone = c < 0 ? QStringLiteral("red") : QStringLiteral("amber");
                lit = (0 <= c && c <= val) || (val <= c && c <= 0);
                break;
            case Kind::Phase:
                zone = c < 0 ? QStringLiteral("red") : QStringLiteral("green");
                lit = (0 <= c && c <= val) || (val <= c && c <= 0);
                break;
        }
        const QRectF seg(bx0 + i * pitch + pitch * 0.12, by, pitch * 0.76, bh);
        QPainterPath path;
        path.addRoundedRect(seg, pitch * 0.12, pitch * 0.12);
        if (lit) glow_fill(p, path, zone_on(zone), std::max(1.5, pitch * 0.5));
        else p.fillPath(path, zone_off(zone));
    }
    // Scale under the bar.
    struct Mark { double v; const char* s; };
    static const Mark dbfs[] = {{-60, "-60"}, {-40, "-40"}, {-30, "-30"}, {-20, "-20"}, {-9, "-9"}, {0, "0"}};
    static const Mark ride[] = {{-12, "-12"}, {-6, "-6"}, {0, "0"}, {6, "+6"}, {12, "+12"}};
    static const Mark phase[] = {{-1, "-1"}, {0, "0"}, {1, "+1"}};
    static const Mark gr[] = {{-24, "-24"}, {-18, "-18"}, {-12, "-12"}, {-6, "-6"}, {0, "0"}};
    const Mark* marks = dbfs;
    std::size_t count = std::size(dbfs);
    if (kind_ == Kind::Ride) { marks = ride; count = std::size(ride); }
    else if (kind_ == Kind::Phase) { marks = phase; count = std::size(phase); }
    else if (kind_ == Kind::Gr) { marks = gr; count = std::size(gr); }
    const QFont sf = disp_font(fs * 0.92, QFont::Bold);
    p.setFont(sf);
    p.setPen(Disp::dim);
    const QFontMetricsF sm(sf);
    const double sy = by + bh + fs * 1.25;
    for (std::size_t i = 0; i < count; ++i) {
        // GR runs right-to-left in value (0 at the left edge).
        const double pos = kind_ == Kind::Gr ? (hi - marks[i].v) / (hi - lo) : (marks[i].v - lo) / (hi - lo);
        const double x = bx0 + pos * (bx1 - bx0);
        const QString s = QString::fromLatin1(marks[i].s);
        const double w = sm.horizontalAdvance(s);
        double tx = x - w / 2.0;
        if (pos <= 0.0001) tx = x;
        else if (pos >= 0.9999) tx = x - w;
        p.drawText(QPointF(tx, sy), s);
    }
    // Cover glass: a dark lip under the bezel and a faint sheen.
    QLinearGradient lip(0, 0, 0, H * 0.25);
    lip.setColorAt(0, QColor(0, 0, 0, 140));
    lip.setColorAt(1, QColor(0, 0, 0, 0));
    p.fillRect(QRectF(0, 0, W, H * 0.25), lip);
    QLinearGradient sheen(0, 0, W, H);
    sheen.setColorAt(0.0, QColor(255, 255, 255, 14));
    sheen.setColorAt(0.45, QColor(255, 255, 255, 4));
    sheen.setColorAt(0.46, QColor(255, 255, 255, 0));
    p.fillRect(rect(), sheen);
}

// =============================================================================
//  SegDisplay
// =============================================================================
SegDisplay::SegDisplay(const QRectF& window, int cells, QWidget* parent) : QWidget(parent), cells_(cells) {
    setGeometry(window.toAlignedRect());
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void SegDisplay::set_text(const QString& t, const QColor& c) {
    if (t == text_ && c == color_) return;
    text_ = t;
    color_ = c;
    update();
}

void SegDisplay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    smooth(p);
    p.fillRect(rect(), Disp::bg);
    QString shown = text_;
    while (seg_text_cells(shown) < cells_) shown.prepend(QLatin1Char(' '));
    const double cw = (width() - 16.0) / cells_;
    draw_seg_text(p, shown, QPointF(8, 9), cw, height() - 18.0, color_.isValid() ? color_ : Disp::lit, Disp::ghost);
    QLinearGradient lip(0, 0, 0, height() * 0.3);
    lip.setColorAt(0, QColor(0, 0, 0, 130));
    lip.setColorAt(1, QColor(0, 0, 0, 0));
    p.fillRect(QRectF(0, 0, width(), height() * 0.3), lip);
}

// =============================================================================
//  Ladder
// =============================================================================
Ladder::Ladder(QPointF centre, int n, const QVector<QPair<double, QString>>& zones, QWidget* parent)
    : QWidget(parent), n_(n), zones_(zones) {
    setGeometry(centred_rect(centre, n * 16 + 4, 22));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void Ladder::paintEvent(QPaintEvent*) {
    QPainter p(this);
    smooth(p);
    p.fillRect(rect(), QColor(0x05, 0x05, 0x06));
    for (int i = 0; i < n_; ++i) {
        const double frac = n_ > 1 ? static_cast<double>(i) / (n_ - 1) : 0.0;
        QString zone = zones_.isEmpty() ? QStringLiteral("amber") : zones_.front().second;
        for (const auto& z : zones_) if (frac >= z.first) zone = z.second;
        const QRectF seg(2 + i * 16 + 2.5, 3, 11, 16);
        QPainterPath path;
        path.addRoundedRect(seg, 1.5, 1.5);
        if (i < lit_) glow_fill(p, path, zone_on(zone), 4.0);
        else p.fillPath(path, zone_off(zone));
    }
}

// =============================================================================
//  MiniStrip
// =============================================================================
MiniStrip::MiniStrip(QWidget* parent) : QWidget(parent) {
    setFixedHeight(76);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void MiniStrip::set(double bar_db, double hold_db, const QVector<Readout>& readouts) {
    if (std::abs(bar_db - bar_) < 0.05 && std::abs(hold_db - hold_) < 0.05 && readouts.size() == readouts_.size()) {
        bool same = true;
        for (int i = 0; i < readouts.size(); ++i)
            same = same && readouts[i].value == readouts_[i].value && readouts[i].label == readouts_[i].label;
        if (same) return;
    }
    bar_ = bar_db;
    hold_ = hold_db;
    readouts_ = readouts;
    update();
}

void MiniStrip::paintEvent(QPaintEvent*) {
    QPainter p(this);
    smooth(p);
    // Dark anodised strip with a bright top edge, like a 1U panel.
    QLinearGradient g(0, 0, 0, height());
    g.setColorAt(0, QColor(0x22, 0x24, 0x28));
    g.setColorAt(1, QColor(0x14, 0x15, 0x18));
    p.fillRect(rect(), g);
    p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    p.drawLine(QPointF(0, 0.5), QPointF(width(), 0.5));

    const double cell_w = 92.0;
    const double right = width() - 24.0 - readouts_.size() * cell_w;
    // Peak bar window.
    const QRectF win(24, 14, std::max(120.0, right - 40.0), 30);
    p.fillRect(win, Disp::bg);
    const int n = 96;
    const double pitch = (win.width() - 12) / n;
    for (int i = 0; i < n; ++i) {
        const double a = -60.0 + 60.0 * i / n, b = -60.0 + 60.0 * (i + 1) / n, c = (a + b) / 2.0;
        const QString zone = c < -20 ? QStringLiteral("green") : (c < -9 ? QStringLiteral("amber") : QStringLiteral("red"));
        const bool lit = c <= bar_ || (hold_ > -60.0 && a <= hold_ && hold_ < b);
        const QRectF seg(win.x() + 6 + i * pitch + pitch * 0.15, win.y() + 7, pitch * 0.7, win.height() - 14);
        p.fillRect(seg, lit ? zone_on(zone) : zone_off(zone));
    }
    p.setFont(make_font(9, QFont::Bold));
    p.setPen(Disp::ink_dim);
    struct Mark { double v; const char* s; };
    static const Mark marks[] = {{-60, "-60"}, {-40, "-40"}, {-30, "-30"}, {-20, "-20"}, {-9, "-9"}, {0, "0 dBFS"}};
    const QFontMetricsF fm(p.font());
    for (const Mark& m : marks) {
        const double x = win.x() + 6 + (m.v + 60.0) / 60.0 * (win.width() - 12);
        const QString t = QString::fromLatin1(m.s);
        const double w = fm.horizontalAdvance(t);
        p.drawText(QPointF(m.v <= -60 ? x : (m.v >= 0 ? x - w : x - w / 2), win.bottom() + 14), t);
    }
    // Read-outs.
    for (int i = 0; i < readouts_.size(); ++i) {
        const double x = right + i * cell_w;
        p.setFont(make_font(9, QFont::Bold, 1.2));
        p.setPen(Disp::ink_dim);
        p.drawText(QPointF(x + 6, 22), readouts_[i].label);
        const QRectF w(x + 4, 28, cell_w - 12, 36);
        p.fillRect(w, Disp::bg);
        QString shown = readouts_[i].value;
        while (seg_text_cells(shown) < 4) shown.prepend(QLatin1Char(' '));
        draw_seg_text(p, shown, QPointF(w.x() + 4, w.y() + 5), (w.width() - 8) / 4.0, w.height() - 10, Disp::lit, Disp::ghost);
    }
}

} // namespace sw::model600::ui::hw
