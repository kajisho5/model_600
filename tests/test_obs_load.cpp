// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  test_obs_load.cpp - loads the built plugin into a real libobs, like OBS does.
//
//    m600_test_obs_load <path/to/model600.so> <data dir> <config dir> <expected filters>
//
//  QApplication (headless) -> obs_startup -> obs_reset_audio -> obs_open_module /
//  obs_init_module -> every MODEL 600 filter type: defaults, create, properties,
//  update, release -> obs_shutdown (runs obs_module_unload). Fails on a load
//  error, a missing filter, a null source / properties, or an error-level log
//  line from the plugin.
// =============================================================================
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <QApplication>
#include <QElapsedTimer>

#include <obs.h>
#include <obs-module.h>
#include <obs-nix-platform.h>

#include <X11/Xlib.h>   // After Qt: Xlib's macros (None, Bool, ...) clash with Qt headers.

namespace {

int g_plugin_errors = 0;

void log_handler(int level, const char* fmt, va_list args, void*) {
    char buf[4096];
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    const char* tag = level <= LOG_ERROR ? "error" : level == LOG_WARNING ? "warn " : level == LOG_INFO ? "info " : "debug";
    std::printf("[%s] %s\n", tag, buf);
    if (level <= LOG_ERROR && std::strstr(buf, "MODEL 600")) ++g_plugin_errors;
}

void pump(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <model600 module> <data dir> <config dir> <expected filters>\n", argv[0]);
        return 2;
    }
    const std::string module_path = argv[1], data_dir = argv[2], config_dir = argv[3];
    const int expected = std::atoi(argv[4]);

    QApplication app(argc, argv);                 // OBS's frontend owns one; the plugin uses Qt timers/network.
    base_set_log_handler(log_handler, nullptr);

    // OBS's Linux frontend hands libobs its X11 connection before obs_startup
    // (run under Xvfb in CI).
    Display* x11 = XOpenDisplay(nullptr);
    if (!x11) {
        std::printf("FAIL no X display (run under xvfb-run)\n");
        return 1;
    }
    obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL);
    obs_set_nix_platform_display(x11);

    int fails = 0;
    if (!obs_startup("en-US", config_dir.c_str(), nullptr)) {
        std::printf("FAIL obs_startup\n");
        return 1;
    }
    obs_audio_info ai = {};
    ai.samples_per_sec = 48000;
    ai.speakers = SPEAKERS_STEREO;
    if (!obs_reset_audio(&ai)) { std::printf("FAIL obs_reset_audio\n"); ++fails; }

    obs_module_t* module = nullptr;
    const int rc = obs_open_module(&module, module_path.c_str(), data_dir.c_str());
    if (rc != MODULE_SUCCESS || !module) {
        std::printf("FAIL obs_open_module rc=%d\n", rc);
        obs_shutdown();
        return 1;
    }
    if (!obs_init_module(module)) {
        std::printf("FAIL obs_init_module (obs_module_load returned false)\n");
        obs_shutdown();
        return 1;
    }
    std::printf("ok   module loaded: %s\n", obs_get_module_name(module) ? obs_get_module_name(module) : "?");

    std::vector<std::string> ids;
    const char* id = nullptr;
    for (size_t i = 0; obs_enum_filter_types(i, &id); ++i)
        if (id && std::strncmp(id, "model600_", 9) == 0) ids.emplace_back(id);
    if (static_cast<int>(ids.size()) != expected) {
        std::printf("FAIL %zu MODEL 600 filters registered, expected %d\n", ids.size(), expected);
        ++fails;
    } else {
        std::printf("ok   %zu filters registered\n", ids.size());
    }

    for (const std::string& fid : ids) {
        obs_data_t* defaults = obs_get_source_defaults(fid.c_str());
        obs_source_t* src = obs_source_create_private(fid.c_str(), fid.c_str(), defaults);
        if (!src) {
            std::printf("FAIL %s: create returned null\n", fid.c_str());
            ++fails;
            obs_data_release(defaults);
            continue;
        }
        const char* display = obs_source_get_display_name(fid.c_str());
        obs_properties_t* props = obs_source_properties(src);
        int nprops = 0;
        for (obs_property_t* p = props ? obs_properties_first(props) : nullptr; p; obs_property_next(&p)) ++nprops;
        obs_source_update(src, defaults);
        pump(150);                                  // Let timers / queued Qt work run against the live filter.
        const bool ok = props != nullptr && display != nullptr;
        if (!ok) ++fails;
        std::printf("%s %s \"%s\": %d properties\n", ok ? "ok  " : "FAIL", fid.c_str(), display ? display : "(null)", nprops);
        obs_properties_destroy(props);
        obs_source_release(src);
        obs_data_release(defaults);
    }

    pump(300);
    obs_shutdown();                                 // Unloads the module (obs_module_unload).
    std::printf("%s shutdown clean\n", obs_initialized() ? "FAIL" : "ok  ");
    if (obs_initialized()) ++fails;
    XCloseDisplay(x11);

    if (g_plugin_errors) {
        std::printf("FAIL %d error-level log line(s) from the plugin\n", g_plugin_errors);
        ++fails;
    }
    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
