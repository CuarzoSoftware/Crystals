#include <Core/Crystals.h>
#include <LLauncher.h>

int main(void)
{
    /*
    setenv("XKB_DEFAULT_LAYOUT",         "latam", 1);
    setenv("CZ_REAM_GAPI",               "GL", 1);
    */

    // Qt apps use the Cuarzo platform theme plugin, even if the session set another one.
    setenv("QT_QPA_PLATFORMTHEME",       "cuarzo", 1);
    // A Wayland session, even when started from a tty (XDG_SESSION_TYPE=tty): Qt only picks its
    // Wayland backend by itself when this says so, else it may fall back to X11, where the Cuarzo
    // theme plugin can't identify its windows (no global menus).
    setenv("XDG_SESSION_TYPE",           "wayland", 1);
    setenv("QT_QPA_PLATFORM",            "wayland;xcb", 0);

    // The desktop name portals use to pick their backends (see xdg-desktop-portal-cz).
    setenv("XDG_CURRENT_DESKTOP",        "Cuarzo", 1);

    setenv("CZ_LOUVRE_WAYLAND_DISPLAY",  "louvre", 0);
    setenv("CZ_LOUVRE_ENABLE_LIBSEAT",   "1", 0);

    setenv("CZ_FIRMAMENT_LOG_LEVEL",         "6", 0);
    setenv("CZ_HEAVEN_BAR_LOG_LEVEL",        "6", 0);
    setenv("CZ_HEAVEN_CLIENT_LOG_LEVEL",     "6", 0);
    setenv("CZ_HEAVEN_COMPOSITOR_LOG_LEVEL", "6", 0);
    setenv("CZ_CRYSTALS_LOG_LEVEL",          "6", 0);
    setenv("CZ_LOUVRE_LOG_LEVEL",            "6", 0);
    setenv("CZ_SRM_LOG_LEVEL",           "6", 0);
    setenv("CZ_REAM_LOG_LEVEL",          "6", 0);
    setenv("CZ_KAY_LOG_LEVEL",           "6", 0);
    setenv("CZ_CORE_LOG_LEVEL",          "6", 0);

    LLauncher::startDaemon();

    Crystals compositor;

    if (!compositor.start())
        return 1;

    // Apps started through D-Bus / systemd activation get the theme too (they don't inherit our
    // environment). Note: this updates the user's whole systemd session.
    // The portal backends (e.g. screen casting, xdg-desktop-portal-cz) need the display and the desktop
    // name too.
    LLauncher::launch("dbus-update-activation-environment --systemd QT_QPA_PLATFORMTHEME WAYLAND_DISPLAY XDG_CURRENT_DESKTOP");

    LLauncher::launch("cz-firmament");
    LLauncher::launch("cz-shelf");
    LLauncher::launch("cz-desk");

    while (compositor.state() != LCompositor::Uninitialized)
        compositor.dispatch(-1);

    return 0;
}

