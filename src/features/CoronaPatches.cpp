#include "CoronaPatches.h"
#include "../Logger.h"
#include "../Memory.h"
#include "../Patch.h"
#include <array>
#include <cstring>

namespace BullyDE {
namespace {

constexpr uintptr_t kVanillaBase = 0x00C660A0;
constexpr uint16_t kVanillaSlots = 56;
constexpr uint16_t kSlots = 1024;
constexpr size_t kStride = 0x30;
alignas(16) uint8_t s_coronas[kSlots * kStride]{};

struct CoronaPatch {
    uintptr_t address;
    size_t size;
    uint8_t vanilla[12];
    uint8_t replacement[12];
};

template <typename T>
CoronaPatch Operand(uintptr_t address, T vanilla, T replacement) {
    CoronaPatch patch{address, sizeof(T), {}, {}};
    std::memcpy(patch.vanilla, &vanilla, sizeof(T));
    std::memcpy(patch.replacement, &replacement, sizeof(T));
    return patch;
}

// The update loop compares a field pointer to the same field one slot past
// the table. Only the comparison uses that address.
CoronaPatch Field(uintptr_t address, uintptr_t offset, bool end = false) {
    const auto base = reinterpret_cast<uintptr_t>(s_coronas);
    return Operand(address, kVanillaBase + offset + (end ? kVanillaSlots * kStride : 0),
                   base + offset + (end ? sizeof(s_coronas) : 0));
}

CoronaPatch Limit(uintptr_t address, unsigned subtract = 0) {
    return Operand<uint16_t>(address, static_cast<uint16_t>(kVanillaSlots - subtract),
                             static_cast<uint16_t>(kSlots - subtract));
}

CoronaPatch ClearLoop(uintptr_t address) {
    // pop ebx / mov ecx, count / mov eax, &slots[0].owner / nop
    CoronaPatch patch{address, 12,
        {0x8D, 0x4B, 0x38, 0xB8, 0xAC, 0x60, 0xC6, 0x00, 0x5B, 0x8D, 0x49, 0x00},
        {0x5B, 0xB9, 0, 0, 0, 0, 0xB8, 0, 0, 0, 0, 0x90}};
    const uint32_t count = kSlots;
    const auto owner = reinterpret_cast<uintptr_t>(s_coronas) + 0x0C;
    std::memcpy(patch.replacement + 2, &count, sizeof(count));
    std::memcpy(patch.replacement + 7, &owner, sizeof(owner));
    return patch;
}

} // namespace

void InstallCoronaPatches() {
    const CoronaPatch patches[] = {
        ClearLoop(0x00510E54),
        Field(0x00510F05, 0x2C),
        Field(0x00511113, 0x2C, true),
        Field(0x005112BD, 0x0C),
        Limit(0x005112C8),
        Limit(0x005112CE),
        Field(0x005112EB, 0x0C),
        Limit(0x005112F7),
        Limit(0x005112FD),
        Field(0x00511310, 0x2E),
        Field(0x0051131C, 0x2C),
        Field(0x00511323, 0x2E),
        Field(0x00511334, 0x0C),
        Field(0x00511358, 0x28),
        Field(0x0051136B, 0x29),
        Field(0x0051138B, 0x2A),
        Field(0x00511391, 0x2B),
        Field(0x00511399, 0x00),
        Field(0x005113A2, 0x04),
        Field(0x005113AB, 0x14),
        Field(0x005113B9, 0x18),
        Field(0x005113BF, 0x08),
        Field(0x005113CD, 0x1C),
        Field(0x005113D3, 0x10),
        Field(0x005113D9, 0x2D),
        Field(0x005113F1, 0x2E),
        Field(0x005113FA, 0x2E),
        Field(0x0051140B, 0x2C),
        Field(0x00511421, 0x0C),
        Field(0x00511430, 0x2E),
        Field(0x00511443, 0x20),
        Field(0x00511456, 0x2E),
        Field(0x00511468, 0x2E),
        Field(0x0051153B, 0x0C),
        Limit(0x00511546),
        Limit(0x0051154C),
        Field(0x0051155C, 0x2C),
        Field(0x00511565, 0x0C),
        Field(0x00511579, 0x00),
        Field(0x00511582, 0x04),
        Field(0x0051158B, 0x18),
        Field(0x00511591, 0x08),
        Limit(0x00511655),
        Field(0x005116BC, 0x2E),
        Field(0x005116CB, 0x2E),
        Field(0x005116D8, 0x0C),
        Field(0x005116E5, 0x2C),
        Field(0x005116EE, 0x2B),
        Field(0x00511702, 0x10),
        Field(0x00511729, 0x00),
        Field(0x00511744, 0x2E),
        Field(0x0051174A, 0x1C),
        Field(0x00511774, 0x1C),
        Field(0x00511755, 0x2E),
        Field(0x0051179A, 0x2C),
        Field(0x005117E0, 0x10),
        Field(0x00511802, 0x0C),
        Field(0x00511841, 0x10),
        Field(0x0051185D, 0x20),
        Field(0x00511863, 0x10),
        Field(0x0051187C, 0x18),
        Field(0x005118FD, 0x2E),
        Field(0x00511903, 0x14),
        Field(0x00511913, 0x2E),
        Field(0x0051191F, 0x14),
        Field(0x00511A42, 0x0C),
        Field(0x00511B23, 0x20),
        Field(0x00511B5F, 0x2A),
        Field(0x00511BC0, 0x29),
        Field(0x00511BFD, 0x28),
        Field(0x00511C5E, 0x14),
        Field(0x00511C78, 0x14),
        Field(0x00511CB6, 0x2A),
        Field(0x00511CD4, 0x29),
        Field(0x00511D09, 0x28),
        Field(0x00511DAA, 0x14),
        Field(0x00511DB0, 0x2E),
        Field(0x00511DC4, 0x2E),
        Field(0x00511DD4, 0x14),
        Field(0x00511ED1, 0x0C),
        Field(0x00511FA9, 0x20),
        Field(0x00511FE5, 0x2A),
        Field(0x0051202A, 0x14),
        Field(0x0051206E, 0x29),
        Field(0x0051209B, 0x28),
        Field(0x00512144, 0x2A),
        Field(0x0051216E, 0x29),
        Field(0x0051219B, 0x28),
        Field(0x0051222E, 0x2E),
        Field(0x00512236, 0x2D),
        Field(0x00512244, 0x2E),
        Field(0x005122CF, 0x2E),
        Field(0x0051237B, 0x2A),
        Field(0x005123A6, 0x29),
        Field(0x005123D7, 0x28),
        Field(0x005124A8, 0x2E),
        Limit(0x008DD4A3, 1),
        Field(0x008DD4A8, 0x08),
    };

    // This runs once at startup. Secure every write before changing either
    // the capacity or a pointer. Array destruction restores overlapping page
    // protections in reverse acquisition order, including on an early return.
    std::array<std::optional<Memory::ScopedProtect>, std::size(patches)> writable;
    for (size_t i = 0; i < std::size(patches); ++i) {
        const auto& patch = patches[i];
        if (!writable[i].emplace(patch.address, patch.size).IsValid()) {
            Logger::Get().Error("DrawDistanceFix", "Cannot write corona patch at 0x{:08X}. Skipping expansion.", patch.address);
            return;
        }
        if (!Patch::Verify("Corona expansion", patch.address, patch.vanilla, patch.size)) {
            return;
        }
    }
    for (const auto& patch : patches) {
        auto* site = reinterpret_cast<void*>(patch.address);
        std::memcpy(site, patch.replacement, patch.size);
        FlushInstructionCache(GetCurrentProcess(), site, patch.size);
    }
    Logger::Get().Info("DrawDistanceFix", "Expanded corona table to {} slots ({} patches).", kSlots, std::size(patches));
}

} // namespace BullyDE
