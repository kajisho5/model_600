// Headless self-test for the multilingual voice intent parser (Layer 1).
#include "voice-command-multilingual.hpp"
#include <cstdio>
int main() {
    const int fails = sw::model600::voice::run_selftest();
    std::printf("[voice] self-test failures: %d\n", fails);
    return fails == 0 ? 0 : 1;
}
