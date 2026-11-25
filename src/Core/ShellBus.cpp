#include <Core/ShellBus.h>
#include <Core/Attention.h>
#include <Core/Workspaces.h>
#include <Core/Crystals.h>
#include <Core/Client.h>
#include <Core/Log.h>
#include <Effects/OpenFromIcon.h>
#include <Core/CZBus.h>
#include <systemd/sd-bus.h>
#include <memory>
#include <cstring>

static std::shared_ptr<CZBus> s_bus; // Kept: the connection closes with its last reference
static sd_bus_slot *s_launchedMatch {};
static sd_bus_slot *s_startedMatch {};
static sd_bus_slot *s_object {};

// Only Desk may announce its windows
static bool SentByDesk(sd_bus_message *m) noexcept
{
    Client *desk { GetCompositor()->deskClient() };

    if (!desk)
        return false;

    pid_t deskPid { 0 };
    desk->credentials(&deskPid);

    sd_bus_creds *creds { nullptr };
    pid_t pid { 0 };
    const bool ok { sd_bus_query_sender_creds(m, SD_BUS_CREDS_PID, &creds) >= 0 && sd_bus_creds_get_pid(creds, &pid) >= 0 };
    sd_bus_creds_unref(creds);
    return ok && pid > 0 && pid == deskPid;
}

static int OpenFromIconMethod(sd_bus_message *m, void *, sd_bus_error *error)
{
    const char *title {};
    Int32 x, y, w, h;

    if (const int r { sd_bus_message_read(m, "siiii", &title, &x, &y, &w, &h) }; r < 0)
        return r;

    if (!SentByDesk(m))
        return sd_bus_error_set(error, SD_BUS_ERROR_ACCESS_DENIED, "Only Desk can announce windows");

    OpenFromIcon::Expect(title ? title : "", SkRect::MakeXYWH(x, y, w, h));
    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable CrystalsVTable[]
{
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("OpenFromIcon", "siiii", "", OpenFromIconMethod, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

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

    // Desk's calls (see OpenFromIcon)
    int r { sd_bus_add_object_vtable(s_bus->bus(), &s_object, "/org/cuarzo/Crystals", "org.cuarzo.Crystals", CrystalsVTable, nullptr) };

    if (r >= 0)
        r = sd_bus_request_name(s_bus->bus(), "org.cuarzo.Crystals", 0);

    if (r < 0)
        Log(CZWarning, CZLN, "Can't own org.cuarzo.Crystals on the session bus: {}", strerror(-r));

    sd_bus_flush(s_bus->bus());
}

void ShellBus::Clear() noexcept
{
    s_object = sd_bus_slot_unref(s_object);
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

void ShellBus::EmitWorkspaceChanged(const std::string &output, UInt32 index) noexcept
{
    if (!s_bus)
        return;

    sd_bus_emit_signal(s_bus->bus(), "/org/cuarzo/Crystals", "org.cuarzo.Crystals", "WorkspaceChanged", "su", output.c_str(), index);
    sd_bus_flush(s_bus->bus());
}
