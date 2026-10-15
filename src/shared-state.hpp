// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  shared-state.hpp  [Layer 1]  Cross-process 16-slot shared memory bus.
//
//  Pure C++20. No OBS / Qt dependency whatsoever (this layer must compile for
//  the future standalone VC Edition, VST3 and iOS targets unchanged).
//
//  - Windows : CreateFileMappingA with a Null-DACL SECURITY_DESCRIPTOR so an
//              elevated OBS and a non-elevated companion app still share it.
//  - macOS/Linux : POSIX shm_open("/sw_m600_bus_v1") (<= 31 chars), with an
//              automatic /tmp/sw_m600_bus_v1.mmap fallback when a sandbox
//              rejects the POSIX object.
//  - Fixed 4096-byte ABI (magic / version / size / reserved padding).
//  - alignas(128) slots[16] to defeat false sharing on both x86_64 and ARM64.
//  - PID + 500 ms heartbeat garbage collection, 150 ms meter decay.
//  - Cooperative Master Summing Guard with an asymmetric 0.5 ms / 180 ms
//    smoothing follower.
//  - An OBS-independent maintenance thread ("脱OBSタイマー") drives GC and the
//    guard so the bus keeps healing even when no audio callback is running.
// =============================================================================
#ifndef SEVENTHWELL_MODEL600_SHARED_STATE_HPP
#define SEVENTHWELL_MODEL600_SHARED_STATE_HPP

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <process.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  include <cerrno>
#endif

