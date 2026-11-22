#include <Effects/Snapshot.h>
#include <Roles/ToplevelRole.h>
#include <Roles/Surface.h>
#include <Decorations/SSD.h>
#include <Decorations/SSDTheme.h>
#include <AK/AKScene.h>
#include <AK/AKTarget.h>
#include <AK/Nodes/AKContainer.h>
#include <Ream/RSurface.h>
#include <Ream/RPass.h>
#include <Ream/RPainter.h>
#include <Effects/SurfaceBlurManager.h>
#include <CZ/skia/core/SkCanvas.h>
#include <Louvre/Roles/LLayerRole.h>
#include <Louvre/Seat/LOutput.h>
#include <Louvre/LCompositor.h>
#include <Core/Crystals.h>
#include <Core/Workspaces.h>
#include <CZ/skia/core/SkColorFilter.h>
#include <CZ/skia/effects/SkImageFilters.h>
#include <Ream/RImage.h>
#include <CZ/skia/core/SkImage.h>
#include <CZ/skia/core/SkPath.h>
#include <CZ/skia/core/SkRRect.h>
#include <Louvre/Roles/LSubsurfaceRole.h>
#include <vector>

namespace
{
    struct BakeScene
    {
        BakeScene() noexcept
        {
            scene = AKScene::Make();
            scene->setRoot(&root);
            target = scene->makeTarget();
            target->setClearColor(SK_ColorTRANSPARENT);
        }

        std::shared_ptr<AKScene> scene;
        AKContainer root { YGFlexDirectionColumn, false };
        std::shared_ptr<AKTarget> target;
    };

    std::unique_ptr<BakeScene> s_bake;

    // A subsurface's view as it was before being shown in the snapshot (see View())
    struct SubsurfaceState
    {
        Surface *surface;
        bool visible, popups, toplevels, blur;
    };

    // Its mapped subsurfaces (and theirs) are shown: they may be hidden with their window (e.g.
    // Expose::HidesSurface() hides the whole tree), only the root view is shown by View()
    void ShowSubsurfaces(Surface &surface, std::vector<SubsurfaceState> &states) noexcept
    {
        for (const auto *list : { &surface.subsurfacesBelow(), &surface.subsurfacesAbove() })
        {
            for (LSubsurfaceRole *role : *list)
            {
                auto *sub { static_cast<Surface*>(role->surface()) };

                if (!sub || !sub->mapped())
                    continue;

                auto *blur { static_cast<SurfaceBlurManager*>(sub->backgroundBlur()) };
                SurfaceView &v { sub->view };
                states.push_back({ sub, v.visible(), v.popups.visible(), v.toplevels.visible(), blur && blur->fx.visible() });

                // Neither its popups or child windows (placed in global coordinates), nor its blur
                v.popups.setVisible(false);
                v.toplevels.setVisible(false);
                v.setVisible(true);

                if (blur)
                    blur->fx.setVisible(false);

                ShowSubsurfaces(*sub, states);
            }
        }
    }

    void RestoreSubsurfaces(const std::vector<SubsurfaceState> &states) noexcept
    {
        for (const auto &st : states)
        {
            SurfaceView &v { st.surface->view };
            v.popups.setVisible(st.popups);
            v.toplevels.setVisible(st.toplevels);
            v.setVisible(st.visible);

            if (auto *blur { static_cast<SurfaceBlurManager*>(st.surface->backgroundBlur()) })
                blur->fx.setVisible(st.blur);
        }
    }

