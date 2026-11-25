#include <Effects/OpenFromIcon.h>
#include <Effects/MinimizeAnimation.h>
#include <Core/Crystals.h>
#include <Core/Client.h>
#include <Roles/Surface.h>
#include <Louvre/Roles/LToplevelRole.h>
#include <Louvre/Roles/LSurface.h>
#include <Core/CZTime.h>
#include <algorithm>
#include <vector>

namespace
{
    struct Expected
    {
        std::string title;
        SkRect icon;
        UInt32 sinceMs;
    };

    std::vector<Expected> &List() noexcept
    {
        static std::vector<Expected> list;
        return list;
    }

    void DropOld() noexcept
    {
        const UInt32 now { CZTime::Ms() };
        std::erase_if(List(), [now](const Expected &e){ return now - e.sinceMs > OpenFromIcon::ExpectMs; });
    }
}

void OpenFromIcon::Expect(const std::string &title, const SkRect &icon) noexcept
{
    DropOld();

    if (!icon.isEmpty())
        List().push_back({ title, icon, CZTime::Ms() });
}

void OpenFromIcon::WindowMapped(LToplevelRole &toplevel) noexcept
{
    auto &list { List() };

    if (list.empty())
        return;

    DropOld();
    LSurface *surface { toplevel.surface() };
    Client *desk { GetCompositor()->deskClient() };

    if (!surface || !desk || surface->client() != desk)
        return;

    // The oldest announcement with its title (several folders may be opened at once)
    auto it { std::find_if(list.begin(), list.end(), [&toplevel](const Expected &e){ return e.title == toplevel.title(); }) };

    if (it == list.end())
        return;

    const SkRect icon { it->icon };
    list.erase(it);
    MinimizeAnimation::StartFrom(toplevel, icon);
    static_cast<Surface*>(surface)->view.syncVisibility();
}

void OpenFromIcon::Clear() noexcept
{
    List().clear();
}
