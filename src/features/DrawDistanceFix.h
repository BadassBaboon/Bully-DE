#pragma once
#include <cstdint>

namespace BullyDE {

class DrawDistanceFix {
public:
    static bool Install();

    // How many objects the visible-object list has had to drop this session.
    static long SectorDrops();

    // How many object definitions were scaled by the sub_666F70 parser hook.
    static uint32_t ObjectsScaled();

    // How many model definitions had their draw distance scaled by SetDrawDistance.
    static uint32_t ModelInfosScaled();
};

} // namespace BullyDE
