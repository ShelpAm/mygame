#include "core/app.hpp"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    App app;
    app.init();
    app.run();
    return 0;
}
