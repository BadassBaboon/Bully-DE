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

extern const CoronaPatch kCoronaPatches[98];
constexpr size_t kCoronaPatchesCount = 98;

// The count and the array extent must agree, or the install loop walks off the
// end of the table. Deriving one from the other removes the chance of updating
// a patch list and forgetting the count.
static_assert(sizeof(kCoronaPatches) / sizeof(kCoronaPatches[0]) == kCoronaPatchesCount,
              "kCoronaPatchesCount does not match the size of kCoronaPatches");

} // namespace BullyDE