    // Clips the subsurfaces of @p surface to @p clip (surface coordinates): their containers get its
    // rect (clipping their children), the direct subsurfaces are moved to stay where they were
    void ClipSubsurfaces(Surface &surface, const SkIRect *clip) noexcept
    {
        SurfaceView &view { surface.view };

        for (AKContainer *c : { &view.below, &view.above })
        {
            c->enableChildrenClipping(clip);
            c->layout().setPosition(YGEdgeLeft, clip ? clip->x() : YGUndefined);
            c->layout().setPosition(YGEdgeTop, clip ? clip->y() : YGUndefined);
            c->layout().setWidth(clip ? clip->width() : YGUndefined);
            c->layout().setHeight(clip ? clip->height() : YGUndefined);
        }

        const SkIPoint offset { clip ? SkIPoint::Make(clip->x(), clip->y()) : SkIPoint::Make(0, 0) };

        for (const auto *list : { &surface.subsurfacesBelow(), &surface.subsurfacesAbove() })
        {
            for (LSubsurfaceRole *role : *list)
            {
                auto *sub { static_cast<Surface*>(role->surface()) };

                if (!sub)
                    continue;

                sub->view.layout().setPosition(YGEdgeLeft, role->localPos().x() - offset.x());
                sub->view.layout().setPosition(YGEdgeTop, role->localPos().y() - offset.y());
            }
        }
    }
}

void Snapshot::Clear() noexcept
{
    s_bake.reset();
}

// The blur's region (surface coordinates, within the surface), as the client set it last
static SkRegion BlurRegion(const SurfaceBlurManager &blur, const Surface &surface) noexcept
{
    const SkIRect bounds { SkIRect::MakeSize(surface.size()) };
    SkRegion region;

    if (blur.fx.areaType() == AKBackgroundBlurEffect::Region)
        region = blur.fx.region();
    else
        region.setRect(bounds);

    region.op(bounds, SkRegion::kIntersect_Op);
    return region;
}

// Clips @p c to the blur's current shape (the client's mask and the window's rounded corners)
static void ClipToBlurShape(SkCanvas &c, const AKBackgroundBlurEffect &fx) noexcept
{
    if (fx.clipType() == AKBackgroundBlurEffect::RoundRect)
    {
        const CZRRect &r { fx.roundRectClip() };
        const SkVector radii[4] {
            SkVector::Make(r.fRadTL, r.fRadTL), SkVector::Make(r.fRadTR, r.fRadTR),
            SkVector::Make(r.fRadBR, r.fRadBR), SkVector::Make(r.fRadBL, r.fRadBL) };
        SkRRect rrect;
        rrect.setRectRadii(SkRect::Make(static_cast<const SkIRect&>(r)), radii);
        c.clipRRect(rrect, true);
    }
    else if (fx.clipType() == AKBackgroundBlurEffect::Path)
        c.clipPath(fx.pathClip(), true);
}

Snapshot::Blur Snapshot::CaptureBlur(Surface &surface, Int32 scale) noexcept
{
    Blur result;
    auto *blur { static_cast<SurfaceBlurManager*>(surface.backgroundBlur()) };

    if (!blur || !blur->visible())
        return result;

    const SkIRect area { BlurRegion(*blur, surface).getBounds() };

    if (area.isEmpty())
        return result;

    auto image { RSurface::Make(SkISize::Make(area.width() * scale, area.height() * scale), 1, true) };
    auto pass { image ? image->beginPass(RPassCap_Painter | RPassCap_SkCanvas) : nullptr };

    if (!pass)
        return result;

    auto *painter { pass->getPainter() };
    painter->setBlendMode(RBlendMode::Src);
    painter->setColor(SK_ColorTRANSPARENT);
    painter->drawColor(SkRegion(SkIRect::MakeWH(area.width() * scale, area.height() * scale)));

    SkCanvas &c { *pass->getCanvas() };
    c.save();
    c.scale(scale, scale);
    c.translate(-area.x(), -area.y());
    const bool drawn { blur->fx.drawBlurred(c) };
    c.restore();
    pass.reset();

    if (!drawn)
        return result;

    result.image = image->image();
    result.area = area;
    return result;
}

