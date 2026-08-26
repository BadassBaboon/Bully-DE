#include "ShadowFix.h"
#include "../Config.h"
#include "../Logger.h"
#include "../Memory.h"
#include "../Patch.h"
#include <cstring>

// ---------------------------------------------------------------------------
// All addresses below were verified byte-for-byte against the unpacked image
// (Bully_unpacked.exe, imagebase 0x400000). The comment on each line gives the
// exact vanilla encoding so a mismatch can be detected at install time.
// ---------------------------------------------------------------------------

namespace BullyDE {

namespace {

// --- Shadow map size hint: NiShadowGenerator::m_usSizeHint (this+0x64) ------
// This word is what NiShadowManager actually reads when it asks for a shadow
// map (sub_7CF290 for 2D dir/spot maps, sub_7CF790 for point-light cubes).
constexpr uintptr_t kSizeHint_BullySetup = 0x0040F9A3; // 66 C7 41 64 00 04  mov word [ecx+64h], 400h
constexpr uintptr_t kSizeHint_Ctor1      = 0x007730DB; // 66 C7 46 64 00 04  mov word [esi+64h], 400h
constexpr uintptr_t kSizeHint_Ctor2      = 0x00773180; // 66 C7 46 64 00 04  mov word [esi+64h], 400h

// --- Per-light runtime override of the size hint ----------------------------
// sub_40FB30 re-assigns the size hint from the light definition every time a
// shadow light is set up, clobbering whatever the constructor put there:
//   0x40FCC9: 66 8B 4A 7A   mov cx, [edx+7Ah]   <- lightDef+122
//   0x40FCCD: 66 89 48 64   mov [eax+64h], cx   <- generator+0x64
// Replacing the load with "mov cx, imm16" (66 B9 iw, also 4 bytes) forces the
// resolution for every shadow-casting light. Without this, every other shadow
// patch is inert -- the generator always ends up back at the data-driven size.
constexpr uintptr_t kRuntimeSizeHintLoad = 0x0040FCC9;

// --- Hard 2048 clamp inside the shadow map allocators -----------------------
// sub_7575D0 (2D maps -- the path the sun and every interior spot light use):
//   0x7575D0: B8 00 08 00 00   mov eax, 800h   <- one constant, clamps BOTH
//                                                 width and height
// sub_757980 (cube maps for point lights):
//   0x757985: 81 FB 00 08 00 00  cmp ebx, 800h
//   0x75798E: BB 00 08 00 00     mov ebx, 800h
// Without these, any size hint above 2048 is silently truncated to 2048.
constexpr uintptr_t kClamp2D_Imm   = 0x007575D1;
constexpr uintptr_t kClampCube_Cmp = 0x00757987;
constexpr uintptr_t kClampCube_Mov = 0x0075798F;

// --- NiShadowManager budget, set in its ctor sub_758F50 ---------------------
//   0x758FDB: C7 86 B4 00 00 00 08 00 00 00   mov [esi+0B4h], 8        (generator count)
//   0x758FE5: C7 86 B8 00 00 00 00 00 00 04   mov [esi+0B8h], 4000000h (64 MB budget)
// sub_7575D0 / sub_757980 refuse to allocate once [+0ACh] + newBytes > [+0B8h]
// and return null. A null shadow map means that light stops casting entirely --
// which is exactly the "shadows break at certain distances" regression.
constexpr uintptr_t kGeneratorCount_Imm = 0x00758FE1;
constexpr uintptr_t kShadowBudget_Imm   = 0x00758FEB;

// --- Technique string pushed to the NiShadowTechnique lookup in sub_40F7F0 --
//   0x40F9B4: 68 60 04 90 00   push offset "NiPCFShadowTechnique"
constexpr uintptr_t kTechniquePush_Imm = 0x0040F9B5;
constexpr uint32_t  kStr_PCF           = 0x00900460; // "NiPCFShadowTechnique"
constexpr uint32_t  kStr_Standard      = 0x009516C8; // "NiStandardShadowTechnique"
constexpr uint32_t  kStr_VSM           = 0x00951654; // "NiVSMShadowTechnique"

// --- CShadows::Render (legacy 2D blob decals) -------------------------------
constexpr uintptr_t kBlobShadowRender = 0x005251C0; // 83 EC 20  sub esp, 20h

} // namespace

bool ShadowFix::Install() {
    const auto& config = Config::Get().Shadows();
    if (!config.enabled) {
        Logger::Get().Info("ShadowFix", "Shadow improvements are disabled in configuration.");
        return true;
    }

    const uint32_t res = config.shadowMapResolution;
    Logger::Get().Info("ShadowFix", "Target shadow map resolution: {}x{}", res, res);

    // 1. Raise the allocator clamps first. Nothing has allocated yet at this
    //    point, but if these fail the size hint below is silently truncated,
    //    so they are logged as their own step.
    if (res > 2048) {
        Patch::Dword("2D shadow map dimension clamp", kClamp2D_Imm,   2048, res);
        Patch::Dword("Cube shadow map clamp (cmp)",   kClampCube_Cmp, 2048, res);
        Patch::Dword("Cube shadow map clamp (mov)",   kClampCube_Mov, 2048, res);
    }

    // 2. The runtime per-light override. This is the one that actually decides
    //    the resolution; the constructor hints below only matter for generators
    //    that never pass through sub_40FB30.
    {
        const uint8_t vanilla[] = { 0x66, 0x8B, 0x4A, 0x7A }; // mov cx, [edx+7Ah]
        if (Patch::Verify("Per-light size hint load", kRuntimeSizeHintLoad, vanilla, sizeof(vanilla))) {
            uint8_t patched[4] = { 0x66, 0xB9, 0, 0 };        // mov cx, imm16
            const uint16_t imm = static_cast<uint16_t>(res);
            memcpy(patched + 2, &imm, sizeof(imm));
            if (Memory::WriteBytes(kRuntimeSizeHintLoad, patched, sizeof(patched))) {
                Logger::Get().Info("ShadowFix",
                    "Per-light size hint @ 0x{:08X}: lightDef+122 -> constant {}", kRuntimeSizeHintLoad, res);
            } else {
                Logger::Get().Error("ShadowFix", "Write failed for per-light size hint");
            }
        }
    }

    // 3. Size hints -- the value NiShadowManager reads to size the map.
    Patch::Word("Bully shadow generator size hint", kSizeHint_BullySetup, 1024, static_cast<uint16_t>(res));
    Patch::Word("NiShadowGenerator ctor size hint", kSizeHint_Ctor1,      1024, static_cast<uint16_t>(res));
    Patch::Word("NiShadowGenerator ctor size hint", kSizeHint_Ctor2,      1024, static_cast<uint16_t>(res));

    // 4. Shadow map memory budget. Vanilla is 64 MB, sized for 8 generators at
    //    1024x1024. Every doubling of resolution quadruples the requirement; if
    //    the budget is not raised the allocator hits its ceiling mid-scene and
    //    starts returning null, dropping shadows at distance.
    const uint32_t generators = config.shadowGeneratorCount;
    if (generators != 8) {
        Patch::Dword("Shadow generator count", kGeneratorCount_Imm, 8, generators);
    }

    // 4 bytes per texel covers the widest format the switch in sub_7575D0
    // selects, plus two generators of slack for the reuse pool.
    const uint64_t perMap = static_cast<uint64_t>(res) * res * 4ull;
    uint64_t budget;
    if (config.shadowBudgetMB != 0) {
        budget = static_cast<uint64_t>(config.shadowBudgetMB) * 1024ull * 1024ull;
        Logger::Get().Info("ShadowFix", "Budget overridden from config: {} MB", config.shadowBudgetMB);
    } else {
        budget = perMap * (generators + 2ull);
    }
    if (budget > 0xC0000000ull) budget = 0xC0000000ull; // never exceed 3 GB
    if (budget < 0x4000000ull)  budget = 0x4000000ull;  // never shrink below vanilla
    Patch::Dword("Shadow map memory budget", kShadowBudget_Imm, 0x4000000u, static_cast<uint32_t>(budget));
    Logger::Get().Info("ShadowFix", "Shadow map is {} MB each; budget {} MB ({} generators)",
        perMap / (1024 * 1024), budget / (1024 * 1024), generators);
    if (budget > (1024ull * 1024ull * 1024ull)) {
        Logger::Get().Warn("ShadowFix",
            "Budget exceeds 1 GB of VRAM. If shadows start disappearing at distance, "
            "lower ShadowMapResolution or set ShadowBudgetMB explicitly.");
    }

    // 5. Filtering technique.
    uint32_t techniqueStr = kStr_PCF;
    const char* techniqueName = "NiPCFShadowTechnique";
    if (config.shadowTechnique == 0) {
        techniqueStr = kStr_Standard;
        techniqueName = "NiStandardShadowTechnique";
    } else if (config.shadowTechnique == 2) {
        techniqueStr = kStr_VSM;
        techniqueName = "NiVSMShadowTechnique";
    }
    if (techniqueStr != kStr_PCF) {
        Patch::Dword("Shadow technique", kTechniquePush_Imm, kStr_PCF, techniqueStr);
    }
    Logger::Get().Info("ShadowFix", "Shadow technique: {}", techniqueName);

    // 6. Optional: kill the legacy 2D capsule blob decals. Off by default --
    //    characters do not cast shadow-map shadows in this build, so removing
    //    the blobs leaves them with no ground contact at all.
    if (config.disableBlobShadows) {
        const uint8_t vanilla[] = { 0x83, 0xEC, 0x20 };
        if (Patch::Verify("CShadows::Render", kBlobShadowRender, vanilla, sizeof(vanilla))) {
            if (Memory::Write<uint8_t>(kBlobShadowRender, 0xC3)) {
                Logger::Get().Info("ShadowFix", "Disabled 2D blob shadow decals (CShadows::Render -> ret)");
            }
        }
    }

    Logger::Get().Info("ShadowFix", "Shadow patches applied.");
    return true;
}

} // namespace BullyDE
