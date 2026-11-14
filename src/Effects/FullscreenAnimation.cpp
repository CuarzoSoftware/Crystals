#include <Effects/FullscreenAnimation.h>
#include <Roles/ToplevelRole.h>
#include <Roles/Surface.h>
#include <Scene/Scene.h>
#include <Core/Workspaces.h>
#include <Decorations/SSDTheme.h>
#include <Decorations/SSD.h>
#include <AK/AKScene.h>
#include <AK/AKTarget.h>
#include <AK/Nodes/AKContainer.h>
#include <Ream/RSurface.h>
#include <Louvre/LCompositor.h>
#include <Louvre/Seat/LOutput.h>
#include <Louvre/Cursor/LCursor.h>
#include <Core/CZEase.h>
#include <Core/CZTimer.h>
#include <algorithm>
#include <cmath>
#include <list>

static std::list<std::unique_ptr<FullscreenAnimation>> &Animations() noexcept
{
    static std::list<std::unique_ptr<FullscreenAnimation>> list;
    return list;
}

// Main thread snapshots (the one of MinimizeAnimation is used from the rendering threads).
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

static std::unique_ptr<BakeScene> s_bake;

void FullscreenAnimation::Clear() noexcept
{
    Animations().clear();
    s_bake.reset();
}

static SkRect Lerp(const SkRect &a, const SkRect &b, SkScalar t) noexcept
{
    return SkRect::MakeLTRB(a.left() + (b.left() - a.left()) * t, a.top() + (b.top() - a.top()) * t,
                            a.right() + (b.right() - a.right()) * t, a.bottom() + (b.bottom() - a.bottom()) * t);
}

// The window as shown: the content plus the title bar (not while fullscreen).
static SkRect WindowRect(ToplevelRole &toplevel) noexcept
{
    const LSurface *s { toplevel.surface() };
    const Int32 titlebar { toplevel.titlebarHeight() };
    return SkRect::Make(SkIRect::MakeXYWH(s->rolePos().x(), s->rolePos().y() - titlebar,
                                          s->size().width(), s->size().height() + titlebar));
}

void FullscreenAnimation::Begin(ToplevelRole &toplevel, bool entering, LOutput *output) noexcept
{
    auto &list { Animations() };
    list.remove_if([&toplevel](const auto &a){ return a->m_finished || a->m_toplevel.get() == &toplevel; });

    if (!output || !toplevel.surface() || !toplevel.surface()->mapped() || toplevel.isMinimized())
        return;

    list.emplace_back(new FullscreenAnimation(toplevel, entering, output));

    if (!list.back()->m_old.image())
        list.pop_back(); // Nothing to show
}

void FullscreenAnimation::StateApplied(ToplevelRole &toplevel) noexcept
{
    for (auto &a : Animations())
    {
        if (a->m_finished || a->m_toplevel.get() != &toplevel || a->m_applied)
            continue;

        a->m_applied = true;

        // Once the commit is fully processed (its new buffer in place)
        CZWeak<FullscreenAnimation> weak { a.get() };
        CZTimer::OneShot(0, [weak](CZTimer*){
            if (weak && !weak->m_finished)
                weak->bakeNew();
        });
    }
}

bool FullscreenAnimation::HidesSurface(const LSurface *surface) noexcept
{
    if (!surface)
        return false;

    for (const auto &a : Animations())
        if (!a->m_finished && a->m_surface == surface)
            return true;

    return false;
}

FullscreenAnimation::FullscreenAnimation(ToplevelRole &toplevel, bool entering, LOutput *output) noexcept :
    m_toplevel(&toplevel),
    m_surface(toplevel.surface()),
    m_entering(entering),
    m_anim(TimeoutMs, [this](CZAnimation*){ update(); }, [this](CZAnimation*){ finish(); })
{
    m_scale = std::max(1, Int32(std::ceil(output->scale())));
    m_from = WindowRect(toplevel);

    // Where it goes: the output, or (estimated until the client draws it) where it was before
    if (entering)
        m_target = SkRect::Make(output->rect());
    else
    {
        const Int32 titlebar { toplevel.decorationMode() == LToplevelRole::ServerSide ? SSDTheme::TitlebarHeight : 0 };
        const SkIRect &prev { toplevel.prevRect };
        m_target = prev.isEmpty() ? m_from : SkRect::Make(SkIRect::MakeXYWH(prev.x(), prev.y(), prev.width(), prev.height() + titlebar));
    }

    auto image { Bake(toplevel, m_scale, m_oldMargins) };

    if (!image)
        return;

    for (AKImage *node : { &m_old, &m_new })
    {
        const SkRegion empty;
        node->setInputRegion(&empty);
        node->enableAutoDamage(true);
        node->layout().setPositionType(YGPositionTypeAbsolute);
        node->setParent(&GetScene()->animations); // Above the windows, below the shell
    }

    m_old.setImage(image);
    m_new.setVisible(false);

    static_cast<Surface*>(toplevel.surface())->view.syncVisibility(); // Hidden meanwhile
    update();
    m_anim.start();
    compositor()->repaintAllOutputs();
}

