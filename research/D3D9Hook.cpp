#include "D3D9Hook.h"
#include "Config.h"
#include "Logger.h"
#include "Memory.h"
#include <unordered_set>
#include <unordered_map>
#include <mutex>

namespace BullyDE {

typedef IDirect3D9* (WINAPI* Direct3DCreate9_t)(UINT SDKVersion);
static Direct3DCreate9_t g_origDirect3DCreate9 = nullptr;

typedef HRESULT (WINAPI* CreateDevice_t)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
static CreateDevice_t g_origCreateDevice = nullptr;

typedef HRESULT (WINAPI* CreateTexture_t)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
static CreateTexture_t g_origCreateTexture = nullptr;

typedef HRESULT (WINAPI* CreateDepthStencilSurface_t)(IDirect3DDevice9*, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9**, HANDLE*);
static CreateDepthStencilSurface_t g_origCreateDepthStencilSurface = nullptr;

typedef HRESULT (WINAPI* SetRenderTarget_t)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
static SetRenderTarget_t g_origSetRenderTarget = nullptr;

typedef HRESULT (WINAPI* SetViewport_t)(IDirect3DDevice9*, const D3DVIEWPORT9*);
static SetViewport_t g_origSetViewport = nullptr;

static std::mutex g_mutex;
static std::unordered_map<IDirect3DSurface9*, UINT> g_upgradedRenderTargets; // Surface -> OriginalSize
static std::unordered_set<IDirect3DSurface9*> g_upgradedDepthStencils;

static IDirect3DSurface9* g_currentUpgradedRT = nullptr;
static UINT g_currentOriginalSize = 0;

template<typename T>
void HookVTable(void** vtable, int index, T hookFunc, T* origFunc) {
    DWORD oldProtect;
    VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &oldProtect);
    *origFunc = (T)vtable[index];
    vtable[index] = (void*)hookFunc;
    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
}

HRESULT WINAPI Hook_CreateTexture(
    IDirect3DDevice9* pDevice,
    UINT Width, UINT Height, UINT Levels, DWORD Usage,
    D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle) 
{
    const auto& config = Config::Get().Shadows();
    UINT targetRes = config.shadowMapResolution;

    // Log diagnostic info for square or rendertarget textures
    if (Width == Height || (Usage & D3DUSAGE_RENDERTARGET) || Format == 21 || Format == 114) {
        Logger::Get().Info("D3D9Hook", "CreateTexture: {}x{}, Levels={}, Usage=0x{:X}, Format={}, Pool={}",
            Width, Height, Levels, Usage, static_cast<int>(Format), static_cast<int>(Pool));
    }

    bool isLowResRT = (Width == Height && Width <= 256 && (Usage & D3DUSAGE_RENDERTARGET));

    UINT originalSize = Width;
    if (isLowResRT && config.enabled && targetRes > originalSize) {
        Logger::Get().Info("D3D9Hook", "Upgrading Pedestrian Shadow / Low-res RT ({}x{} -> {}x{}, Format: {})", 
            Width, Height, targetRes, targetRes, static_cast<int>(Format));
        Width = targetRes;
        Height = targetRes;
    }

    HRESULT hr = g_origCreateTexture(pDevice, Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle);

    if (SUCCEEDED(hr) && isLowResRT && Width == targetRes) {
        IDirect3DSurface9* pSurface = nullptr;
        if (SUCCEEDED((*ppTexture)->GetSurfaceLevel(0, &pSurface))) {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_upgradedRenderTargets[pSurface] = originalSize;
            pSurface->Release(); // GetSurfaceLevel adds a ref
        }
    }

    return hr;
}

HRESULT WINAPI Hook_CreateDepthStencilSurface(
    IDirect3DDevice9* pDevice,
    UINT Width, UINT Height, D3DFORMAT Format,
    D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
    BOOL Discard, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
{
    const auto& config = Config::Get().Shadows();
    UINT targetRes = config.shadowMapResolution;

    Logger::Get().Info("D3D9Hook", "CreateDepthStencilSurface: {}x{}, Format={}, MultiSample={}",
        Width, Height, static_cast<int>(Format), static_cast<int>(MultiSample));

    bool isLowResDepth = (Width == Height && Width <= 256);

    if (isLowResDepth && config.enabled && targetRes > Width) {
        Logger::Get().Info("D3D9Hook", "Upgrading Pedestrian Shadow / Low-res Depth Stencil ({}x{} -> {}x{})", 
            Width, Height, targetRes, targetRes);
        Width = targetRes;
        Height = targetRes;
    }

    HRESULT hr = g_origCreateDepthStencilSurface(pDevice, Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);

    if (SUCCEEDED(hr) && isLowResDepth && Width == targetRes) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_upgradedDepthStencils.insert(*ppSurface);
    }

    return hr;
}

HRESULT WINAPI Hook_SetRenderTarget(IDirect3DDevice9* pDevice, DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget)
{
    if (RenderTargetIndex == 0) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (pRenderTarget && g_upgradedRenderTargets.count(pRenderTarget)) {
            g_currentUpgradedRT = pRenderTarget;
            g_currentOriginalSize = g_upgradedRenderTargets[pRenderTarget];
        } else {
            g_currentUpgradedRT = nullptr;
            g_currentOriginalSize = 0;
        }
    }
    return g_origSetRenderTarget(pDevice, RenderTargetIndex, pRenderTarget);
}

