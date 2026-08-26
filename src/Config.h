#pragma once
#include <cstdint>
#include <filesystem>
#include "Logger.h"

namespace BullyDE {

struct ShadowSettings {
    bool enabled{ true };
    uint32_t shadowMapResolution{ 8192 }; // 1024, 2048, 4096, 8192; matches the shipped INI
    uint32_t shadowTechnique{ 1 };        // 0 = Standard (hard), 1 = PCF (vanilla), 2 = VSM
    uint32_t shadowGeneratorCount{ 8 };   // Simultaneous shadow-casting lights (vanilla 8, NiShadowManager+0xB4)
    uint32_t shadowBudgetMB{ 0 };         // NiShadowManager map budget in MB; 0 = auto-size from resolution
    bool disableBlobShadows{ false };     // Disable legacy 2D capsule blob decals (shad_ped/shad_car)
    bool dumpUnpackedBinary{ false };     // Dump the decrypted Bully.exe image next to the .asi, for IDA
};

struct GeneralSettings {
    LogLevel logLevel{ LogLevel::Info };
    bool logToFile{ true };
};

class Config {
public:
    static Config& Get();

    void Load(const std::filesystem::path& iniPath);
    void Save(const std::filesystem::path& iniPath);

    const ShadowSettings& Shadows() const { return m_shadows; }
    const GeneralSettings& General() const { return m_general; }
    const std::filesystem::path& GetIniPath() const { return m_iniPath; }

private:
    Config() = default;

    ShadowSettings m_shadows;
    GeneralSettings m_general;
    std::filesystem::path m_iniPath;
};

} // namespace BullyDE
