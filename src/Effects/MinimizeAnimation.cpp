#include <Effects/MinimizeAnimation.h>
#include <Roles/Surface.h>
#include <Scene/Scene.h>
#include <Seat/Output.h>
#include <AK/AKScene.h>
#include <AK/AKTarget.h>
#include <AK/Nodes/AKContainer.h>
#include <Ream/RSurface.h>
#include <Louvre/Roles/LToplevelRole.h>
#include <Louvre/Roles/LForeignToplevelController.h>
#include <Louvre/Seat/LOutput.h>
#include <Louvre/LCompositor.h>
#include <Core/CZEase.h>
#include <algorithm>
#include <list>
#include <memory>

// Timing modeled after the macOS "Scale" minimize effect.
static constexpr UInt32 DurationMs { 420 };

static std::list<std::unique_ptr<MinimizeAnimation>> &Animations() noexcept
{
    static std::list<std::unique_ptr<MinimizeAnimation>> list;
    return list;
}

// Auxiliary scene used to bake toplevels offscreen.
struct AuxScene
{
    AuxScene() noexcept
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

// Created on first use, destroyed by Clear() (while the main scene still exists).
static std::unique_ptr<AuxScene> s_aux;

static AuxScene &Aux() noexcept
{
    if (!s_aux)
        s_aux = std::make_unique<AuxScene>();

    return *s_aux;
}

void MinimizeAnimation::Clear() noexcept
{
    Animations().clear();
    s_aux.reset();
}

void MinimizeAnimation::Start(LToplevelRole &toplevel, bool minimizing) noexcept
{
    auto &list { Animations() };
    list.remove_if([&toplevel](const auto &a){ return a->m_toplevel == &toplevel; });
    list.emplace_back(new MinimizeAnimation(toplevel, minimizing));
}

bool MinimizeAnimation::HidesSurface(const LSurface *surface) noexcept
{
    for (const auto &a : Animations())
        if (!a->m_minimizing && !a->m_finished && a->m_surface == surface)
            return true;

    return false;
}

void MinimizeAnimation::ProcessPending(Output &output) noexcept
{
    auto &list { Animations() };
    list.remove_if([](const auto &a){ return a->m_finished || !a->m_toplevel; });

    for (auto &a : list)
    {
        if (a->m_baked)
            continue;

        if (!a->bake(output))
        {
            a->m_finished = true;
            continue;
        }

        a->m_baked = true;
        a->update(0.0);
        a->m_anim.start();
    }
}

MinimizeAnimation::MinimizeAnimation(LToplevelRole &toplevel, bool minimizing) noexcept :
    m_toplevel(&toplevel),
    m_surface(toplevel.surface()),
    m_minimizing(minimizing),
    m_anim(DurationMs,
        [this](CZAnimation *a) { update(a->value()); },
        [this](CZAnimation *) {
            update(1.0);
            m_finished = true;
            m_node.setVisible(false);

            // Show the unminimized window again (see HidesSurface()).
            if (m_toplevel)
                static_cast<Surface*>(m_toplevel->surface())->view.syncVisibility();

            compositor()->repaintAllOutputs();
        })
{
    m_window = windowRect();
    m_lastDock = dockRect();

    m_node.setVisible(false);
    m_node.enableAutoDamage(true);
    m_node.layout().setPositionType(YGPositionTypeAbsolute);

    // Above everything else (layers included).
    m_node.setParent(&GetScene()->root);

    compositor()->repaintAllOutputs();
}

MinimizeAnimation::~MinimizeAnimation()
{
    m_anim.pause();
}

SkRect MinimizeAnimation::windowRect() const noexcept
{
    if (!m_toplevel)
        return m_window;

    const LSurface *s { m_toplevel->surface() };
    return SkRect::Make(SkIRect::MakePtSize(s->rolePos(), s->size()));
}

SkRect MinimizeAnimation::dockRect() const noexcept
{
    if (m_toplevel)
    {
        for (const LForeignToplevelController *c : m_toplevel->foreignControllers())
        {
            if (!c->taskbar() || c->taskbarIconRect().isEmpty())
                continue;

            return SkRect::Make(c->taskbarIconRect().makeOffset(c->taskbar()->rolePos()));
        }
    }

    if (!m_lastDock.isEmpty())
        return m_lastDock;

    // No dock: the bottom center of the window's output.
    LOutput *output { nullptr };

    for (LOutput *o : compositor()->outputs())
        if (SkIRect::Intersects(o->rect(), m_window.roundOut())) { output = o; break; }

    if (!output && !compositor()->outputs().empty())
        output = compositor()->outputs().front();

    if (!output)
        return SkRect::MakeXYWH(m_window.centerX(), m_window.bottom(), 1, 1);

    const SkIRect &r { output->rect() };
    return SkRect::MakeXYWH(r.x() + r.width() / 2 - 24.f, r.bottom() - 48.f, 48.f, 48.f);
}

bool MinimizeAnimation::bake(Output &output) noexcept
{
    if (!m_toplevel)
        return false;

    auto *surface { static_cast<Surface*>(m_toplevel->surface()) };
    const SkISize size { surface->size() };

    if (size.isEmpty() || !surface->image())
        return false;

    auto &aux { Aux() };
    SurfaceView &view { surface->view };

    // Move the view (with its subsurfaces) into the aux scene at the origin. Child popups/toplevels
    // are anchored to the main scene and aren't part of the window: leave them out.
    AKNode *parent { view.parent() };
    AKNode *next { view.next() };
    const bool wasVisible { view.visible() };
    const bool popupsVisible { view.popups.visible() };
    const bool toplevelsVisible { view.toplevels.visible() };

    view.popups.setVisible(false);
    view.toplevels.setVisible(false);
    view.setVisible(true);
    view.setParent(&aux.root);
    view.layout().setPosition(YGEdgeLeft, 0.f);
    view.layout().setPosition(YGEdgeTop, 0.f);

    // The target is exactly the toplevel surface: whatever falls outside is clipped.
    aux.root.layout().setWidth(size.width());
    aux.root.layout().setHeight(size.height());

    const Int32 scale { std::max(1, Int32(std::ceil(output.scale()))) };
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

    // Back where it was.
    if (next && next->parent() == parent)
        view.insertBefore(next);
    else
        view.setParent(parent);

    view.popups.setVisible(popupsVisible);
    view.toplevels.setVisible(toplevelsVisible);
    view.setVisible(wasVisible);
    view.layout().setPosition(YGEdgeLeft, view.surface.rolePos().x());
    view.layout().setPosition(YGEdgeTop, view.surface.rolePos().y());
    view.syncVisibility();

    if (!ok || !image)
        return false;

    m_node.setImage(image->image());
    m_node.setVisible(true);
    return true;
}

void MinimizeAnimation::update(Float64 t) noexcept
{
    const SkRect dock { dockRect() };
    m_lastDock = dock;

    // Minimizing goes window -> dock, unminimizing dock -> window.
    const Float64 p { m_minimizing ? t : 1.0 - t };
    const SkScalar e { SkScalar(CZEase::InOutCubic(p)) };

    // Keep the window's aspect ratio, fitting it inside the dock rect at the end.
    const SkRect &from { m_window };
    const SkScalar fit { std::min(dock.width() / std::max(1.f, from.width()), dock.height() / std::max(1.f, from.height())) };
    const SkRect to { SkRect::MakeXYWH(
        dock.centerX() - from.width() * fit * 0.5f,
        dock.centerY() - from.height() * fit * 0.5f,
        from.width() * fit,
        from.height() * fit) };

    const SkRect r { SkRect::MakeLTRB(
        from.left() + (to.left() - from.left()) * e,
        from.top() + (to.top() - from.top()) * e,
        from.right() + (to.right() - from.right()) * e,
        from.bottom() + (to.bottom() - from.bottom()) * e) };

    m_node.layout().setPosition(YGEdgeLeft, r.x());
    m_node.layout().setPosition(YGEdgeTop, r.y());
    m_node.layout().setWidth(std::max(1.f, r.width()));
    m_node.layout().setHeight(std::max(1.f, r.height()));

    // Fades out over the second half, as it lands on the dock item.
    m_node.setOpacity(std::clamp(SkScalar(2.0 - 2.0 * p), 0.f, 1.f));
}
