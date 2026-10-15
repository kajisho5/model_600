// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  ui-widgets.hpp  [Layer 2]  Custom-painted widgets of the approved design.
//
//  No Qt style sheets and no emoji: every control is painted from the tokens in
//  ui-theme.hpp, and physical parts (keys, fader caps, LEDs, knob, screws) are
//  Blender renders loaded through ui::part().
// =============================================================================
#ifndef SEVENTHWELL_MODEL600_UI_WIDGETS_HPP
#define SEVENTHWELL_MODEL600_UI_WIDGETS_HPP

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QColor>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QPainter;
class QFocusEvent;

namespace sw::model600::ui {

// Draws `text` with every digit in an equal-width cell (tabular figures) so
// changing values never jiggle, independent of the Qt version's OpenType support.
void draw_tabular(QPainter& p, const QRectF& r, const QString& text, Qt::Alignment align);
qreal tabular_width(const QFont& f, const QString& text);

// ---- Key -------------------------------------------------------------------------
class RackKey : public QAbstractButton {
    Q_OBJECT
public:
    enum class Style { Default, Danger, Primary };
    enum class Icon { None, Pin, Fold, Unfold, Reset };

    explicit RackKey(const QString& text, QWidget* parent = nullptr);
    RackKey(Icon icon, QWidget* parent = nullptr);

    void set_style(Style s) { style_ = s; update(); }
    // A toggle/momentary key shows an LED; `led` = colour when lit.
    void set_led(const QString& part_name) { led_ = part_name; update(); }
    void set_icon(Icon i) { icon_ = i; update(); }
    // Light the LED from outside (e.g. cough mute triggered by a hotkey).
    void set_lit(bool on) { if (lit_ != on) { lit_ = on; update(); } }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override { hover_ = true; update(); }
    void leaveEvent(QEvent*) override { hover_ = false; update(); }
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;

private:
    bool kb_focus_ = false;   // Focus ring only for keyboard (Tab) focus.
    Style style_ = Style::Default;
    Icon icon_ = Icon::None;
    QString led_;            // e.g. "led_amber"; empty = no LED.
    bool hover_ = false;
    bool lit_ = false;
};

// ---- Segmented control (replaces combo boxes) ------------------------------------
class SegmentedControl : public QWidget {
    Q_OBJECT
public:
    explicit SegmentedControl(const QStringList& options, QWidget* parent = nullptr);
    void set_options(const QStringList& options);
    void set_current(int i);
    int current() const noexcept { return current_; }
    void set_text_px(int px) { text_px_ = px; update(); }
    QSize sizeHint() const override;
signals:
    void currentChanged(int index);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
private:
    bool kb_focus_ = false;
    QStringList options_;
    int current_ = 0;
    int text_px_ = 11;
};

// ---- Readout box: LABEL / value unit ------------------------------------------
class Readout : public QWidget {
    Q_OBJECT
public:
    explicit Readout(const QString& label, const QString& unit = QString(), bool big = false,
                     QWidget* parent = nullptr);
    void set_value(const QString& v, const QColor& color = QColor());
    void set_label(const QString& l) { label_ = l; update(); }
    void set_unit(const QString& u) { unit_ = u; update(); }
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString label_, unit_, value_ = QStringLiteral("-inf");
    QColor color_;
    bool big_ = false;
};

// ---- Small value box (peak / fader value). Click = clicked() -------------------
class ValueBox : public QWidget {
    Q_OBJECT
public:
    explicit ValueBox(QWidget* parent = nullptr);
    void set_text(const QString& t, const QColor& c = QColor());
    QSize sizeHint() const override { return QSize(56, 20); }
signals:
    void clicked();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override { emit clicked(); }
private:
    QString text_;
    QColor color_;
};

// ---- Meters -----------------------------------------------------------------
class LedMeter : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Level, GainReduction };
    LedMeter(Kind kind, Qt::Orientation o = Qt::Vertical, QWidget* parent = nullptr);
    // Level: peak / rms / hold in dBFS. GR: gr in dB (<= 0), 0..-24 scale.
    void set_level(float peak_db, float rms_db, float hold_db);
    void set_gr(float gr_db);
    void set_scale_visible(bool on) { scale_ = on; updateGeometry(); update(); }
    void reset_hold();
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
signals:
    void clicked();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override { reset_hold(); emit clicked(); }
private:
    QRectF bar_rect() const;
    Kind kind_;
    Qt::Orientation orient_;
    bool scale_ = false;               // Horizontal only: tick labels under the bar.
    float peak_ = -120.0f, rms_ = -120.0f, hold_ = -120.0f, gr_ = 0.0f;
    float shown_peak_ = -120.0f, shown_rms_ = -120.0f;   // After visual ballistics.
    QElapsedTimer clock_;
};

// dB labels drawn next to a vertical meter, aligned to its bar.
class MeterScale : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Level, GainReduction };
    explicit MeterScale(Kind kind, QWidget* parent = nullptr);
    void set_gr_span(qreal frac) { gr_span_ = frac; update(); }   // GR meter height fraction.
    QSize sizeHint() const override { return QSize(28, 160); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    Kind kind_;
    qreal gr_span_ = 1.0;
};

