#pragma once
#include <cstdint>
#include <cstddef>

namespace BullyDE {

struct CoronaPatch {
    uintptr_t address;
    size_t size;
    uint8_t vanilla[16];
    uint8_t patched[16];
};

extern const CoronaPatch kCoronaPatches[97];
constexpr size_t kCoronaPatchesCount = 97;

} // namespace BullyDE
