#include "DrawDistanceFix.h"
#include "CoronaPatches.h"
#include "../Config.h"
#include "../Logger.h"
#include "../Memory.h"
#include "../Patch.h"
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace BullyDE {

namespace {

// ---------------------------------------------------------------------------
// Helper to install a relative JMP + NOP padding hook
// ---------------------------------------------------------------------------
static bool InstallJmpHook(uintptr_t site, const void* hookFunc, size_t totalBytes) {
    if (totalBytes < 5) return false;
    std::vector<uint8_t> patch(totalBytes, 0x90);
    patch[0] = 0xE9;
    int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(hookFunc) - (site + 5));
    std::memcpy(&patch[1], &rel, 4);
    return Memory::WriteBytes(site, patch.data(), totalBytes);
}

// 1. LOD Capacity Pools in sub_44D320
struct LodPoolDef {
    const char* name;
    uintptr_t immAddr;
    uint32_t vanillaVal;
    // Pool 7 holds LOD mesh slots and is the one pool that runs out first when
    // draw distance grows, because every distant building needs a slot whether
    // or not it is on screen. Scaling it from its tiny vanilla 220 the way the
    // others scale would still starve it, so it is given a flat 2048 floor and
    // grows from there. Every other pool scales linearly from its vanilla value.
    bool useFlatFloor;
};

constexpr LodPoolDef kLodPools[] = {
    { "LOD Pool 1 (Buildings/World)",        0x0044D34F, 15000, false },
    { "LOD Pool 2 (Static Props)",           0x0044D38D,  2000, false },
    { "LOD Pool 3 (Render Nodes)",           0x0044D3CB,    15, false },
    { "LOD Pool 4 (Dynamic Props)",          0x0044D693,    57, false },
    { "LOD Pool 5 (Small Objects)",          0x0044D6D1,     8, false },
    { "LOD Pool 6 (Light References)",       0x0044D70F,   300, false },
    { "LOD Pool 7 (LOD Meshes)",             0x0044D74D,   220,  true },
    { "LOD Pool 8 (World Geometry)",         0x0044D78B,  2250, false },
    { "LOD Pool 9 (Root)",                   0x0044D7C9,     1, false },
    { "LOD Pool 10 (Render Instances)",      0x0044D841,  4150, false },
    { "LOD Pool 11 (Effects/Decals)",        0x0044D87F,    87, false },
    { "LOD Pool 12 (Shadow/Light Casters)",  0x0044D8BD,   275, false },
    { "LOD Pool 13 (Close Occluders)",       0x0044D8FB,    35, false },
    { "LOD Pool 14 (Animated Geometry)",     0x0044D939,    30, false },
};

// 2. Pedestrian and Vehicle population buffers in sub_6D3EA0
// --- PedPop.dat control variables, scaled in memory after the file is parsed --
//
// sub_49C3D0 parses Config\Dat\PedPop.dat into the object pointed at by
// dword_C2C108. The first data line is scanned as "%f %f %f %f %f %f %f %i %i"
// straight into consecutive float slots, and the header in the .dat names them:
//
//   base[7690] m_fOnScreenCullRange     base[7693] m_fOffScreenCullRange
//   base[7691] m_fOnScreenMinRadius     base[7694] m_fOffScreenMinRadius
//   base[7692] m_fOnScreenMaxRadius     base[7695] m_fOffScreenMaxRadius
//   base[7708] m_fOffScreenHeightCull
//   base[7709] m_nChancePerFrame (int)  base[7710] m_nNumAttempts (int)
//
// The parser then copies 7690..7695 into 7696..7701, so both sets have to move
// together or the game reverts to the unscaled copy.
//
// Scaling the ranges here rather than shipping an edited PedPop.dat keeps the
// mod self-contained: the player's own data file is read normally and only the
// in-memory values change, so a custom PedPop.dat still works and is scaled on
// top of whatever it contains.
//
// The minimum radii are deliberately left alone. They set how close a ped may
// spawn to the camera, and raising them with everything else makes NPCs pop in
// on top of the player.
constexpr uintptr_t kPedPopParseCall = 0x0049F1A0; // call sub_49C3D0
constexpr uintptr_t kPedPopParseFunc = 0x0049C3D0;

static float s_pedPopScale = 1.0f;

// Counters so the parser hook can report what the density pass actually did.
// Hook_PedPopParsed wraps the whole of sub_49C3D0, and the row loop runs inside
// it, so every row has been through here by the time the summary is logged.
static uint32_t s_rowsScaled = 0;
static uint32_t s_popBefore = 0;
static uint32_t s_popAfter = 0;


static void __cdecl ScalePedPopRanges(float* base) {
    if (base == nullptr || s_pedPopScale <= 1.0f) {
        return;
    }
    const float k = s_pedPopScale;

    // Ranges and maxima scale; minima (7691, 7694) stay put.
    const int scaled[] = { 7690, 7692, 7693, 7695, 7708 };
    for (int idx : scaled) {
        base[idx] *= k;
    }
    // Mirror the parser's own copy of 7690..7695 so it cannot revert.
    for (int i = 0; i < 6; ++i) {
        base[7696 + i] = base[7690 + i];
    }

    if (s_rowsScaled > 0) {
        Logger::Get().Info("DrawDistanceFix",
            "PedPop density scaled {:.2f}x across {} rows: total population {} -> {}",
            s_pedPopScale, s_rowsScaled, s_popBefore, s_popAfter);
    } else {
        Logger::Get().Warn("DrawDistanceFix",
            "PedPop density hook installed but no rows were scaled. Population counts "
            "are unchanged; only spawn distance is affected.");
    }

    Logger::Get().Info("DrawDistanceFix",
        "PedPop ranges scaled {:.2f}x: onScreenCull={:.0f} onScreenMax={:.0f} "
        "offScreenCull={:.0f} offScreenMax={:.0f} heightCull={:.0f} "
        "(min radii left at {:.0f}/{:.0f})",
        k, base[7690], base[7692], base[7693], base[7695], base[7708],
        base[7691], base[7694]);
}

// --- Per-area population counts -------------------------------------------
//
// The control line scaled above sets how FAR pedestrians spawn. How MANY spawn
// is a separate set of per-area, per-time-period rows further down the same
// file, parsed by sub_4645F0. In sub_49C3D0 the record is addressed as:
//
//   49C566: imul ecx, 1D8h            ; areaId * 0x1D8
//   49C56D: lea  eax, [edi+edi*8]     ; period * 9
//   49C570: add  ecx, esi             ; + table base
//   49C572: lea  ecx, [ecx+eax*4+4]   ; + period*36 + 4
//   49C576: call sub_4645F0
//
// so each area holds four 36-byte rows, one per time period. Inside a row:
//
//   rec[+0x04] int   total
//   rec[+0x08] bytes per-category counts, one per column in the file
//   rec[+0x14] int   write cursor, which ends up holding how many were written
//
// The file's own totals are the sum of the categories ("7, 0,0,0,0,0,0,2,0,0,4,
// 0,1" is 2+4+1), so the categories are scaled and the total recomputed from
// them rather than scaled independently.
//
// sub_4645F0 is __thiscall with one stack argument and ends in `retn 4`, so the
// hook has to re-push the line pointer and clean the caller's argument itself.
constexpr uintptr_t kPopRowParseCall = 0x0049C576; // call sub_4645F0, ped rows only
constexpr uintptr_t kPopRowParseFunc = 0x004645F0;

static void __cdecl ScalePopRow(uint8_t* rec) {
    if (rec == nullptr || s_pedPopScale <= 1.0f) {
        return;
    }
    const uint32_t written = *reinterpret_cast<const uint32_t*>(rec + 0x14);
    if (written == 0 || written > 12) {
        return; // not a row shape we recognise; leave it alone
    }

    uint32_t sum = 0;
    for (uint32_t i = 0; i < written; ++i) {
        uint32_t v = static_cast<uint32_t>(rec[8 + i] * s_pedPopScale);
        if (v > 255) v = 255;
        rec[8 + i] = static_cast<uint8_t>(v);
        sum += v;
    }
    s_popBefore += static_cast<uint32_t>(*reinterpret_cast<const int32_t*>(rec + 4));
    *reinterpret_cast<int32_t*>(rec + 4) = static_cast<int32_t>(sum);
    s_popAfter += sum;
    ++s_rowsScaled;
}

static_assert(kPopRowParseFunc == 0x004645F0,
              "keep the literal in Hook_PopRowParsed in step with kPopRowParseFunc");

__declspec(naked) static void Hook_PopRowParsed() {
    __asm {
        // On entry: ecx = row record, [esp] = return address, [esp+4] = line ptr.
        push ecx                     // save the record; line is now at [esp+8]
        push dword ptr [esp+8]       // re-push the line argument
        mov  ecx, [esp+4]            // record back into ecx for the thiscall
        mov  eax, 0x004645F0         // kPopRowParseFunc
        call eax                     // callee cleans the pushed argument (retn 4)
        pop  ecx                     // our saved record
        push ecx
        call ScalePopRow
        add  esp, 4
        ret  4                       // clean the caller's argument
    }
}

static_assert(kPedPopParseFunc == 0x0049C3D0,
              "keep the literal in Hook_PedPopParsed in step with kPedPopParseFunc");

__declspec(naked) static void Hook_PedPopParsed() {
    __asm {
        push ecx                    // preserve the this pointer
        // Literal rather than the constant: MSVC inline asm treats a constexpr
        // symbol as a MEMORY operand, so `mov eax, kPedPopParseFunc` assembles to
        // `mov eax, dword ptr [&kPedPopParseFunc]`. That happens to work, but it
        // is not what the line appears to say. The static_assert above keeps the
        // two in step.
        mov  eax, 0x0049C3D0        // kPedPopParseFunc
        call eax                    // run the original parser (this already in ecx)
        pop  ecx                    // ecx = base again
        push ecx
        call ScalePedPopRanges
        add  esp, 4
        ret
    }
}

constexpr uintptr_t kPedPool1_Size      = 0x006D3F0A;
constexpr uintptr_t kPedPool1_LoopBound = 0x006D3F4E;
constexpr uintptr_t kPedPool1_Alloc     = 0x006D40BF;
constexpr uintptr_t kPedPool2_Alloc     = 0x006D426C;

// 3. Camera Far Clip Plane in sub_452F20 (Sector Raymarcher)
constexpr uintptr_t kFarClipFrustum1 = 0x00453046;
constexpr uintptr_t kFarClipFrustum2 = 0x004530BA;
static double s_customFarClip = 1200.0;

// 4. Distance Culling Early-Out Bypasses in sub_511130 and sub_5115A0
constexpr uintptr_t kCullingEarlyOut1 = 0x005111D0;
constexpr uintptr_t kCullingEarlyOut2 = 0x0051175E;

// 5. Global LOD Distance Multiplier Hook
constexpr uintptr_t kCameraInitHookSite   = 0x004F3720;
constexpr uintptr_t kCameraInitResume     = 0x004F372E;
constexpr uintptr_t kGlobalLodVar         = 0x00C3CD00;

static float s_customLodMult = 2.0f;

__declspec(naked) static void Hook_CameraInit() {
    __asm {
        push ebx
        xor ebx, ebx
        push esi
        mov esi, ecx
        mov eax, dword ptr [s_customLodMult]
        mov [esi+98h], eax
        jmp dword ptr [kCameraInitResume]
    }
}

// 6. Force High-Detail LOD Models
constexpr uintptr_t kForceHighLodSite = 0x005273F4;

// 7. Frustum Sector Traversal & Overflow Guard
constexpr uintptr_t kAllSectorTraversalSite = 0x0045302D;
constexpr uintptr_t kSectorGuardSite1       = 0x004525C1;
constexpr uintptr_t kSectorGuardSite2       = 0x004526D2;
constexpr uintptr_t kSectorGuardSite3       = 0x00452891;
constexpr uintptr_t kSectorGuardSite4       = 0x004529E2;
constexpr uintptr_t kSectorGuardSite5       = 0x00452C22;

__declspec(naked) static void Hook_AllSectorTraversal() {
    __asm {
        mov byte ptr ds:[0x00C13F04], 0
        mov edi, 0x00C1B178
        mov esi, 0x510 // 1296 sectors (36 x 36)

    loc_sector_loop:
        push edi
        mov  eax, 0x00452430
        call eax
        add  esp, 4
        add  edi, 0x14
        dec  esi
        jne  loc_sector_loop

        push 0x004539D3
        ret
    }
}

__declspec(naked) static void Hook_SectorGuard1() {
    __asm {
        cmp dword ptr ds:[0x00C13F00], 0x7CF
        jae loc_skip1
        mov ecx, ds:[0x00C11D60]
        mov [ecx], esi
        push 0x004525C9
        ret
    loc_skip1:
        push 0x004525D7
        ret
    }
}

__declspec(naked) static void Hook_SectorGuard2() {
    __asm {
        cmp dword ptr ds:[0x00C13F00], 0x7CF
        jae loc_skip2
        mov eax, ds:[0x00C11D60]
        mov [eax], esi
        push 0x004526D9
        ret
    loc_skip2:
        push 0x004526E7
        ret
    }
}

__declspec(naked) static void Hook_SectorGuard3() {
    __asm {
        cmp dword ptr ds:[0x00C13F00], 0x7CF
        jae loc_skip3
        mov edx, ds:[0x00C11D60]
        mov [edx], esi
        push 0x00452899
        ret
    loc_skip3:
        push 0x004528A6
        ret
    }
}

__declspec(naked) static void Hook_SectorGuard4() {
    __asm {
        cmp dword ptr ds:[0x00C13F00], 0x7CF
        jae loc_skip4
        mov ecx, ds:[0x00C11D60]
        mov [ecx], esi
        push 0x004529EA
        ret
    loc_skip4:
        push 0x004529F7
        ret
    }
}

__declspec(naked) static void Hook_SectorGuard5() {
    __asm {
        cmp dword ptr ds:[0x00C13F00], 0x7CF
        jae loc_skip5
        mov ecx, ds:[0x00C11D60]
        mov [ecx], esi
        push 0x00452C2A
        ret
    loc_skip5:
        push 0x00452C38
        ret
    }
}

// 8. Distant Horizon Cliff & Mountain Terrain Draw Distance
// sub_516DC0:
//   0x00516EC9: 85 DB                         test ebx, ebx            ; 2 bytes
//   0x00516ECB: 8B 9C 24 AC 00 00 00          mov ebx, [esp+0ACh]       ; 7 bytes
//   0x00516ED2: 0F 84 F2 02 00 00             je loc_5171CA            ; 6 bytes
// Total 9 bytes replaced. Next instruction begins at 0x00516ED2.
constexpr uintptr_t kTerrainDrawDistSite = 0x00516EC9;

static void __stdcall CheckTerrainModel(uint8_t* entity, const char* name) {
    if (!entity || !name) return;
    __try {
        if (strstr(name, "cliff") != nullptr || strstr(name, "Cliff") != nullptr ||
            strstr(name, "rock")  != nullptr || strstr(name, "Rock")  != nullptr ||
            strstr(name, "terrain") != nullptr) {
            entity[0x1A] = 0xE0; // Maximum draw distance flag
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

__declspec(naked) static void Hook_TerrainDrawDistance() {
    __asm {
        pushad
        test edi, edi
        je   loc_skip

        // [esp + 0xA0] was the model info pointer. pushad added 0x20 bytes.
        mov  eax, [esp + 0xC0]
        test eax, eax
        je   loc_skip

        mov  eax, [eax + 8] // model name string pointer
        test eax, eax
        je   loc_skip

        push eax
        push edi
        call CheckTerrainModel

    loc_skip:
        popad

        test ebx, ebx
        mov  ebx, [esp + 0xAC]
        push 0x00516ED2 // Resumes cleanly at 0x00516ED2 (je loc_5171CA)
        ret
    }
}

// 9. Radar / Minimap Ambient Ped Blip Filter
// sub_402E90:
//   0x00403083: 8B 92 48 01 00 00 -> mov edx, [edx+148h] (followed by call edx at 0x40309A)
//   The virtual method is __stdcall with 7 arguments (28 bytes = 0x1C).
constexpr uintptr_t kRadarBlipFilterSite = 0x00403083;

__declspec(naked) static void Hook_RadarBlipFilter() {
    __asm {
        push ebp
        mov  ebp, esp
        sub  esp, 0x18

        // Query property 0x1B
        lea  eax, [ebp - 4]
        push eax
        push 0x1B
        mov  ecx, [ebp + 8]
        push ecx
        mov  edx, [ecx]
        call dword ptr [edx + 0xE8]

        // Query property 0x0F
        lea  eax, [ebp - 8]
        push eax
        push 0x0F
        mov  ecx, [ebp + 8]
        push ecx
        mov  edx, [ecx]
        call dword ptr [edx + 0xE8]

        // Query property 0x0E
        lea  eax, [ebp - 0x0C]
        push eax
        push 0x0E
        mov  ecx, [ebp + 8]
        push ecx
        mov  edx, [ecx]
        call dword ptr [edx + 0xE8]

        // If any property is 0, render the blip
        cmp  dword ptr [ebp - 4], 0
        je   loc_draw
        cmp  dword ptr [ebp - 8], 0
        je   loc_draw
        cmp  dword ptr [ebp - 0x0C], 0
        je   loc_draw

        // Unimportant ambient pedestrian -> skip drawing
        // Pop 7 arguments (28 bytes = 0x1C) and return
        leave
        ret  0x1C

    loc_draw:
        mov  ecx, [ebp + 8]
        mov  edx, [ecx]
        mov  edx, [edx + 0x148]
        leave
        jmp  edx
    }
}

// 10. NiCamera::SetViewFrustum (DirectX 9 Hardware Projection Matrix Near/Far Clip)
// In sub_762D80:
//   0x00762DDF: D9 42 14 D9 99 18 01 00 00 8A 42 18 88 81 1C 01 00 00 C2 04 00 (21 bytes)
constexpr uintptr_t kNiCameraFrustumSite = 0x00762DDF;
static float s_customFarClipFloat = 600.0f; // kept in step with s_customFarClip below

// Near plane. The engine ships 0.25 m and that is the default here too.
//
// Raising it buys depth precision: what matters for Z-fighting is the far/near
// ratio, the timecycle carries a NearFarRatio column set to 2500, and Gamebryo
// tracks m_fMaxFarNearRatio. At the shipped 600 m far clip, 0.25 m gives 2400:1,
// which is inside that budget, so vanilla needs no help.
//
// Raising it is NOT free, which an earlier version of this comment claimed. The
// near plane is the distance at which geometry starts being clipped away, and
// cutscene cameras sit well inside a metre of characters' faces. At 1.0 m the
// front of the model is clipped and you see through into the hollow interior.
//
// So this only wants raising alongside a much larger FarClipOverride, and even
// then 0.5 is a safer step than 1.0.
static float s_nearPlane = 0.25f;

__declspec(naked) static void Hook_NiCameraFrustum() {
    __asm {
        mov eax, [edx + 0x14]
        cmp eax, 0x41200000 // 10.0f (2D UI / radar / HUD camera)
        je  loc_ui_cam

        // 3D World Camera:
        mov eax, dword ptr [s_nearPlane]
        mov dword ptr [ecx + 0x114], eax         // m_fNear, configurable (engine ships 0.25f)
        mov eax, dword ptr [s_customFarClipFloat] // m_fFar

    loc_ui_cam:
        mov dword ptr [ecx + 0x118], eax
        mov al, byte ptr [edx + 0x18]
        mov byte ptr [ecx + 0x11C], al
        ret 4
    }
}

} // namespace

bool DrawDistanceFix::Install() {
    const auto& config = Config::Get().DrawDistance();
    if (!config.enabled) {
        Logger::Get().Info("DrawDistanceFix", "Draw distance enhancements are disabled in configuration.");
        return true;
    }

    const float mult = std::clamp(config.lodMultiplier, 0.0f, 10.0f);
    Logger::Get().Info("DrawDistanceFix", "Applying Draw Distance & LOD Enhancements (Multiplier: {:.2f}x)...", mult);

    // 1. Scale LOD Object Pool Capacities (clamp to vanilla minimum so 0.0 doesn't crash)
    for (const auto& pool : kLodPools) {
        uint32_t newVal;
        if (pool.useFlatFloor) {
            newVal = static_cast<uint32_t>(2048.0f * (mult > 0.0f ? (mult / 2.0f) : 1.0f));
            if (newVal < 2048) newVal = 2048;
        } else {
            newVal = static_cast<uint32_t>(pool.vanillaVal * (mult > 1.0f ? mult : 1.0f));
        }
        Patch::Dword(pool.name, pool.immAddr, pool.vanillaVal, newVal);
    }

    // 2. Expand Pedestrian & Vehicle Population Pools
    if (config.extendPedPools) {
        // The loop bound is twice the pool size in vanilla (980 for 490), so it
        // has to move with it or the game iterates past the array it allocated.
        const uint32_t pedPool = std::clamp(config.pedPoolSize, 490u, 8192u);
        const uint32_t vehPool = std::clamp(config.vehiclePoolSize, 250u, 8192u);
        Logger::Get().Info("DrawDistanceFix",
            "Expanding population pools: pedestrians {} -> {}, vehicles {} -> {}...",
            490, pedPool, 250, vehPool);
        Patch::Dword("Ped population pool size",        kPedPool1_Size,       490, pedPool);
        Patch::Dword("Ped population loop bound",       kPedPool1_LoopBound,  980, pedPool * 2);
        Patch::Dword("Ped population allocation size",  kPedPool1_Alloc,      490, pedPool);
        Patch::Dword("Vehicle population pool size",    kPedPool2_Alloc,      250, vehPool);
    }

    // 2b. Scale the PedPop.dat ranges in memory, which is what actually asks the
    //     game to populate more of the map. Without this the pool patches above
    //     only raise the ceiling and nothing fills it.
    if (config.pedPopScale > 1.0f) {
        s_pedPopScale = std::clamp(config.pedPopScale, 1.0f, 6.0f);
        // The full 5-byte call, not just the E8. A one-byte check would accept
        // any call at this address, including one a different plugin had already
        // installed, and we would then chain into its hook instead of the parser.
        //   rel32 = 0x0049C3D0 - (0x0049F1A0 + 5) = -11733 = 0xFFFFD22B
        const uint8_t vanillaCall[5] = { 0xE8, 0x2B, 0xD2, 0xFF, 0xFF };
        if (Patch::Verify("PedPop parse call", kPedPopParseCall, vanillaCall, sizeof(vanillaCall))) {
            if (Memory::WriteCall(kPedPopParseCall, reinterpret_cast<uintptr_t>(&Hook_PedPopParsed))) {
                Logger::Get().Info("DrawDistanceFix",
                    "PedPop range scaling hook installed @ 0x{:08X} ({:.2f}x).",
                    kPedPopParseCall, s_pedPopScale);
            } else {
                Logger::Get().Error("DrawDistanceFix", "Failed to hook the PedPop parser.");
            }
        }

        // Per-area counts. rel32 = 0x004645F0 - (0x0049C576 + 5) = -228737
        const uint8_t vanillaRowCall[5] = { 0xE8, 0x75, 0x80, 0xFC, 0xFF };
        if (Patch::Verify("PedPop row parse call", kPopRowParseCall, vanillaRowCall, sizeof(vanillaRowCall))) {
            if (Memory::WriteCall(kPopRowParseCall, reinterpret_cast<uintptr_t>(&Hook_PopRowParsed))) {
                Logger::Get().Info("DrawDistanceFix",
                    "PedPop density hook installed @ 0x{:08X} ({:.2f}x).",
                    kPopRowParseCall, s_pedPopScale);
            } else {
                Logger::Get().Error("DrawDistanceFix", "Failed to hook the PedPop row parser.");
            }
        }
    }

    // 3. Camera Far Clip Plane Override (Sector Traverser)
    if (config.farClipOverride > 0.0f) {
        s_customFarClip = static_cast<double>(config.farClipOverride);
    } else {
        s_customFarClip = 300.0 * (mult > 0.0f ? static_cast<double>(mult) : 0.1);
    }
    Logger::Get().Info("DrawDistanceFix", "Overriding Camera Far Clip Plane to {:.1f}m (vanilla 300.0m)...", s_customFarClip);
    // These two sites hold the ADDRESS of the float the game loads, not the value.
    // The log prints both as plain integers, which reads like a corrupted number,
    // so say what they actually are.
    Patch::Dword("Camera far clip constant pointer 1", kFarClipFrustum1, 0x00906530,
                 reinterpret_cast<uintptr_t>(&s_customFarClip));
    Patch::Dword("Camera far clip constant pointer 2", kFarClipFrustum2, 0x00906530,
                 reinterpret_cast<uintptr_t>(&s_customFarClip));
    Logger::Get().Info("DrawDistanceFix",
        "  (0x00906530 was the game's own far-clip constant; both sites now point at "
        "our {:.1f}m value instead.)", s_customFarClip);

    // 4. Object Distance Culling Bypass
    if (config.bypassDistanceCulling) {
        Logger::Get().Info("DrawDistanceFix", "Bypassing early-out distance culling checks...");
        const uint8_t vanillaCull1[] = { 0x0F, 0x84, 0x9E, 0x02, 0x00, 0x00 };
        Patch::Nop("Distance Culling Bypass 1", kCullingEarlyOut1, vanillaCull1, sizeof(vanillaCull1));

        const uint8_t vanillaCull2[] = { 0x0F, 0x85, 0x4B, 0x0D, 0x00, 0x00 };
        Patch::Nop("Distance Culling Bypass 2", kCullingEarlyOut2, vanillaCull2, sizeof(vanillaCull2));
    }

    // 5. Global LOD Distance Multiplier Hook & Memory Write
    s_customLodMult = mult;
    Memory::Write<float>(kGlobalLodVar, s_customLodMult);

    const uintptr_t hookAddr = reinterpret_cast<uintptr_t>(Hook_CameraInit);
    const int32_t relOffset = static_cast<int32_t>(hookAddr - (kCameraInitHookSite + 5));

    uint8_t patchBytes[6];
    patchBytes[0] = 0xE9; // JMP rel32
    std::memcpy(&patchBytes[1], &relOffset, 4);
    patchBytes[5] = 0x90; // NOP

    const uint8_t vanillaBytes[6] = { 0xD9, 0xE8, 0x53, 0x33, 0xDB, 0x56 };
    if (Patch::Bytes("Camera LOD Multiplier Hook", kCameraInitHookSite, vanillaBytes, patchBytes, sizeof(patchBytes))) {
        Logger::Get().Info("DrawDistanceFix", "Camera LOD Multiplier Hook installed (LOD scale: {:.2f}x).", s_customLodMult);
    }

    // 6. Force High-Detail Models (No LOD Switching)
    if (config.forceHighDetailModels) {
        Logger::Get().Info("DrawDistanceFix", "Forcing high-detail models everywhere (disabling LOD mesh switching)...");
        const uint8_t vanillaHighLod[] = { 0x75, 0x06 };
        Patch::Nop("Force High Detail Models", kForceHighLodSite, vanillaHighLod, sizeof(vanillaHighLod));
    }

    // 7. Frustum Sector Traversal & Overflow Guard
    if (config.enableSectorOverflowGuard) {
        Logger::Get().Info("DrawDistanceFix", "Installing Frustum Sector Traversal & Overflow Guards...");

        // Traversal loop over all 1296 sectors
        const uint8_t vanillaTrav[9] = { 0xD9, 0xEE, 0xC6, 0x05, 0x04, 0x3F, 0xC1, 0x00, 0x00 };
        if (Patch::Verify("All-sector traversal site", kAllSectorTraversalSite, vanillaTrav, sizeof(vanillaTrav))) {
            InstallJmpHook(kAllSectorTraversalSite, Hook_AllSectorTraversal, 9);
            Logger::Get().Info("DrawDistanceFix", "Installed 1296-sector traversal hook @ 0x{:08X}.", kAllSectorTraversalSite);
        }

        // 5 bounds-check insertion guards in sub_452430
        const uint8_t vanillaGuard1[8] = { 0x8B, 0x0D, 0x60, 0x1D, 0xC1, 0x00, 0x89, 0x31 };
        if (Patch::Verify("Sector guard 1", kSectorGuardSite1, vanillaGuard1, sizeof(vanillaGuard1))) {
            InstallJmpHook(kSectorGuardSite1, Hook_SectorGuard1, 8);
        }

        const uint8_t vanillaGuard2[7] = { 0xA1, 0x60, 0x1D, 0xC1, 0x00, 0x89, 0x30 };
        if (Patch::Verify("Sector guard 2", kSectorGuardSite2, vanillaGuard2, sizeof(vanillaGuard2))) {
            InstallJmpHook(kSectorGuardSite2, Hook_SectorGuard2, 7);
        }

        const uint8_t vanillaGuard3[8] = { 0x8B, 0x15, 0x60, 0x1D, 0xC1, 0x00, 0x89, 0x32 };
        if (Patch::Verify("Sector guard 3", kSectorGuardSite3, vanillaGuard3, sizeof(vanillaGuard3))) {
            InstallJmpHook(kSectorGuardSite3, Hook_SectorGuard3, 8);
        }

        const uint8_t vanillaGuard4[8] = { 0x8B, 0x0D, 0x60, 0x1D, 0xC1, 0x00, 0x89, 0x31 };
        if (Patch::Verify("Sector guard 4", kSectorGuardSite4, vanillaGuard4, sizeof(vanillaGuard4))) {
            InstallJmpHook(kSectorGuardSite4, Hook_SectorGuard4, 8);
        }

        const uint8_t vanillaGuard5[8] = { 0x8B, 0x0D, 0x60, 0x1D, 0xC1, 0x00, 0x89, 0x31 };
        if (Patch::Verify("Sector guard 5", kSectorGuardSite5, vanillaGuard5, sizeof(vanillaGuard5))) {
            InstallJmpHook(kSectorGuardSite5, Hook_SectorGuard5, 8);
        }
        Logger::Get().Info("DrawDistanceFix", "Installed 5 sector insertion bounds guards.");
    }

    // 8. Corona / Visible Light Table Expansion (56 -> 1024)
    if (config.extendCoronaBuffer) {
        Logger::Get().Info("DrawDistanceFix", "Expanding Corona/Light table from 56 to 1024 slots...");
        DWORD oldProtect = 0;
        if (VirtualProtect(reinterpret_cast<LPVOID>(0x020F4000), 0x20000, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            std::memset(reinterpret_cast<void*>(0x020F4000), 0, 0x20000);

            size_t applied = 0;
            for (size_t i = 0; i < kCoronaPatchesCount; ++i) {
                const auto& cp = kCoronaPatches[i];
                if (Patch::Bytes("Corona patch", cp.address, cp.vanilla, cp.patched, cp.size)) {
                    applied++;
                }
            }
            Logger::Get().Info("DrawDistanceFix", "Applied {}/{} corona table expansion patches.", applied, kCoronaPatchesCount);
        } else {
            Logger::Get().Error("DrawDistanceFix", "Failed to unprotect .bind memory for corona table @ 0x020F4000.");
        }
    }

    // 9. Horizon Cliff & Mountain Terrain Draw Distance
    if (config.extendTerrainDrawDistance) {
        const uint8_t vanillaTerrain[9] = { 0x85, 0xDB, 0x8B, 0x9C, 0x24, 0xAC, 0x00, 0x00, 0x00 };
        if (Patch::Verify("Terrain draw distance check", kTerrainDrawDistSite, vanillaTerrain, sizeof(vanillaTerrain))) {
            InstallJmpHook(kTerrainDrawDistSite, Hook_TerrainDrawDistance, 9);
            Logger::Get().Info("DrawDistanceFix", "Horizon terrain & cliff draw distance hook installed @ 0x{:08X}.", kTerrainDrawDistSite);
        }
    }

    // 10. Radar / Minimap Ambient Ped Blip Filter
    if (config.filterRadarPedBlips) {
        const uint8_t vanillaRadar[6] = { 0x8B, 0x92, 0x48, 0x01, 0x00, 0x00 };
        if (Patch::Verify("Radar ped blip dispatch", kRadarBlipFilterSite, vanillaRadar, sizeof(vanillaRadar))) {
            uint8_t patch[6];
            patch[0] = 0xBA; // mov edx, imm32
            const uint32_t funcAddr = reinterpret_cast<uint32_t>(&Hook_RadarBlipFilter);
            std::memcpy(&patch[1], &funcAddr, 4);
            patch[5] = 0x90; // NOP
            if (Memory::WriteBytes(kRadarBlipFilterSite, patch, sizeof(patch))) {
                Logger::Get().Info("DrawDistanceFix", "Minimap ped blip filter hook installed @ 0x{:08X}.", kRadarBlipFilterSite);
            }
        }
    }

    // 11. NiCamera::SetViewFrustum (DirectX 9 Hardware Projection Matrix Near/Far Clip)
    if (config.farClipOverride > 0.0f) {
        s_customFarClipFloat = config.farClipOverride;
    } else {
        s_customFarClipFloat = static_cast<float>(s_customFarClip);
    }
    s_nearPlane = std::clamp(config.nearPlane, 0.05f, 10.0f);
    {
    }
    const uint8_t vanillaFrustum[21] = {
        0xD9, 0x42, 0x14,
        0xD9, 0x99, 0x18, 0x01, 0x00, 0x00,
        0x8A, 0x42, 0x18,
        0x88, 0x81, 0x1C, 0x01, 0x00, 0x00,
        0xC2, 0x04, 0x00
    };
    if (Patch::Verify("NiCamera::SetViewFrustum", kNiCameraFrustumSite, vanillaFrustum, sizeof(vanillaFrustum))) {
        InstallJmpHook(kNiCameraFrustumSite, Hook_NiCameraFrustum, 21);
        const float ratio = s_customFarClipFloat / (s_nearPlane > 0.0f ? s_nearPlane : 0.25f);
        Logger::Get().Info("DrawDistanceFix",
            "NiCamera 3D view frustum hook installed (Far: {:.1f}m, Near: {:.2f}m, ratio {:.0f}:1).",
            s_customFarClipFloat, s_nearPlane, ratio);
        if (s_nearPlane > 0.25f) {
            Logger::Get().Warn("DrawDistanceFix",
                "NearPlane is raised above the engine's 0.25. This clips geometry closer "
                "than {:.2f}m from the camera, and cutscene cameras sit within a metre of "
                "characters' faces -- check a cutscene for clipping through models. Only "
                "0.25 is verified safe.", s_nearPlane);
        }
        if (ratio > 2500.0f) {
            Logger::Get().Warn("DrawDistanceFix",
                "Far/near ratio {:.0f}:1 exceeds the 2500:1 the timecycle's NearFarRatio column "
                "assumes. Expect Z-fighting on distant coplanar surfaces; raise NearPlane or "
                "lower FarClipOverride.", ratio);
        }
    }

    Logger::Get().Info("DrawDistanceFix", "Draw distance enhancements successfully active.");
    return true;
}

} // namespace BullyDE
