#include "UnpackHook.h"
#include "Config.h"
#include "Logger.h"
#include "Memory.h"
#include <Windows.h>
#include <vector>
#include <atomic>
#include <fstream>

namespace BullyDE {

namespace {
    std::vector<UnpackHook::OnUnpackedCallback> g_callbacks;
    std::atomic<bool> g_hookTriggered{ false };

    decltype(&SystemParametersInfoA) g_origSystemParametersInfoA = nullptr;
    void* g_iatTargetEntry = nullptr;
    uint8_t g_origPrologue[5]{ 0 };
    bool g_hookedViaInline = false;

    void DumpUnpackedExecutable() {
        HMODULE hExe = GetModuleHandleW(nullptr);
        if (!hExe) return;

        uint8_t* base = reinterpret_cast<uint8_t*>(hExe);
        size_t imageSize = Memory::GetModuleSize(reinterpret_cast<uintptr_t>(hExe));
        if (imageSize == 0) imageSize = 0x2000000; // ~32MB fallback

        // Written next to the .asi so the mod does not depend on a particular drive
        // layout. Load this in IDA; it maps at 0x400000, so addresses match the
        // running process with no rebasing.
        const auto dumpPath = Config::Get().GetIniPath().parent_path() / L"Bully_unpacked.exe";

        std::ofstream dumpFile(dumpPath, std::ios::binary);
        if (dumpFile.is_open()) {
            dumpFile.write(reinterpret_cast<const char*>(base), imageSize);
            dumpFile.close();
            Logger::Get().Info("UnpackHook", "Dumped unpacked image to {} ({} MB)",
                dumpPath.string(), imageSize / (1024 * 1024));
        } else {
            Logger::Get().Error("UnpackHook", "Could not open {} for writing", dumpPath.string());
        }
    }
}

bool UnpackHook::IsUnpacked() {
    __try {
        const uint32_t magic = *reinterpret_cast<const uint32_t*>(0x860C6B);
        return magic == 0xFEFD85C7;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void UnpackHook::Trigger() {
    if (g_hookTriggered.exchange(true)) {
        return;
    }

    Logger::Get().Info("UnpackHook", "Executable unpacked in memory. Initializing modules...");

    // Dump decrypted memory for IDA reverse engineering (debug aid, off by default)
    if (Config::Get().Shadows().dumpUnpackedBinary) {
        DumpUnpackedExecutable();
    }

    for (const auto& cb : g_callbacks) {
        if (cb) {
            cb();
        }
    }
}

BOOL WINAPI UnpackHook::HookedSystemParametersInfoA(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni) {
    if (g_iatTargetEntry) {
        Memory::ScopedProtect protect(g_iatTargetEntry, sizeof(void*));
        if (protect.IsValid()) {
            *reinterpret_cast<void**>(g_iatTargetEntry) = reinterpret_cast<void*>(g_origSystemParametersInfoA);
        }
    } else if (g_hookedViaInline && g_origSystemParametersInfoA) {
        Memory::WriteBytes(reinterpret_cast<uintptr_t>(g_origSystemParametersInfoA), g_origPrologue, sizeof(g_origPrologue));
    }

    Trigger();

    if (g_origSystemParametersInfoA) {
        return g_origSystemParametersInfoA(uiAction, uiParam, pvParam, fWinIni);
    }
    return FALSE;
}

bool UnpackHook::Register(OnUnpackedCallback callback) {
    // Registering after the unpack has already fired would queue the callback
    // somewhere nothing drains it, so run it now instead.
    if (g_hookTriggered.load()) {
        if (callback) {
            callback();
        }
        return true;
    }

    g_callbacks.push_back(callback);

    if (IsUnpacked()) {
        Logger::Get().Info("UnpackHook", "Process memory already unpacked. Triggering callback directly.");
        Trigger();
        return true;
    }

    if (g_origSystemParametersInfoA != nullptr) {
        return true;
    }

    // GetModuleHandleW only, no LoadLibrary fallback: this runs from DllMain
    // under the loader lock, where calling LoadLibrary can deadlock. Any process
    // that opens a window already has user32.dll resident, so the fallback was
    // never reachable in practice.
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) {
        Logger::Get().Error("UnpackHook", "USER32.DLL is not loaded; cannot install the unpack hook.");
        return false;
    }

    g_origSystemParametersInfoA = reinterpret_cast<decltype(&SystemParametersInfoA)>(GetProcAddress(hUser32, "SystemParametersInfoA"));
    if (!g_origSystemParametersInfoA) {
        Logger::Get().Error("UnpackHook", "Failed to locate SystemParametersInfoA.");
        return false;
    }

    HMODULE hExe = GetModuleHandleW(nullptr);
    if (hExe) {
        uint8_t* base = reinterpret_cast<uint8_t*>(hExe);
        auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
        if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE) {
            auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dosHeader->e_lfanew);
            if (ntHeaders->Signature == IMAGE_NT_SIGNATURE) {
                auto& importDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
                if (importDir.VirtualAddress != 0) {
                    auto importDesc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base + importDir.VirtualAddress);
                    while (importDesc->Name != 0) {
                        const char* modName = reinterpret_cast<const char*>(base + importDesc->Name);
                        if (_stricmp(modName, "USER32.DLL") == 0) {
                            auto thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + importDesc->FirstThunk);
                            auto origThunk = importDesc->OriginalFirstThunk ? 
                                reinterpret_cast<PIMAGE_THUNK_DATA>(base + importDesc->OriginalFirstThunk) : thunk;

                            while (origThunk->u1.AddressOfData != 0) {
                                if (!(origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                                    auto importByName = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + origThunk->u1.AddressOfData);
                                    if (strcmp(reinterpret_cast<const char*>(importByName->Name), "SystemParametersInfoA") == 0) {
                                        g_iatTargetEntry = &thunk->u1.Function;
                                        Memory::ScopedProtect protect(g_iatTargetEntry, sizeof(void*));
                                        if (protect.IsValid()) {
                                            *reinterpret_cast<void**>(g_iatTargetEntry) = reinterpret_cast<void*>(&HookedSystemParametersInfoA);
                                            Logger::Get().Info("UnpackHook", "Installed IAT hook on SystemParametersInfoA successfully.");
                                            return true;
                                        }
                                    }
                                }
                                ++thunk;
                                ++origThunk;
                            }
                        }
                        ++importDesc;
                    }
                }
            }
        }
    }

    memcpy(g_origPrologue, reinterpret_cast<const void*>(g_origSystemParametersInfoA), sizeof(g_origPrologue));
    g_hookedViaInline = true;
    if (Memory::WriteJmp(reinterpret_cast<uintptr_t>(g_origSystemParametersInfoA), reinterpret_cast<uintptr_t>(&HookedSystemParametersInfoA))) {
        Logger::Get().Info("UnpackHook", "Installed inline hook on SystemParametersInfoA.");
        return true;
    }

    Logger::Get().Error("UnpackHook", "Failed to hook SystemParametersInfoA.");
    return false;
}

} // namespace BullyDE
