#include "Config.h"
#include <Windows.h>
#include <string>
#include <algorithm>

namespace BullyDE {

Config& Config::Get() {
    static Config instance;
    return instance;
}

void Config::Load(const std::filesystem::path& iniPath) {
    m_iniPath = iniPath;
    std::wstring pathW = iniPath.wstring();

    if (!std::filesystem::exists(iniPath)) {
        Save(iniPath);
        return;
    }

    // [General]
    int logLevelInt = GetPrivateProfileIntW(L"General", L"LogLevel", 1, pathW.c_str());
    m_general.logLevel = static_cast<LogLevel>(std::clamp(logLevelInt, 0, 3));
    m_general.logToFile = GetPrivateProfileIntW(L"General", L"LogToFile", 1, pathW.c_str()) != 0;

    // [Shadows]
    m_shadows.enabled = GetPrivateProfileIntW(L"Shadows", L"EnableShadowImprovements", 1, pathW.c_str()) != 0;

    int res = GetPrivateProfileIntW(L"Shadows", L"ShadowMapResolution", 8192, pathW.c_str());
    if (res >= 8192) {
        m_shadows.shadowMapResolution = 8192;
    } else if (res >= 4096) {
        m_shadows.shadowMapResolution = 4096;
    } else if (res >= 2048) {
        m_shadows.shadowMapResolution = 2048;
    } else if (res >= 1024) {
        m_shadows.shadowMapResolution = 1024;
    } else {
        m_shadows.shadowMapResolution = 512;
    }

    int technique = GetPrivateProfileIntW(L"Shadows", L"ShadowTechnique", 1, pathW.c_str());
    m_shadows.shadowTechnique = static_cast<uint32_t>(std::clamp(technique, 0, 1));
    int gens = GetPrivateProfileIntW(L"Shadows", L"ShadowGeneratorCount", 8, pathW.c_str());
    m_shadows.shadowGeneratorCount = static_cast<uint32_t>(std::clamp(gens, 1, 32));

    int budget = GetPrivateProfileIntW(L"Shadows", L"ShadowBudgetMB", 0, pathW.c_str());
    m_shadows.shadowBudgetMB = static_cast<uint32_t>(std::clamp(budget, 0, 3072));

    m_shadows.disableBlobShadows = GetPrivateProfileIntW(L"Shadows", L"DisableBlobShadows", 0, pathW.c_str()) != 0;
    m_shadows.dumpUnpackedBinary = GetPrivateProfileIntW(L"Shadows", L"DumpUnpackedBinary", 0, pathW.c_str()) != 0;
    m_shadows.forceDistanceShadows = GetPrivateProfileIntW(L"Shadows", L"ForceDistanceShadows", 1, pathW.c_str()) != 0;

    // [Bloom]
    m_bloom.enabled = GetPrivateProfileIntW(L"Bloom", L"EnableBloomChanges", 1, pathW.c_str()) != 0;

    int radius = GetPrivateProfileIntW(L"Bloom", L"BloomRadiusPercent", 50, pathW.c_str());
    // Snap to the three reachable values rather than silently rounding.
    if (radius >= 100)     m_bloom.radiusPercent = 100;
    else if (radius >= 50) m_bloom.radiusPercent = 50;
    else                   m_bloom.radiusPercent = 25;

    m_bloom.mode = static_cast<uint32_t>(
        std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Bloom", L"BloomMode", 0, pathW.c_str())), 0, 2));

    auto readOverride = [&](const wchar_t* key, int hi) {
        const int v = static_cast<int>(GetPrivateProfileIntW(L"Bloom", key, static_cast<UINT>(-1), pathW.c_str()));
        return (v < 0) ? -1 : std::clamp(v, 0, hi);
    };
    m_bloom.threshold = readOverride(L"BloomThreshold", 255);
    m_bloom.strength  = readOverride(L"BloomStrength", 255);
    m_bloom.scale     = readOverride(L"BloomScale", 64);

    // [DrawDistance]
    m_drawDist.enabled = GetPrivateProfileIntW(L"DrawDistance", L"EnableDrawDistanceChanges", 1, pathW.c_str()) != 0;
    WCHAR lodBuf[32]{ 0 };
    if (GetPrivateProfileStringW(L"DrawDistance", L"LodMultiplier", L"4.0", lodBuf, 32, pathW.c_str()) > 0) {
        m_drawDist.lodMultiplier = std::stof(lodBuf);
    }
    WCHAR farClipBuf[32]{ 0 };
    if (GetPrivateProfileStringW(L"DrawDistance", L"FarClipOverride", L"0.0", farClipBuf, 32, pathW.c_str()) > 0) {
        // std::stof throws on garbage, and Load() runs from DllMain -- an escaped
        // exception there kills the process before the game starts.
        try {
            m_drawDist.farClipOverride = std::stof(farClipBuf);
        } catch (...) {
            m_drawDist.farClipOverride = 0.0f;
        }
    }
    m_drawDist.bypassDistanceCulling = GetPrivateProfileIntW(L"DrawDistance", L"BypassDistanceCulling", 1, pathW.c_str()) != 0;
    m_drawDist.forceHighDetailModels = GetPrivateProfileIntW(L"DrawDistance", L"ForceHighDetailModels", 0, pathW.c_str()) != 0;
    m_drawDist.extendPedPools = GetPrivateProfileIntW(L"DrawDistance", L"ExtendPedPools", 1, pathW.c_str()) != 0;
    m_drawDist.pedPoolSize = static_cast<uint32_t>(std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"DrawDistance", L"PedPoolSize", 2048, pathW.c_str())), 490, 8192));
    m_drawDist.vehiclePoolSize = static_cast<uint32_t>(std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"DrawDistance", L"VehiclePoolSize", 2048, pathW.c_str())), 250, 8192));

    WCHAR pedPopBuf[32]{ 0 };
    if (GetPrivateProfileStringW(L"DrawDistance", L"PedPopScale", L"2.0", pedPopBuf, 32, pathW.c_str()) > 0) {
        try {
            m_drawDist.pedPopScale = std::clamp(std::stof(pedPopBuf), 1.0f, 6.0f);
        } catch (...) {
            m_drawDist.pedPopScale = 2.0f;
        }
    }

    WCHAR nearBuf[32]{ 0 };
    if (GetPrivateProfileStringW(L"DrawDistance", L"NearPlane", L"1.0", nearBuf, 32, pathW.c_str()) > 0) {
        try {
            m_drawDist.nearPlane = std::clamp(std::stof(nearBuf), 0.05f, 10.0f);
        } catch (...) {
            m_drawDist.nearPlane = 1.0f;
        }
    }
    m_drawDist.extendCoronaBuffer = GetPrivateProfileIntW(L"DrawDistance", L"ExtendCoronaBuffer", 1, pathW.c_str()) != 0;
    m_drawDist.enableSectorOverflowGuard = GetPrivateProfileIntW(L"DrawDistance", L"EnableSectorOverflowGuard", 1, pathW.c_str()) != 0;
    m_drawDist.extendTerrainDrawDistance = GetPrivateProfileIntW(L"DrawDistance", L"ExtendTerrainDrawDistance", 1, pathW.c_str()) != 0;
    m_drawDist.filterRadarPedBlips = GetPrivateProfileIntW(L"DrawDistance", L"FilterRadarPedBlips", 0, pathW.c_str()) != 0;

    // [AntiAliasing]
    m_aa.enabled = GetPrivateProfileIntW(L"AntiAliasing", L"EnableAAFixes", 1, pathW.c_str()) != 0;
    m_aa.fixVegetationOutlines = GetPrivateProfileIntW(L"AntiAliasing", L"FixVegetationOutlines", 1, pathW.c_str()) != 0;

    // [Graphics]
    m_graphics.disableDistanceFog = GetPrivateProfileIntW(L"Graphics", L"DisableDistanceFog", 0, pathW.c_str()) != 0;
    m_graphics.disableMotionBlur = GetPrivateProfileIntW(L"Graphics", L"DisableMotionBlur", 0, pathW.c_str()) != 0;

    // [Diagnostics]
    m_diag.logPostFXState = GetPrivateProfileIntW(L"Diagnostics", L"LogPostFXState", 0, pathW.c_str()) != 0;
}

