#include <Roles/Surface.h>
#include <Scene/Scene.h>

void Surface::damageChanged()           { view.syncDamage(); view.syncImage(); }
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
            view.setParent(&GetScene()->layers[layer()]);
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
    Log(CZInfo, "Size Map {} x {}", size().width(), size().height());
    LSurface::mappingChanged();
    view.syncVisibility();
}

void Surface::raised() noexcept
{
    if (parent())
        return;

    view.setParent(&GetScene()->layers[layer()]);
}

void Surface::layerChanged() noexcept
{
    if (parent())
        return;

    view.setParent(&GetScene()->layers[layer()]);
}
