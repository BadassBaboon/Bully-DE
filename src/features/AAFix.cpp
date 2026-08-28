#include "AAFix.h"
#include "../Config.h"
#include "../Logger.h"
#include "../Memory.h"
#include "../Patch.h"
#include <cstring>

namespace BullyDE {

namespace {

// There is deliberately no "flickering texture" fix here.
//
// An earlier attempt wrote 0xEB over 0x005E6837, which is not an opcode but the
// displacement of an existing `jz short loc_5E6841` at
// 0x5E6836. That retargets the jump to 0x5E6923 -- inside a different function
// (sub_5E6920) and in the middle of `mov edi, ecx`, so execution resumed on the
// 0xF9 byte as `stc`, ran that function's body without its prologue, and hit a
// pop-based epilogue against the wrong stack frame.
//
// No displacement from that jz can express a correct fix; every reachable
// target is inside sub_5E6510 or past its end.
//
// Flickering under MSAA is also not purely a code problem -- it comes from the
// alpha-tested textures themselves, so it is not something an ASI can fix on
// its own regardless of where the patch goes.

// Vegetation & Wire Fence Alpha-To-Coverage (ATOC/A2M) Fix
// sub_8826E0 initializes hardware Alpha-To-Coverage (NVIDIA ATOC / ATI A2M).
// In vanilla Bully, a flawed bitmask check (shr ax, 9; test bl, al) caused ATOC
// to never activate on foliage and wire fences when MSAA is engaged, resulting
// in bright white outlines / halos around every leaf, bush, and chain-link fence.
// We intercept the check at 0x008827A3 (12 bytes) and properly enable ATOC.
constexpr uintptr_t kAtocSite = 0x008827A3;

__declspec(naked) void Hook_VegetationATOC() {
    __asm {
        // Test bit 1 of [ebp+19h] (standard alpha-testing flag)
        test byte ptr [ebp + 0x19], 2
        jne  enable_atoc

        // Check if called from the specific foliage / fence render pass (0x00889234)
        cmp  dword ptr [esp + 0x24], 0x00889234
        jne  disable_atoc

        // Test bit 6 of [ebp+19h] (foliage / fence geometry flag)
        test byte ptr [ebp + 0x19], 0x40
        je   disable_atoc

    enable_atoc:
        push 0x008827AF  // Resume at ATOC / A2M activation
        ret

    disable_atoc:
        push 0x00882829  // Skip ATOC / A2M activation
        ret
    }
}

} // namespace

bool AAFix::Install() {
    const auto& config = Config::Get().AntiAliasing();
    if (!config.enabled) {
        Logger::Get().Info("AAFix", "Anti-Aliasing bug fixes are disabled in configuration.");
        return true;
    }

    Logger::Get().Info("AAFix", "Applying Anti-Aliasing Bug Fixes (MSAA compatibility)...");

    if (config.fixVegetationOutlines) {
        const uint8_t vanilla[12] = {
            0x66, 0x8B, 0x45, 0x18, // mov ax, [ebp+18h]
            0x66, 0xC1, 0xE8, 0x09, // shr ax, 9
            0x84, 0xC3,             // test bl, al
            0x74, 0x7A              // jz short loc_882829
        };

        if (Patch::Verify("Foliage ATOC outline check", kAtocSite, vanilla, sizeof(vanilla))) {
            uint8_t patch[12];
            memset(patch, 0x90, sizeof(patch)); // Fill with NOPs
            patch[0] = 0xE9; // Relative JMP
            const int32_t relOffset = static_cast<int32_t>(
                reinterpret_cast<uintptr_t>(&Hook_VegetationATOC) - (kAtocSite + 5));
            memcpy(patch + 1, &relOffset, sizeof(relOffset));

            if (Memory::WriteBytes(kAtocSite, patch, sizeof(patch))) {
                Logger::Get().Info("AAFix", "Foliage & fence MSAA Alpha-To-Coverage hook installed @ 0x{:08X}.", kAtocSite);
            } else {
                Logger::Get().Error("AAFix", "Failed to install foliage ATOC hook @ 0x{:08X}.", kAtocSite);
            }
        }
    }

    Logger::Get().Info("AAFix", "Anti-Aliasing fixes successfully active.");
    return true;
}

} // namespace BullyDE
