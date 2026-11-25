#include "app/App.h"

#include <cstdlib>

int main(int, char**)
{
    App app;
    if (!app.init("OpenGL Template", 1280, 720))
        return EXIT_FAILURE;
    app.run();
    return EXIT_SUCCESS;
}
