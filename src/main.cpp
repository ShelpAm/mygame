#include "core/app.hpp"
#include <spdlog/spdlog.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

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
