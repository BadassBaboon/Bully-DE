#include "Diagnostics.h"
#include "../Config.h"
#include "../Logger.h"
#include "DrawDistanceFix.h"
#include "StabilityFix.h"
#include <Windows.h>
#include <cstdint>

// ---------------------------------------------------------------------------
// Read-only sampling of the post-processing gate state.
//
// This patches nothing and hooks nothing; it starts a thread that reads a
// handful of globals once a second and logs them when they change. It exists
// because static reasoning about which screen effects run has produced several
// plausible theories that turned out to be wrong, and the only way to settle it
// is to look at the values the game is actually using while it renders.
//
// The two globals that matter most are the pair the dispatcher bails out on:
//
//   55E2C3: cmp byte_BCBB53, 0
//   55E2CA: jnz  bail            ; skips every screen effect
//   55E2D0: cmp byte_C1A998, 0
//   55E2D7: jnz  bail            ; same
//
// If either is non-zero during normal gameplay, no screen effect runs at all
// and every patch downstream of it is irrelevant.
// ---------------------------------------------------------------------------

namespace BullyDE {

namespace {

constexpr uintptr_t kDispatcherGateA = 0x00BCBB53; // byte, non-zero = skip all post FX
constexpr uintptr_t kDispatcherGateB = 0x00C1A998; // byte, non-zero = skip all post FX
constexpr uintptr_t kAreaId          = 0x00BD1008; // read as int by the dispatcher
constexpr uintptr_t kBlurTextureSrc  = 0x00CF12EC; // gBlurTexture source object
constexpr uintptr_t kBlurReadyFlag   = 0x00CF12E0; // set once the gaussian pass has run
constexpr uintptr_t kBloomEnable     = 0x00AC7608; // dword, per-area, copied each frame

struct Sample {
    uint8_t  gateA{}, gateB{};
    uint32_t areaId{};
    uint32_t blurSrc{};
    uint8_t  blurReady{};
    uint32_t bloomEnable{};

    bool operator==(const Sample& o) const {
        return gateA == o.gateA && gateB == o.gateB && areaId == o.areaId
            && blurSrc == o.blurSrc
            && blurReady == o.blurReady && bloomEnable == o.bloomEnable;
    }
};

template <typename T>
T Peek(uintptr_t addr) { return *reinterpret_cast<volatile T*>(addr); }

Sample Read() {
    Sample s;
    s.gateA       = Peek<uint8_t>(kDispatcherGateA);
    s.gateB       = Peek<uint8_t>(kDispatcherGateB);
    s.areaId      = Peek<uint32_t>(kAreaId);
    s.blurSrc     = Peek<uint32_t>(kBlurTextureSrc);
    s.blurReady   = Peek<uint8_t>(kBlurReadyFlag);
    s.bloomEnable = Peek<uint32_t>(kBloomEnable);
    return s;
}

DWORD WINAPI SampleThread(LPVOID) {
    Sample previous{};
    bool haveSample = false;

    for (;;) {
        Sample now;
        __try {
            now = Read();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Sleep(1000);
            continue;
        }

        if (!haveSample || !(now == previous)) {
            Logger::Get().Info("Diag",
                "postfx gates: BCBB53={} C1A998={} | area={} | "
                "blurSrc=0x{:08X} blurReady={} | bloomEnable={}",
                now.gateA, now.gateB, now.areaId,
                now.blurSrc, now.blurReady, now.bloomEnable);

            if (!haveSample && (now.gateA != 0 || now.gateB != 0)) {
                Logger::Get().Warn("Diag",
                    "A dispatcher gate is set, so no screen effect runs at all right now.");
            }
            previous = now;
            haveSample = true;
        }
        Sleep(1000);
    }
    return 0; // not reached; the loop runs for the life of the process
}

// ---------------------------------------------------------------------------
// Counter watcher -- always on, unlike the post-FX sampler above.
//
// Two counters record damage the game does not otherwise report: objects the
// visible-object list had to drop because it was full, and null dereferences
// the heap guards caught. Both were previously only logged from
// DLL_PROCESS_DETACH, which does not run when the process terminates -- and
// quitting a game IS process termination, so neither ever appeared in a log.
//
// Polling them here means the evidence lands in the log while the game is
// running, survives a crash, and carries a timestamp you can line up against
// what you were doing at the time.
// ---------------------------------------------------------------------------
DWORD WINAPI WatchThread(LPVOID) {
    long lastDrops = 0, lastHits = 0;
    int dropQuiet = 0;

    for (;;) {
        Sleep(1000);
        if (dropQuiet > 0) --dropQuiet;

        long drops = 0, hits = 0;
        __try {
            drops = DrawDistanceFix::SectorDrops();
            hits  = StabilityFix::GuardHits();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }

        if (drops > lastDrops && dropQuiet == 0) {
            uint32_t area = 0;
            __try { area = Peek<uint32_t>(kAreaId); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            Logger::Get().Warn("Diag",
                "Visible-object list is full -- {} object(s) dropped so far (+{} since the "
                "last report), current area {}. A dropped object has no mesh and no "
                "collision, so this is what vanishing geometry and falling through floors "
                "look like. Lower LodMultiplier or FarClipOverride.",
                drops, drops - lastDrops, area);
            lastDrops = drops;
            dropQuiet = 10;   // at most one line every 10s while it keeps saturating
        }
        // While inside the quiet window lastDrops is deliberately left alone, so
        // the next line reports the full delta rather than only the last second.

        if (hits > lastHits) {
            Logger::Get().Info("Diag",
                "Heap free-list guards have now caught {} null dereference(s) that the "
                "unpatched game would have performed.", hits);
            lastHits = hits;
        }
    }
    return 0; // not reached
}

} // namespace

bool Diagnostics::Install() {
    // The watcher runs regardless of LogPostFXState: it costs one sleeping
    // thread and it is the only place these counters are ever reported.
    HANDLE watch = CreateThread(nullptr, 0, &WatchThread, nullptr, 0, nullptr);
    if (watch != nullptr) {
        CloseHandle(watch);
        Logger::Get().Info("Diag",
            "Counter watch active: reporting dropped objects and heap guard hits as "
            "they happen. If nothing from [Diag] follows this line, neither occurred.");
    } else {
        Logger::Get().Error("Diag", "Could not start the counter watch thread.");
    }

    if (!Config::Get().Diagnostics().logPostFXState) {
        return true;
    }

    HANDLE thread = CreateThread(nullptr, 0, &SampleThread, nullptr, 0, nullptr);
    if (thread == nullptr) {
        Logger::Get().Error("Diag", "Could not start the post-processing sampling thread.");
        return false;
    }
    CloseHandle(thread);

    Logger::Get().Info("Diag",
        "Sampling post-processing state once a second; a line is written whenever it changes. "
        "This reads memory only and patches nothing.");
    return true;
}

} // namespace BullyDE
