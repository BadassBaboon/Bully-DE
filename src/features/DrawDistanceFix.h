#pragma once

namespace BullyDE {

class DrawDistanceFix {
public:
    static bool Install();

    // How many objects the visible-object list has had to drop this session.
    static long SectorDrops();

};

} // namespace BullyDE
