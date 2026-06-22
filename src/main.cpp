#include "core/app.hpp"
#include <spdlog/spdlog.h>

int main([[maybe_unused]] int argc, [[maybe_unused]] char **argv)
{

    // Report error on Release, to let user know instead of silent crash. In
    // Debug we want the exception to propagate for easier debugging.
#ifdef NDEBUG
    try {
#endif
        App app;
        app.init();
        app.run();
        app.shutdown();
#ifdef NDEBUG
    }
    catch (std::exception &e) {
        spdlog::critical("Fatal error: {}", e.what());
    }
#endif
    return 0;
}
