#include <Core/Compositor.h>
#include <LLauncher.h>

int main(void)
{
    setenv("LOUVRE_WAYLAND_DISPLAY", "louvre", 0);
    setenv("LOUVRE_ENABLE_LIBSEAT", "1", 1);
    setenv("CZ_CRYSTALS_LOG_LEVEL",   "6", 0);

    /*
    setenv("CZ_LOUVRE_LOG_LEVEL",   "6", 0);
    setenv("CZ_KAY_LOG_LEVEL",      "6", 0);
    setenv("CZ_SRM_LOG_LEVEL",      "6", 0);
    setenv("CZ_REAM_LOG_LEVEL",     "6", 0);
    */

    setenv("CZ_REAM_GAPI",          "GL", 0);

    LLauncher::startDaemon();

    Compositor compositor;
    compositor.start();

    while (compositor.state() != LCompositor::Uninitialized)
        compositor.processLoop(-1);

    return 0;
}

