// One libFuzzer binary per target: -DMC_FUZZ_TARGET=<index into targets()> via MC_FUZZ_NAME.
#include <cstring>

#include "targets.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n) {
    static const mc::fuzz::Target* t = [] {
        size_t c;
        const auto* all = mc::fuzz::targets(c);
        for (size_t i = 0; i < c; ++i)
            if (!std::strcmp(all[i].name, MC_FUZZ_NAME)) return &all[i];
        __builtin_trap();
    }();
    t->fn(d, n);
    return 0;
}
