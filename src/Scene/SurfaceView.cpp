#include <Scene/SurfaceView.h>
#include <Scene/Scene.h>
#include <Roles/ToplevelRole.h>
#include <Roles/Surface.h>
#include <Effects/MinimizeAnimation.h>
#include <Effects/TopbarAutoHide.h>
#include <Effects/FullscreenAnimation.h>
#include <Core/Workspaces.h>
#include <Louvre/Roles/LPopupRole.h>
#include <Decorations/SSDTheme.h>
#include <algorithm>
#include <cmath>
#include <Louvre/LCompositor.h>
#include <Louvre/Seat/LOutput.h>

SurfaceView::SurfaceView(Surface &surface) noexcept :
    AKContainer(YGFlexDirectionColumn, false, &GetScene()->layers[LLayerMiddle]),
    surface(surface)
{
    setVisible(false);
    layout().setPositionType(YGPositionTypeAbsolute);
    above.layout().setPositionType(YGPositionTypeAbsolute);
    below.layout().setPositionType(YGPositionTypeAbsolute);

    // Detach pos from parent and use Louvre's world pos
    popups.layout().setPositionType(YGPositionTypeAbsolute);
    popups.layout().setAnchorNode(&GetScene()->root);
    toplevels.layout().setPositionType(YGPositionTypeAbsolute);
    toplevels.layout().setAnchorNode(&GetScene()->root);

    // The view itself carries the surface's input region (syncInputRegion()), its parts take none.
    const SkRegion empty;

    for (AKNode *part : std::initializer_list<AKNode*>{ &below, &view, &bottomLeft, &bottomMiddle, &bottomRight, &above, &popups, &toplevels })
        part->setInputRegion(&empty);

    view.layout().setPositionType(YGPositionTypeRelative);
    view.enableAutoDamage(false);
    view.setSrcRectMode(AKImage::SrcRectMode::Custom);
    view.layout().setWidthPercent(100.f);
    view.layout().setHeightPercent(100.f);

    for (AKImage *piece : { &bottomLeft, &bottomMiddle, &bottomRight })
    {
        piece->setVisible(false);
        piece->enableAutoDamage(false);
        piece->setSrcRectMode(AKImage::SrcRectMode::Custom);
        piece->layout().setPositionType(YGPositionTypeAbsolute);
    }
}

bool SurfaceView::split() const noexcept
{
    const SkISize size { surface.size() };
    return m_bottomRadius > 0 && surface.bufferTransform() == CZTransform::Normal &&
           size.width() > 2 * m_bottomRadius && size.height() > m_bottomRadius;
}

SkRect SurfaceView::pieceSrc(const SkIRect &piece) const noexcept
{
    // The surface's source rect (logical units) maps to its whole size: take the piece's share.
    const SkRect &src { surface.srcRect() };
    const SkISize size { surface.size() };
    const SkScalar sx { src.width() / SkScalar(std::max(1, size.width())) };
    const SkScalar sy { src.height() / SkScalar(std::max(1, size.height())) };
    return SkRect::MakeXYWH(src.x() + piece.x() * sx, src.y() + piece.y() * sy, piece.width() * sx, piece.height() * sy);
}

void SurfaceView::setBottomRadius(Int32 radius) noexcept
{
    radius = std::max(0, radius);

    if (m_bottomRadius == radius)
        return;

    m_bottomRadius = radius;
    syncPieces();
    syncOpaqueRegion();
    view.addDamage(AK_IRECT_INF);
}

void SurfaceView::syncPieces() noexcept
{
    const bool on { split() };
    const SkISize size { surface.size() };
    const Int32 r { m_bottomRadius };

    for (AKImage *piece : { &bottomLeft, &bottomMiddle, &bottomRight })
        piece->setVisible(on);

    if (!on)
    {
        view.layout().setHeightPercent(100.f);
        view.setCustomSrcRect(surface.srcRect());
        return;
    }

    view.layout().setHeight(size.height() - r);
    view.setCustomSrcRect(pieceSrc(SkIRect::MakeWH(size.width(), size.height() - r)));

    const SkIRect rects[3] {
        SkIRect::MakeXYWH(0, size.height() - r, r, r),
        SkIRect::MakeXYWH(r, size.height() - r, size.width() - 2 * r, r),
        SkIRect::MakeXYWH(size.width() - r, size.height() - r, r, r) };
    AKImage *pieces[3] { &bottomLeft, &bottomMiddle, &bottomRight };

    for (int i = 0; i < 3; i++)
    {
        pieces[i]->layout().setPosition(YGEdgeLeft, rects[i].x());
        pieces[i]->layout().setPosition(YGEdgeTop, rects[i].y());
        pieces[i]->layout().setWidth(rects[i].width());
        pieces[i]->layout().setHeight(rects[i].height());
        pieces[i]->setImage(surface.image());
        pieces[i]->setCustomSrcRectScale(surface.scale());
        pieces[i]->setCustomSrcRect(pieceSrc(rects[i]));
        pieces[i]->addDamage(AK_IRECT_INF);
    }

    // Masks at the highest output scale (sharp everywhere; stretched over the corners).
    Int32 scale { 1 };
    for (LOutput *o : compositor()->outputs())
        scale = std::max(scale, Int32(std::ceil(o->scale())));
    bottomLeft.setMask(SSDTheme::CornerMask(scale, true));
    bottomRight.setMask(SSDTheme::CornerMask(scale, false));
}