namespace sw::model600 {

// -----------------------------------------------------------------------------
//  Compile-time constants shared by every unit and every target.
// -----------------------------------------------------------------------------
inline constexpr std::uint32_t kBusMagic        = 0x3630304Du; // 'M','0','0','6'
inline constexpr std::uint32_t kBusStructVersion = 1u;
inline constexpr std::uint32_t kBusStructSize    = 4096u;      // Frozen ABI size.
inline constexpr std::size_t   kMaxSlots         = 16u;
inline constexpr int           kModeCount        = 5;          // 01..05

// Processing mode identifiers. The numeric values are part of the ABI and the
// OBS display-name table; never renumber them.
enum Mode : std::int32_t {
    MODE_VOICE    = 0, // [01] VOCAL MASTER
    MODE_COLLAB   = 1, // [02] COLLAB RIDER
    MODE_GAME     = 2, // [03] GAME IMPACT
    MODE_BGM      = 3, // [04] BGM POCKET
    MODE_DIRECTOR = 4  // [05] STREAM DIRECTOR (Pro only)
};

// init_state values (std::atomic<int32_t>, acquire/release ordered).
enum BusInitState : std::int32_t {
    BUS_UNINITIALIZED = 0,
    BUS_INITIALIZING  = 1,
    BUS_READY         = 2
};

// Light math helpers used by this layer (and shared with dsp-core.hpp). Kept in
// the project namespace so we never pollute std::.
inline float m600_exp(float x) noexcept { return std::exp(x); }
inline float m600_db_to_lin(float db) noexcept { return std::exp(db * 0.11512925465f); } // 10^(db/20)
inline float m600_lin_to_db(float lin) noexcept {
    return lin > 1.0e-9f ? 20.0f * std::log10(lin) : -180.0f;
}

// Monotonic microsecond clock, used everywhere for heartbeats and decay.
inline std::int64_t now_us() noexcept {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

// -----------------------------------------------------------------------------
//  Per-instance telemetry + control slot.
//
//  alignas(128) places every slot on its own pair of cache lines so two audio
//  threads writing different slots never thrash a shared line. Every mutable
//  field is std::atomic<> so readers in other processes never see torn values.
// -----------------------------------------------------------------------------
struct alignas(128) InstanceSlot {
    std::atomic<std::int32_t> in_use;        // 0 = free, 1 = claimed.
    std::atomic<std::int32_t> pid;           // Owning process id (for GC).
    std::atomic<std::int32_t> mode;          // One of Mode.
    std::atomic<std::int64_t> heartbeat_us;  // Last liveness stamp.

    // Telemetry (driven by the audio thread, read by UNIT 05 / companion).
    std::atomic<float> true_peak_db;         // Inter-sample true peak (dBTP).
    std::atomic<float> rms_db;               // Short window RMS (dBFS).
    std::atomic<float> short_term_lufs;      // 3 s short-term loudness.
    std::atomic<float> gr_db;                // Limiter gain reduction (<= 0).
    std::atomic<float> duck_db;              // Applied ducking amount (<= 0).
    std::atomic<float> output_energy;        // Linear post-fader energy for guard.

    // Voice pitch tracker broadcast (UNIT 01 -> UNIT 02/03/04).
    std::atomic<float> voice_center_hz;      // 1200..2600 Hz band centroid.
    std::atomic<float> voice_activity;       // 0..1 speech confidence.

    // Control / linkage flags.
    std::atomic<std::int32_t> is_mic_muted;  // 1 when UNIT 01 is muted.
    std::atomic<std::int32_t> signal_present;// 1 when above the noise floor.
    std::atomic<std::int32_t> is_loud_now;   // 1 when this unit is peaking hard.

    // Pad to a clean 128-byte footprint so sizeof() stays deterministic.
    std::atomic<std::int32_t> slot_reserved0;
    std::atomic<std::int32_t> slot_reserved1;
    std::atomic<std::int32_t> slot_reserved2;
    std::atomic<std::int32_t> slot_reserved3;
    std::atomic<std::int32_t> slot_reserved4;
    std::atomic<std::int32_t> slot_reserved5;
    std::atomic<std::int32_t> slot_reserved6;

    void reset() noexcept {
        in_use.store(0, std::memory_order_relaxed);
        pid.store(0, std::memory_order_relaxed);
        mode.store(MODE_VOICE, std::memory_order_relaxed);
        heartbeat_us.store(0, std::memory_order_relaxed);
        true_peak_db.store(-120.0f, std::memory_order_relaxed);
        rms_db.store(-120.0f, std::memory_order_relaxed);
        short_term_lufs.store(-120.0f, std::memory_order_relaxed);
        gr_db.store(0.0f, std::memory_order_relaxed);
        duck_db.store(0.0f, std::memory_order_relaxed);
        output_energy.store(0.0f, std::memory_order_relaxed);
        voice_center_hz.store(1600.0f, std::memory_order_relaxed);
        voice_activity.store(0.0f, std::memory_order_relaxed);
        is_mic_muted.store(0, std::memory_order_relaxed);
        signal_present.store(0, std::memory_order_relaxed);
        is_loud_now.store(0, std::memory_order_relaxed);
        slot_reserved0.store(0, std::memory_order_relaxed);
        slot_reserved1.store(0, std::memory_order_relaxed);
        slot_reserved2.store(0, std::memory_order_relaxed);
        slot_reserved3.store(0, std::memory_order_relaxed);
        slot_reserved4.store(0, std::memory_order_relaxed);
        slot_reserved5.store(0, std::memory_order_relaxed);
        slot_reserved6.store(0, std::memory_order_relaxed);
    }
};
static_assert(sizeof(InstanceSlot) == 128, "InstanceSlot must stay one aligned block");

// -----------------------------------------------------------------------------
//  The frozen 4096-byte bus layout. Offsets are part of the ABI.
// -----------------------------------------------------------------------------
struct BusLayout {
    // --- Header (identity + init handshake) ---------------------------------
    std::atomic<std::uint32_t> magic;          // kBusMagic once initialized.
    std::uint32_t              struct_version;  // kBusStructVersion.
    std::uint32_t              struct_size;     // kBusStructSize (4096).
    std::atomic<std::int32_t>  init_state;      // BusInitState handshake.

    // --- Cooperative Master Summing Guard -----------------------------------
    std::atomic<float>         master_sum_energy;   // Latest summed Track-1 energy.
    std::atomic<float>         summing_guard_scale;  // 0..1 ceiling scale (smoothed).
    std::atomic<std::int32_t>  guard_active;        // 1 while actively pulling down.

    // --- Standalone companion linkage ---------------------------------------
    std::atomic<std::int32_t>  is_standalone_vc_active; // Discord VC edition online.

    // --- Bidirectional remote-fader persistence -----------------------------
    std::atomic<std::uint32_t> trim_revision[kModeCount]; // Bumped by UNIT 05.
    std::atomic<float>         trim_value_db[kModeCount];  // Desired output trim.

    // --- Global talk-state hints (for auto-swell / solo pad) ----------------
    std::atomic<std::int64_t>  last_voice_active_us;
    std::atomic<std::int64_t>  last_game_active_us;
    std::atomic<std::int64_t>  last_bgm_active_us;

    // Explicit header pad so the slots[] array begins on a 128-byte boundary
    // and the whole struct is exactly 4096 bytes. Verified by static_assert.
    char header_padding[256
        - sizeof(std::atomic<std::uint32_t>)        // magic
        - sizeof(std::uint32_t)                     // struct_version
        - sizeof(std::uint32_t)                     // struct_size
        - sizeof(std::atomic<std::int32_t>)         // init_state
        - sizeof(std::atomic<float>)                // master_sum_energy
        - sizeof(std::atomic<float>)                // summing_guard_scale
        - sizeof(std::atomic<std::int32_t>)         // guard_active
        - sizeof(std::atomic<std::int32_t>)         // is_standalone_vc_active
        - sizeof(std::atomic<std::uint32_t>) * kModeCount // trim_revision
        - sizeof(std::atomic<float>) * kModeCount         // trim_value_db
        - sizeof(std::atomic<std::int64_t>) * 3];   // last_*_active_us

    // --- Per-instance slots -------------------------------------------------
    alignas(128) InstanceSlot slots[kMaxSlots];     // 16 * 128 = 2048 bytes.

    // --- Trailing reserve so future fields never break the 4096 ABI ---------
    char reserved_padding[4096 - 256 - (128 * kMaxSlots)];
};
static_assert(sizeof(BusLayout) == kBusStructSize,
              "BusLayout must be exactly 4096 bytes (frozen ABI)");
static_assert(offsetof(BusLayout, slots) == 256,
              "slots[] must start at the 256-byte boundary (frozen ABI)");

// -----------------------------------------------------------------------------
//  RAII wrapper that maps the bus, claims a slot and runs maintenance.
// -----------------------------------------------------------------------------
class SharedBus {
public:
    // GC / decay thresholds (microseconds).
    static constexpr std::int64_t kHeartbeatGcUs   = 500'000; // 500 ms -> free slot.
    static constexpr std::int64_t kMeterDecayUs    = 150'000; // 150 ms -> decay meters.
    static constexpr std::int64_t kHeartbeatBeatUs = 100'000; // Own beat cadence.

    SharedBus() = default;

    ~SharedBus() { shutdown(); }

    SharedBus(const SharedBus&)            = delete;
    SharedBus& operator=(const SharedBus&) = delete;

    // Map (creating if needed) the bus and claim a slot for `mode`. Safe to call
    // once per filter instance. Returns true on success.
    bool open(Mode mode) noexcept {
        if (view_) return true;
        if (!map_shared_region()) return false;
        perform_init_handshake();
        my_slot_index_ = claim_slot(mode);
        if (my_slot_index_ < 0) { unmap_shared_region(); return false; }
        // Start the OBS-independent maintenance thread exactly once per process.
        start_maintenance_thread();
        return true;
    }

    // Detach from the bus and release our slot. Idempotent.
    void shutdown() noexcept {
        stop_maintenance_thread();
        if (view_ && my_slot_index_ >= 0) {
            view_->slots[my_slot_index_].reset();
        }
        unmap_shared_region();
        my_slot_index_ = -1;
    }

    bool valid() const noexcept { return view_ != nullptr && my_slot_index_ >= 0; }

    BusLayout*       raw()       noexcept { return view_; }
    const BusLayout* raw() const noexcept { return view_; }

    InstanceSlot& my_slot() noexcept { return view_->slots[my_slot_index_]; }
    int my_index() const noexcept { return my_slot_index_; }

    // ---- Audio-thread safe helpers (wait-free, no allocation) --------------

    // Called once per processed block to keep this slot's liveness fresh.
    void beat() noexcept {
        if (!valid()) return;
        view_->slots[my_slot_index_].heartbeat_us.store(now_us(), std::memory_order_release);
    }

    // Publish the pitch tracker centroid (UNIT 01) for consumers to pick up.
    void publish_voice_center(float hz, float activity) noexcept {
        if (!valid()) return;
        auto& s = view_->slots[my_slot_index_];
        s.voice_center_hz.store(hz, std::memory_order_release);
        s.voice_activity.store(activity, std::memory_order_release);
        if (activity > 0.5f)
            view_->last_voice_active_us.store(now_us(), std::memory_order_release);
    }

    // Read the loudest / most relevant UNIT 01 voice centroid from any process.
    float read_voice_center(float* out_activity = nullptr) const noexcept {
        float best_hz = 1600.0f, best_act = 0.0f;
        if (!view_) { if (out_activity) *out_activity = 0.0f; return best_hz; }
        const std::int64_t t = now_us();
        for (std::size_t i = 0; i < kMaxSlots; ++i) {
            const auto& s = view_->slots[i];
            if (s.in_use.load(std::memory_order_acquire) == 0) continue;
            if (s.mode.load(std::memory_order_acquire) != MODE_VOICE) continue;
            if (t - s.heartbeat_us.load(std::memory_order_acquire) > kHeartbeatGcUs) continue;
            const float a = s.voice_activity.load(std::memory_order_acquire);
            if (a > best_act) { best_act = a; best_hz = s.voice_center_hz.load(std::memory_order_acquire); }
        }
        if (out_activity) *out_activity = best_act;
        return best_hz;
    }

    // Publish this unit's post-fader telemetry for UNIT 05 and the guard.
    void publish_telemetry(float true_peak_db, float rms_db, float st_lufs,
                           float gr_db, float duck_db, float out_energy,
                           bool loud_now) noexcept {
        if (!valid()) return;
        auto& s = view_->slots[my_slot_index_];
        s.true_peak_db.store(true_peak_db, std::memory_order_release);
        s.rms_db.store(rms_db, std::memory_order_release);
        s.short_term_lufs.store(st_lufs, std::memory_order_release);
        s.gr_db.store(gr_db, std::memory_order_release);
        s.duck_db.store(duck_db, std::memory_order_release);
        s.output_energy.store(out_energy, std::memory_order_release);
        s.is_loud_now.store(loud_now ? 1 : 0, std::memory_order_release);
        const std::int64_t t = now_us();
        switch (s.mode.load(std::memory_order_acquire)) {
            case MODE_GAME: if (out_energy > 1.0e-5f) view_->last_game_active_us.store(t, std::memory_order_release); break;
            case MODE_BGM:  if (out_energy > 1.0e-5f) view_->last_bgm_active_us.store(t, std::memory_order_release);  break;
            default: break;
        }
    }

    void set_mic_muted(bool muted) noexcept {
        if (!valid()) return;
        view_->slots[my_slot_index_].is_mic_muted.store(muted ? 1 : 0, std::memory_order_release);
    }

    void set_signal_present(bool present) noexcept {
        if (!valid()) return;
        view_->slots[my_slot_index_].signal_present.store(present ? 1 : 0, std::memory_order_release);
    }

    // The cooperative ceiling scale (0..1). A unit multiplies its limiter
    // threshold by this. Already smoothed by the maintenance follower.
    float summing_guard_scale() const noexcept {
        if (!view_) return 1.0f;
        return view_->summing_guard_scale.load(std::memory_order_acquire);
    }

    bool standalone_vc_active() const noexcept {
        if (!view_) return false;
        return view_->is_standalone_vc_active.load(std::memory_order_acquire) != 0;
    }

    void set_standalone_vc_active(bool active) noexcept {
        if (!view_) return;
        view_->is_standalone_vc_active.store(active ? 1 : 0, std::memory_order_release);
    }

    // "Acapella chat" detector: no game and no bgm energy for >= window_us, and
    // a live voice recently. Used by UNIT 01's -1.5 dB solo pad.
    bool is_acapella_chat(std::int64_t window_us = 5'000'000) const noexcept {
        if (!view_) return false;
        const std::int64_t t = now_us();
        const bool game_quiet = (t - view_->last_game_active_us.load(std::memory_order_acquire)) > window_us;
        const bool bgm_quiet  = (t - view_->last_bgm_active_us.load(std::memory_order_acquire))  > window_us;
        const bool voice_live = (t - view_->last_voice_active_us.load(std::memory_order_acquire)) < 2'000'000;
        return game_quiet && bgm_quiet && voice_live;
    }

    // BGM auto-swell gate: mic explicitly muted/absent AND game silent for 10 s.
    bool should_bgm_swell(std::int64_t hold_us = 10'000'000) const noexcept {
        if (!view_) return false;
        const std::int64_t t = now_us();
        bool any_mic_live = false, mic_muted = true;
        for (std::size_t i = 0; i < kMaxSlots; ++i) {
            const auto& s = view_->slots[i];
            if (s.in_use.load(std::memory_order_acquire) == 0) continue;
            if (s.mode.load(std::memory_order_acquire) != MODE_VOICE) continue;
            if (t - s.heartbeat_us.load(std::memory_order_acquire) > kHeartbeatGcUs) continue;
            any_mic_live = true;
            if (s.is_mic_muted.load(std::memory_order_acquire) == 0 &&
                s.signal_present.load(std::memory_order_acquire) != 0)
                mic_muted = false;
        }
        const bool game_quiet = (t - view_->last_game_active_us.load(std::memory_order_acquire)) > hold_us;
        const bool mic_absent = !any_mic_live || mic_muted;
        const bool voice_recent = (t - view_->last_voice_active_us.load(std::memory_order_acquire)) < hold_us;
        return mic_absent && game_quiet && !voice_recent;
    }

    // ---- Remote-fader protocol ---------------------------------------------

    // UNIT 05 writes a new trim for `mode` and bumps the revision.
    void push_trim(Mode mode, float trim_db) noexcept {
        if (!view_ || mode < 0 || mode >= kModeCount) return;
        view_->trim_value_db[mode].store(trim_db, std::memory_order_release);
        view_->trim_revision[mode].fetch_add(1, std::memory_order_acq_rel);
    }

    // A child unit polls for a revision change; returns true (and the new value)
    // when UNIT 05 moved its fader since `last_seen_rev`.
    bool poll_trim(Mode mode, std::uint32_t& last_seen_rev, float& out_trim_db) const noexcept {
        if (!view_ || mode < 0 || mode >= kModeCount) return false;
        const std::uint32_t rev = view_->trim_revision[mode].load(std::memory_order_acquire);
        if (rev == last_seen_rev) return false;
        last_seen_rev = rev;
        out_trim_db = view_->trim_value_db[mode].load(std::memory_order_acquire);
        return true;
    }

private:
    // ---- Platform mapping ---------------------------------------------------
    bool map_shared_region() noexcept {
#if defined(_WIN32)
        // Null DACL grants every integrity level access to the mapping so an
        // elevated OBS and a standard-user companion can both attach.
        SECURITY_DESCRIPTOR sd;
        if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION)) return false;
        if (!SetSecurityDescriptorDacl(&sd, TRUE, nullptr /*NULL DACL*/, FALSE)) return false;
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = &sd;
        sa.bInheritHandle = FALSE;

        map_handle_ = CreateFileMappingA(
            INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, kBusStructSize,
            "Local\\SEVENTHWELL_MODEL600_BUS_V1");
        if (map_handle_ == nullptr) return false;
        created_ = (GetLastError() != ERROR_ALREADY_EXISTS);
        void* p = MapViewOfFile(map_handle_, FILE_MAP_ALL_ACCESS, 0, 0, kBusStructSize);
        if (!p) { CloseHandle(map_handle_); map_handle_ = nullptr; return false; }
        view_ = reinterpret_cast<BusLayout*>(p);
        return true;
#else
        // POSIX shared memory. Name is "/sw_m600_bus_v1" (15 chars, well under
        // the 31-char limit macOS enforces).
        const char* name = "/sw_m600_bus_v1";
        int fd = ::shm_open(name, O_CREAT | O_RDWR, 0666);
        if (fd >= 0) {
            // ftruncate to the ABI size. If a sibling already sized it this is a
            // harmless no-op; a failure here is non-fatal because the region may
            // already be the right size.
            struct stat st{};
            if (::fstat(fd, &st) == 0 && st.st_size < static_cast<off_t>(kBusStructSize)) {
                created_ = (::ftruncate(fd, kBusStructSize) == 0);
            }
            void* p = ::mmap(nullptr, kBusStructSize, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, 0);
            ::close(fd);
            if (p != MAP_FAILED) {
                view_ = reinterpret_cast<BusLayout*>(p);
                used_posix_shm_ = true;
                return true;
            }
        }
        // Fallback: a file-backed mapping under /tmp for sandboxed hosts that
        // deny POSIX shm objects.
        return map_tmp_fallback();
#endif
    }

#if !defined(_WIN32)
    bool map_tmp_fallback() noexcept {
        const char* path = "/tmp/sw_m600_bus_v1.mmap";
        int fd = ::open(path, O_CREAT | O_RDWR, 0666);
        if (fd < 0) return false;
        struct stat st{};
        if (::fstat(fd, &st) != 0) { ::close(fd); return false; }
        if (st.st_size < static_cast<off_t>(kBusStructSize)) {
            created_ = true;
            if (::ftruncate(fd, kBusStructSize) != 0) { ::close(fd); return false; }
        }
        void* p = ::mmap(nullptr, kBusStructSize, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);
        ::close(fd);
        if (p == MAP_FAILED) return false;
        view_ = reinterpret_cast<BusLayout*>(p);
        used_posix_shm_ = false;
        return true;
    }
#endif

