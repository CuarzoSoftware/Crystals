#include <Roles/ToplevelRole.h>
#include <Core/Crystals.h>
#include <Louvre/Seat/LSeat.h>
#include <Roles/Surface.h>
#include <Scene/Scene.h>
#include <Effects/TopbarAutoHide.h>
#include <Core/Workspaces.h>

void Surface::damageChanged()           { view.syncDamage(); view.syncImage(); TopbarAutoHide::SurfaceDamaged(this); }
void Surface::opaqueRegionChanged()     { view.syncOpaqueRegion(); }
void Surface::invisibleRegionChanged()  { view.syncInvisibleRegion(); }
void Surface::inputRegionChanged()      { view.syncInputRegion(); }

void Surface::bufferSizeChanged()       { view.syncImage(); }
void Surface::sizeChanged()             { view.syncSize(); }
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
            view.setParent(&p->view.popups);
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
        Workspaces::Sync(*static_cast<ToplevelRole*>(toplevel()));

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