void SurfaceView::syncPos() noexcept
{
    if (surface.subsurface())
        return;

    // Where its workspace is (sliding, or parked away while not shown: see Workspaces)
    const SkIPoint pos { surface.rolePos() + Workspaces::ViewOffset(&surface) };
    layout().setPosition(YGEdgeLeft, pos.x());
    layout().setPosition(YGEdgeTop, pos.y());
}

void SurfaceView::syncSize() noexcept
{
    layout().setWidth(surface.size().width());
    layout().setHeight(surface.size().height());
    syncPieces();

    // Decorations follow the window size.
    if (surface.toplevel())
        static_cast<ToplevelRole*>(surface.toplevel())->updateDecorations();
}

void SurfaceView::syncDamage() noexcept
{
    if (!split())
    {
        view.addDamage(surface.damage());
        return;
    }

    // Each piece gets its share of the damage, in its own coordinates.
    const SkISize size { surface.size() };
    const Int32 r { m_bottomRadius };
    SkRegion top { surface.damage() };
    top.op(SkIRect::MakeWH(size.width(), size.height() - r), SkRegion::kIntersect_Op);
    view.addDamage(top);

    const std::pair<AKImage*, SkIRect> pieces[3] {
        { &bottomLeft, SkIRect::MakeXYWH(0, size.height() - r, r, r) },
        { &bottomMiddle, SkIRect::MakeXYWH(r, size.height() - r, size.width() - 2 * r, r) },
        { &bottomRight, SkIRect::MakeXYWH(size.width() - r, size.height() - r, r, r) } };

    for (const auto &[piece, rect] : pieces)
    {
        SkRegion damage { surface.damage() };
        damage.op(rect, SkRegion::kIntersect_Op);
        damage.translate(-rect.x(), -rect.y());
        piece->addDamage(damage);
    }
}

void SurfaceView::syncOpaqueRegion() noexcept
{
    if (!split())
    {
        view.opaqueRegion.set(surface.opaqueRegion());
        return;
    }

    // The masked corners are never opaque.
    const SkISize size { surface.size() };
    const Int32 r { m_bottomRadius };
    SkRegion top { surface.opaqueRegion() };
    top.op(SkIRect::MakeWH(size.width(), size.height() - r), SkRegion::kIntersect_Op);
    view.opaqueRegion.set(top);

    SkRegion middle { surface.opaqueRegion() };
    middle.op(SkIRect::MakeXYWH(r, size.height() - r, size.width() - 2 * r, r), SkRegion::kIntersect_Op);
    middle.translate(-r, -(size.height() - r));
    bottomMiddle.opaqueRegion.set(middle);
    bottomLeft.opaqueRegion.set(SkRegion());
    bottomRight.opaqueRegion.set(SkRegion());
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

    for (AKImage *piece : { &bottomLeft, &bottomMiddle, &bottomRight })
        piece->setImage(surface.image());
}

void SurfaceView::syncScale() noexcept
{
    view.setCustomSrcRectScale(surface.scale());

    for (AKImage *piece : { &bottomLeft, &bottomMiddle, &bottomRight })
        piece->setCustomSrcRectScale(surface.scale());
}

void SurfaceView::syncTransform() noexcept
{
    view.setSrcTransform(surface.bufferTransform());
    syncPieces(); // Rounded only without a buffer transform
}

void SurfaceView::syncSrcRect() noexcept
{
    syncPieces();
}

void SurfaceView::syncVisibility() noexcept
{
    const auto isMinimized { surface.toplevel() && surface.toplevel()->isMinimized() };

    setVisible(
        surface.mapped() &&
        !surface.cursorRole() &&
        !isMinimized &&
        !MinimizeAnimation::HidesSurface(&surface) && // Unminimizing: the animation shows it
        !TopbarAutoHide::HidesSurface(&surface) &&    // A top bar hidden by a fullscreen window
        !Workspaces::HidesSurface(&surface) &&        // Its workspace is out of view
        !FullscreenAnimation::HidesSurface(&surface)); // Entering/leaving fullscreen: the animation shows it
}