FullscreenAnimation::~FullscreenAnimation()
{
    m_anim.pause();
}

SkRect FullscreenAnimation::targetRect() const noexcept
{
    // Once applied, where it actually is (it may have chosen another size)
    return m_applied && m_toplevel && !m_entering ? WindowRect(*m_toplevel.get()) : m_target;
}

void FullscreenAnimation::update() noexcept
{
    if (m_finished)
        return;

    const Int64 ms { Int64(m_anim.value() * TimeoutMs) };
    // The workspace slide's curve (a critically damped spring from rest, see Workspaces):
    // x(t) = 1 - (1 + wt)e^(-wt), w as in CZSpringAnimation. Done at 99.5%, like the slide visually.
    const Float64 w { std::sqrt(Workspaces::FullscreenStiffness * 0.01) };
    const Int64 durationMs { Int64(1000.0 * 7.4 / w) }; // x(7.4/w) ~ 0.995
    const Float64 secs { Float64(ms) * 0.001 };
    const Float64 x { ms >= durationMs ? 1.0 : 1.0 - (1.0 + w * secs) * std::exp(-w * secs) };
    const SkScalar t { SkScalar(x) };
    const SkRect r { Lerp(m_from, targetRect(), t) };

    place(m_old, r, m_oldMargins);
    place(m_new, r, m_newMargins);

    // Crossfade: the first frame 1 -> 0, the new one 0 -> 1, over the rest of the movement (at least
    // FadeMs if it came late)
    SkScalar fade { 0.f };

    if (m_hasNew)
    {
        const Int64 span { std::max<Int64>(FadeMs, durationMs - m_newMs) };
        fade = std::clamp(SkScalar(ms - m_newMs) / SkScalar(span), 0.f, 1.f);
    }

    m_old.setOpacity(1.f - fade);
    m_new.setOpacity(fade);

    // Done once it got there, faded, and its workspace stopped sliding (the window is right there)
    const Workspaces *ws { m_toplevel ? Workspaces::Find(m_toplevel->surface()) : nullptr };

    if (ms >= durationMs && m_hasNew && fade >= 1.f && !(ws && ws->moving()))
        finish();
}

void FullscreenAnimation::place(AKImage &node, const SkRect &window, const SkIRect &margins) noexcept
{
    // The shadow keeps its size around the window (exact where the image is shown unscaled: at
    // the start for the first frame, at the end for the new one)
    node.layout().setPosition(YGEdgeLeft, window.x() - margins.left());
    node.layout().setPosition(YGEdgeTop, window.y() - margins.top());
    node.layout().setWidth(std::max(1.f, window.width() + margins.left() + margins.right()));
    node.layout().setHeight(std::max(1.f, window.height() + margins.top() + margins.bottom()));
}

void FullscreenAnimation::finish() noexcept
{
    if (m_finished)
        return;

    m_finished = true;
    m_anim.pause();
    m_old.setVisible(false);
    m_new.setVisible(false);
    m_old.setImage(nullptr);
    m_new.setImage(nullptr);

    if (m_toplevel)
        static_cast<Surface*>(m_toplevel->surface())->view.syncVisibility();

    compositor()->repaintAllOutputs();
}

void FullscreenAnimation::bakeNew() noexcept
{
    if (m_hasNew || !m_toplevel)
        return;

    auto image { Bake(*m_toplevel.get(), m_scale, m_newMargins) };

    if (!image)
        return;

    m_new.setImage(image);
    m_new.setOpacity(0.f);
    m_new.setVisible(true);
    m_hasNew = true;
    m_newMs = Int64(m_anim.value() * TimeoutMs);
    compositor()->repaintAllOutputs();
}

std::shared_ptr<RImage> FullscreenAnimation::Bake(ToplevelRole &toplevel, Int32 scale, SkIRect &margins) noexcept
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

    if (surface->size().isEmpty() || !surface->image())
        return nullptr;

    if (!s_bake)
        s_bake = std::make_unique<BakeScene>();

    auto &aux { *s_bake };
    SurfaceView &view { surface->view };

    // The view (with its subsurfaces and decorations) at the origin of the auxiliary scene, without
    // its popups and child windows (they aren't part of the window)
    AKNode *parent { view.parent() };
    AKNode *next { view.next() };
    const bool wasVisible { view.visible() };
    const bool popupsVisible { view.popups.visible() };
    const bool toplevelsVisible { view.toplevels.visible() };

    view.popups.setVisible(false);
    view.toplevels.setVisible(false);
    view.setVisible(true);
    view.setParent(&aux.root);
    view.layout().setPosition(YGEdgeLeft, margins.left());
    view.layout().setPosition(YGEdgeTop, titlebar + margins.top());

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

    if (next && next->parent() == parent)
        view.insertBefore(next);
    else
        view.setParent(parent);

    view.popups.setVisible(popupsVisible);
    view.toplevels.setVisible(toplevelsVisible);
    view.setVisible(wasVisible);
    view.syncPos();
    view.syncVisibility();

    return ok && image ? image->image() : nullptr;
}
