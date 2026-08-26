#include "Patch.h"
#include "Logger.h"
#include "Memory.h"
#include <cstring>

namespace BullyDE::Patch {

namespace {
    // Renders a byte range as "AA BB CC" for log output.
    std::string Hex(const uint8_t* data, size_t size) {
        static const char* digits = "0123456789ABCDEF";
        std::string out;
        out.reserve(size * 3);
        for (size_t i = 0; i < size; ++i) {
            if (i) out.push_back(' ');
            out.push_back(digits[data[i] >> 4]);
            out.push_back(digits[data[i] & 0xF]);
        }
        return out;
    }
}

bool Verify(const char* what, uintptr_t addr, const uint8_t* expected, size_t size) {
    const auto* actual = reinterpret_cast<const uint8_t*>(addr);
    if (std::memcmp(actual, expected, size) == 0) {
        return true;
    }
    Logger::Get().Error("Patch",
        "Signature mismatch for {} at 0x{:08X}: expected [{}], found [{}]. Skipping.",
        what, addr, Hex(expected, size), Hex(actual, size));
    return false;
}

namespace {
    template <typename T>
    bool WriteScalar(const char* what, uintptr_t addr, T expectedOld, T value) {
        if (!Verify(what, addr, reinterpret_cast<const uint8_t*>(&expectedOld), sizeof(T))) {
            return false;
        }
        if (!Memory::Write<T>(addr, value)) {
            Logger::Get().Error("Patch", "Write failed for {} at 0x{:08X}", what, addr);
            return false;
        }
        Logger::Get().Info("Patch", "{} @ 0x{:08X}: {} -> {}",
            what, addr, static_cast<uint32_t>(expectedOld), static_cast<uint32_t>(value));
        return true;
    }
}

bool Byte(const char* what, uintptr_t addr, uint8_t expectedOld, uint8_t value) {
    return WriteScalar<uint8_t>(what, addr, expectedOld, value);
}

bool Word(const char* what, uintptr_t addr, uint16_t expectedOld, uint16_t value) {
    return WriteScalar<uint16_t>(what, addr, expectedOld, value);
}

bool Dword(const char* what, uintptr_t addr, uint32_t expectedOld, uint32_t value) {
    return WriteScalar<uint32_t>(what, addr, expectedOld, value);
}

bool Bytes(const char* what, uintptr_t addr,
           const uint8_t* expected, const uint8_t* replacement, size_t size) {
    if (!Verify(what, addr, expected, size)) {
        return false;
    }
    if (!Memory::WriteBytes(addr, replacement, size)) {
        Logger::Get().Error("Patch", "Write failed for {} at 0x{:08X}", what, addr);
        return false;
    }
    Logger::Get().Info("Patch", "{} @ 0x{:08X}: [{}] -> [{}]",
        what, addr, Hex(expected, size), Hex(replacement, size));
    return true;
}

bool Nop(const char* what, uintptr_t addr, const uint8_t* expected, size_t size) {
    if (!Verify(what, addr, expected, size)) {
        return false;
    }
    if (!Memory::WriteNops(addr, size)) {
        Logger::Get().Error("Patch", "Write failed for {} at 0x{:08X}", what, addr);
        return false;
    }
    Logger::Get().Info("Patch", "{} @ 0x{:08X}: [{}] -> {} x NOP",
        what, addr, Hex(expected, size), size);
    return true;
}

} // namespace BullyDE::Patch
