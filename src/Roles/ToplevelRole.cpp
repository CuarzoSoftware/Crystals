#include <Roles/ToplevelRole.h>
#include <Roles/Surface.h>
#include <Core/Client.h>
#include <Louvre/Seat/LSeat.h>

void ToplevelRole::minimizedChanged()
{
    LToplevelRole::minimizedChanged();

    auto *surf { static_cast<Surface*>(surface()) };
    surf->view.syncVisibility();
}

void ToplevelRole::appIdChanged()
{
    LToplevelRole::appIdChanged();

    // Keep the bar's view of the active app in sync (e.g. an app id set after activation).
    if (seat()->activeToplevel() == this)
        static_cast<Client*>(client())->setActiveHeavenClient(this);
}
