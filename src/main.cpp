#include "engine/app/App.h"
#include "game/Gameplay.h"

#ifdef WITH_EDITOR
#include "editor/Editor.h"
#include "engine/core/LogCapture.h"
#endif

#include <cstdlib>
#include <memory>

int main(int, char**)
{
#ifdef WITH_EDITOR
    logcapture::start();
    struct StopCapture
    {
        ~StopCapture() { logcapture::stop(); }
    } stopCapture;
#endif
    App app;
    app.setGame(std::make_unique<Gameplay>());
#ifdef WITH_EDITOR
    app.setOverlay(std::make_unique<Editor>());
#endif
    if (!app.init("OpenGL Template", 1280, 720))
        return EXIT_FAILURE;
    app.run();
    return EXIT_SUCCESS;
}
