#include <Scene/SurfaceView.h>
#include <Scene/Scene.h>
#include <Roles/Surface.h>

SurfaceView::SurfaceView(Surface &surface) noexcept :
    AKContainer(YGFlexDirectionColumn, false, &GetScene()->layers[LLayerMiddle]),
    surface(surface)
{
    layout().setPositionType(YGPositionTypeAbsolute);
    above.layout().setPositionType(YGPositionTypeAbsolute);
    below.layout().setPositionType(YGPositionTypeAbsolute);
    popups.layout().setPositionType(YGPositionTypeAbsolute);
    toplevels.layout().setPositionType(YGPositionTypeAbsolute);
    view.layout().setPositionType(YGPositionTypeAbsolute);
    view.enableAutoDamage(false);
    view.setSrcRectMode(AKImage::SrcRectMode::Custom);
    view.layout().setWidthPercent(100.f);
    view.layout().setHeightPercent(100.f);
}

void SurfaceView::syncPos() noexcept
{
    if (surface.subsurface())
        return;

    const auto &rolePos { surface.rolePos() };
    layout().setPosition(YGEdgeLeft, rolePos.x());
    layout().setPosition(YGEdgeTop, rolePos.y());
}

void SurfaceView::syncSize() noexcept
{
    layout().setWidth(surface.size().width());
    layout().setHeight(surface.size().height());
}

void SurfaceView::syncDamage() noexcept
{
    view.addDamage(surface.damage());
}

void SurfaceView::syncOpaqueRegion() noexcept
{
    view.opaqueRegion.set(surface.opaqueRegion());
}

void SurfaceView::syncInvisibleRegion() noexcept
{
    view.invisibleRegion.set(surface.invisibleRegion());
}

void SurfaceView::syncInputRegion() noexcept
{
    setInputRegion(&surface.inputRegion());
}

void SurfaceView::syncImage() noexcept
{
    view.setImage(surface.image());
}

void SurfaceView::syncScale() noexcept
{
    view.setCustomSrcRectScale(surface.bufferScale());
}

void SurfaceView::syncTransform() noexcept
{
    view.setSrcTransform(surface.bufferTransform());
}

void SurfaceView::syncSrcRect() noexcept
{
    view.setCustomSrcRect(surface.srcRect());
}

void SurfaceView::syncVisibility() noexcept
{
    setVisible(
        surface.mapped() &&
        !surface.minimized() &&
        !surface.cursorRole());
}
