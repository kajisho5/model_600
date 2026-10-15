// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  custom-ui-dialog.hpp  [Layer 2]  19" studio-rack console + UNIT 05 mixer.
//
//  Declares the GUI. Rendering is optimised: the metal chassis, rack ears,
//  screws and scale text are baked once into a QPixmap (regenerated only when
//  devicePixelRatio changes); the 30 fps timer repaints only the moving LED
//  bars and stops entirely while the window is hidden (0.00 % CPU).
//
//  All audio-facing reads are atomic (dsp::Telemetry / SharedBus); the GUI
//  never touches a lock the audio thread could hold.
//
//  Qt6::Widgets + Qt6::Gui + Qt6::Multimedia. No OBS types here; the console is
//  handed plain pointers so it links unchanged into the standalone VC edition.
// =============================================================================
#ifndef SEVENTHWELL_MODEL600_CUSTOM_UI_DIALOG_HPP
#define SEVENTHWELL_MODEL600_CUSTOM_UI_DIALOG_HPP

#include "dsp-core.hpp"
#include "shared-state.hpp"

#include <array>
#include <functional>

#include <QColor>
#include <QDialog>
#include <QElapsedTimer>
#include <QPixmap>
#include <QPointer>
#include <QSlider>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QComboBox;
class QCheckBox;
class QProgressBar;
class QTimer;
class QPaintEvent;
class QShowEvent;
class QHideEvent;
class QWheelEvent;
class QMouseEvent;
class QHBoxLayout;
class QVBoxLayout;
class QGridLayout;

namespace sw::model600 { class SharedBus; }

namespace sw::model600::ui {

// Language toggle for the JP/EN live switch.
enum class UiLang : int { EN = 0, JA = 1 };

// A localised string pair.
struct L { QString en, ja; const QString& pick(UiLang l) const { return l == UiLang::JA ? ja : en; } };

// Palette for the brushed-metal broadcast aesthetic.
struct Skin {
    QColor chassis_top{64, 66, 70};
    QColor chassis_bottom{34, 35, 38};
    QColor panel{24, 25, 28};
    QColor ear{44, 46, 50};
    QColor screw{150, 152, 156};
    QColor text{214, 216, 220};
    QColor text_dim{120, 123, 128};
    QColor accent{255, 176, 32};     // SEVENTHWELL amber.
    QColor led_green{64, 220, 110};
    QColor led_yellow{244, 208, 54};
    QColor led_red{236, 72, 54};
    QColor led_off{40, 42, 46};
    QColor gr_blue{72, 170, 244};
};

// -----------------------------------------------------------------------------
//  A single custom-painted LED meter column (peak + RMS + hold, ballistics).
// -----------------------------------------------------------------------------
class LedMeter : public QWidget {
    Q_OBJECT
public:
    enum Kind { Level, GainReduction };
    explicit LedMeter(Kind kind, const Skin* skin, QWidget* parent = nullptr);
    void set_values(float peak_db, float rms_db, float hold_db) noexcept;
    void set_gr(float gr_db) noexcept;
    void reset_hold() noexcept;
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
private:
    Kind kind_;
    const Skin* skin_;
    float peak_db_ = kSilenceFloor, rms_db_ = kSilenceFloor, hold_db_ = kSilenceFloor, gr_db_ = 0.0f;
    static constexpr float kSilenceFloor = -60.0f;
};

// -----------------------------------------------------------------------------
//  A fader that ignores the plain wheel, uses Ctrl+wheel for 0.1 dB nudges and
//  snaps to 0.0 dB on double-click.
// -----------------------------------------------------------------------------
class TrimFader : public QSlider {
    Q_OBJECT
public:
    explicit TrimFader(QWidget* parent = nullptr);
    void set_db(float db);
    float db() const;
signals:
    void dbChanged(float db);
protected:
    void wheelEvent(QWheelEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
};

// -----------------------------------------------------------------------------
//  One channel strip for the UNIT 05 mixer (also reused as the compact view).
// -----------------------------------------------------------------------------
class ChannelStrip : public QWidget {
    Q_OBJECT
public:
    ChannelStrip(Mode mode, const L& title, const Skin* skin, QWidget* parent = nullptr);
    void update_from(float true_peak, float peak_hold, float rms, float st_lufs, float gr, float duck, bool live);
    void set_language(UiLang l);
    Mode mode() const noexcept { return mode_; }
    TrimFader* fader() const noexcept { return fader_; }
signals:
    void trimMoved(Mode mode, float db);
private:
    Mode mode_;
    L title_;
    const Skin* skin_;
    QLabel* name_ = nullptr;
    QLabel* readout_ = nullptr;
    LedMeter* level_ = nullptr;
    LedMeter* gr_ = nullptr;
    TrimFader* fader_ = nullptr;
    UiLang lang_ = UiLang::JA;
};

// -----------------------------------------------------------------------------
//  The calibration modal (10 s cue-card measurement).
// -----------------------------------------------------------------------------
class CalibrationDialog : public QDialog {
    Q_OBJECT
public:
    CalibrationDialog(dsp::Engine* engine, const Skin* skin, UiLang lang, QWidget* parent = nullptr);
protected:
    void showEvent(QShowEvent*) override;
private slots:
    void tick();
private:
    void finish();
    dsp::Engine* engine_;
    const Skin* skin_;
    UiLang lang_;
    QLabel* cue_ = nullptr;
    QLabel* status_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QPushButton* close_btn_ = nullptr;
    QTimer* timer_ = nullptr;
    bool running_ = false;
};

// -----------------------------------------------------------------------------
//  The main studio console.
// -----------------------------------------------------------------------------
class StudioConsole : public QDialog {
    Q_OBJECT
public:
    // `director` enables the UNIT 05 master section (Pro + MODE_DIRECTOR).
    StudioConsole(dsp::Engine* engine, SharedBus* bus, bool director, bool pro,
                  QWidget* parent = nullptr);
    ~StudioConsole() override;

