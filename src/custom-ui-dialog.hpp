// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  custom-ui-dialog.hpp  [Layer 2]  Studio console, UNIT 05 mixer, calibration.
//
//  Implements the approved design (docs/design/). The console is a 19" rack
//  faceplate: Blender-rendered screws on the ears, a two-row header, a body
//  specific to the unit (01-04) or the integrated mixer (05), and a status
//  line. A 30 fps timer refreshes only while the window is visible.
//
//  All audio-facing reads are atomic (dsp::Telemetry / SharedBus). No OBS types
//  appear here, so the console links unchanged into the standalone VC edition.
// =============================================================================
#ifndef SEVENTHWELL_MODEL600_CUSTOM_UI_DIALOG_HPP
#define SEVENTHWELL_MODEL600_CUSTOM_UI_DIALOG_HPP

#include "dsp-core.hpp"
#include "shared-state.hpp"

#include <array>
#include <functional>

#include <QDialog>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QStackedLayout;
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
//  One mixer channel strip (UNIT 05).
// -----------------------------------------------------------------------------
class ChannelStrip : public QWidget {
    Q_OBJECT
public:
    ChannelStrip(Mode mode, const QString& number, const QString& name, QWidget* parent = nullptr);
    void update_from(float true_peak, float rms, float st_lufs, float gr, float duck, bool live);
    Mode mode() const noexcept { return mode_; }
    RackFader* fader() const noexcept { return fader_; }
signals:
    void trimMoved(Mode mode, float db);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    Mode mode_;
    QString number_, name_;
    float st_lufs_ = -120.0f;
    bool live_ = false;
    ValueBox* pk_box_ = nullptr;
    ValueBox* duck_box_ = nullptr;
    ValueBox* fader_box_ = nullptr;
    LedMeter* level_ = nullptr;
    LedMeter* duck_ = nullptr;
    RackFader* fader_ = nullptr;
    float hold_ = -120.0f;
};

// -----------------------------------------------------------------------------
//  10-second automatic input calibration (measuring / result states).
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
    void build_measuring_page();
    void build_result_page();
    dsp::Engine* engine_;
    UiLang lang_;
    State state_ = State::Measuring;
    bool started_ = false;
    QTimer* timer_ = nullptr;
    QStackedLayout* pages_ = nullptr;
    // Measuring page.
    LedMeter* input_meter_ = nullptr;
    QWidget* progress_ = nullptr;
    QLabel* time_label_ = nullptr;
    // Result page.
    QLabel* title_ = nullptr;
    QLabel* body_ = nullptr;
    QWidget* result_bar_ = nullptr;
    Readout* r1_ = nullptr;
    Readout* r2_ = nullptr;
    Readout* r3_ = nullptr;
    KnobView* knob_ = nullptr;
    QLabel* knob_text_ = nullptr;
    LedMeter* measured_meter_ = nullptr;
    RackKey* secondary_ = nullptr;
    RackKey* primary_ = nullptr;
};

// -----------------------------------------------------------------------------
//  The studio console.
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

private slots:
    void refresh();              // 30 fps meter tick.
    void toggle_pin(bool on);
    void toggle_mini(bool on);
    void panic_reset();
    void run_calibration();
    void toggle_audition();

private:
    void build_ui();
    QWidget* build_unit_body();
    QWidget* build_director_body();
    QWidget* build_mini_bar();
    QWidget* build_trim_row(QWidget* parent);
    QWidget* build_param_row(QWidget* parent, RackFader*& fader, ValueBox*& box,
                             float lo, float hi, const QVector<float>& marks, float value,
                             const QString& unit);
    void relabel();
    void clamp_to_screen();
    void changed();              // Fires on_params_changed.
    void update_status();
    QString unit_number() const;
    QString unit_name() const;

    dsp::Engine* engine_;
    SharedBus*   bus_;
    bool director_, pro_;
    UiLang lang_ = UiLang::JA;
    QTimer* timer_ = nullptr;
    bool mini_ = false;
    QString update_version_;

    HeaderBar* header_ = nullptr;
    QWidget* body_ = nullptr;
    QWidget* mini_bar_ = nullptr;
    StatusLine* status_ = nullptr;

    // Unit body (01-04).
    LedMeter* out_meter_ = nullptr;
    LedMeter* gr_meter_ = nullptr;
    ValueBox* pk_box_ = nullptr;
    ValueBox* gr_box_ = nullptr;
    std::array<Readout*, 4> readouts_{nullptr, nullptr, nullptr, nullptr};
    std::array<SectionLabel*, 6> section_{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    SegmentedControl* character_ = nullptr;
    SegmentedControl* space_fx_ = nullptr;
    RackKey* calib_key_ = nullptr;
    RackKey* audition_key_ = nullptr;
    RackKey* bleep_key_ = nullptr;
    RackKey* cough_key_ = nullptr;
    RackKey* safe_key_ = nullptr;
    RackKey* swell_key_ = nullptr;
    RackFader* trim_ = nullptr;
    ValueBox* trim_box_ = nullptr;
    RackFader* param_a_ = nullptr;
    ValueBox* param_a_box_ = nullptr;
    QString param_a_unit_;
    RackFader* param_b_ = nullptr;
    ValueBox* param_b_box_ = nullptr;

    // Director body (05).
    std::array<ChannelStrip*, 4> strips_{nullptr, nullptr, nullptr, nullptr};
    std::array<Readout*, 3> master_{nullptr, nullptr, nullptr};
    SectionLabel* target_label_ = nullptr;
    SectionLabel* phase_label_ = nullptr;
    SectionLabel* scene_label_ = nullptr;
    SectionLabel* master_label_ = nullptr;
    TargetBar* target_ = nullptr;
    CorrelationBar* corr_ = nullptr;
    RackKey* mono_key_ = nullptr;
    RackKey* meter_reset_key_ = nullptr;
    SegmentedControl* scene_ = nullptr;
    RackKey* chapter_key_ = nullptr;
    RackKey* karte_key_ = nullptr;

    // Mini strip.
    LedMeter* mini_meter_ = nullptr;
    QWidget* mini_values_ = nullptr;
};

// Golden zone for YouTube Live: -12.0 +/- 1.5 LUFS integrated.
inline bool in_youtube_golden_zone(float lufs_i) noexcept { return lufs_i >= -13.5f && lufs_i <= -10.5f; }

} // namespace sw::model600::ui

#endif // SEVENTHWELL_MODEL600_CUSTOM_UI_DIALOG_HPP