Snapshot::Blur Snapshot::Backdrop(ToplevelRole &toplevel, LOutput *output, Int32 scale) noexcept
{
    Blur result;
    auto *surface { static_cast<Surface*>(toplevel.surface()) };
    auto *blur { surface ? static_cast<SurfaceBlurManager*>(surface->backgroundBlur()) : nullptr };

    if (!output || !blur || !blur->visible() || surface->size().isEmpty())
        return result;

    // Where it is with its workspace in view (the windows of a workspace not shown are moved away
    // together, see Workspaces::ParkOffset()). The blur reaches ~3 sigma beyond: captured too.
    const auto unparked = [](const LSurface *s){ return s->rolePos() - Workspaces::ParkOffset(s); };
    const SkIPoint origin { unparked(surface) };
    const SkIRect rect { SkIRect::MakeXYWH(origin.x(), origin.y(), surface->size().width(), surface->size().height()) };
    const SkScalar sigma { blur->fx.blurRadius() };
    const Int32 pad { SkScalarCeilToInt(sigma * 3.f) };
    const SkIRect area { rect.makeOutset(pad, pad) };
    const SkISize px { area.width() * scale, area.height() * scale };

    // What's behind it, bottom to top (snapshotted first: no render pass nested in another)
    std::vector<std::pair<std::shared_ptr<RImage>, SkIRect>> layers;

    // The desktop picture (behind every workspace)
    for (LSurface *s : compositor()->layers()[LLayerBackground])
    {
        if (s->parent() || !s->mapped() || !Crystals::IsDesk(s) || s->layerRole()->exclusiveOutput() != output)
            continue;

        if (auto image { View(*static_cast<Surface*>(s), s->size(), SkIPoint::Make(0, 0), scale) })
            layers.emplace_back(image, SkIRect::MakeXYWH(output->pos().x(), output->pos().y(), s->size().width(), s->size().height()));
    }

    // The windows below it in its workspace, where they overlap it
    size_t index;
    const Workspaces *ws { Workspaces::Find(surface, &index) };

    for (LSurface *s : compositor()->layers()[LLayerMiddle])
    {
        if (s == surface)
            break;

        auto *other { static_cast<ToplevelRole*>(s->toplevel()) };
        size_t otherIndex;

        if (!other || s->parent() || !s->mapped() || other->isMinimized() ||
            Workspaces::Find(s, &otherIndex) != ws || otherIndex != index)
            continue;

        SkIRect margins;
        const Int32 titlebar { other->titlebarHeight() };
        const SkIPoint pos { unparked(s) };
        const SkIRect bounds { SkIRect::MakeXYWH(pos.x(), pos.y() - titlebar, s->size().width(), s->size().height() + titlebar) };

        if (!SkIRect::Intersects(bounds.makeOutset(64, 64), area))
            continue;

        if (auto image { Toplevel(*other, scale, margins) })
            layers.emplace_back(image, SkIRect::MakeLTRB(bounds.left() - margins.left(), bounds.top() - margins.top(),
                                                         bounds.right() + margins.right(), bounds.bottom() + margins.bottom()));
    }

    auto backdrop { RSurface::Make(px, 1, true) };
    auto pass { backdrop ? backdrop->beginPass(RPassCap_Painter | RPassCap_SkCanvas) : nullptr };

    if (!pass)
        return result;

    {
        auto *painter { pass->getPainter() };
        painter->setBlendMode(RBlendMode::Src);
        painter->setColor(SK_ColorBLACK);
        painter->drawColor(SkRegion(SkIRect::MakeSize(px)));
    }

    SkCanvas &c { *pass->getCanvas() };
    c.save();
    c.scale(scale, scale);
    c.translate(-area.x(), -area.y());

    for (const auto &[image, dst] : layers)
        if (auto sk { image->skImage() })
            c.drawImageRect(sk, SkRect::Make(dst), SkSamplingOptions(SkFilterMode::kLinear));

    c.restore();
    pass.reset();

    // Blurred with its material (like AKBackgroundBlurEffect: a Gaussian of sigma blurRadius(),
    // then saturation and tint in one color matrix; both are linear, so the order doesn't matter)
    auto blurred { RSurface::Make(SkISize::Make(rect.width() * scale, rect.height() * scale), 1, true) };
    auto blurPass { blurred ? blurred->beginPass(RPassCap_SkCanvas) : nullptr };
    auto source { backdrop->image() ? backdrop->image()->skImage() : nullptr };

    if (!blurPass || !source)
        return result;

    const auto &mat { blur->fx.material(blur->fx.colorScheme()) };
    const auto m { RImageEffectParams::MakeVibrancyMatrix(mat.saturation, mat.tint) };
    const float matrix[20] {
        m[0], m[1], m[2],  0.f, m[3],
        m[4], m[5], m[6],  0.f, m[7],
        m[8], m[9], m[10], 0.f, m[11],
        0.f,  0.f,  0.f,   1.f, 0.f };

    SkPaint paint;
    paint.setColorFilter(SkColorFilters::Matrix(matrix));
    paint.setImageFilter(SkImageFilters::Blur(sigma * scale, sigma * scale, SkTileMode::kClamp, nullptr));

    SkCanvas &bc { *blurPass->getCanvas() };
    bc.clear(SK_ColorTRANSPARENT);
    bc.drawImage(source, -pad * scale, -pad * scale, SkSamplingOptions(SkFilterMode::kLinear), &paint);
    blurPass.reset();

    result.image = blurred->image();
    result.area = SkIRect::MakeSize(surface->size());
    result.stretch = false;
    return result;
}

