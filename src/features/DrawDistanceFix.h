#pragma once

namespace BullyDE {

class DrawDistanceFix {
public:
    static bool Install();

    // Logs how many objects the visible-object list had to drop. Called on unload.
    static void Report();
};

} // namespace BullyDE
