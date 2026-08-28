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
    bool forceDistanceShadows{ true };    // Force NiShadowGenerator perspective projection / distance rendering (0x0040FCDA)
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

struct DrawDistanceSettings {
    bool enabled{ true };
    float lodMultiplier{ 2.0f };         // Multiplier for LOD distance (0.0 = all low LOD, 1.0 = vanilla, 2.0 = 2x, etc.)
    float farClipOverride{ 0.0f };       // Camera far clip plane in meters (0.0 = auto: 300.0 * lodMultiplier)
    bool bypassDistanceCulling{ true };  // Bypass early-out distance culling checks (sub_511130 & sub_5115A0)
    bool forceHighDetailModels{ false }; // NOP LOD model distance switch (forces high-detail models everywhere)
    bool extendPedPools{ true };
    uint32_t pedPoolSize{ 2048 };        // vanilla 490; loop bound follows at 2x
    uint32_t vehiclePoolSize{ 2048 };    // vanilla 250
    float pedPopScale{ 2.0f };           // scales PedPop.dat ranges in memory; 1.0 = off
    float nearPlane{ 0.25f };            // camera near plane; 0.25 is the engine default
    bool extendCoronaBuffer{ true };     // Expand corona / visible light slots from 56 to 1024
    bool enableSectorOverflowGuard{ true }; // Prevent sector list buffer overflow when draw distance is high
    bool extendTerrainDrawDistance{ true }; // Keep distant horizon cliffs, mountains, and landscape terrain visible
    bool filterRadarPedBlips{ false };   // Filter distant ambient ped blips from cluttering the minimap
};

struct AASettings {
    bool enabled{ true };
    bool fixVegetationOutlines{ true };  // Fix white halos around tree foliage, grass, and wire fences (0x008827A3)
};

struct GraphicsSettings {
    bool disableDistanceFog{ false };    // Disable distance fog/haze by blanking shader uniforms
    bool disableMotionBlur{ false };     // Disable motion blur by blanking shader uniforms
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
    const DrawDistanceSettings& DrawDistance() const { return m_drawDist; }
    const AASettings& AntiAliasing() const { return m_aa; }
    const GraphicsSettings& Graphics() const { return m_graphics; }
    const DiagnosticsSettings& Diagnostics() const { return m_diag; }
    const GeneralSettings& General() const { return m_general; }
    const std::filesystem::path& GetIniPath() const { return m_iniPath; }

private:
    Config() = default;

    ShadowSettings m_shadows;
    BloomSettings m_bloom;
    DrawDistanceSettings m_drawDist;
    AASettings m_aa;
    GraphicsSettings m_graphics;
    DiagnosticsSettings m_diag;
    GeneralSettings m_general;
    std::filesystem::path m_iniPath;
};

} // namespace BullyDE