std::shared_ptr<RImage> Snapshot::View(Surface &surface, SkISize size, SkIPoint viewPos, Int32 scale,
                                       const SkIRect *subsurfaceClip, const Blur *lastBlur) noexcept
{
    if (size.isEmpty() || surface.size().isEmpty() || !surface.image())
        return nullptr;

    if (!s_bake)
        s_bake = std::make_unique<BakeScene>();

    // Its blurred background as last shown on screen (before rendering anything: see the docs),
    // in buffer pixels like the window captures (see ToplevelRole::imageCaptureFrameRequest())
    auto *blur { static_cast<SurfaceBlurManager*>(surface.backgroundBlur()) };
    const SkISize px { size.width() * scale, size.height() * scale };
    const SkRegion all { SkIRect::MakeSize(px) };
    std::shared_ptr<RSurface> backdrop;

    if (blur && blur->visible())
    {
        backdrop = RSurface::Make(px, 1, true);

        if (auto pass { backdrop ? backdrop->beginPass(RPassCap_Painter | RPassCap_SkCanvas) : nullptr })
        {
            auto *painter { pass->getPainter() };
            painter->setBlendMode(RBlendMode::Src);
            painter->setColor(SK_ColorTRANSPARENT);
            painter->drawColor(all);

            SkCanvas &c { *pass->getCanvas() };
            c.save();
            c.scale(scale, scale);
            c.translate(viewPos.x(), viewPos.y());
            bool drawn { false };

            if (lastBlur && lastBlur->image)
            {
                // The given one: generated before (stretched over the current region) or
                // precalculated for where it is (see Backdrop())
                const SkRegion region { BlurRegion(*blur, surface) };
                auto skImage { lastBlur->image->skImage() };

                if (!region.isEmpty() && skImage)
                {
                    SkPath area;
                    region.getBoundaryPath(&area);
                    c.clipPath(area);
                    ClipToBlurShape(c, blur->fx);
                    SkPaint paint;
                    paint.setAntiAlias(true);
                    c.drawImageRect(skImage, SkRect::MakeWH(skImage->width(), skImage->height()),
                                    SkRect::Make(lastBlur->stretch ? region.getBounds() : lastBlur->area),
                                    SkSamplingOptions(SkFilterMode::kLinear), &paint, SkCanvas::kFast_SrcRectConstraint);
                    drawn = true;
                }
            }
            else
                drawn = blur->fx.drawBlurred(c);

            c.restore();

            if (!drawn)
                backdrop.reset();
        }
        else
            backdrop.reset();
    }

    auto &aux { *s_bake };
    SurfaceView &view { surface.view };

    // Not its blur (see above)
    const bool blurVisible { blur && blur->fx.visible() };

    if (blur)
        blur->fx.setVisible(false);

    // Without its popups and child windows (they aren't part of it)
    AKNode *parent { view.parent() };
    AKNode *next { view.next() };
    const bool wasVisible { view.visible() };
    const bool popupsVisible { view.popups.visible() };
    const bool toplevelsVisible { view.toplevels.visible() };

    view.popups.setVisible(false);
    view.toplevels.setVisible(false);
    view.setVisible(true);

    // With its subsurfaces, clipped if asked
    std::vector<SubsurfaceState> subsurfaces;
    ShowSubsurfaces(surface, subsurfaces);

    if (subsurfaceClip)
        ClipSubsurfaces(surface, subsurfaceClip);

    view.setParent(&aux.root);
    view.layout().setPosition(YGEdgeLeft, viewPos.x());
    view.layout().setPosition(YGEdgeTop, viewPos.y());

    aux.root.layout().setWidth(size.width());
    aux.root.layout().setHeight(size.height());

    auto image { RSurface::Make(size, scale, true) };
    bool ok { false };

    if (image)
    {
        aux.target->surface = image;
        aux.target->age = 0;
        aux.target->setBakedNodesScale(scale);
        ok = aux.scene->render(aux.target);
        aux.target->surface.reset();
    }

    // Back where it was
    if (next && next->parent() == parent)
        view.insertBefore(next);
    else
        view.setParent(parent);

    if (subsurfaceClip)
        ClipSubsurfaces(surface, nullptr);

    RestoreSubsurfaces(subsurfaces);

    view.popups.setVisible(popupsVisible);
    view.toplevels.setVisible(toplevelsVisible);
    view.setVisible(wasVisible);
    view.syncPos();
    view.syncVisibility();

    if (blur)
        blur->fx.setVisible(blurVisible);

    if (!ok || !image)
        return nullptr;

    if (!backdrop)
        return image->image();

    // Over its blurred background
    auto pass { backdrop->beginPass(RPassCap_Painter) };

    if (!pass)
        return image->image();

    auto *painter { pass->getPainter() };
    painter->setBlendMode(RBlendMode::SrcOver);
    RDrawImageInfo info {};
    info.image = image->image();
    info.src = SkRect::Make(size);
    info.srcScale = scale;
    info.dst = SkIRect::MakeSize(px);
    info.minFilter = info.magFilter = RImageFilter::Linear;
    painter->drawImage(info, &all);
    pass.reset();
    return backdrop->image();
}

