// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  ui-theme.hpp  [Layer 2]  Design tokens, bundled fonts, meter law, parts.
//
//  Single source of truth for the approved GUI design (docs/design/). Every
//  widget takes its colours, type and geometry rules from here.
// =============================================================================
#ifndef SEVENTHWELL_MODEL600_UI_THEME_HPP
#define SEVENTHWELL_MODEL600_UI_THEME_HPP

#include <array>
#include <utility>

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QPixmap>
#include <QString>
#include <QStringList>

namespace sw::model600::ui {

// ---- Colour tokens (DESIGN_SPEC.md 2.1) ----------------------------------------
struct Tok {
    static inline const QColor chassis{0x0E, 0x10, 0x12};
    static inline const QColor faceplate{0x19, 0x1C, 0x20};
    static inline const QColor panel{0x12, 0x14, 0x17};
    static inline const QColor raised{0x23, 0x27, 0x2C};
    static inline const QColor raised_active{0x2E, 0x33, 0x3A};
    static inline const QColor line{0x2C, 0x31, 0x37};
    static inline const QColor line_strong{0x3A, 0x40, 0x47};
    static inline const QColor t1{0xE7, 0xE9, 0xEC};
    static inline const QColor t2{0x9A, 0xA1, 0xA9};
    static inline const QColor t3{0x64, 0x6B, 0x73};
    static inline const QColor accent{0xF0, 0xA3, 0x3A};
    static inline const QColor green{0x46, 0xC1, 0x7B};
    static inline const QColor yellow{0xE2, 0xBF, 0x4E};
    static inline const QColor red{0xE5, 0x53, 0x4B};
    static inline const QColor off{0x1E, 0x22, 0x26};
    static inline const QColor gr{0x4C, 0x9B, 0xE8};
    static inline const QColor ok{0x46, 0xC1, 0x7B};
    static inline const QColor warn{0xE3, 0xA8, 0x3A};
    static inline const QColor error{0xE5, 0x53, 0x4B};
    static inline const QColor info{0x4C, 0x9B, 0xE8};
    static inline const QColor band{0x1F, 0x3A, 0x2B};       // Target band fill.
    static inline const QColor band_fill{0x3E, 0x7A, 0x57};  // Value fill inside bars.
    static inline const QColor pill_bg{0x2A, 0x21, 0x14};    // Update pill.
};

// ---- Fonts (DESIGN_SPEC.md 2.2) -----------------------------------------------
// Inter for Latin + numerals, Noto Sans CJK JP (subset) for Japanese. Both are
// compiled into the plugin (OFL 1.1) so Windows and macOS render identically.
inline void install_fonts() {
    static bool done = false;
    if (done) return;
    done = true;
    const char* files[] = {
        ":/m600/fonts/Inter-Regular.otf", ":/m600/fonts/Inter-Medium.otf",
        ":/m600/fonts/Inter-SemiBold.otf", ":/m600/fonts/Inter-Bold.otf",
        ":/m600/fonts/NotoSansJP-Subset-Regular.otf", ":/m600/fonts/NotoSansJP-Subset-Bold.otf"};
    for (const char* f : files) QFontDatabase::addApplicationFont(QString::fromLatin1(f));
}

inline QFont make_font(int px, int weight = QFont::Medium, qreal letter_spacing = 0.0) {
    QFont f;
    f.setFamilies({QStringLiteral("Inter"), QStringLiteral("Noto Sans CJK JP"),
                   QStringLiteral("Noto Sans JP"), QStringLiteral("Yu Gothic UI"),
                   QStringLiteral("Hiragino Sans")});
    f.setPixelSize(px);
    f.setWeight(static_cast<QFont::Weight>(weight));
    if (letter_spacing != 0.0) f.setLetterSpacing(QFont::AbsoluteSpacing, letter_spacing);
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

// Type scale.
inline QFont font_display() { return make_font(22, QFont::Medium); }
inline QFont font_title()   { return make_font(16, QFont::Bold, 1.0); }
inline QFont font_body()    { return make_font(12, QFont::Medium); }
inline QFont font_label()   { return make_font(10, QFont::DemiBold, 1.2); }
inline QFont font_scale()   { return make_font(9, QFont::Normal); }

// ---- Meter law (DESIGN_SPEC.md 3) ----------------------------------------------
// Piecewise-linear dB -> 0..1 (from the bottom / left). The top 12 dB get ~38%
// of the length, where broadcast levels live.
inline qreal meter_pos(qreal db) {
    static const std::array<std::pair<qreal, qreal>, 8> law{{
        {-60, 0.00}, {-48, 0.08}, {-36, 0.20}, {-24, 0.37},
        {-18, 0.48}, {-12, 0.62}, {-6, 0.80}, {0, 1.00}}};
    if (db <= -60) return 0.0;
    if (db >= 0) return 1.0;
    for (std::size_t i = 0; i + 1 < law.size(); ++i) {
        const auto& a = law[i];
        const auto& b = law[i + 1];
        if (db >= a.first && db <= b.first)
            return a.second + (db - a.first) / (b.first - a.first) * (b.second - a.second);
    }
    return 0.0;
}

inline qreal meter_db(qreal pos) {
    static const std::array<std::pair<qreal, qreal>, 8> law{{
        {-60, 0.00}, {-48, 0.08}, {-36, 0.20}, {-24, 0.37},
        {-18, 0.48}, {-12, 0.62}, {-6, 0.80}, {0, 1.00}}};
    for (std::size_t i = 0; i + 1 < law.size(); ++i) {
        const auto& a = law[i];
        const auto& b = law[i + 1];
        if (pos >= a.second && pos <= b.second)
            return a.first + (pos - a.second) / (b.second - a.second) * (b.first - a.first);
    }
    return 0.0;
}

inline QColor zone_color(qreal db) {
    if (db > -3) return Tok::red;
    if (db > -12) return Tok::yellow;
    return Tok::green;
}

// ---- Rendered parts (tools/blender/render_parts.py, 4x) ------------------------
inline QPixmap part(const QString& name) {   // QPixmap is implicitly shared: cheap copy.
    static QHash<QString, QPixmap> cache;
    auto it = cache.find(name);
    if (it == cache.end()) {
        // Rendered at 4x the logical size. Kept at device-pixel-ratio 1 so source
        // rectangles are always in plain pixels; painters scale to the target.
        it = cache.insert(name, QPixmap(QStringLiteral(":/m600/parts/%1.png").arg(name)));
    }
    return it.value();
}

// Number formatting used by every readout ("-inf" below the meter floor).
inline QString fmt_db(float db, int prec = 1) {
    if (db <= -119.0f) return QStringLiteral("-inf");
    if (db > -0.05f && db < 0.05f) return QString::number(0.0, 'f', prec);
    return QString::number(static_cast<double>(db), 'f', prec);
}
inline QString fmt_signed(float db, int prec = 1) {
    if (db > -0.05f && db < 0.05f) return QString::number(0.0, 'f', prec);
    return (db > 0 ? QStringLiteral("+") : QString()) + QString::number(static_cast<double>(db), 'f', prec);
}

} // namespace sw::model600::ui

#endif // SEVENTHWELL_MODEL600_UI_THEME_HPP
