#include "Memory.h"
#include <sstream>
#include <iomanip>
#include <Psapi.h>

namespace BullyDE::Memory {

bool WriteBytes(uintptr_t address, const void* data, size_t size) {
    ScopedProtect protect(address, size);
    if (!protect.IsValid()) return false;
    memcpy(reinterpret_cast<void*>(address), data, size);
    return true;
}

bool WriteNops(uintptr_t address, size_t count) {
    ScopedProtect protect(address, count);
    if (!protect.IsValid()) return false;
    memset(reinterpret_cast<void*>(address), 0x90, count);
    return true;
}

bool WriteCall(uintptr_t hookAddress, uintptr_t targetFunction) {
    ScopedProtect protect(hookAddress, 5);
    if (!protect.IsValid()) return false;

    *reinterpret_cast<uint8_t*>(hookAddress) = 0xE8; // CALL rel32
    *reinterpret_cast<int32_t*>(hookAddress + 1) = static_cast<int32_t>(targetFunction - (hookAddress + 5));
    return true;
}

bool WriteJmp(uintptr_t hookAddress, uintptr_t targetFunction) {
    ScopedProtect protect(hookAddress, 5);
    if (!protect.IsValid()) return false;

    *reinterpret_cast<uint8_t*>(hookAddress) = 0xE9; // JMP rel32
    *reinterpret_cast<int32_t*>(hookAddress + 1) = static_cast<int32_t>(targetFunction - (hookAddress + 5));
    return true;
}

uintptr_t GetModuleBase(const wchar_t* moduleName) {
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(moduleName));
}

size_t GetModuleSize(uintptr_t moduleBase) {
    if (!moduleBase) {
        moduleBase = GetModuleBase();
    }
    MODULEINFO modInfo{};
    if (GetModuleInformation(GetCurrentProcess(), reinterpret_cast<HMODULE>(moduleBase), &modInfo, sizeof(modInfo))) {
        return modInfo.SizeOfImage;
    }
    return 0;
}

namespace {
    struct PatternByte {
        uint8_t byte{ 0 };
        bool isWildcard{ false };
    };

    std::vector<PatternByte> ParsePattern(std::string_view pattern) {
        std::vector<PatternByte> result;
        std::istringstream stream{ std::string(pattern) };
        std::string token;

        while (stream >> token) {
            if (token == "?" || token == "??") {
                result.push_back({ 0, true });
            } else {
                result.push_back({ static_cast<uint8_t>(std::stoul(token, nullptr, 16)), false });
            }
        }
        return result;
    }
}

std::optional<uintptr_t> FindPattern(std::string_view pattern, uintptr_t moduleBase, size_t scanSize) {
    if (!moduleBase) {
        moduleBase = GetModuleBase();
    }
    if (!scanSize) {
        scanSize = GetModuleSize(moduleBase);
    }
    if (!scanSize) {
        return std::nullopt;
    }

    auto patternBytes = ParsePattern(pattern);
    if (patternBytes.empty()) {
        return std::nullopt;
    }

    const uint8_t* scanStart = reinterpret_cast<const uint8_t*>(moduleBase);
    const size_t patternLen = patternBytes.size();

    for (size_t i = 0; i <= scanSize - patternLen; ++i) {
        bool found = true;
        for (size_t j = 0; j < patternLen; ++j) {
            if (!patternBytes[j].isWildcard && scanStart[i + j] != patternBytes[j].byte) {
                found = false;
                break;
            }
        }
        if (found) {
            return moduleBase + i;
        }
    }

    return std::nullopt;
}

std::vector<uintptr_t> FindAllPatterns(std::string_view pattern, uintptr_t moduleBase, size_t scanSize) {
    std::vector<uintptr_t> matches;

    if (!moduleBase) {
        moduleBase = GetModuleBase();
    }
    if (!scanSize) {
        scanSize = GetModuleSize(moduleBase);
    }
    if (!scanSize) {
        return matches;
    }

    auto patternBytes = ParsePattern(pattern);
    if (patternBytes.empty()) {
        return matches;
    }

    const uint8_t* scanStart = reinterpret_cast<const uint8_t*>(moduleBase);
    const size_t patternLen = patternBytes.size();

    for (size_t i = 0; i <= scanSize - patternLen; ++i) {
        bool found = true;
        for (size_t j = 0; j < patternLen; ++j) {
            if (!patternBytes[j].isWildcard && scanStart[i + j] != patternBytes[j].byte) {
                found = false;
                break;
            }
        }
        if (found) {
            matches.push_back(moduleBase + i);
        }
    }

    return matches;
}

void* InstallIATHook(HMODULE hModule, const char* dllName, const char* funcName, void* hookFunc) {
    if (!hModule) return nullptr;
    
    uint8_t* base = reinterpret_cast<uint8_t*>(hModule);
    auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    
    auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    
    auto& importDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir.VirtualAddress == 0) return nullptr;
    
    auto importDesc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base + importDir.VirtualAddress);
    while (importDesc->Name != 0) {
        const char* modName = reinterpret_cast<const char*>(base + importDesc->Name);
        if (_stricmp(modName, dllName) == 0) {
            auto thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + importDesc->FirstThunk);
            auto origThunk = importDesc->OriginalFirstThunk ? 
                reinterpret_cast<PIMAGE_THUNK_DATA>(base + importDesc->OriginalFirstThunk) : thunk;

            while (origThunk->u1.AddressOfData != 0) {
                if (!(origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                    auto importByName = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + origThunk->u1.AddressOfData);
                    if (strcmp(reinterpret_cast<const char*>(importByName->Name), funcName) == 0) {
                        void* origFunc = reinterpret_cast<void*>(thunk->u1.Function);
                        ScopedProtect protect(&thunk->u1.Function, sizeof(void*));
                        if (protect.IsValid()) {
                            thunk->u1.Function = reinterpret_cast<uintptr_t>(hookFunc);
                            return origFunc;
                        }
                    }
                }
                ++thunk;
                ++origThunk;
            }
        }
        ++importDesc;
    }
    return nullptr;
}

} // namespace BullyDE::Memory
