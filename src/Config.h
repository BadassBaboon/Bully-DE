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

struct BloomSettings {
    bool enabled{ true };
    // Percent of the vanilla blur radius. Only powers of two are reachable,
    // because the radius comes from a shift: 100 = vanilla, 50 = half, 25 = quarter.
    uint32_t radiusPercent{ 50 };
    uint32_t mode{ 0 };              // 0 = per-area default, 1 = force on, 2 = force off
    int32_t  threshold{ -1 };        // -1 = game value (stock 230); else 0-255
    int32_t  strength{ -1 };         // -1 = game value (stock 80);  else 0-255
    int32_t  scale{ -1 };            // -1 = game value (stock 4);   else 0-64
};

struct DiagnosticsSettings {
    bool logPostFXState{ false };    // Sample and log the screen-effect gate state each second
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
    const BloomSettings& Bloom() const { return m_bloom; }
    const DiagnosticsSettings& Diagnostics() const { return m_diag; }
    const GeneralSettings& General() const { return m_general; }
    const std::filesystem::path& GetIniPath() const { return m_iniPath; }

private:
    Config() = default;

    ShadowSettings m_shadows;
    BloomSettings m_bloom;
    DiagnosticsSettings m_diag;
    GeneralSettings m_general;
    std::filesystem::path m_iniPath;
};

} // namespace BullyDE
