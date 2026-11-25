#include <Effects/MinimizeAnimation.h>
#include <Effects/Snapshot.h>
#include <Louvre/Roles/LLayerRole.h>
#include <Louvre/Cursor/LCursor.h>
#include <Roles/Surface.h>
#include <Roles/ToplevelRole.h>
#include <Decorations/SSD.h>
#include <Decorations/SSDTheme.h>
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

void MinimizeAnimation::Clear() noexcept
{
    Animations().clear();
}

void MinimizeAnimation::Start(LToplevelRole &toplevel, bool minimizing) noexcept
{
    auto &list { Animations() };
    list.remove_if([&toplevel](const auto &a){ return a->m_toplevel == &toplevel; });
    list.emplace_back(new MinimizeAnimation(toplevel, minimizing));
}

void MinimizeAnimation::StartFrom(LToplevelRole &toplevel, const SkRect &source) noexcept
{
    auto &list { Animations() };
    list.remove_if([&toplevel](const auto &a){ return a->m_toplevel == &toplevel; });
    list.emplace_back(new MinimizeAnimation(toplevel, false, source));
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

MinimizeAnimation::MinimizeAnimation(LToplevelRole &toplevel, bool minimizing, const SkRect &source) noexcept :
    m_toplevel(&toplevel),
    m_surface(toplevel.surface()),
    m_minimizing(minimizing),
    m_source(source),
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
    m_dockOutput = cursor()->output();
    m_window = windowRect();
    m_lastDock = dockRect();

    m_node.setVisible(false);
    m_node.enableAutoDamage(true);
    m_node.layout().setPositionType(YGPositionTypeAbsolute);

    // Above every window, below the dock (and the rest of the shell).
    m_node.setParent(&GetScene()->animations);

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

    // The content plus the title bar (server-side decorations).
    const LSurface *s { m_toplevel->surface() };
    const Int32 titlebar { static_cast<ToplevelRole*>(m_toplevel.get())->titlebarHeight() };
    return SkRect::Make(SkIRect::MakeXYWH(s->rolePos().x(), s->rolePos().y() - titlebar,
                                          s->size().width(), s->size().height() + titlebar));
}

// The dock (taskbar) reports the window's spot on one of its surfaces (e.g. Shelf, on the first
// screen's dock). The animation goes to the dock of the screen the cursor was on: the same client's
// surface with the same scope there, the spot mapped relative to each dock's bounds (their input
// region: docks may have different sizes on each screen).
static SkRect OnDockOf(LSurface &taskbar, const SkIRect &iconRect, const LOutput *output) noexcept
{
    const SkRect reported { SkRect::Make(iconRect.makeOffset(taskbar.rolePos())) };
    LLayerRole *layer { taskbar.layerRole() };

    if (!output || !layer || layer->exclusiveOutput() == output)
        return reported;

    LSurface *target { nullptr };

    for (LSurface *s : compositor()->surfaces())
        if (s != &taskbar && !s->parent() && s->mapped() && s->client() == taskbar.client() && s->layerRole() &&
            s->layerRole()->exclusiveOutput() == output && s->layerRole()->scope() == layer->scope())
        {
            target = s;
            break;
        }

    if (!target)
        return reported;

    const auto bounds = [](LSurface &s) {
        const SkIRect in { s.inputRegion().isEmpty() ? SkIRect::MakeSize(s.size()) : s.inputRegion().getBounds() };
        return SkRect::Make(in.makeOffset(s.rolePos()));
    };

    const SkRect from { bounds(taskbar) }, to { bounds(*target) };

    if (from.isEmpty() || to.isEmpty())
        return reported;

    // Same place along the dock, scaled like its height (its icons' size)
    const SkScalar fx { (reported.centerX() - from.x()) / from.width() };
    const SkScalar fy { (reported.centerY() - from.y()) / from.height() };
    const SkScalar scale { to.height() / from.height() };
    const SkScalar w { reported.width() * scale }, h { reported.height() * scale };
    return SkRect::MakeXYWH(to.x() + fx * to.width() - w * 0.5f, to.y() + fy * to.height() - h * 0.5f, w, h);
}

SkRect MinimizeAnimation::dockRect() const noexcept
{
    if (!m_source.isEmpty())
        return m_source;

    if (m_toplevel)
    {
        for (const LForeignToplevelController *c : m_toplevel->foreignControllers())
        {
            if (!c->taskbar() || c->taskbarIconRect().isEmpty())
                continue;

            return OnDockOf(*c->taskbar(), c->taskbarIconRect(), m_dockOutput);
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

    if (surface->size().isEmpty() || !surface->image())
        return false;

    // The window as shown (title bar and shadow included), over its background blur. Minimizing: as
    // last shown on screen (rendering the blur away from the screen would blur nothing and overwrite
    // what it blurred there, which the dock's thumbnail of the window draws too, see Snapshot).
    // Appearing (opened from an icon, unminimized): what will be behind it where it lands, blurred
    // (a new window was never shown, and what's behind a minimized one may have changed).
    auto &toplevel { *static_cast<ToplevelRole*>(m_toplevel.get()) };
    const Int32 scale { std::max(1, Int32(std::ceil(output.scale()))) };
    Snapshot::Blur backdrop;

    if (!m_minimizing)
    {
        LOutput *destination { &output };

        for (LOutput *o : compositor()->outputs())
            if (SkIRect::Intersects(o->rect(), m_window.roundOut())) { destination = o; break; }

        backdrop = Snapshot::Backdrop(toplevel, destination, scale);
    }

    auto image { Snapshot::Toplevel(toplevel, scale, m_margins, backdrop.image ? &backdrop : nullptr) };

    if (!image)
        return false;

    m_node.setImage(image);
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

    // The shadow around it, scaled with it (the image is scaled uniformly)
    const SkScalar s { r.width() / std::max(1.f, from.width()) };
    m_node.layout().setPosition(YGEdgeLeft, r.x() - m_margins.left() * s);
    m_node.layout().setPosition(YGEdgeTop, r.y() - m_margins.top() * s);
    m_node.layout().setWidth(std::max(1.f, r.width() + (m_margins.left() + m_margins.right()) * s));
    m_node.layout().setHeight(std::max(1.f, r.height() + (m_margins.top() + m_margins.bottom()) * s));

    // Fades out over the second half, as it lands on the dock item.
    m_node.setOpacity(std::clamp(SkScalar(2.0 - 2.0 * p), 0.f, 1.f));
}
