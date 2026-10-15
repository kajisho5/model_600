// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  hw-panel.hpp  [Layer 2]  Hardware-style faceplate widgets (GUI v2).
//
//  Implements the approved v2 design (docs/design/v2/DESIGN_V2.md): every unit
//  is a rack faceplate rendered in Blender. The static plate is one background
//  image per unit; every moving part (knobs, selectors, toggle levers, button
//  caps, lamps, LINK LEDs) is a sprite or a filmstrip from the same renders, so
//  light and shadow line up. Digital meters, 7-segment windows and LED ladders
//  are drawn live with QPainter.
//
//  Geometry comes from :/m600/hw/hw.json (written by docs/design/v2/
//  pack_assets.py from the same layout that printed the panel), so the app
//  and the renders can never drift apart. All coordinates are logical pixels
//  of the faceplate; assets are 2x for HiDPI.
// =============================================================================
#ifndef SEVENTHWELL_MODEL600_HW_PANEL_HPP
#define SEVENTHWELL_MODEL600_HW_PANEL_HPP

#include <QAbstractButton>
#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QPixmap>
#include <QString>
#include <QVector>
#include <QWidget>

#include <array>
#include <functional>

class QContextMenuEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QWheelEvent;

namespace sw::model600::ui::hw {

// -----------------------------------------------------------------------------
//  Asset set (loaded once from the Qt resource).
// -----------------------------------------------------------------------------
struct Strip {
    QPixmap sheet;          // Frames packed in a grid.
    int frame_px = 0;       // Frame edge in pixels.
    int cols = 1;
    QVector<double> angles; // Pointer angle of every frame (degrees, 0 = 12 o'clock).
    double logical = 0;     // Frame edge in logical px.
    double radius = 0;      // Knob radius the strip was rendered at (logical px).
    int nearest(double angle) const;
    QRectF source(int frame) const;
};

class Assets {
public:
    static const Assets& get();
    bool ok() const noexcept { return ok_; }
    QJsonObject unit(const QString& number) const;
    QPixmap background(const QString& number) const;
    const Strip* strip(const QString& name) const;
    QPixmap sprite(const QString& name) const;
    QSizeF sprite_size(const QString& name) const;   // Logical size.
    qreal scale() const noexcept { return scale_; }
private:
    Assets();
    bool ok_ = false;
    qreal scale_ = 2.0;
    QJsonObject units_;
    QHash<QString, QString> bg_files_;
    QHash<QString, Strip> strips_;
    QHash<QString, QSizeF> sizes_;
    QHash<QString, QString> files_;
    mutable QHash<QString, QPixmap> cache_;
};

// Colours shared by the live displays (match docs/design/v2/make_textures.py).
struct Disp {
    static inline const QColor bg{0x0B, 0x08, 0x06};
    static inline const QColor lit{0xFF, 0xB3, 0x47};
    static inline const QColor ghost{0x1A, 0x11, 0x08};
    static inline const QColor dim{0xB0, 0x7A, 0x35};
    static inline const QColor green{0x57, 0xE0, 0x8A};
    static inline const QColor green_off{0x0E, 0x1F, 0x14};
    static inline const QColor amber{0xFF, 0xB3, 0x47};
    static inline const QColor amber_off{0x23, 0x17, 0x0A};
    static inline const QColor red{0xFF, 0x4A, 0x3A};
    static inline const QColor red_off{0x26, 0x0C, 0x0A};
    static inline const QColor ink{0xE9, 0xE3, 0xD3};
    static inline const QColor ink_dim{0xA9, 0xA4, 0x97};
};

// Draw a 7-segment string (with "." folded into the previous cell) into a cell row.
// `ghost` paints every segment dim first, like a real display.
void draw_seg_text(QPainter& p, const QString& text, QPointF origin, double cell_w, double cell_h,
                   const QColor& lit, const QColor& ghost);
double seg_text_cells(const QString& text);

// -----------------------------------------------------------------------------
//  The faceplate: background image + live status line at the bottom.
// -----------------------------------------------------------------------------
class Panel : public QWidget {
    Q_OBJECT
public:
    explicit Panel(const QString& unit_number, QWidget* parent = nullptr);
    const QJsonObject& layout() const noexcept { return layout_; }
    QString number() const { return number_; }
    QSize sizeHint() const override { return size_; }
    QSize minimumSizeHint() const override { return size_; }
    // Status read-out drawn where the sample shows the serial line.
    enum class Level { Ok, Warn, Error, Info };
    void set_status(Level level, const QString& text, const QString& right = QString());
signals:
    void contextRequested(QPoint global_pos);
    void statusClicked();
protected:
    void paintEvent(QPaintEvent*) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
private:
    QString number_;
    QJsonObject layout_;
    QSize size_;
    QPixmap bg_;
    Level level_ = Level::Ok;
    QString status_, right_;
    QRectF status_rect_;
};

// -----------------------------------------------------------------------------
//  Continuous knob (filmstrip). Vertical drag, wheel, arrows, double-click resets.
// -----------------------------------------------------------------------------
class Knob : public QWidget {
    Q_OBJECT
public:
    // `strip` = "knob_big" | "knob_mid" | "knob_trim"; centre/radius in panel px.
    Knob(const QString& strip, QPointF centre, double radius, double lo, double hi, double def,
         QWidget* parent);
    void set_value(double v, bool notify = false);
    double value() const noexcept { return value_; }
    bool dragging() const noexcept { return dragging_; }
    void set_step(double step) { step_ = step; }
    void set_formatter(std::function<QString(double)> f) { fmt_ = std::move(f); }
    void set_label(const QString& label) { label_ = label; update_tip(); }
signals:
    void valueChanged(double v);
    void released();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    double angle() const;
    void update_tip();
    void show_value(const QPoint& global);
    const Strip* strip_;
    double radius_, lo_, hi_, def_, value_, step_;
    double drag_value_ = 0;
    int drag_y_ = 0;
    bool dragging_ = false;
    bool kb_focus_ = false;   // Focus ring only after keyboard use.
    QString label_;
    std::function<QString(double)> fmt_;
};

// -----------------------------------------------------------------------------
//  Stepped selector (chicken head). Click = next, right-click = previous.
// -----------------------------------------------------------------------------
class Selector : public QWidget {
    Q_OBJECT
public:
    Selector(QPointF centre, double radius, const QVector<double>& angles, const QStringList& names,
             QWidget* parent);
    void set_index(int i, bool notify = false);
    int index() const noexcept { return index_; }
    void set_names(const QStringList& names) { names_ = names; update_tip(); }
signals:
    void indexChanged(int i);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void contextMenuEvent(QContextMenuEvent* e) override;   // Passed up to the panel menu.
private:
    void update_tip();
    const Strip* strip_;
    double radius_;
    QVector<double> angles_;
    QStringList names_;
    int index_ = 0;
};

// -----------------------------------------------------------------------------
//  Bat-lever toggle switch (the nut is in the background).
// -----------------------------------------------------------------------------
class Toggle : public QAbstractButton {
    Q_OBJECT
public:
    Toggle(QPointF centre, bool horizontal, QWidget* parent);
protected:
    void paintEvent(QPaintEvent*) override;
    bool hitButton(const QPoint& pos) const override;
private:
    bool horizontal_;
};

// -----------------------------------------------------------------------------
//  Illuminated push button (cap sprite; bezel is in the background).
// -----------------------------------------------------------------------------
class Button : public QAbstractButton {
    Q_OBJECT
public:
    // `color` = "amber" | "red"; size in panel px (54x34 or 52x32).
    Button(QPointF centre, int w, int h, const QString& color, QWidget* parent);
    void set_lit(bool on) { if (lit_ != on) { lit_ = on; update(); } }
    bool lit() const noexcept { return lit_; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString color_;
    int w_, h_;
    bool lit_ = false;
};

// -----------------------------------------------------------------------------
//  Pilot lamp lens.
// -----------------------------------------------------------------------------
class Lamp : public QWidget {
    Q_OBJECT
public:
    Lamp(QPointF centre, const QString& color, QWidget* parent);
    void set_lit(bool on) { if (lit_ != on) { lit_ = on; update(); } }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString color_;
    bool lit_ = false;
};

// -----------------------------------------------------------------------------
//  LINK row: five LEDs. on = linked with audio, idle = linked but silent,
//  off = no instance. A count > 1 is shown as "x2" beside the LED.
// -----------------------------------------------------------------------------
class LinkRow : public QWidget {
    Q_OBJECT
public:
    enum class State { Off, Idle, On };
    LinkRow(double x, double y, double dx, QWidget* parent);
    void set(const std::array<State, 5>& states, const std::array<int, 5>& counts);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    double x_, y_, dx_;
    std::array<State, 5> states_{};
    std::array<int, 5> counts_{};
};

// -----------------------------------------------------------------------------
//  Digital meter window: label + aux on top, big 7-segment value + unit, and a
//  segment bar with scale. kinds: dBFS (-60..0, OBS zones -20 / -9, peak hold),
//  ride (-12..+12 dB centre zero), phase (-1..+1 centre zero), gr (0..-24 dB).
// -----------------------------------------------------------------------------
class DigitalMeter : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Dbfs, Ride, Phase, Gr };
    DigitalMeter(const QRectF& window, Kind kind, QWidget* parent);
    void set_kind(Kind k) { if (kind_ != k) { kind_ = k; update(); } }
    Kind kind() const noexcept { return kind_; }
    // `bar` and `hold` in the kind's unit; `text` is the big number already formatted.
    void set_values(double bar, double hold, const QString& text, const QString& label,
                    const QString& unit, const QString& aux);
signals:
    void clicked();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
private:
    Kind kind_;
    double bar_ = -120, hold_ = -120;
    QString text_ = QStringLiteral("-  "), label_, unit_, aux_;
};

