#include <Core/Client.h>
#include <Core/Crystals.h>
#include <Core/Types.h>
#include <Louvre/Roles/LToplevelRole.h>
#include <cctype>
#include <filesystem>
#include <system_error>

// True if the client process is xwayland-satellite, which bridges every X11 app through a single
// Wayland client (so its pid/process name don't identify the actual app).
static bool IsXWaylandSatellite(pid_t pid) noexcept
{
    if (pid <= 0)
        return false;

    std::error_code ec;
    const auto exe { std::filesystem::read_symlink("/proc/" + std::to_string(pid) + "/exe", ec) };
    return !ec && exe.filename() == "xwayland-satellite";
}

// Display name derived from an X11 WM_CLASS-based app id: last reverse-DNS component, capitalized
// (e.g. "firefox" -> "Firefox", "org.inkscape.Inkscape" -> "Inkscape").
static std::string NameFromAppId(const std::string &appId) noexcept
{
    std::string name { appId.substr(appId.find_last_of('.') == std::string::npos ? 0 : appId.find_last_of('.') + 1) };

    if (name.empty())
        name = appId;

    if (!name.empty())
        name[0] = std::toupper(static_cast<unsigned char>(name[0]));

    return name;
}

void Client::setActiveHeavenClient(LToplevelRole *toplevel) noexcept
{
    auto heaven { GetCompositor()->heaven };

    if (!heaven)
        return;

    pid_t pid {};
    uid_t uid {};
    gid_t gid {};
    credentials(&pid, &uid, &gid);

    const std::string appId { toplevel ? toplevel->appId() : std::string {} };
    std::string appName;

    if (IsXWaylandSatellite(pid))
    {
        // The satellite maps the X11 WM_CLASS to the xdg_toplevel app id, which is the best name we
        // have (the bar may still resolve the app id to a .desktop entry for its actions). Its pid is the satellite's own, shared by every X11 app, so don't advertise it (the
        // bar would otherwise e.g. SIGTERM the satellite on "Quit", killing all X11 apps).
        appName = NameFromAppId(appId);
        pid = 0;
    }

    heaven->setActiveClient(dbusId, pid, uid, gid, appId, appName);
}
