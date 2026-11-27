#include <Core/TrackpadSettings.h>
#include <Core/Log.h>
#include <Core/CZEventSource.h>
#include <Seat/Seat.h>
#include <libinput.h>
#include <sys/inotify.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>

namespace
{
struct Settings
{
    double trackingSpeed { 0. };
    bool secondaryCorner { false }; // BottomRight (else TwoFingers)
    bool tapToClick { false };
    bool naturalScrolling { true };
    bool workspaceSwipe { true }, expose { true }, showDesktop { true };
};

struct State
{
    Settings settings;
    std::shared_ptr<CZEventSource> watch;
};

std::unique_ptr<State> s;
}

static std::string Trim(const std::string &str) noexcept
{
    const auto b { str.find_first_not_of(" \t\r\n") };

    if (b == std::string::npos)
        return {};

    return str.substr(b, str.find_last_not_of(" \t\r\n") - b + 1);
}

static std::filesystem::path ConfigDir() noexcept
{
    if (const char *config { getenv("XDG_CONFIG_HOME") }; config && config[0] == '/')
        return std::filesystem::path(config) / "cz-crystals";

    if (const char *home { getenv("HOME") })
        return std::filesystem::path(home) / ".config" / "cz-crystals";

    return {};
}

static Settings Load() noexcept
{
    Settings st;
    std::ifstream file { ConfigDir() / "trackpad.conf" };
    std::string line;

    while (std::getline(file, line))
    {
        const auto eq { line.find('=') };

        if (line.empty() || line[0] == '#' || line[0] == '[' || eq == std::string::npos)
            continue;

        const std::string key { Trim(line.substr(0, eq)) }, value { Trim(line.substr(eq + 1)) };
        const bool on { value == "true" || value == "1" };

        if (key == "TrackingSpeed")
            st.trackingSpeed = std::clamp(std::atof(value.c_str()), -1., 1.);
        else if (key == "SecondaryClick")
            st.secondaryCorner = value == "BottomRight";
        else if (key == "TapToClick")
            st.tapToClick = on;
        else if (key == "NaturalScrolling")
            st.naturalScrolling = on;
        else if (key == "WorkspaceSwipe")
            st.workspaceSwipe = on;
        else if (key == "Expose")
            st.expose = on;
        else if (key == "ShowDesktop")
            st.showDesktop = on;
    }

    return st;
}

bool TrackpadSettings::Configure(libinput_device *dev) noexcept
{
    // Touchpads are the devices that can tap
    if (!dev || libinput_device_config_tap_get_finger_count(dev) <= 0)
        return false;

    const Settings st { s ? s->settings : Settings() };

    if (libinput_device_config_accel_is_available(dev))
        libinput_device_config_accel_set_speed(dev, st.trackingSpeed);

    // Secondary click: two fingers anywhere, or one in the bottom right corner (if it can)
    const UInt32 methods { libinput_device_config_click_get_methods(dev) };
    const bool corner { st.secondaryCorner && (methods & LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS) };

    if (corner)
        libinput_device_config_click_set_method(dev, LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS);
    else if (methods & LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER)
        libinput_device_config_click_set_method(dev, LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);

    // Taps: one finger is the primary button, two the secondary one
    libinput_device_config_tap_set_enabled(dev, st.tapToClick ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
    libinput_device_config_tap_set_button_map(dev, LIBINPUT_CONFIG_TAP_MAP_LRM);

    if (libinput_device_config_scroll_has_natural_scroll(dev))
        libinput_device_config_scroll_set_natural_scroll_enabled(dev, st.naturalScrolling);

    return true;
}

void TrackpadSettings::Init() noexcept
{
    if (s)
        return;

    s = std::make_unique<State>();
    s->settings = Load();

    // Changes: the file is renamed into place (see System Settings), watch the directory
    const auto dir { ConfigDir() };
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const int fd { inotify_init1(IN_NONBLOCK | IN_CLOEXEC) };

    if (fd < 0 || inotify_add_watch(fd, dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE) < 0)
    {
        if (fd >= 0)
            close(fd);

        Log(CZWarning, CZLN, "Can't watch {}: trackpad settings apply on restart only", dir.string());
        return;
    }

    s->watch = CZEventSource::Make(fd, EPOLLIN, CZOwn::Own, [](int fd, UInt32, CZEventSource *) {
        alignas(inotify_event) char buf[4096];
        ssize_t n;
        bool changed { false };

        while ((n = read(fd, buf, sizeof(buf))) > 0)
            for (char *p = buf; p < buf + n;)
            {
                auto *ev { reinterpret_cast<inotify_event*>(p) };

                if (ev->len > 0 && std::string(ev->name) == "trackpad.conf")
                    changed = true;

                p += sizeof(inotify_event) + ev->len;
            }

        if (!changed)
            return;

        s->settings = Load();

        // Not while the session is left (another TTY): the devices are configured once back
        if (seat()->enabled())
            GetSeat()->configureInputDevices();
    });
}

void TrackpadSettings::Clear() noexcept
{
    s.reset();
}

bool TrackpadSettings::WorkspaceSwipe() noexcept { return !s || s->settings.workspaceSwipe; }
bool TrackpadSettings::Expose() noexcept { return !s || s->settings.expose; }
bool TrackpadSettings::ShowDesktop() noexcept { return !s || s->settings.showDesktop; }
