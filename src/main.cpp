#include <Core/Compositor.h>
#include <LLauncher.h>

int main(void)
{
    /*
    setenv("XKB_DEFAULT_LAYOUT",         "latam", 1);
    setenv("CZ_REAM_GAPI",               "GL", 1);
    */

    setenv("CZ_LOUVRE_WAYLAND_DISPLAY",  "louvre", 0);
    setenv("CZ_LOUVRE_ENABLE_LIBSEAT",   "1", 0);

    setenv("CZ_CRYSTALS_LOG_LEVEL",      "0", 0);
    setenv("CZ_LOUVRE_LOG_LEVEL",        "4", 1);
    setenv("CZ_SRM_LOG_LEVEL",           "4", 0);
    setenv("CZ_REAM_LOG_LEVEL",          "4", 0);
    setenv("CZ_KAY_LOG_LEVEL",           "4", 1);
    setenv("CZ_CORE_LOG_LEVEL",          "4", 1);

    LLauncher::startDaemon();

    Compositor compositor;
    compositor.start();

    while (compositor.state() != LCompositor::Uninitialized)
        compositor.dispatch(-1);

    return 0;
}

