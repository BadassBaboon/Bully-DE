#pragma once
#include <cstdint>
#include <vector>
#include <string_view>
#include <optional>
#include <Windows.h>

namespace BullyDE::Memory {

class ScopedProtect {
public:
    ScopedProtect(uintptr_t address, size_t size, DWORD newProtect = PAGE_EXECUTE_READWRITE)
        : m_address(reinterpret_cast<void*>(address)), m_size(size), m_success(false) {
        m_success = VirtualProtect(m_address, m_size, newProtect, &m_oldProtect) != 0;
    }

    ScopedProtect(void* address, size_t size, DWORD newProtect = PAGE_EXECUTE_READWRITE)
        : m_address(address), m_size(size), m_success(false) {
        m_success = VirtualProtect(m_address, m_size, newProtect, &m_oldProtect) != 0;
    }

    ~ScopedProtect() {
        if (m_success) {
            DWORD temp;
            VirtualProtect(m_address, m_size, m_oldProtect, &temp);
        }
    }

    bool IsValid() const { return m_success; }

private:
    void* m_address;
    size_t m_size;
    DWORD m_oldProtect{ 0 };
    BOOL m_success{ FALSE };
};

template <typename T>
bool Write(uintptr_t address, const T& value) {
    ScopedProtect protect(address, sizeof(T));
    if (!protect.IsValid()) return false;
    *reinterpret_cast<T*>(address) = value;
    return true;
}

template <typename T>
T Read(uintptr_t address) {
    return *reinterpret_cast<const T*>(address);
}

bool WriteBytes(uintptr_t address, const void* data, size_t size);
bool WriteNops(uintptr_t address, size_t count);
bool WriteCall(uintptr_t hookAddress, uintptr_t targetFunction);
bool WriteJmp(uintptr_t hookAddress, uintptr_t targetFunction);

void* CreateTrampolineHook(void* targetFunction, void* hookFunction);

// Pattern scanner for signature matching in process memory
std::optional<uintptr_t> FindPattern(std::string_view pattern, uintptr_t moduleBase = 0, size_t scanSize = 0);
std::vector<uintptr_t> FindAllPatterns(std::string_view pattern, uintptr_t moduleBase = 0, size_t scanSize = 0);

void* InstallIATHook(HMODULE hModule, const char* dllName, const char* funcName, void* hookFunc);

uintptr_t GetModuleBase(const wchar_t* moduleName = nullptr);
size_t GetModuleSize(uintptr_t moduleBase);

} // namespace BullyDE::Memory
