// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  custom-ui-dialog.hpp  [Layer 2]  Studio console, UNIT 05 mixer, calibration.
//
//  Implements the approved GUI v2 (docs/design/v2/DESIGN_V2.md): each unit is a
//  Blender-rendered rack faceplate with live digital meters (peak dBFS with
//  OBS's colour zones). A 30 fps timer refreshes only while the window is visible.
//
//  All audio-facing reads are atomic (dsp::Telemetry / SharedBus). No OBS types
//  appear here, so the console links unchanged into the standalone VC edition.
// =============================================================================
#ifndef SEVENTHWELL_MODEL600_CUSTOM_UI_DIALOG_HPP
#define SEVENTHWELL_MODEL600_CUSTOM_UI_DIALOG_HPP

#include "dsp-core.hpp"
#include "hw-panel.hpp"
#include "shared-state.hpp"

#include <array>
#include <atomic>
#include <functional>

#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QPixmap>
#include <QString>
#include <QVector>
#include <QWidget>

class QContextMenuEvent;
class QMouseEvent;
class QTimer;

namespace sw::model600::ui {


class HeaderBar;
class LedMeter;
class MeterScale;
class RackFader;
class RackKey;
class Readout;
class SectionLabel;
class SegmentedControl;
class StatusLine;
class ValueBox;
class TargetBar;
class CorrelationBar;
class KnobView;

enum class UiLang : int { EN = 0, JA = 1 };

// Japanese / English string pair.
struct L {
    QString en, ja;
    const QString& pick(UiLang l) const { return l == UiLang::JA ? ja : en; }
};

// -----------------------------------------------------------------------------
//  10-second automatic input calibration: the "INPUT CALIBRATOR" module of the
//  v2 design (docs/design/v2/DESIGN_V2.md section 13). One Blender plate for all
//  states; the cue card, meter, 7-segment window, progress LEDs, status lamp
//  and buttons change with the state.
// -----------------------------------------------------------------------------
class CalibrationDialog : public QDialog {
    Q_OBJECT
public:
    CalibrationDialog(dsp::Engine* engine, UiLang lang, QWidget* parent = nullptr);
    enum class State { Measuring, Success, Clipped, NoVoice };
    State state() const noexcept { return state_; }
protected:
    void showEvent(QShowEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void reject() override;
private slots:
    void tick();
private:
    void start();
    void show_result();
    void set_buttons(const QString& secondary, const QString& primary, bool primary_enabled);
    QString tr2(const char* en, const char* ja) const;

    dsp::Engine* engine_;
    UiLang lang_;
    State state_ = State::Measuring;
    bool started_ = false;
    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
    qint64 last_ms_ = 0;
    hw::PeakHold peak_;

    // Plate and live parts.
    QPixmap bg_;
    QJsonObject cal_;
    std::array<hw::Led*, 3> steps_{nullptr, nullptr, nullptr};
    hw::Lamp* lamp_ = nullptr;
    hw::DigitalMeter* meter_ = nullptr;
    hw::SegDisplay* seg_ = nullptr;
    hw::Ladder* ladder_ = nullptr;
    hw::Knob* guide_ = nullptr;
    hw::Button* secondary_ = nullptr;
    hw::Button* primary_ = nullptr;

    // Text printed by the app over the plate (follows the UI language).
    QString title_, card_head_, seg_label_, secondary_text_, primary_text_;
    QStringList body_, card_, hint_;
    bool card_large_ = true;
    bool primary_enabled_ = true;
};

// -----------------------------------------------------------------------------
//  The studio console (GUI v2): the unit's hardware faceplate, rendered from
//  the Blender asset set (hw-panel.hpp), with every control wired to the DSP
//  parameters / shared bus. Settings that used to live in a header bar
//  (language, always-on-top, compact view, reset, licence) are on the
//  right-click menu; the faceplate's bottom line is the live status read-out.
// -----------------------------------------------------------------------------
class StudioConsole : public QDialog {
    Q_OBJECT
public:
    // `director` builds the UNIT 05 mixer (Pro + MODE_DIRECTOR).
    StudioConsole(dsp::Engine* engine, SharedBus* bus, bool director, bool pro,
                  QWidget* parent = nullptr);
    ~StudioConsole() override;

    void set_language(UiLang lang);
    UiLang language() const noexcept { return lang_; }
    void set_update_available(const QString& version);
    void set_pinned(bool on) { toggle_pin(on); }
    void set_compact(bool on) { toggle_mini(on); }

    // Host hooks.
    std::function<void()> on_request_chapter;
    std::function<void(int /*scene*/)> on_scene_macro;
    std::function<void()> on_export_karte;
    std::function<void()> on_deactivate_pc;
    std::function<void()> on_params_changed;   // Persist params into the OBS scene.

signals:
    void chapterRequested();

protected:
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
    void paintEvent(QPaintEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private slots:
    void refresh();              // 30 fps meter tick.
    void toggle_pin(bool on);
    void toggle_mini(bool on);
    void panic_reset();
    void run_calibration();
    void toggle_audition();

private:
    void build_ui();
    void build_panel();

    std::atomic<float>* param_for(const QString& knob_id) const;
    void sync_from_params();
    void refresh_unit(qint64 now_ms, float dt);
#if defined(MODEL600_PRO_EDITION)
    void refresh_director(qint64 now_ms, float dt);   // UNIT 05 is compiled into the Pro binary only.
#endif
    void refresh_links();
    void show_menu(const QPoint& global);
    void relabel();
    void clamp_to_screen();
    void changed();              // Fires on_params_changed.
    void update_status();
    QString unit_number() const;
    QString unit_name() const;
    QString tr2(const char* en, const char* ja) const;

    dsp::Engine* engine_;
    SharedBus*   bus_;
    bool director_, pro_;
    UiLang lang_ = UiLang::JA;
    QTimer* timer_ = nullptr;
    bool mini_ = false;
    bool pinned_ = false;
    QString update_version_;

    hw::Panel* panel_ = nullptr;
    hw::LinkRow* links_ = nullptr;
    QHash<QString, hw::Knob*> knobs_;
    QHash<QString, hw::Selector*> selectors_;
    QHash<QString, hw::Toggle*> toggles_;
    QHash<QString, hw::Button*> buttons_;
    QHash<QString, hw::Lamp*> lamps_;          // Keyed by the printed label.
    QVector<hw::DigitalMeter*> meters_;
    QVector<hw::Knob*> trims_;                 // UNIT 05 channel trims (01..04).
    hw::Ladder* ladder_ = nullptr;
    QHash<QString, hw::SegDisplay*> segs_;
    std::array<hw::PeakHold, 6> peaks_{};      // 0..3 unit / channels, 4 master.
    QElapsedTimer clock_;
    qint64 last_ms_ = 0;
    qint64 flash_chapter_until_ = 0, flash_report_until_ = 0;

    // Compact strip.
    hw::MiniStrip* mini_bar_ = nullptr;
};

// Golden zone for YouTube Live: -12.0 +/- 1.5 LUFS integrated.
inline bool in_youtube_golden_zone(float lufs_i) noexcept { return lufs_i >= -13.5f && lufs_i <= -10.5f; }

} // namespace sw::model600::ui

#endif // SEVENTHWELL_MODEL600_CUSTOM_UI_DIALOG_HPP
