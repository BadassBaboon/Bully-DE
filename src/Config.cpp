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
}

} // namespace BullyDE