HRESULT WINAPI Hook_SetViewport(IDirect3DDevice9* pDevice, const D3DVIEWPORT9* pViewport)
{
    if (g_currentUpgradedRT != nullptr && pViewport != nullptr) {
        // If the engine sets the viewport for the original size (e.g. 128x128),
        // scale it so the ped shadow uses the full 2048x2048 texture.
        if (pViewport->Width == g_currentOriginalSize && pViewport->Height == g_currentOriginalSize) {
            const auto& config = Config::Get().Shadows();
            D3DVIEWPORT9 vp = *pViewport;
            vp.Width = config.shadowMapResolution;
            vp.Height = config.shadowMapResolution;
            return g_origSetViewport(pDevice, &vp);
        }
    }
    return g_origSetViewport(pDevice, pViewport);
}

void InstallDeviceHooks(IDirect3DDevice9* pDevice) {
    if (g_origCreateTexture) return; // Already hooked

    void** vtable = *reinterpret_cast<void***>(pDevice);
    HookVTable(vtable, 23, Hook_CreateTexture, &g_origCreateTexture);
    HookVTable(vtable, 36, Hook_CreateDepthStencilSurface, &g_origCreateDepthStencilSurface);
    HookVTable(vtable, 37, Hook_SetRenderTarget, &g_origSetRenderTarget);
    HookVTable(vtable, 47, Hook_SetViewport, &g_origSetViewport);

    Logger::Get().Info("D3D9Hook", "Successfully hooked IDirect3DDevice9 (Ped Shadow Upgrade Engine Active).");
}

HRESULT WINAPI Hook_CreateDevice(
    IDirect3D9* pD3D9, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
    DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice9** ppReturnedDeviceInterface)
{
    HRESULT hr = g_origCreateDevice(pD3D9, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
    if (SUCCEEDED(hr) && ppReturnedDeviceInterface && *ppReturnedDeviceInterface) {
        InstallDeviceHooks(*ppReturnedDeviceInterface);
    }
    return hr;
}

IDirect3D9* WINAPI Hook_Direct3DCreate9(UINT SDKVersion) {
    IDirect3D9* pD3D = g_origDirect3DCreate9(SDKVersion);
    if (pD3D && !g_origCreateDevice) {
        void** vtable = *reinterpret_cast<void***>(pD3D);
        HookVTable(vtable, 16, Hook_CreateDevice, &g_origCreateDevice);
        Logger::Get().Info("D3D9Hook", "Successfully hooked IDirect3D9::CreateDevice.");
    }
    return pD3D;
}

typedef FARPROC(WINAPI* GetProcAddress_t)(HMODULE, LPCSTR);
static GetProcAddress_t g_origGetProcAddress = nullptr;

FARPROC WINAPI Hook_GetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    if (lpProcName != nullptr && (uintptr_t)lpProcName > 0xFFFF) {
        if (strcmp(lpProcName, "Direct3DCreate9") == 0) {
            Logger::Get().Info("D3D9Hook", "Intercepted GetProcAddress for Direct3DCreate9!");
            return (FARPROC)Hook_Direct3DCreate9;
        }
    }
    return g_origGetProcAddress(hModule, lpProcName);
}

bool D3D9Hook::Install() {
    HMODULE hExe = GetModuleHandle(NULL);
    
    // First try IAT hooking Direct3DCreate9 directly in case it's statically linked
    g_origDirect3DCreate9 = (Direct3DCreate9_t)Memory::InstallIATHook(hExe, "d3d9.dll", "Direct3DCreate9", (void*)Hook_Direct3DCreate9);
    
    if (g_origDirect3DCreate9) {
        Logger::Get().Info("D3D9Hook", "Successfully hooked Direct3DCreate9 via IAT.");
        return true;
    } 
    
    // If that fails, it's dynamically loaded via GetProcAddress. Hook GetProcAddress instead.
    Logger::Get().Info("D3D9Hook", "Direct3DCreate9 not in IAT, hooking GetProcAddress...");
    g_origGetProcAddress = (GetProcAddress_t)Memory::InstallIATHook(hExe, "KERNEL32.DLL", "GetProcAddress", (void*)Hook_GetProcAddress);
    
    if (g_origGetProcAddress) {
        Logger::Get().Info("D3D9Hook", "Successfully hooked GetProcAddress to intercept Direct3DCreate9.");
        
        // As a fallback, try to get the real Direct3DCreate9 pointer manually
        HMODULE hD3D9 = GetModuleHandleW(L"d3d9.dll");
        if (!hD3D9) hD3D9 = LoadLibraryW(L"d3d9.dll");
        if (hD3D9) {
            g_origDirect3DCreate9 = (Direct3DCreate9_t)g_origGetProcAddress(hD3D9, "Direct3DCreate9");
        }
        return true;
    }
    
    Logger::Get().Error("D3D9Hook", "Failed to find Direct3DCreate9 and failed to hook GetProcAddress.");
    return false;
}

} // namespace BullyDE