    void set_language(UiLang lang);
    UiLang language() const noexcept { return lang_; }

    // Called by the update checker (queued to the UI thread).
    void set_update_available(const QString& version);

    // Hook the console can call to request a YouTube chapter mark / karte / etc.
    std::function<void()> on_request_chapter;
    std::function<void(int /*scene*/)> on_scene_macro;
    std::function<void()> on_export_karte;
    std::function<void()> on_deactivate_pc;
    // Fired whenever the console changes an engine parameter, so the host can
    // write the new values into the OBS scene data.
    std::function<void()> on_params_changed;

signals:
    void chapterRequested();

protected:
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
    void paintEvent(QPaintEvent*) override;

private slots:
    void refresh();              // 30 fps meter tick.
    void toggle_pin();
    void toggle_mini();
    void toggle_language();
    void panic_reset();
    void run_calibration();
    void start_audition();
    void fire_bleep();
    void hold_cough();

private:
    void build_ui();
    void build_header(QHBoxLayout* into);
    void build_unit01(QVBoxLayout* into);
    void build_unit05(QVBoxLayout* into);
    void rebuild_chassis_pixmap();
    void relabel();
    void clamp_to_screen();      // showEvent geometry guard.
    void attach_strip(ChannelStrip* strip);

    dsp::Engine* engine_;
    SharedBus*   bus_;
    bool director_, pro_;
    UiLang lang_ = UiLang::JA;
    Skin skin_;

    QPixmap chassis_;
    qreal   chassis_dpr_ = 0.0;
    QSize   chassis_size_;

    QTimer* timer_ = nullptr;
    bool    pinned_ = false, mini_ = false;

    // Header widgets.
    QLabel* link_matrix_ = nullptr;
    QPushButton* pin_btn_ = nullptr;
    QPushButton* mini_btn_ = nullptr;
    QPushButton* lang_btn_ = nullptr;
    QPushButton* reset_btn_ = nullptr;
    QPushButton* update_btn_ = nullptr;
    QString update_version_;
    bool update_available_ = false;

    // UNIT 01 section.
    QWidget* unit01_box_ = nullptr;
    LedMeter* mic_level_ = nullptr;
    LedMeter* mic_gr_ = nullptr;
    QLabel* mic_readout_ = nullptr;
    QLabel* mic_hint_ = nullptr;
    QComboBox* character_ = nullptr;
    QComboBox* space_fx_ = nullptr;
    QPushButton* calib_btn_ = nullptr;
    QPushButton* audition_btn_ = nullptr;
    QPushButton* bleep_btn_ = nullptr;
    QPushButton* cough_btn_ = nullptr;
    TrimFader* mic_trim_ = nullptr;

    // UNIT 05 section.
    QWidget* unit05_box_ = nullptr;
    std::array<ChannelStrip*, 4> strips_{nullptr, nullptr, nullptr, nullptr};
    QLabel* master_lufs_ = nullptr;
    QLabel* golden_zone_ = nullptr;
    QLabel* phase_corr_ = nullptr;
    QCheckBox* mono_check_ = nullptr;
    QPushButton* scene_btns_[4] = {nullptr, nullptr, nullptr, nullptr};
    QPushButton* chapter_btn_ = nullptr;
    QPushButton* karte_btn_ = nullptr;
    QPushButton* deactivate_btn_ = nullptr;

    // Collected relabel callbacks so JP/EN flips every string live.
    QVector<std::function<void()>> relabel_hooks_;
};

// -----------------------------------------------------------------------------
//  Loudness / meter formatting helpers (header-inline, used by .cpp + tests).
// -----------------------------------------------------------------------------
inline QString fmt_db(float db, int prec = 1) {
    if (db <= -119.0f) return QStringLiteral("-∞");   // -INF
    return QString::number(static_cast<double>(db), 'f', prec);
}
inline bool in_youtube_golden_zone(float lufs_i) noexcept {
    return lufs_i >= -13.5f && lufs_i <= -10.5f; // -12.0 +/- 1.5
}

} // namespace sw::model600::ui

#endif // SEVENTHWELL_MODEL600_CUSTOM_UI_DIALOG_HPP
