#pragma once
#include <cstdint>

namespace BullyDE {

// Guards two heap free-list routines that dereference linked-list neighbours
// without checking them for null. See StabilityFix.cpp for the full analysis.
class StabilityFix {
public:
    static bool Install();

    // How many times a guard stopped a dereference the vanilla code would have
    // performed. Zero means the game never reached the bad state this session.
    static long GuardHits();

    // Logs the guard count. Called once on unload.
    static void Report();
};

} // namespace BullyDE
