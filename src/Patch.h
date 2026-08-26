#pragma once
#include <cstdint>
#include <cstddef>

// Verified patching helpers shared by the feature modules.
//
// Every write checks the bytes it is about to overwrite against the vanilla
// encoding first. A wrong address produces a log line and a skipped patch
// rather than a corrupted instruction stream.

namespace BullyDE::Patch {

// Returns true when the bytes at `addr` match `expected`. Logs and returns
// false otherwise.
bool Verify(const char* what, uintptr_t addr, const uint8_t* expected, size_t size);

// Each of these verifies `expectedOld` at `addr` before writing `value`.
// Returns true only when the site matched and the write succeeded.
bool Byte(const char* what, uintptr_t addr, uint8_t expectedOld, uint8_t value);
bool Word(const char* what, uintptr_t addr, uint16_t expectedOld, uint16_t value);
bool Dword(const char* what, uintptr_t addr, uint32_t expectedOld, uint32_t value);

// Verifies `expected` at `addr`, then writes `replacement`. Both buffers must
// be `size` bytes, so the patch cannot change instruction lengths.
bool Bytes(const char* what, uintptr_t addr,
           const uint8_t* expected, const uint8_t* replacement, size_t size);

// Verifies `expected` at `addr`, then fills the range with 0x90.
bool Nop(const char* what, uintptr_t addr, const uint8_t* expected, size_t size);

} // namespace BullyDE::Patch
