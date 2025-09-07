#include <Core/Compositor.h>
#include <LLauncher.h>

int main(void)
{
    setenv("CZ_LOUVRE_WAYLAND_DISPLAY",  "louvre", 0);
    setenv("CZ_LOUVRE_ENABLE_LIBSEAT",   "1", 0);
    setenv("CZ_CRYSTALS_LOG_LEVEL",   "6", 0);    
    setenv("CZ_LOUVRE_LOG_LEVEL",   "6", 0);
    setenv("CZ_SRM_LOG_LEVEL",      "6", 0);
    setenv("CZ_REAM_LOG_LEVEL",     "6", 0);
    setenv("CZ_KAY_LOG_LEVEL",      "6", 0);
    setenv("CZ_REAM_GAPI",          "GL", 1);

    LLauncher::startDaemon();

    Compositor compositor;
    compositor.start();

    LLauncher::launch("swaybg --image=/home/eduardo/tower.jpg");

    while (compositor.state() != LCompositor::Uninitialized)
        compositor.processLoop(-1);

    return 0;
}

