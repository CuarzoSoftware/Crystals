#include <Core/ShellBus.h>
#include <Core/Attention.h>
#include <Core/Workspaces.h>
#include <Core/CZBus.h>
#include <systemd/sd-bus.h>
#include <memory>

static std::shared_ptr<CZBus> s_bus; // Kept: the connection closes with its last reference
static sd_bus_slot *s_launchedMatch {};
static sd_bus_slot *s_startedMatch {};

void ShellBus::Init() noexcept
{
    if (s_bus)
        return;

    s_bus = CZBus::GetOrMakeUser();

    if (!s_bus)
        return;

    sd_bus_match_signal_async(s_bus->bus(), &s_launchedMatch, nullptr, "/org/cuarzo/Shelf",
        "org.cuarzo.Shelf", "AppLaunched",
        [](sd_bus_message *m, void *, sd_bus_error *) -> int {
            const char *appId {};

            if (sd_bus_message_read(m, "s", &appId) >= 0 && appId)
                Attention::UserLaunched(appId);

            return 0;
        }, nullptr, nullptr);

    sd_bus_match_signal_async(s_bus->bus(), &s_startedMatch, nullptr, "/org/cuarzo/Shelf",
        "org.cuarzo.Shelf", "Started",
        [](sd_bus_message *, void *, sd_bus_error *) -> int {
            Workspaces::NotifyFullscreenAll();
            return 0;
        }, nullptr, nullptr);

    sd_bus_flush(s_bus->bus());
}

void ShellBus::Clear() noexcept
{
    s_launchedMatch = sd_bus_slot_unref(s_launchedMatch);
    s_startedMatch = sd_bus_slot_unref(s_startedMatch);
    s_bus.reset();
}

void ShellBus::EmitAppAttention(const std::string &appId) noexcept
{
    if (!s_bus || appId.empty())
        return;

    sd_bus_emit_signal(s_bus->bus(), "/org/cuarzo/Crystals", "org.cuarzo.Crystals", "AppAttention", "s", appId.c_str());
    sd_bus_flush(s_bus->bus());
}

void ShellBus::EmitFullscreenChanged(bool fullscreen, const std::string &output) noexcept
{
    if (!s_bus)
        return;

    sd_bus_emit_signal(s_bus->bus(), "/org/cuarzo/Crystals", "org.cuarzo.Crystals", "FullscreenChanged", "bs", int(fullscreen), output.c_str());
    sd_bus_flush(s_bus->bus());
}