std::shared_ptr<RImage> Snapshot::Toplevel(ToplevelRole &toplevel, Int32 scale, SkIRect &margins, const Blur *blur) noexcept
{
    auto *surface { static_cast<Surface*>(toplevel.surface()) };
    const Int32 titlebar { toplevel.titlebarHeight() };

    // The decorations' shadow (when shown, see SSD::update())
    margins.setEmpty();
    const SkISize min { SSD::MinSize() };

    if (toplevel.ssd() && !toplevel.isFullscreen() &&
        surface->size().width() >= min.width() && surface->size().height() >= min.height())
    {
        const auto sm { SSDTheme::Margins(toplevel.isActivated()) };
        margins = SkIRect::MakeLTRB(sm.left, sm.top, sm.right, sm.bottom);
    }

    const SkISize size { surface->size().width() + margins.left() + margins.right(),
                         surface->size().height() + titlebar + margins.top() + margins.bottom() };

    // Its subsurfaces clipped to the window: its content (server-side decorations) or its geometry
    // (the client's decorations, without their shadows)
    const SkIRect &geo { toplevel.windowGeometry() };
    const SkIRect clip { (titlebar > 0 || geo.isEmpty()) ? SkIRect::MakeSize(surface->size()) : geo };

    return View(*surface, size, SkIPoint::Make(margins.left(), titlebar + margins.top()), scale, &clip, blur);
}
