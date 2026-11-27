#include <Roles/ToplevelRole.h>
#include <Core/Attention.h>
#include <Core/Crystals.h>
#include <Louvre/Seat/LSeat.h>
#include <Roles/Surface.h>
#include <Effects/OpenFromIcon.h>
#include <Scene/Scene.h>
#include <Effects/TopbarAutoHide.h>
#include <Core/Workspaces.h>
#include <Effects/Expose.h>
#include <Effects/ShowDesktop.h>
#include <Effects/ResizeBlackout.h>

void Surface::damageChanged()           { view.syncDamage(); view.syncImage(); TopbarAutoHide::SurfaceDamaged(this); Expose::SurfaceDamaged(this); ShowDesktop::SurfaceDamaged(this); }
void Surface::opaqueRegionChanged()     { view.syncOpaqueRegion(); }
void Surface::invisibleRegionChanged()  { view.syncInvisibleRegion(); }
void Surface::inputRegionChanged()      { view.syncInputRegion(); }

void Surface::bufferSizeChanged()       { view.syncImage(); }
void Surface::sizeChanged()             { view.syncSize(); ResizeBlackout::SurfaceResized(this); }
void Surface::srcRectChanged()          { view.syncSrcRect(); }
void Surface::bufferScaleChanged()      { view.syncScale(); }
void Surface::bufferTransformChanged()  { view.syncTransform(); }

void Surface::roleChanged() noexcept
{
    if (cursorRole())
        view.setParent(nullptr);
    else if (dndIcon())
        view.setParent(&GetScene()->top); // Above everything (e.g. docks)
}

void Surface::parentChanged() noexcept
{
    if (parent())
    {
        Surface *p { static_cast<Surface*>(parent()) };
        switch (roleId())
        {
        case Role::Toplevel:
            view.setParent(&p->view.toplevels);
            break;
        case Role::Popup:
            // Above everything (see Scene); popups of popups inside their parent
            view.setParent(p->popup() ? &p->view.popups : &GetScene()->popups);
            break;
        case Role::Subsurface:
            view.setParent(&p->view.above);
            break;
        default:
            break;
        }
    }
    else
    {
        switch (roleId())
        {
        case Role::Toplevel:
            view.setParent(&GetScene()->containerFor(*this));
            break;
        case Role::Popup:
            view.setParent(nullptr);
            break;
        case Role::Subsurface:
            view.setParent(nullptr);
            break;
        default:
            break;
        }
    }
}

void Surface::mappingChanged()
{
    LSurface::mappingChanged();

    // The default placement centers the content alone (and also moves maximized windows): place
    // toplevels considering their title bar and state instead.
    if (mapped() && toplevel())
        static_cast<ToplevelRole*>(toplevel())->placeMapped();

    view.syncVisibility();

    // Layer surfaces: their scope (desktop, shell) is known by now
    if (!parent() && !dndIcon() && !cursorRole())
        view.setParent(&GetScene()->containerFor(*this));

    GetCompositor()->restack();

    if (toplevel())
    {
        Workspaces::Sync(*static_cast<ToplevelRole*>(toplevel()));
        Expose::WindowsChanged();

        // A new window: launched by the user from the dock, it's shown wherever it opened. Else, if
        // it landed in another workspace, its dock icon bounces (see Attention). Its app id is
        // surely known by now.
        if (mapped())
        {
            auto &role { *static_cast<ToplevelRole*>(toplevel()) };

            // Desk's new windows may grow from the icon they were opened from
            if (!role.mappedOnce)
            {
                role.mappedOnce = true;
                OpenFromIcon::WindowMapped(role);
            }
            const bool launched { Attention::ConsumeUserLaunch(role.appId()) };

            // A window appeared: the windows moved aside come back
            ShowDesktop::WindowActivated(&role);

            if (role.attentionPending)
            {
                if (launched)
                {
                    role.attentionPending = false;
                    role.configureState(role.pendingConfiguration().windowState | CZWinActivated);
                }
                else
                    Attention::Request(role);
            }
        }

        // A new window activated before mapping (see ToplevelRole::stateChanged()): its workspace is
        // known now that it's placed
        if (mapped() && toplevel()->isActivated())
        {
            Workspaces::Reveal(*static_cast<ToplevelRole*>(toplevel()));

            // E.g. an app launched from the dock: Exposé closes onto its window
            Expose::LeaveFor(*static_cast<ToplevelRole*>(toplevel()));
        }
    }

    TopbarAutoHide::UpdateAll();

    // The desktop appeared/disappeared: it owns the menus while no toplevel is active.
    if ((!seat()->activeToplevel() || GetCompositor()->deskActive()) && Crystals::IsDesk(this))
        GetCompositor()->updateActiveHeavenClient();
}

void Surface::raised() noexcept
{
    if (parent() || dndIcon())
        return;

    view.setParent(&GetScene()->containerFor(*this));
    GetCompositor()->restack();
    TopbarAutoHide::UpdateAll(); // The topmost window of an output may have changed
}

void Surface::layerChanged() noexcept
{
    if (parent() || dndIcon())
        return;

    view.setParent(&GetScene()->containerFor(*this));
    GetCompositor()->restack();
    TopbarAutoHide::UpdateAll();
}
