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
    m_shadows.shadowTechnique = static_cast<uint32_t>(std::clamp(technique, 0, 2));
    int gens = GetPrivateProfileIntW(L"Shadows", L"ShadowGeneratorCount", 8, pathW.c_str());
    m_shadows.shadowGeneratorCount = static_cast<uint32_t>(std::clamp(gens, 1, 32));

    int budget = GetPrivateProfileIntW(L"Shadows", L"ShadowBudgetMB", 0, pathW.c_str());
    m_shadows.shadowBudgetMB = static_cast<uint32_t>(std::clamp(budget, 0, 3072));

    m_shadows.disableBlobShadows = GetPrivateProfileIntW(L"Shadows", L"DisableBlobShadows", 0, pathW.c_str()) != 0;
    m_shadows.dumpUnpackedBinary = GetPrivateProfileIntW(L"Shadows", L"DumpUnpackedBinary", 0, pathW.c_str()) != 0;


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

    WritePrivateProfileStringW(L"Bloom", L"EnableBloomChanges", m_bloom.enabled ? L"1" : L"0", pathW.c_str());
    WritePrivateProfileStringW(L"Bloom", L"BloomRadiusPercent", std::to_wstring(m_bloom.radiusPercent).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"Bloom", L"BloomMode", std::to_wstring(m_bloom.mode).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"Bloom", L"BloomThreshold", std::to_wstring(m_bloom.threshold).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"Bloom", L"BloomStrength", std::to_wstring(m_bloom.strength).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"Bloom", L"BloomScale", std::to_wstring(m_bloom.scale).c_str(), pathW.c_str());
    WritePrivateProfileStringW(L"Diagnostics", L"LogPostFXState", m_diag.logPostFXState ? L"1" : L"0", pathW.c_str());
}

} // namespace BullyDE
