#include <ui/SurfaceView.h>
#include <ui/Scene.h>
#include <roles/Surface.h>
#include <utils/Converter.h>

SurfaceView::SurfaceView(Surface &surface) noexcept :
    AKRenderableImage(&G::scene()->layers[LLayerMiddle]),
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
    layout().setWidth(surface.size().w());
    layout().setHeight(surface.size().h());
}

void SurfaceView::syncDamage() noexcept
{
    SkRegion damage;
    Converter::LtSRegion(surface.damage(), &damage);
    addDamage(damage);
}

void SurfaceView::syncOpaqueRegion() noexcept
{
    SkRegion region;
    Converter::LtSRegion(surface.opaqueRegion(), &region);
    opaqueRegion.set(region);
    repaint();
}

void SurfaceView::syncInvisibleRegion() noexcept
{
    SkRegion region;
    Converter::LtSRegion(surface.invisibleRegion(), &region);
    invisibleRegion.set(region);
    repaint();
}

void SurfaceView::syncInputRegion() noexcept
{
    SkRegion region;
    Converter::LtSRegion(surface.inputRegion(), &region);
    setInputRegion(&region);
}

void SurfaceView::syncTexture() noexcept
{
    setImage(Converter::LtSTexture(surface.texture()));
}

void SurfaceView::syncScale() noexcept
{
    setCustomSrcRectScale(surface.bufferScale());
}

void SurfaceView::syncTransform() noexcept
{
    setSrcTransform((AKTransform)surface.bufferTransform());
}

void SurfaceView::syncSrcRect() noexcept
{
    setCustomSrcRect(SkRect::MakeXYWH(
        surface.srcRect().x(),
        surface.srcRect().y(),
        surface.srcRect().w(),
        surface.srcRect().h()));
}

void SurfaceView::syncVisibility() noexcept
{
    setVisible(
        surface.mapped() &&
        !surface.minimized() &&
        !surface.cursorRole());
}