    void unmap_shared_region() noexcept {
        if (!view_) return;
#if defined(_WIN32)
        UnmapViewOfFile(view_);
        if (map_handle_) { CloseHandle(map_handle_); map_handle_ = nullptr; }
#else
        ::munmap(view_, kBusStructSize);
#endif
        view_ = nullptr;
    }

    static std::int32_t self_pid() noexcept {
#if defined(_WIN32)
        return static_cast<std::int32_t>(GetCurrentProcessId());
#else
        return static_cast<std::int32_t>(::getpid());
#endif
    }

    // Lock-free init: the first attacher flips UNINITIALIZED -> INITIALIZING,
    // zeroes everything, then publishes READY with release semantics. Late
    // attachers spin-wait (briefly) on acquire until READY.
    void perform_init_handshake() noexcept {
        std::int32_t expected = BUS_UNINITIALIZED;
        if (view_->init_state.compare_exchange_strong(
                expected, BUS_INITIALIZING,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            // We won the race -> initialize the region from scratch.
            view_->magic.store(0, std::memory_order_relaxed);
            view_->struct_version = kBusStructVersion;
            view_->struct_size    = kBusStructSize;
            view_->master_sum_energy.store(0.0f, std::memory_order_relaxed);
            view_->summing_guard_scale.store(1.0f, std::memory_order_relaxed);
            view_->guard_active.store(0, std::memory_order_relaxed);
            view_->is_standalone_vc_active.store(0, std::memory_order_relaxed);
            for (int m = 0; m < kModeCount; ++m) {
                view_->trim_revision[m].store(0, std::memory_order_relaxed);
                view_->trim_value_db[m].store(0.0f, std::memory_order_relaxed);
            }
            const std::int64_t t = now_us();
            view_->last_voice_active_us.store(t, std::memory_order_relaxed);
            view_->last_game_active_us.store(t, std::memory_order_relaxed);
            view_->last_bgm_active_us.store(t, std::memory_order_relaxed);
            for (std::size_t i = 0; i < kMaxSlots; ++i) view_->slots[i].reset();
            view_->magic.store(kBusMagic, std::memory_order_release);
            view_->init_state.store(BUS_READY, std::memory_order_release);
            return;
        }
        // Someone else is initializing (or already did). Wait for READY.
        for (int spins = 0; spins < 10000; ++spins) {
            if (view_->init_state.load(std::memory_order_acquire) == BUS_READY) break;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }

    // Claim a free slot (or reclaim one whose owner died). GC of stale slots
    // happens inline here so a crashed sibling never blocks a new instance.
    int claim_slot(Mode mode) noexcept {
        const std::int64_t t = now_us();
        const std::int32_t pid = self_pid();
        for (std::size_t i = 0; i < kMaxSlots; ++i) {
            auto& s = view_->slots[i];
            const bool busy = s.in_use.load(std::memory_order_acquire) != 0;
            const bool stale = busy &&
                (t - s.heartbeat_us.load(std::memory_order_acquire) > kHeartbeatGcUs);
            if (!busy || stale) {
                std::int32_t expected = busy ? 1 : 0;
                if (s.in_use.compare_exchange_strong(
                        expected, 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
                    s.reset();
                    s.in_use.store(1, std::memory_order_release);
                    s.pid.store(pid, std::memory_order_release);
                    s.mode.store(mode, std::memory_order_release);
                    s.heartbeat_us.store(t, std::memory_order_release);
                    return static_cast<int>(i);
                }
            }
        }
        return -1; // All 16 slots are live.
    }

    // ---- OBS-independent maintenance thread ---------------------------------
    void start_maintenance_thread() noexcept {
        if (maint_running_.exchange(true)) return; // Already running.
        maint_thread_ = std::thread([this] { maintenance_loop(); });
    }

    void stop_maintenance_thread() noexcept {
        if (!maint_running_.exchange(false)) return;
        if (maint_thread_.joinable()) maint_thread_.join();
    }

    void maintenance_loop() noexcept {
        // ~1 ms cadence so the 0.5 ms attack follower stays well sampled.
        using namespace std::chrono;
        auto last = steady_clock::now();
        while (maint_running_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(milliseconds(1));
            if (!view_) continue;
            const auto nowt = steady_clock::now();
            const float dt = static_cast<float>(duration_cast<microseconds>(nowt - last).count()) * 1.0e-6f;
            last = nowt;
            gc_and_decay();
            update_summing_guard(dt);
            beat(); // Keep our own slot fresh even when audio is paused.
        }
    }

    // Free slots whose owner stopped beating; decay stale meters toward silence.
    void gc_and_decay() noexcept {
        const std::int64_t t = now_us();
        for (std::size_t i = 0; i < kMaxSlots; ++i) {
            auto& s = view_->slots[i];
            if (s.in_use.load(std::memory_order_acquire) == 0) continue;
            const std::int64_t age = t - s.heartbeat_us.load(std::memory_order_acquire);
            if (age > kHeartbeatGcUs) {
                s.reset(); // Crashed owner -> reclaim.
                continue;
            }
            if (age > kMeterDecayUs) {
                // Exponential decay of visible meters toward -120 dB / 0 energy.
                decay_toward(s.true_peak_db, -120.0f, 0.5f);
                decay_toward(s.rms_db, -120.0f, 0.5f);
                decay_toward(s.short_term_lufs, -120.0f, 0.5f);
                decay_toward(s.gr_db, 0.0f, 0.5f);
                decay_toward(s.duck_db, 0.0f, 0.5f);
                s.output_energy.store(s.output_energy.load(std::memory_order_relaxed) * 0.5f,
                                      std::memory_order_release);
            }
        }
    }

    static void decay_toward(std::atomic<float>& v, float target, float coeff) noexcept {
        const float cur = v.load(std::memory_order_relaxed);
        v.store(cur + (target - cur) * coeff, std::memory_order_release);
    }

    // Cooperative Master Summing Guard with asymmetric ballistics. Sums the
    // output energy of every live slot carried on the main stream and, if the
    // combined true peak would cross -1.0 dBFS, computes a ceiling scale and
    // smooths it (attack 0.5 ms pulling down, release 180 ms easing up).
    void update_summing_guard(float dt_s) noexcept {
        float sum_energy = 0.0f;
        for (std::size_t i = 0; i < kMaxSlots; ++i) {
            const auto& s = view_->slots[i];
            if (s.in_use.load(std::memory_order_acquire) == 0) continue;
            sum_energy += s.output_energy.load(std::memory_order_acquire);
        }
        view_->master_sum_energy.store(sum_energy, std::memory_order_release);

        // -1.0 dBFS linear ceiling, squared because we accumulate energy.
        constexpr float kCeilDb   = -1.0f;
        const float ceil_lin      = db_to_lin(kCeilDb);
        const float ceil_energy   = ceil_lin * ceil_lin;
        float target_scale = 1.0f;
        if (sum_energy > ceil_energy && sum_energy > 1.0e-9f) {
            target_scale = ceil_energy / sum_energy; // <1 pulls the ceiling down.
            if (target_scale < 0.05f) target_scale = 0.05f;
        }

        const float cur = view_->summing_guard_scale.load(std::memory_order_acquire);
        const bool pulling = target_scale < cur;
        const float tau = pulling ? 0.0005f : 0.180f; // 0.5 ms attack / 180 ms release.
        const float a = 1.0f - m600_exp(-dt_s / tau);
        const float next = cur + (target_scale - cur) * a;
        view_->summing_guard_scale.store(next, std::memory_order_release);
        view_->guard_active.store(next < 0.999f ? 1 : 0, std::memory_order_release);
    }

    static float db_to_lin(float db) noexcept { return m600_db_to_lin(db); }

    // ---- State --------------------------------------------------------------
    BusLayout* view_ = nullptr;
    int        my_slot_index_ = -1;
    bool       created_ = false;
    bool       used_posix_shm_ = false;
#if defined(_WIN32)
    void*      map_handle_ = nullptr; // HANDLE
#endif
    std::atomic<bool> maint_running_{false};
    std::thread       maint_thread_;
};

} // namespace sw::model600

#endif // SEVENTHWELL_MODEL600_SHARED_STATE_HPP
