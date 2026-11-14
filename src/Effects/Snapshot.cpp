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
}

void Snapshot::Clear() noexcept
{
    s_bake.reset();
}

std::shared_ptr<RImage> Snapshot::View(Surface &surface, SkISize size, SkIPoint viewPos, Int32 scale) noexcept
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
            const bool drawn { blur->fx.drawBlurred(c) };
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

std::shared_ptr<RImage> Snapshot::Toplevel(ToplevelRole &toplevel, Int32 scale, SkIRect &margins) noexcept
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

    return View(*surface, size, SkIPoint::Make(margins.left(), titlebar + margins.top()), scale);
}
