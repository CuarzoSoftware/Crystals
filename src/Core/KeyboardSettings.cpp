#include <Core/KeyboardSettings.h>
#include <Core/Log.h>
#include <Core/CZTimer.h>
#include <Core/CZBusWorker.h>
#include <Core/CZEventSource.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Seat/LKeyboard.h>
#include <systemd/sd-bus.h>
#include <sys/inotify.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

static constexpr const char *SystemKeyboardConf { "/etc/X11/xorg.conf.d/00-keyboard.conf" };
static constexpr const char *UPower { "org.freedesktop.UPower" };
static constexpr const char *KbdPath { "/org/freedesktop/UPower/KbdBacklight" };
static constexpr const char *KbdIface { "org.freedesktop.UPower.KbdBacklight" };

namespace
{
struct Settings
{
    Int32 repeatRate { 32 }, repeatDelay { 500 };
    std::string layouts, variants; // Empty layouts: not set
    Int32 backlightTimeout { 0 };
};

struct State
{
    Settings applied;
    bool appliedOnce { false };
    std::shared_ptr<CZEventSource> watch;
    int watchFd { -1 };

    // The backlight
    std::unique_ptr<CZBusWorker> worker;
    std::unique_ptr<CZTimer> idleTimer;
    std::chrono::steady_clock::time_point lastInput;
    Int32 timeoutSec { 0 };
    bool off { false };          // Turned off by us
    Int32 restore { -1 };        // The brightness to restore (-1: unknown)
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
    std::ifstream file { ConfigDir() / "keyboard.conf" };
    std::string line;

    while (std::getline(file, line))
    {
        const auto eq { line.find('=') };

        if (line.empty() || line[0] == '#' || line[0] == '[' || eq == std::string::npos)
            continue;

        const std::string key { Trim(line.substr(0, eq)) }, value { Trim(line.substr(eq + 1)) };

        if (key == "RepeatRate")
            st.repeatRate = std::clamp(std::atoi(value.c_str()), 0, 100);
        else if (key == "RepeatDelay")
            st.repeatDelay = std::clamp(std::atoi(value.c_str()), 100, 5000);
        else if (key == "Layouts")
            st.layouts = value;
        else if (key == "Variants")
            st.variants = value;
        else if (key == "BacklightTimeout")
            st.backlightTimeout = std::max(0, std::atoi(value.c_str()));
    }

    // No layout chosen: the system's (unless the environment sets XKB's default)
    if (st.layouts.empty() && !getenv("XKB_DEFAULT_LAYOUT"))
    {
        std::ifstream sys { SystemKeyboardConf };

        const auto quoted = [](const std::string &l) -> std::string {
            const auto last { l.rfind('"') };
            const auto first { last == std::string::npos || last == 0 ? std::string::npos : l.rfind('"', last - 1) };
            return first == std::string::npos ? std::string() : l.substr(first + 1, last - first - 1);
        };

        while (std::getline(sys, line))
        {
            if (line.find("\"XkbLayout\"") != std::string::npos)
                st.layouts = quoted(line);
            else if (line.find("\"XkbVariant\"") != std::string::npos)
                st.variants = quoted(line);
        }
    }

