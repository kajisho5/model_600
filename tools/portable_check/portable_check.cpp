// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  portable_check.cpp  -  Compile-only check of every OBS-independent header.
//
//  Built by the MODEL600_BUILD_PORTABLE_CHECK target (CI on Windows / MSVC and
//  macOS / Apple Clang, warnings as errors) together with the Qt UI sources
//  and the platform voice bridges. The OBS adapters (audio-filter.cpp,
//  plugin-main.cpp) need the libobs SDK and are not part of this check.
//
//  Each header is used once below so its inline code is fully compiled on the
//  target compiler, including the _WIN32 / __APPLE__ branches.
// =============================================================================
#include "dsp-core.hpp"
#include "license-manager.hpp"
#include "os-conflict-shield.hpp"
#include "shared-state.hpp"
#include "update-checker.hpp"
#include "voice-command-multilingual.hpp"

#include <cstdio>

int m600_portable_check_entry();

int m600_portable_check_entry() {
    sw::model600::dsp::Engine engine(sw::model600::MODE_VOICE);
    engine.prepare(48000.0f, 2);
    sw::model600::SharedBus bus;
    std::printf("portable check: %d\n", static_cast<int>(sizeof(bus) > 0));
    return 0;
}
