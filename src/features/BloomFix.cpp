#include "BloomFix.h"
#include "../Config.h"
#include "../Logger.h"
#include "../Patch.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Bully's bloom smears because its blur radius is wide, not because it is
// undersampled.
//
// Both bloom pixel shaders are 58 instructions with 13 texld and contain no
// LOOP or REP, so the blur is fully unrolled and BLUR_SAMPLES is compile-time.
// The C++ reads it back only to size the kernel array it uploads. Thirteen taps
// per axis is already generous; adding more would make the result smoother,
// which is the opposite of what is wanted.
//
// The radius lives in the kernel sub_560480 builds. Each entry is
// offset = tapIndex / divisor, and the divisor is the screen dimension shifted
// right by two:
//
//   5605E7: call sub_405BA0    ; screen dims
//   5605EC: mov  eax, [eax]    ; width
//   5605EE: cdq
//   5605EF: and  edx, 3
//   5605F2: add  eax, edx
//   5605F4: C1 F8 02  sar eax, 2   ; divisor = width / 4    <- horizontal
//   ...
//   56071B: C1 F8 02  sar eax, 2   ; divisor = height / 4   <- vertical
//
// With 13 taps that works out to a +/-24 full-res pixel radius per axis.
// Lowering the shift tightens it: 2 -> 1 halves the radius, 2 -> 0 quarters it.
//
// Only the shift byte changes. The `and edx, 3` above it is the compiler's
// signed-division rounding fixup, and screen dimensions are always positive, so
// edx is zero and the mask is inert whatever the shift is.
//
// Bloom parameters themselves (enable, threshold, strength, scale) come from a
// per-area table at dword_CF0A68 and are copied into dword_AC7608..AC7614 every
// frame, so patching those globals would not stick. The shift is code, so it
// does.
// ---------------------------------------------------------------------------