// -----------------------------------------------------------------------------
//  Plain 7-segment window (UNIT 05 master read-outs).
// -----------------------------------------------------------------------------
class SegDisplay : public QWidget {
    Q_OBJECT
public:
    SegDisplay(const QRectF& window, int cells, QWidget* parent);
    void set_text(const QString& t, const QColor& c = QColor());
protected:
    void paintEvent(QPaintEvent*) override;
private:
    int cells_;
    QString text_;
    QColor color_;
};

// -----------------------------------------------------------------------------
//  LED ladder (LIMITING / POCKET): n segments, lit from the left.
// -----------------------------------------------------------------------------
class Ladder : public QWidget {
    Q_OBJECT
public:
    // `zones`: (start fraction, "green"|"amber"|"red") pairs, as in the layout.
    Ladder(QPointF centre, int n, const QVector<QPair<double, QString>>& zones, QWidget* parent);
    void set_lit(int count) { count = qBound(0, count, n_); if (count != lit_) { lit_ = count; update(); } }
    int segments() const noexcept { return n_; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    int n_;
    QVector<QPair<double, QString>> zones_;
    int lit_ = 0;
};

// -----------------------------------------------------------------------------
//  Compact strip: one long peak bar (dBFS, OBS zones) and up to four
//  7-segment read-outs. Shown instead of the faceplate in compact view.
// -----------------------------------------------------------------------------
class MiniStrip : public QWidget {
    Q_OBJECT
public:
    explicit MiniStrip(QWidget* parent = nullptr);
    struct Readout { QString label, value; };
    void set(double bar_db, double hold_db, const QVector<Readout>& readouts);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    double bar_ = -120, hold_ = -120;
    QVector<Readout> readouts_;
};

} // namespace sw::model600::ui::hw

#endif // SEVENTHWELL_MODEL600_HW_PANEL_HPP