void Config::Save(const std::filesystem::path& iniPath) {
    m_iniPath = iniPath;
    std::wstring pathW = iniPath.wstring();

    WritePrivateProfileStringW(L"General", L"LogLevel", std::to_wstring(static_cast<int>(m_general.logLevel)).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"General", L"LogToFile", m_general.logToFile ? L"1" : L"0", pathW.c_str());

    WritePrivateProfileStringW(L"Shadows", L"EnableShadowImprovements", m_shadows.enabled ? L"1" : L"0", pathW.c_str());
    WritePrivateProfileStringW(L"Shadows", L"ShadowMapResolution", std::to_wstring(m_shadows.shadowMapResolution).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"Shadows", L"ShadowTechnique", std::to_wstring(m_shadows.shadowTechnique).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"Shadows", L"ShadowGeneratorCount", std::to_wstring(m_shadows.shadowGeneratorCount).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"Shadows", L"ShadowBudgetMB", std::to_wstring(m_shadows.shadowBudgetMB).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"Shadows", L"DisableBlobShadows", m_shadows.disableBlobShadows ? L"1" : L"0", pathW.c_str());
    WritePrivateProfileStringW(L"Shadows", L"DumpUnpackedBinary", m_shadows.dumpUnpackedBinary ? L"1" : L"0", pathW.c_str());
    WritePrivateProfileStringW(L"Shadows", L"ForceDistanceShadows", m_shadows.forceDistanceShadows ? L"1" : L"0", pathW.c_str());

    WritePrivateProfileStringW(L"Bloom", L"EnableBloomChanges", m_bloom.enabled ? L"1" : L"0", pathW.c_str());
    WritePrivateProfileStringW(L"Bloom", L"BloomRadiusPercent", std::to_wstring(m_bloom.radiusPercent).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"Bloom", L"BloomMode", std::to_wstring(m_bloom.mode).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"Bloom", L"BloomThreshold", std::to_wstring(m_bloom.threshold).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"Bloom", L"BloomStrength", std::to_wstring(m_bloom.strength).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"Bloom", L"BloomScale", std::to_wstring(m_bloom.scale).c_str(), pathW.c_str());

    WritePrivateProfileStringW(L"DrawDistance", L"EnableDrawDistanceChanges", m_drawDist.enabled ? L"1" : L"0", pathW.c_str());
    WritePrivateProfileStringW(L"DrawDistance", L"LodMultiplier", std::to_wstring(m_drawDist.lodMultiplier).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"DrawDistance", L"FarClipOverride", std::to_wstring(m_drawDist.farClipOverride).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"DrawDistance", L"BypassDistanceCulling", m_drawDist.bypassDistanceCulling ? L"1" : L"0", pathW.c_str());
    WritePrivateProfileStringW(L"DrawDistance", L"ForceHighDetailModels", m_drawDist.forceHighDetailModels ? L"1" : L"0", pathW.c_str());
    WritePrivateProfileStringW(L"DrawDistance", L"ExtendPedPools", m_drawDist.extendPedPools ? L"1" : L"0", pathW.c_str());
    WritePrivateProfileStringW(L"DrawDistance", L"PedPoolSize", std::to_wstring(m_drawDist.pedPoolSize).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"DrawDistance", L"VehiclePoolSize", std::to_wstring(m_drawDist.vehiclePoolSize).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"DrawDistance", L"PedPopScale", std::to_wstring(m_drawDist.pedPopScale).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"DrawDistance", L"NearPlane", std::to_wstring(m_drawDist.nearPlane).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"DrawDistance", L"ExtendCoronaBuffer", m_drawDist.extendCoronaBuffer ? L"1" : L"0", pathW.c_str());
    WritePrivateProfileStringW(L"DrawDistance", L"EnableSectorOverflowGuard", m_drawDist.enableSectorOverflowGuard ? L"1" : L"0", pathW.c_str());
    WritePrivateProfileStringW(L"DrawDistance", L"ExtendTerrainDrawDistance", m_drawDist.extendTerrainDrawDistance ? L"1" : L"0", pathW.c_str());
    WritePrivateProfileStringW(L"DrawDistance", L"FilterRadarPedBlips", m_drawDist.filterRadarPedBlips ? L"1" : L"0", pathW.c_str());

    WritePrivateProfileStringW(L"AntiAliasing", L"EnableAAFixes", m_aa.enabled ? L"1" : L"0", pathW.c_str());
    WritePrivateProfileStringW(L"AntiAliasing", L"FixVegetationOutlines", m_aa.fixVegetationOutlines ? L"1" : L"0", pathW.c_str());

    WritePrivateProfileStringW(L"Graphics", L"DisableDistanceFog", m_graphics.disableDistanceFog ? L"1" : L"0", pathW.c_str());
    WritePrivateProfileStringW(L"Graphics", L"DisableMotionBlur", m_graphics.disableMotionBlur ? L"1" : L"0", pathW.c_str());

    WritePrivateProfileStringW(L"Diagnostics", L"LogPostFXState", m_diag.logPostFXState ? L"1" : L"0", pathW.c_str());
}

} // namespace BullyDE
