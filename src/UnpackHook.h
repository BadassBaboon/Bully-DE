#pragma once
#include <Windows.h>
#include <functional>

namespace BullyDE {

class UnpackHook {
public:
    using OnUnpackedCallback = std::function<void()>;

    static bool Register(OnUnpackedCallback callback);
    static bool IsUnpacked();

private:
    static void Trigger();
    static BOOL WINAPI HookedSystemParametersInfoA(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni);
};

} // namespace BullyDE