namespace BullyDE {

namespace {

constexpr uintptr_t kHorizontalShift = 0x005605F6; // imm of `sar eax, 2`
constexpr uintptr_t kVerticalShift   = 0x0056071D; // imm of `sar eax, 2`
constexpr uint8_t   kVanillaShift    = 2;

// --- Overexposure diagnostic ------------------------------------------------
// The bloom call site in sub_55E2C0. dword_AC7608 is a per-area enable, copied
// every frame out of the table at dword_CF0A68, so bloom does not run at all in
// an area whose table entry is zero -- and then no amount of radius tuning is
// visible.
//
//   55E543: 39 2D 08 76 AC 00   cmp dword_AC7608, ebp
//   55E549: 74 24               jz  skip                   <- the enable gate
//   55E551: 8B 15 14 76 AC 00   mov edx, [AC7614]  ; scale
//   55E557: A1 10 76 AC 00      mov eax, [AC7610]  ; strength
//   55E55E: 8B 0D 0C 76 AC 00   mov ecx, [AC760C]  ; threshold
//   55E567: call sub_560480
//
// Forcing the gate open and replacing the three parameter loads with extreme
// constants answers one question: does this code path reach the screen at all?
// A blown-out image means the knobs work and the effect was merely off or
// subtle. No change means sub_560480 never reaches the framebuffer and the
// radius patch was never going to matter.
constexpr uintptr_t kEnableGate    = 0x0055E549;
constexpr uintptr_t kScaleLoad     = 0x0055E551;
constexpr uintptr_t kStrengthLoad  = 0x0055E557;
constexpr uintptr_t kThresholdLoad = 0x0055E55E;

// Vanilla encodings at the three parameter loads.
const uint8_t kScaleVanilla[]     = { 0x8B, 0x15, 0x14, 0x76, 0xAC, 0x00 }; // mov edx, [AC7614]
const uint8_t kStrengthVanilla[]  = { 0xA1, 0x10, 0x76, 0xAC, 0x00 };       // mov eax, [AC7610]
const uint8_t kThresholdVanilla[] = { 0x8B, 0x0D, 0x0C, 0x76, 0xAC, 0x00 }; // mov ecx, [AC760C]
const uint8_t kGateVanilla[]      = { 0x74, 0x24 };                         // jz skip

// Replace `mov <reg>, [global]` with `mov <reg>, imm32`, padding to the same
// length. opcode is the B8+r form for the register the original load targets.
bool OverrideParam(const char* what, uintptr_t addr, const uint8_t* vanilla,
                   size_t size, uint8_t movOpcode, uint32_t value) {
    uint8_t patched[6] = { movOpcode, 0, 0, 0, 0, 0x90 };
    std::memcpy(patched + 1, &value, sizeof(value));
    return Patch::Bytes(what, addr, vanilla, patched, size);
}

// The radius scales by 2^(kVanillaShift - shift), so only powers of two are
// reachable from a single byte.
uint8_t ShiftForRadiusPercent(uint32_t percent) {
    if (percent >= 100) return 2; // vanilla
    if (percent >= 50)  return 1; // half radius
    return 0;                     // quarter radius
}

} // namespace

bool BloomFix::Install() {
    const auto& config = Config::Get().Bloom();
    if (!config.enabled) {
        Logger::Get().Info("BloomFix", "Bloom changes are disabled in configuration.");
        return true;
    }

    // Mode: 0 leaves the per-area enable alone, 1 forces bloom on everywhere,
    // 2 forces it off everywhere.
    if (config.mode == 1) {
        Patch::Nop("Bloom per-area enable gate (force on)", kEnableGate, kGateVanilla, sizeof(kGateVanilla));
    } else if (config.mode == 2) {
        const uint8_t always[] = { 0xEB, 0x24 }; // jz -> jmp, always skip the call
        Patch::Bytes("Bloom per-area enable gate (force off)", kEnableGate,
                     kGateVanilla, always, sizeof(kGateVanilla));
        Logger::Get().Info("BloomFix", "Bloom disabled entirely.");
        return true; // nothing downstream can matter
    }

    // Parameter overrides. -1 keeps whatever the area table supplies.
    if (config.threshold >= 0) {
        OverrideParam("Bloom threshold", kThresholdLoad, kThresholdVanilla,
                      sizeof(kThresholdVanilla), 0xB9 /* mov ecx */,
                      static_cast<uint32_t>(config.threshold));
    }
    if (config.strength >= 0) {
        OverrideParam("Bloom strength", kStrengthLoad, kStrengthVanilla,
                      sizeof(kStrengthVanilla), 0xB8 /* mov eax */,
                      static_cast<uint32_t>(config.strength));
    }
    if (config.scale >= 0) {
        OverrideParam("Bloom scale", kScaleLoad, kScaleVanilla,
                      sizeof(kScaleVanilla), 0xBA /* mov edx */,
                      static_cast<uint32_t>(config.scale));
    }

    const uint8_t shift = ShiftForRadiusPercent(config.radiusPercent);
    if (shift != kVanillaShift) {
        const bool h = Patch::Byte("Bloom blur radius (horizontal)", kHorizontalShift, kVanillaShift, shift);
        const bool v = Patch::Byte("Bloom blur radius (vertical)",   kVerticalShift,   kVanillaShift, shift);
        if (h && v) {
            Logger::Get().Info("BloomFix",
                "Bloom radius set to {}% of vanilla (screen dimension >> {} instead of >> {}).",
                config.radiusPercent, shift, kVanillaShift);
        } else if (h != v) {
            Logger::Get().Warn("BloomFix",
                "Only one bloom axis was patched. The blur is now asymmetric; "
                "set BloomRadiusPercent = 100 to go back to stock.");
        }
    }

    Logger::Get().Info("BloomFix",
        "Bloom: mode={} radius={}% threshold={} strength={} scale={} "
        "(-1 means the game's own per-area value; stock is 230 / 80 / 4).",
        config.mode, config.radiusPercent, config.threshold, config.strength, config.scale);
    return true;
}

} // namespace BullyDE
