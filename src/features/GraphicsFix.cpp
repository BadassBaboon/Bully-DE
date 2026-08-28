#include "GraphicsFix.h"
#include "../Config.h"
#include "../Logger.h"
#include "../Memory.h"
#include "../Patch.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Fog and motion blur are switched off by blanking the shader uniform NAME the
// engine looks the constant up by. With the name gone the by-name lookup fails,
// the parameter is never uploaded, and the shader's constant keeps its default
// -- which reads as the effect being off.
//
// The strings sit back to back with unrelated uniforms, so the blanked length
// has to be exact. Measured from the image:
//
//   0x90064C: "cFog" + 4 pad (8) + "vFogNearFar\0" (12) = 20 bytes
//             then "BoneIdx\0" begins at +20   <- skinning bone index
//   0x91B940: "gMotionBlurStrength\0"           = 20 bytes
//             then "gMotionBlurTexture\0" begins at +20
//
// An earlier version blanked 24 bytes at each site with no verification, which
// overran 4 bytes into "BoneIdx" and into "gMotionBlurTexture". Twenty is the
// correct length for both; the mechanism itself was always sound.
// ---------------------------------------------------------------------------

namespace BullyDE {

namespace {

constexpr uintptr_t kFogNames        = 0x0090064C;
constexpr uintptr_t kMotionBlurNames = 0x0091B940;

// Exact vanilla bytes, so a wrong address is caught before anything is written.
const uint8_t kFogVanilla[20] = {
    'c','F','o','g', 0, 0, 0, 0,
    'v','F','o','g','N','e','a','r','F','a','r', 0
};
const uint8_t kMotionBlurVanilla[20] = {
    'g','M','o','t','i','o','n','B','l','u','r','S','t','r','e','n','g','t','h', 0
};

bool BlankUniformNames(const char* what, uintptr_t addr, const uint8_t* vanilla, size_t size) {
    if (!Patch::Verify(what, addr, vanilla, size)) {
        return false;
    }
    uint8_t zeros[20]{};
    if (size > sizeof(zeros)) {
        Logger::Get().Error("GraphicsFix", "{}: refusing to blank {} bytes.", what, size);
        return false;
    }
    if (!Memory::WriteBytes(addr, zeros, size)) {
        Logger::Get().Error("GraphicsFix", "{}: write failed at 0x{:08X}.", what, addr);
        return false;
    }
    Logger::Get().Info("GraphicsFix", "{} @ 0x{:08X}: {} bytes blanked.", what, addr, size);
    return true;
}

} // namespace

bool GraphicsFix::Install() {
    const auto& config = Config::Get().Graphics();

    if (config.disableDistanceFog) {
        if (BlankUniformNames("Fog uniform names (cFog, vFogNearFar)",
                              kFogNames, kFogVanilla, sizeof(kFogVanilla))) {
            Logger::Get().Info("GraphicsFix", "Distance fog disabled.");
        }
    }

    if (config.disableMotionBlur) {
        if (BlankUniformNames("Motion blur uniform name (gMotionBlurStrength)",
                              kMotionBlurNames, kMotionBlurVanilla, sizeof(kMotionBlurVanilla))) {
            Logger::Get().Info("GraphicsFix", "Motion blur disabled.");
        }
    }

    return true;
}

} // namespace BullyDE
