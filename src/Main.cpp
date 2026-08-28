#include <Windows.h>
#include <filesystem>
#include "Config.h"
#include "Logger.h"
#include "UnpackHook.h"
#include "features/ShadowFix.h"
#include "features/BloomFix.h"
#include "features/DrawDistanceFix.h"
#include "features/AAFix.h"
#include "features/GraphicsFix.h"
#include "features/Diagnostics.h"

namespace {
    HMODULE g_hModule = nullptr;
    LONG g_initCount = 0;

    std::filesystem::path GetModuleDirectory(HMODULE hMod) {
        WCHAR pathBuffer[MAX_PATH];
        if (GetModuleFileNameW(hMod, pathBuffer, MAX_PATH)) {
            return std::filesystem::path(pathBuffer).parent_path();
        }
        return std::filesystem::current_path();
    }

    void Initialize() {
        if (_InterlockedCompareExchange(&g_initCount, 1, 0) != 0) {
            return;
        }

        auto moduleDir = GetModuleDirectory(g_hModule);
        auto iniPath = moduleDir / L"Bully-DE.ini";
        auto logPath = moduleDir / L"Bully-DE.log";

        BullyDE::Config::Get().Load(iniPath);
        const auto& general = BullyDE::Config::Get().General();

        BullyDE::Logger::Get().Initialize(logPath, general.logLevel, general.logToFile);
        BullyDE::Logger::Get().Info("Core", "Bully: Definitive Edition (Bully-DE.asi) Initializing...");
        BullyDE::Logger::Get().Info("Core", "Plugin Directory: {}", moduleDir.string());
        BullyDE::Logger::Get().Info("Core", "Config Path: {}", iniPath.string());

        // Register post-unpack hook to install shadow, bloom, draw distance, AA, and graphics patches directly in Gamebryo engine memory
        BullyDE::UnpackHook::Register([]() {
            BullyDE::ShadowFix::Install();
            BullyDE::BloomFix::Install();
            BullyDE::DrawDistanceFix::Install();
            BullyDE::AAFix::Install();
            BullyDE::GraphicsFix::Install();
            BullyDE::Diagnostics::Install();
        });
    }
}

extern "C" __declspec(dllexport) void InitializeASI() {
    Initialize();
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hinstDLL);
        g_hModule = hinstDLL;
        Initialize();
        break;
    }
    case DLL_PROCESS_DETACH: {
        // lpvReserved is non-null when the process is terminating rather than
        // being unloaded with FreeLibrary. At that point every other thread has
        // already been killed, so a thread could have died holding the logger's
        // mutex and taking it here would hang the process on exit. The OS
        // reclaims the file handle regardless, so there is nothing to do.
        if (lpvReserved == nullptr) {
            BullyDE::Logger::Get().Info("Core", "Bully: Definitive Edition unloading.");
            BullyDE::Logger::Get().Shutdown();
        }
        break;
    }
    }
    return TRUE;
}
