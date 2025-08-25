#include <Scene/SurfaceView.h>
#include <Scene/Scene.h>
#include <Roles/Surface.h>

SurfaceView::SurfaceView(Surface &surface) noexcept :
    AKImage(&GetScene()->layers[LLayerMiddle]),
    surface(surface)
{
    enableAutoDamage(false);
    setSrcRectMode(SrcRectMode::Custom);
    layout().setPositionType(YGPositionTypeAbsolute);
}

void SurfaceView::syncPos() noexcept
{
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
    addDamage(surface.damage());
}

void SurfaceView::syncOpaqueRegion() noexcept
{
    opaqueRegion.set(surface.opaqueRegion());
}

void SurfaceView::syncInvisibleRegion() noexcept
{
    invisibleRegion.set(surface.invisibleRegion());
}

void SurfaceView::syncInputRegion() noexcept
{
    setInputRegion(&surface.inputRegion());
}

void SurfaceView::syncImage() noexcept
{
    setImage(surface.image());
}

void SurfaceView::syncScale() noexcept
{
    setCustomSrcRectScale(surface.bufferScale());
}

void SurfaceView::syncTransform() noexcept
{
    setSrcTransform(surface.bufferTransform());
}

void SurfaceView::syncSrcRect() noexcept
{
    setCustomSrcRect(surface.srcRect());
}

void SurfaceView::syncVisibility() noexcept
{
    setVisible(
        surface.mapped() &&
        !surface.minimized() &&
        !surface.cursorRole());
}
