#include <Core/Attention.h>
#include <Core/SoundSettings.h>
#include <Core/Workspaces.h>
#include <Core/ShellBus.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <vector>
#include <Core/Log.h>
#include <Effects/Expose.h>
#include <Roles/ToplevelRole.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Cursor/LCursor.h>
#include <Louvre/Roles/LSurface.h>

static bool s_activatingByToken { false };

struct Launch
{
    std::string appId;
    std::chrono::steady_clock::time_point time;
};

static std::vector<Launch> s_launches;

static std::string Normalized(std::string id) noexcept
{
    if (id.ends_with(".desktop"))
        id.resize(id.size() - 8);

    std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c){ return std::tolower(c); });
    return id;
}

// The desktop entry id (from the dock) and the window's app id usually match, else one may be the
// reverse-DNS form of the other (e.g. "org.mozilla.firefox" and "firefox")
static bool SameApp(const std::string &a, const std::string &b) noexcept
{
    if (a.empty() || b.empty())
        return false;

    return a == b || a.ends_with("." + b) || b.ends_with("." + a);
}

static std::vector<Launch>::iterator FindLaunch(const std::string &appId) noexcept
{
    const auto now { std::chrono::steady_clock::now() };
    std::erase_if(s_launches, [&now](const Launch &l){
        return now - l.time > std::chrono::milliseconds(Attention::UserLaunchTimeoutMs); });

    const std::string id { Normalized(appId) };
    return std::find_if(s_launches.begin(), s_launches.end(), [&id](const Launch &l){ return SameApp(l.appId, id); });
}

void Attention::UserLaunched(const std::string &appId) noexcept
{
    if (appId.empty())
        return;

    if (auto it { FindLaunch(appId) }; it != s_launches.end())
        s_launches.erase(it);

    s_launches.push_back({ Normalized(appId), std::chrono::steady_clock::now() });
}

bool Attention::IsUserLaunch(const std::string &appId) noexcept
{
    return FindLaunch(appId) != s_launches.end();
}

bool Attention::ConsumeUserLaunch(const std::string &appId) noexcept
{
    auto it { FindLaunch(appId) };

    if (it == s_launches.end())
        return false;

    s_launches.erase(it);
    return true;
}

bool Attention::ActivatingByToken() noexcept
{
    return s_activatingByToken;
}

bool Attention::WouldLeave(const ToplevelRole &toplevel) noexcept
{
    LSurface *surface { toplevel.surface() };

    // Dialogs go with their parent; Exposé shows every window anyway
    if (!surface || surface->parent() || Expose::Active())
        return false;

    // The app being used opened it (e.g. a new window of a fullscreen browser): the user asked for it
    if (LToplevelRole *active { seat()->activeToplevel() }; active && active != &toplevel &&
        active->surface() && active->surface()->client() == surface->client())
        return false;

    // Already in a workspace: whether it's another one than the one shown
    if (surface->mapped())
    {
        size_t index;
        const Workspaces *ws { Workspaces::Find(surface, &index) };
        return ws && index != ws->current();
    }

    // New: it lands on the current desktop, or the first one if a fullscreen window's is shown
    const Workspaces *ws { Workspaces::Of(cursor()->output()) };
    return ws && ws->currentFullscreen();
}

void Attention::Request(ToplevelRole &toplevel) noexcept
{
    if (!toplevel.surface() || !toplevel.surface()->mapped())
    {
        toplevel.attentionPending = true;
        return;
    }

    toplevel.attentionPending = false;
    SoundSettings::PlayAlert();
    ShellBus::EmitAppAttention(toplevel.appId());
}

void ActivationTokenManager::activateSurfaceRequest(LSurface *surface)
{
    // Louvre's checks (a recent user event of the app asking), then ToplevelRole::activateRequest()
    s_activatingByToken = true;
    LActivationTokenManager::activateSurfaceRequest(surface);
    s_activatingByToken = false;
}
