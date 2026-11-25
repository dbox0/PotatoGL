#include "app/App.h"

#ifdef WITH_EDITOR
#include "editor/Editor.h"
#endif

#include <cstdlib>
#include <memory>

int main(int, char**)
{
    App app;
#ifdef WITH_EDITOR
    app.setOverlay(std::make_unique<Editor>());
#endif
    if (!app.init("OpenGL Template", 1280, 720))
        return EXIT_FAILURE;
    app.run();
    return EXIT_SUCCESS;
}
