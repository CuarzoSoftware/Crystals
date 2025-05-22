#include <core/Compositor.h>
#include <LLauncher.h>

int main(void)
{
    setenv("LOUVRE_DEBUG", "3", 0);
    setenv("KAY_DEBUG", "3", 0);
    setenv("SRM_DEBUG", "3", 0);

    setenv("LOUVRE_WAYLAND_DISPLAY", "crystals", 0);
    setenv("SRM_RENDER_MODE_ITSELF_FB_COUNT", "3", 0);
    setenv("SRM_FORCE_GL_ALLOCATION", "1", 0);

    LLauncher::startDaemon();

    // Enable screencasting through xdg-desktop-portal-wlr
    LLauncher::launch("dbus-update-activation-environment --systemd WAYLAND_DISPLAY XDG_CURRENT_DESKTOP=wlroots | systemctl --user restart xdg-desktop-portal");

    Compositor compositor;

    compositor.start();

    while (compositor.state() != LCompositor::Uninitialized)
        compositor.processLoop(-1);

    return 0;
}

