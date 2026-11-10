#include <Roles/ToplevelRole.h>
#include <Roles/Surface.h>
#include <Effects/MinimizeAnimation.h>
#include <Core/Client.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Seat/LKeyboard.h>
#include <Louvre/Roles/LSurface.h>

void ToplevelRole::minimizedChanged()
{
    LToplevelRole::minimizedChanged();

    auto *surf { static_cast<Surface*>(surface()) };

    if (surf->mapped())
        MinimizeAnimation::Start(*this, isMinimized());

    surf->view.syncVisibility();
}

void ToplevelRole::appIdChanged()
{
    LToplevelRole::appIdChanged();

    // Keep the bar's view of the active app in sync (e.g. an app id set after activation).
    if (seat()->activeToplevel() == this)
        static_cast<Client*>(client())->setActiveHeavenClient(this);
}

void ToplevelRole::stateChanged(CZBitset<Changes> changes, const State &prev)
{
    LToplevelRole::stateChanged(changes, prev);

    // Activated (new, clicked, or asked by e.g. the dock): it also gets the keyboard focus, which
    // apps (e.g. Qt ones) treat as being the active window.
    if (changes.has(StateChanged) && isActivated() && !prev.windowState.has(CZWinActivated))
    {
        auto *keyboard { seat()->keyboard() };

        if (!keyboard->grab() && !surface()->hasKeyboardFocus())
            keyboard->setFocus(surface());
    }
}
