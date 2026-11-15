#include <Effects/FullscreenAnimation.h>
#include <Roles/ToplevelRole.h>
#include <Roles/Surface.h>
#include <Scene/Scene.h>
#include <Effects/Snapshot.h>
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


void FullscreenAnimation::Clear() noexcept
{
    Animations().clear();
    Snapshot::Clear();
}

static SkRect Lerp(const SkRect &a, const SkRect &b, SkScalar t) noexcept
{
    return SkRect::MakeLTRB(a.left() + (b.left() - a.left()) * t, a.top() + (b.top() - a.top()) * t,
                            a.right() + (b.right() - a.right()) * t, a.bottom() + (b.bottom() - a.bottom()) * t);
}

/*
 * The workspace slide's curve (its spring from rest, see Workspaces), w as in CZSpringAnimation.
 * Underdamped from rest (see Workspaces::SpringDamping): x(t) = 1 - e^(-zwt)(cos(wd t) + z/sqrt(1-z^2) sin(wd t))
 */
static Float64 SpringW() noexcept { return std::sqrt(Workspaces::FullscreenStiffness * 0.01); }

static Int64 SpringDurationMs() noexcept
{
    return Int64(1000.0 * 6.6 / (Workspaces::SpringDamping * SpringW())); // Within ~0.3% (the envelope)
}

static Float64 SpringProgress(Int64 ms) noexcept
{
    if (ms >= SpringDurationMs())
        return 1.0;

    const Float64 w { SpringW() };
    const Float64 z { Workspaces::SpringDamping };
    const Float64 root { std::sqrt(1.0 - z * z) };
    const Float64 wd { w * root };
    const Float64 secs { Float64(std::max<Int64>(0, ms)) * 0.001 };
    return 1.0 - std::exp(-z * w * secs) * (std::cos(wd * secs) + (z / root) * std::sin(wd * secs));
}

// Smootherstep: zero velocity and acceleration at both ends (no felt start or stop)
static SkScalar SmoothStep(SkScalar x) noexcept
{
    x = std::clamp(x, 0.f, 1.f);
    return x * x * x * (x * (x * 6.f - 15.f) + 10.f);
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

        // Baked on the next frame, once the commit is fully processed (its new buffer in place).
        // Not with a main thread timer: those can be delayed while the outputs repaint.
        a->m_applied = true;
        compositor()->repaintAllOutputs();
    }
}

void FullscreenAnimation::ProcessPending() noexcept
{
    for (auto &a : Animations())
        if (a->m_applied && !a->m_finished && (!a->m_hasNew || a->treeDamage() != a->m_newDamage))
            a->bakeNew(); // First, or new content (live)
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

bool FullscreenAnimation::WantsFrames(const LSurface *surface) noexcept
{
    // Its window: the root of its subsurface tree
    while (surface && surface->subsurface() && surface->parent())
        surface = surface->parent();

    return HidesSurface(surface);
}

FullscreenAnimation::FullscreenAnimation(ToplevelRole &toplevel, bool entering, LOutput *output) noexcept :
    m_toplevel(&toplevel),
    m_surface(toplevel.surface()),
    m_entering(entering),
    m_anim(TimeoutMs, [this](CZAnimation*){ update(); }, [this](CZAnimation*){ finish(); })
{
    m_scale = std::max(1, Int32(std::ceil(output->scale())));
    m_from = WindowRect(toplevel);
    m_durationMs = SpringDurationMs();

    // Where it goes: the output, or (estimated until the client draws it) where it was before
    if (entering)
        m_target = SkRect::Make(output->rect());
    else
    {
        const Int32 titlebar { toplevel.decorationMode() == LToplevelRole::ServerSide ? SSDTheme::TitlebarHeight : 0 };
        const SkIRect &prev { toplevel.prevRect };
        m_target = prev.isEmpty() ? m_from : SkRect::Make(SkIRect::MakeXYWH(prev.x(), prev.y(), prev.width(), prev.height() + titlebar));
    }

    auto image { Snapshot::Toplevel(toplevel, m_scale, m_oldMargins) };

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
    const SkScalar t { SkScalar(SpringProgress(ms)) };
    const SkRect r { Lerp(m_from, targetRect(), t) };

    place(m_old, r, m_oldMargins);
    place(m_new, r, m_newMargins);

    // Dissolve: the new frame over the first one, following the movement from where it was when the
    // frame arrived to FadeEndProgress, so it eases out with it (the spring decelerates), plus a
    // smootherstep for soft ends. Too late to follow it: over FadeMs.
    SkScalar linear { 0.f }; // The dissolve's progress, before easing

    if (m_hasNew)
    {
        const Float64 left { FadeEndProgress - m_newProgress };

        if (left >= MinFadeProgress)
            linear = std::clamp(SkScalar((Float64(t) - m_newProgress) / left), 0.f, 1.f);
        else
            linear = std::clamp(SkScalar(ms - m_newMs) / SkScalar(FadeMs), 0.f, 1.f);
    }

    const SkScalar fade { SmoothStep(linear) };

    // Entering: the window frame stays opaque under the fullscreen one fading in. Leaving: the
    // fullscreen frame fades out slowly, the window frame in over it faster (both eased from the
    // same linear progress, so each starts and ends softly).
    SkScalar newOpacity { fade };
    SkScalar oldOpacity { 1.f };

    if (!m_entering && m_hasNew)
    {
        newOpacity = SmoothStep(linear * LeavingNewFadeSpeed);
        oldOpacity = 1.f - fade;
    }

    m_new.setOpacity(newOpacity);
    m_old.setOpacity(oldOpacity);

    // Done once it got there, faded, and its workspace stopped sliding (the window is right there)
    const Workspaces *ws { m_toplevel ? Workspaces::Find(m_toplevel->surface()) : nullptr };

    if (ms >= m_durationMs && m_hasNew && fade >= 1.f && !(ws && ws->moving()))
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

UInt64 FullscreenAnimation::treeDamage() const noexcept
{
    // Each surface's damage ID only grows: their sum changes whenever any of them commits damage
    UInt64 sum { 0 };

    for (Surface *surface : GetSurfaces())
    {
        const LSurface *root { surface };

        while (root->subsurface() && root->parent())
            root = root->parent();

        if (root == m_surface)
            sum += surface->damageId();
    }

    return sum;
}

void FullscreenAnimation::bakeNew() noexcept
{
    if (!m_toplevel)
        return;

    const UInt64 damage { treeDamage() };
    auto image { Snapshot::Toplevel(*m_toplevel.get(), m_scale, m_newMargins) };

    if (!image)
        return;

    m_newDamage = damage;
    m_new.setImage(image);

    // Refreshed (live): same dissolve
    if (m_hasNew)
    {
        compositor()->repaintAllOutputs();
        return;
    }

    m_new.setOpacity(0.f);
    m_new.setVisible(true);
    m_hasNew = true;
    m_newMs = Int64(m_anim.value() * TimeoutMs);
    m_newProgress = SpringProgress(m_newMs);
    compositor()->repaintAllOutputs();
}