    return st;
}

/* ----------------------------------------- Backlight ----------------------------------------- */

static void BacklightOff() noexcept
{
    if (s->off || !s->worker)
        return;

    s->off = true;

    // What it is now (the user may have changed it), restored with the next input
    s->worker->run([](sd_bus *bus) -> std::function<void()> {
        Int32 brightness { -1 };

        if (bus)
        {
            sd_bus_message *reply {};

            if (sd_bus_call_method(bus, UPower, KbdPath, KbdIface, "GetBrightness", nullptr, &reply, "") >= 0)
            {
                sd_bus_message_read(reply, "i", &brightness);
                sd_bus_message_unref(reply);
            }

            if (brightness > 0)
                sd_bus_call_method(bus, UPower, KbdPath, KbdIface, "SetBrightness", nullptr, nullptr, "i", 0);
        }

        return [brightness]{
            if (s)
                s->restore = brightness;
        };
    });
}

static void BacklightOn() noexcept
{
    if (!s->off || !s->worker)
        return;

    s->off = false;

    // Restored once the brightness read when turning it off is known (jobs run in order)
    s->worker->run([](sd_bus *) -> std::function<void()> {
        return []{
            if (!s || s->off || s->restore <= 0)
                return;

            const Int32 brightness { s->restore };
            s->restore = -1;
            s->worker->run([brightness](sd_bus *bus) -> std::function<void()> {
                if (bus)
                    sd_bus_call_method(bus, UPower, KbdPath, KbdIface, "SetBrightness", nullptr, nullptr, "i", brightness);
                return {};
            });
        };
    });
}

static void StartIdleTimer() noexcept
{
    if (s->timeoutSec <= 0)
    {
        s->idleTimer->stop();
        BacklightOn();
        return;
    }

    if (!s->worker)
        s->worker = std::make_unique<CZBusWorker>(CZBusWorker::System);

    s->idleTimer->start(UInt32(s->timeoutSec) * 1000);
}

/* ------------------------------------------- Apply ------------------------------------------- */

static void Apply() noexcept
{
    const Settings st { Load() };
    auto *keyboard { seat()->keyboard() };

    if (!s->appliedOnce || st.repeatRate != s->applied.repeatRate || st.repeatDelay != s->applied.repeatDelay)
        keyboard->setRepeatInfo(st.repeatRate, st.repeatDelay);

    if (!st.layouts.empty() && (!s->appliedOnce || st.layouts != s->applied.layouts || st.variants != s->applied.variants))
    {
        // Several: Super+Space switches between them
        const char *options { st.layouts.find(',') != std::string::npos ? "grp:win_space_toggle" : nullptr };

        if (!keyboard->setKeymap(nullptr, nullptr, st.layouts.c_str(), st.variants.c_str(), options))
            Log(CZWarning, CZLN, "Invalid keyboard layouts \"{}\" (variants \"{}\")", st.layouts, st.variants);
    }

    if (st.backlightTimeout != s->timeoutSec)
    {
        s->timeoutSec = st.backlightTimeout;
        StartIdleTimer();
    }

    s->applied = st;
    s->appliedOnce = true;
}

void KeyboardSettings::Init() noexcept
{
    if (s)
        return;

    s = std::make_unique<State>();
    s->lastInput = std::chrono::steady_clock::now();

    // Off once the timeout passed since the last input (the timer isn't restarted per event)
    s->idleTimer = std::make_unique<CZTimer>([](CZTimer *timer){
        const auto idle { std::chrono::steady_clock::now() - s->lastInput };
        const auto timeout { std::chrono::seconds(s->timeoutSec) };

        if (s->timeoutSec <= 0)
            return;

        if (idle >= timeout)
            BacklightOff();
        else
            timer->start(UInt32(std::chrono::duration_cast<std::chrono::milliseconds>(timeout - idle).count()) + 1);
    });

    Apply();

    // Changes: the file is renamed into place (see System Settings), watch the directory
    const auto dir { ConfigDir() };
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const int fd { inotify_init1(IN_NONBLOCK | IN_CLOEXEC) };

    if (fd < 0 || inotify_add_watch(fd, dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE) < 0)
    {
        if (fd >= 0)
            close(fd);

        Log(CZWarning, CZLN, "Can't watch {}: keyboard settings apply on restart only", dir.string());
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

                if (ev->len > 0 && std::string(ev->name) == "keyboard.conf")
                    changed = true;

                p += sizeof(inotify_event) + ev->len;
            }

        if (changed)
            Apply();
    });
}

void KeyboardSettings::Clear() noexcept
{
    if (!s)
        return;

    s->watch.reset();
    s->idleTimer.reset();
    s->worker.reset(); // Waits for the running job
    s.reset();
}

void KeyboardSettings::Activity() noexcept
{
    if (!s || s->timeoutSec <= 0)
        return;

    s->lastInput = std::chrono::steady_clock::now();

    if (s->off)
    {
        BacklightOn();
        s->idleTimer->start(UInt32(s->timeoutSec) * 1000);
    }
    else if (!s->idleTimer->running())
        s->idleTimer->start(UInt32(s->timeoutSec) * 1000);
}