// ---- Fader (Blender cap, dB scale, Ctrl+wheel 0.1 dB, double-click 0 dB) --------
class RackFader : public QAbstractSlider {
    Q_OBJECT
public:
    enum class Law { Console, Linear };
    RackFader(Qt::Orientation o, float min_db, float max_db, Law law = Law::Linear,
              QWidget* parent = nullptr);
    void set_db(float db);
    float db() const;
    void set_marks(const QVector<float>& marks) { marks_ = marks; update(); }
    void set_default_db(float db) { default_db_ = db; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
signals:
    void dbChanged(float db);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
private:
    bool kb_focus_ = false;
    qreal pos_of(float db) const;      // 0..1 along the travel.
    float db_at(qreal pos) const;
    QRectF track_rect() const;
    void set_from_point(const QPointF& pt);
    float min_db_, max_db_, default_db_ = 0.0f;
    Law law_;
    QVector<float> marks_;
};

// ---- Status line ---------------------------------------------------------------
class StatusLine : public QWidget {
    Q_OBJECT
public:
    enum class Level { Ok, Warn, Error, Info };
    explicit StatusLine(QWidget* parent = nullptr);
    void set_message(Level level, const QString& msg);
    void set_right(const QString& text) { right_ = text; update(); }
    void set_link_text(const QString& link) { link_ = link; update(); }   // Clickable item.
    QSize sizeHint() const override { return QSize(400, 30); }
signals:
    void linkClicked();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
private:
    Level level_ = Level::Ok;
    QString msg_, right_, link_;
    QRectF link_rect_;
};

// ---- Gain knob guidance (Blender knob body + painted pointer / arcs) -----------
class KnobView : public QWidget {
    Q_OBJECT
public:
    explicit KnobView(QWidget* parent = nullptr);
    // Angles in degrees, 0 = 12 o'clock, clockwise positive.
    void set_pointer(qreal deg) { pointer_ = deg; update(); }
    void set_ok_range(qreal from, qreal to) { ok_from_ = from; ok_to_ = to; show_ok_ = true; update(); }
    void set_turn_down_hint(bool on) { turn_down_ = on; update(); }
    QSize sizeHint() const override { return QSize(96, 96); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    qreal pointer_ = 45, ok_from_ = 30, ok_to_ = 60;
    bool show_ok_ = false, turn_down_ = false;
};

// ---- Loudness target band (-24..-6 LUFS) --------------------------------------
class TargetBar : public QWidget {
    Q_OBJECT
public:
    explicit TargetBar(QWidget* parent = nullptr);
    void set_value(float lufs) { value_ = lufs; update(); }
    void set_target(float center, float tolerance) { center_ = center; tol_ = tolerance; update(); }
    QSize sizeHint() const override { return QSize(300, 34); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    float value_ = -120.0f, center_ = -12.0f, tol_ = 1.5f;
};

// ---- Phase correlation (-1..+1) ------------------------------------------------
class CorrelationBar : public QWidget {
    Q_OBJECT
public:
    explicit CorrelationBar(QWidget* parent = nullptr);
    void set_value(float c) { value_ = c; update(); }
    QSize sizeHint() const override { return QSize(300, 32); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    float value_ = 1.0f;
};

// ---- Section label (uppercase, tracked) ---------------------------------------
class SectionLabel : public QWidget {
    Q_OBJECT
public:
    explicit SectionLabel(const QString& text, QWidget* parent = nullptr);
    void set_text(const QString& t) { text_ = t; updateGeometry(); update(); }
    void set_centered(bool on) { centered_ = on; update(); }
    void set_right(const QString& t, const QColor& c, bool led = false) { right_ = t; right_color_ = c; right_led_ = led; update(); }
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString text_, right_;
    QColor right_color_;
    bool right_led_ = false;
    bool centered_ = false;
};

// ---- Rack header (wordmark, unit badge, LINK lamps, window keys) ----------------
class HeaderBar : public QWidget {
    Q_OBJECT
public:
    explicit HeaderBar(QWidget* parent = nullptr);
    void set_unit(const QString& number, const QString& name) { num_ = number; name_ = name; update(); }
    void set_links(const bool live[5]);
    void set_update(const QString& version) { update_ = version; update(); }
    void set_compact(bool compact);
    RackKey* pin_key() const noexcept { return pin_; }
    RackKey* fold_key() const noexcept { return fold_; }
    RackKey* reset_key() const noexcept { return reset_; }
    SegmentedControl* lang() const noexcept { return lang_; }
    QSize sizeHint() const override;
signals:
    void updateClicked();
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
private:
    void layout_keys();
    QString num_ = QStringLiteral("01"), name_ = QStringLiteral("VOCAL MASTER"), update_;
    bool links_[5] = {false, false, false, false, false};
    bool compact_ = false;
    QRectF update_rect_;
    RackKey* pin_ = nullptr;
    RackKey* fold_ = nullptr;
    RackKey* reset_ = nullptr;
    SegmentedControl* lang_ = nullptr;
};

} // namespace sw::model600::ui

#endif // SEVENTHWELL_MODEL600_UI_WIDGETS_HPP
