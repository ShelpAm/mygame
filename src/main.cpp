#include "core/App.hpp"

auto main() -> int {
    App app;
    if (!app.init()) {
        return 1;
    }
    app.run();
    app.shutdown();
    return 0;
}
