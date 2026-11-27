#include <Effects/ShowDesktop.h>
#include <Effects/FullscreenAnimation.h>
#include <Effects/Snapshot.h>
#include <Effects/Expose.h>
#include <Core/Workspaces.h>
#include <Core/Crystals.h>
#include <Roles/ToplevelRole.h>
#include <Roles/Surface.h>
#include <Scene/Scene.h>
#include <Seat/Output.h>
#include <Louvre/LCompositor.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Seat/LKeyboard.h>
#include <Louvre/Cursor/LCursor.h>
#include <linux/input-event-codes.h>
#include <algorithm>
#include <cmath>

static std::unique_ptr<ShowDesktop> s_showDesktop;

// Hidden windows are moved this far down (see ParkOffset())
static constexpr Int32 ParkY { 1 << 21 };

ShowDesktop *ShowDesktop::Get(bool create) noexcept
{
    if (!s_showDesktop && create)
        s_showDesktop.reset(new ShowDesktop());

    return s_showDesktop.get();
}

void ShowDesktop::Clear() noexcept
{
    s_showDesktop.reset();
}

ShowDesktop::ShowDesktop() noexcept :
    m_spring(0.0, 0.0, 0.0, SpringStiffness, Workspaces::SpringDamping,
        [this](CZAnimation *a)
        {
            m_progress = a->value();
            apply();
            compositor()->repaintAllOutputs();
        },
        [this](CZAnimation*)
        {
            m_progress = m_spring.to();
            apply();
            compositor()->repaintAllOutputs();

            // Back: the windows are shown again
            if (!m_open && !m_swiping && m_progress <= 0.001)
                deactivate();
        })
{}

ShowDesktop::~ShowDesktop()
{
    m_spring.pause();
}

bool ShowDesktop::Active() noexcept
{
    return s_showDesktop && s_showDesktop->m_active;
}

bool ShowDesktop::Shown() noexcept
{
    return Active() && s_showDesktop->m_open;
}

const ShowDesktop::Window *ShowDesktop::find(const LSurface *surface) const noexcept
{
    for (const auto &w : m_windows)
        if (w.toplevel && w.toplevel->surface() == surface)
            return &w;

    return nullptr;
}

bool ShowDesktop::HidesSurface(const LSurface *surface) noexcept
{
    if (!Active() || !surface)
        return false;

    // Its whole tree (subsurfaces, dialogs)
    return s_showDesktop->find(surface->parent() ? surface->topmostParent() : surface);
}

SkIPoint ShowDesktop::ParkOffset(const LSurface *surface) noexcept
{
    return HidesSurface(surface) ? SkIPoint::Make(0, ParkY) : SkIPoint::Make(0, 0);
}

/* ----------------------------------------- Geometry ----------------------------------------- */

// Where the window's content is, without any offset (parked, sliding)
static SkIPoint ContentPos(ToplevelRole &toplevel) noexcept
{
    return toplevel.LToplevelRole::rolePos() + SkIPoint::Make(0, toplevel.titlebarHeight());
}

// The window's frame: its title bar and content (server-side), or its geometry (the client's
// decorations, without their shadows)
static SkIRect Frame(ToplevelRole &toplevel) noexcept
{
    const SkIPoint content { ContentPos(toplevel) };
    const Int32 titlebar { toplevel.titlebarHeight() };
    const SkISize size { toplevel.surface()->size() };

    if (titlebar > 0)
        return SkIRect::MakeXYWH(content.x(), content.y() - titlebar, size.width(), size.height() + titlebar);

    const SkIRect &geo { toplevel.windowGeometry() };

    if (geo.isEmpty())
        return SkIRect::MakeXYWH(content.x(), content.y(), size.width(), size.height());

    return geo.makeOffset(content);
}

// What its snapshot covers (besides its shadow margins): the surface and title bar (see Expose)
static SkIRect SnapshotRect(ToplevelRole &toplevel) noexcept
{
    const SkIPoint content { ContentPos(toplevel) };
    const Int32 titlebar { toplevel.titlebarHeight() };
    const SkISize size { toplevel.surface()->size() };
    return SkIRect::MakeXYWH(content.x(), content.y() - titlebar, size.width(), size.height() + titlebar);
}

SkIPoint ShowDesktop::fullOffset(const Window &w) const noexcept
{
    if (!w.toplevel || !w.output)
        return SkIPoint::Make(0, 0);

    // From where the window is now (it may resize meanwhile)
    const SkIRect frame { Frame(*w.toplevel.get()) };
    const SkIRect &screen { w.output->rect() };
    const SkIRect &area { w.output->availableGeometry() };

    // Just Peek of it on the screen
    const Int32 dx { w.side == Side::Left ? screen.left() + Peek - frame.right() : screen.right() - Peek - frame.left() };

    // Centered in its slot along the side (between the top bar and the dock)
    const Float64 slotCenter { area.top() + area.height() * (Float64(w.slot) + 0.5) / Float64(std::max<size_t>(1, w.slots)) };
    const Int32 dy { Int32(std::lround(slotCenter - (frame.top() + frame.bottom()) * 0.5)) };
    return SkIPoint::Make(dx, dy);
}

// Offset of @p w (its root's, aside by the progress) plus its workspace's slide
static SkPoint ShownOffset(SkIPoint full, Float64 progress, Output *output, size_t workspace) noexcept
{
    SkScalar slide { 0.f };

    if (output && output->workspaces)
        slide = SkScalar(output->workspaces->slideX(workspace));

    return SkPoint::Make(SkScalar(full.x() * progress) + slide, SkScalar(full.y() * progress));
}

SkRect ShowDesktop::shownFrame(const Window &w) const noexcept
{
    if (!w.toplevel || !w.root)
        return SkRect::MakeEmpty();

    const SkPoint offset { ShownOffset(fullOffset(*w.root), m_progress, w.output, w.workspace) };
    return SkRect::Make(Frame(*w.toplevel.get())).makeOffset(offset.x(), offset.y());
}

/* ----------------------------------------- Showing ----------------------------------------- */

bool ShowDesktop::activate() noexcept
{
    if (m_active)
        return true;

    m_windows.clear();
    m_screens.clear();

    // Every output's workspaces (created on first use: otherwise no window would be found in them)
    for (LOutput *o : compositor()->outputs())
        Workspaces::Of(o);

    // The current desktops' windows, bottom to top, each followed by its dialogs
    const auto add = [this](ToplevelRole *toplevel, Window *root, Output *output, size_t workspace, const auto &self) -> void
    {
        LSurface *s { toplevel->surface() };

        if (!s->mapped() || toplevel->isMinimized() || s->size().isEmpty())
            return;

        Window &w { m_windows.emplace_back() };
        w.toplevel.reset(toplevel);
        w.root = root ? root : &w;
        w.output = output;
        w.workspace = workspace;

        for (LToplevelRole *child : toplevel->childToplevels())
            self(static_cast<ToplevelRole*>(child), w.root, output, workspace, self);
    };

    for (LSurfaceLayer l : { LLayerBottom, LLayerMiddle, LLayerTop })
    {
        for (LSurface *s : compositor()->layers()[l])
        {
            auto *toplevel { static_cast<ToplevelRole*>(s->toplevel()) };

            if (!toplevel || s->parent() || toplevel->isFullscreen() || FullscreenAnimation::HidesSurface(s))
                continue;

            // Not while sliding between workspaces
            size_t index;
            Workspaces *ws { Workspaces::Find(s, &index) };

            if (!ws || index != ws->current() || !ws->isDesktop(index) || ws->moving())
                continue;

            add(toplevel, nullptr, &ws->output(), index, add);
        }
    }

    if (m_windows.empty())
        return false;

    // Each window to the closest side of its output, spread along it in the order they are (top
    // to bottom)
    for (LOutput *o : compositor()->outputs())
    {
        for (Side side : { Side::Left, Side::Right })
        {
            std::vector<std::pair<Int32, Window*>> roots;

            for (Window &w : m_windows)
            {
                if (w.root != &w || w.output != o)
                    continue;

                const SkIRect frame { Frame(*w.toplevel.get()) };
                const SkIRect &screen { o->rect() };
                w.side = frame.left() + frame.right() < screen.left() + screen.right() ? Side::Left : Side::Right;

                if (w.side == side)
                    roots.emplace_back(frame.top() + frame.bottom(), &w);
            }

            std::stable_sort(roots.begin(), roots.end(), [](const auto &a, const auto &b){ return a.first < b.first; });

            for (size_t i = 0; i < roots.size(); i++)
            {
                roots[i].second->slot = i;
                roots[i].second->slots = roots.size();
            }
        }
    }

    // A clipped layer per output, below the top bars and docks
    AKContainer &parent { GetScene()->animations };
    const SkRegion empty;

    for (LOutput *o : compositor()->outputs())
    {
        auto &screen { *m_screens.emplace_back(std::make_unique<Screen>()) };
        screen.output = static_cast<Output*>(o);

        for (AKContainer *c : { &screen.clip, &screen.layer })
        {
            c->setInputRegion(&empty);
            c->layout().setPositionType(YGPositionTypeAbsolute);
        }

        if (parent.children(true).empty())
            screen.clip.setParent(&parent);
        else
            screen.clip.insertBefore(parent.children(true).front());
    }

    for (Window &w : m_windows)
    {
        w.node.setInputRegion(&empty);
        w.node.enableAutoDamage(true);
        w.node.layout().setPositionType(YGPositionTypeAbsolute);

        for (auto &screen : m_screens)
            if (screen->output == w.output)
                w.node.setParent(&screen->layer);

        snapshot(w);
    }

    m_active = true;

    // Their menus would be left floating
    seat()->dismissPopups();

    for (Surface *surface : GetSurfaces())
        surface->view.syncVisibility();

    apply();
    compositor()->repaintAllOutputs();
    return true;
}

void ShowDesktop::deactivate() noexcept
{
    if (!m_active)
        return;

    m_active = false;
    m_progress = 0.0;

    // Destroyed the next time (this may be an animation step)
    for (Window &w : m_windows)
        w.node.setVisible(false);

    for (Surface *surface : GetSurfaces())
        surface->view.syncVisibility();

    compositor()->repaintAllOutputs();
}

void ShowDesktop::snapshot(Window &w) noexcept
{
    w.dirty = false;

    if (!w.toplevel || !w.output || !w.toplevel->surface()->mapped())
        return;

    const Int32 scale { std::max(1, Int32(std::ceil(w.output->scale()))) };
    auto image { Snapshot::Toplevel(*w.toplevel.get(), scale, w.margins) };

    if (!image)
        return;

    // Swapping images doesn't damage the node (AKImage leaves that to the owner): redrawn whole
    w.node.setImage(image);
    w.node.addDamage(AK_IRECT_INF);
}

void ShowDesktop::apply() noexcept
{
    if (!m_active)
        return;

    for (auto &screen : m_screens)
    {
        const SkIRect r { screen->output->rect() };
        screen->clip.layout().setPosition(YGEdgeLeft, r.x());
        screen->clip.layout().setPosition(YGEdgeTop, r.y());
        screen->clip.layout().setWidth(r.width());
        screen->clip.layout().setHeight(r.height());
        screen->layer.layout().setPosition(YGEdgeLeft, -r.x());
        screen->layer.layout().setPosition(YGEdgeTop, -r.y());
    }

    const SkScalar opacity { 1.f - (1.f - Opacity) * SkScalar(std::clamp(m_progress, 0.0, 1.0)) };

    for (Window &w : m_windows)
    {
        ToplevelRole *toplevel { w.toplevel.get() };
        const bool shown { toplevel && w.root->toplevel && toplevel->surface()->mapped() &&
                           !toplevel->isMinimized() && w.node.image() };
        w.node.setVisible(shown);

        if (!shown)
            continue;

        const SkPoint offset { ShownOffset(fullOffset(*w.root), m_progress, w.output, w.workspace) };
        const SkRect rect { SkRect::Make(SnapshotRect(*toplevel)).makeOffset(offset.x(), offset.y()) };
        w.node.layout().setPosition(YGEdgeLeft, rect.x() - w.margins.left());
        w.node.layout().setPosition(YGEdgeTop, rect.y() - w.margins.top());
        w.node.layout().setWidth(std::max(1.f, rect.width() + w.margins.left() + w.margins.right()));
        w.node.layout().setHeight(std::max(1.f, rect.height() + w.margins.top() + w.margins.bottom()));
        w.node.setOpacity(opacity);
    }
}

void ShowDesktop::ProcessPending(Output &output) noexcept
{
    ShowDesktop *s { Get(false) };

    if (!s || !s->m_active)
        return;

    for (Window &w : s->m_windows)
        if (w.dirty && w.output == &output)
            s->snapshot(w);

    s->apply();
}

void ShowDesktop::SurfaceDamaged(const LSurface *surface) noexcept
{
    ShowDesktop *s { Get(false) };

    if (!s || !s->m_active || !surface)
        return;

    const LSurface *root { surface->parent() ? surface->topmostParent() : surface };

    // Its window (or the dialog among its children that drew)
    for (const LSurface *target : { surface, root })
    {
        const LSurface *t { target };

        while (t && t->subsurface() && t->parent())
            t = t->parent();

        for (Window &w : s->m_windows)
            if (w.toplevel && w.toplevel->surface() == t)
            {
                // Taken again on its output's next frame (see ProcessPending()), which also answers
                // its frame callbacks
                w.dirty = true;

                if (w.output)
                    w.output->repaint();
                return;
            }
    }
}

void ShowDesktop::animateTo(Float64 target, Float64 velocity) noexcept
{
    m_open = target > 0.5;

    if (m_open && !activate())
    {
        m_open = false;
        return;
    }

    if (!m_active)
        return;

    m_spring.setStiffness(SpringStiffness);
    m_spring.setFrom(m_progress);
    m_spring.setTo(target);
    m_spring.setVelocity(velocity);
    m_spring.reset();
}

void ShowDesktop::Toggle() noexcept
{
    // Exposé shows the windows its own way
    if (Expose::Active())
        return;

    ShowDesktop *s { Get(true) };

    if (s->m_open)
        Leave();
    else
        s->animateTo(1.0, 0.0);
}

void ShowDesktop::Leave() noexcept
{
    ShowDesktop *s { Get(false) };

    if (!s || !s->m_active || (!s->m_open && !s->m_swiping))
        return;

    s->m_swiping = false;
    s->animateTo(0.0, 0.0);
}

void ShowDesktop::Reset() noexcept
{
    ShowDesktop *s { Get(false) };

    if (!s || !s->m_active)
        return;

    s->m_open = false;
    s->m_swiping = false;
    s->m_spring.pause();
    s->deactivate();
}

void ShowDesktop::WindowActivated(ToplevelRole *toplevel) noexcept
{
    if (!toplevel || !Shown())
        return;

    Leave();
}

/* ------------------------------------------ Input ------------------------------------------ */

bool ShowDesktop::KeyEvent(const CZKeyboardKeyEvent &e) noexcept
{
    if (e.code != KEY_D)
        return false;

    const auto *keyboard { seat()->keyboard() };
    const bool super { keyboard->isKeyCodePressed(KEY_LEFTMETA) || keyboard->isKeyCodePressed(KEY_RIGHTMETA) };
    const bool others { keyboard->isKeyCodePressed(KEY_LEFTCTRL) || keyboard->isKeyCodePressed(KEY_RIGHTCTRL) ||
                        keyboard->isKeyCodePressed(KEY_LEFTALT) || keyboard->isKeyCodePressed(KEY_RIGHTALT) ||
                        keyboard->isKeyCodePressed(KEY_LEFTSHIFT) || keyboard->isKeyCodePressed(KEY_RIGHTSHIFT) };

    if (!super || others)
        return false;

    if (e.isPressed && !e.isRepeat)
        Toggle();

    return true;
}

void ShowDesktop::SwipeBegin() noexcept
{
    ShowDesktop *s { Get(true) };
    s->m_swiping = true;
    s->m_swipeRaw = s->m_spring.isRunning() ? s->m_spring.to() : s->m_progress;
    s->m_swipeVelocity = 0.0;
    s->m_swipeLast = std::chrono::steady_clock::now();
}

void ShowDesktop::SwipeUpdate(const SkPoint &delta) noexcept
{
    ShowDesktop *s { Get(false) };

    if (!s || !s->m_swiping)
        return;

    // Fingers down: the windows move aside. Resisting past the ends (rubber band, see Workspaces).
    constexpr Float64 maxOver { 0.05 }, slope { 0.25 };
    const auto rubber = [](Float64 x){ return maxOver * x / (x + maxOver / slope); };
    s->m_swipeRaw = std::clamp(s->m_swipeRaw + delta.y() / SwipeDistance, -1.0, 2.0);
    const Float64 raw { s->m_swipeRaw };
    const Float64 target { raw < 0.0 ? -rubber(-raw) : raw > 1.0 ? 1.0 + rubber(raw - 1.0) : raw };

    const auto now { std::chrono::steady_clock::now() };
    const Float64 dt { std::chrono::duration<Float64>(now - s->m_swipeLast).count() };
    const Float64 prev { s->m_spring.isRunning() ? s->m_spring.to() : s->m_progress };
    s->m_swipeLast = now;

    if (dt > 0.0)
        s->m_swipeVelocity = 0.6 * ((target - prev) / std::max(dt, 0.004)) + 0.4 * s->m_swipeVelocity;

    // Once the windows start moving (nothing for a swipe up while not shown, or without windows)
    if (target > 0.0 && !s->activate())
        return;

    if (!s->m_active)
        return;

    // Followed through a stiff spring (smooth, see Workspaces)
    s->m_spring.setStiffness(SwipeFollowStiffness);

    if (s->m_spring.isRunning())
        s->m_spring.setTo(target);
    else
    {
        s->m_spring.setFrom(s->m_progress);
        s->m_spring.setTo(target);
        s->m_spring.setVelocity(0.0);
        s->m_spring.reset();
    }
}

void ShowDesktop::SwipeEnd(bool cancelled) noexcept
{
    ShowDesktop *s { Get(false) };

    if (!s || !s->m_swiping)
        return;

    s->m_swiping = false;

    if (!s->m_active)
        return;

    const Float64 idle { std::chrono::duration<Float64>(std::chrono::steady_clock::now() - s->m_swipeLast).count() };
    const Float64 fingers { cancelled || idle > 0.1 ? 0.0 : s->m_swipeVelocity };
    const Float64 at { s->m_spring.isRunning() ? s->m_spring.to() : s->m_progress };

    // Flicked, or released past halfway (cancelled: back where it was)
    bool open { s->m_open };

    if (!cancelled)
        open = fingers > 0.8 || (fingers > -0.8 && at > 0.5);

    const Float64 velocity { s->m_spring.isRunning() ? std::clamp(s->m_spring.velocity(), -5.0, 5.0) : 0.0 };
    s->animateTo(open ? 1.0 : 0.0, velocity);
}

ToplevelRole *ShowDesktop::windowAt(const SkPoint &pos) const noexcept
{
    // Only while shown (coming back, they take input again)
    if (!m_active || !m_open)
        return nullptr;

    // The windows are parked: anything there but the desktop is above the strips (top bars,
    // docks, popups)
    const SkIPoint p { Int32(std::floor(pos.x())), Int32(std::floor(pos.y())) };

    if (LSurface *s { seat()->surfaceAt(p) })
    {
        LSurface *root { s->parent() ? s->topmostParent() : s };

        if (s->popup() || s->isSubchildOf(LSurface::Role::Popup) || !Crystals::IsDesk(root))
            return nullptr;
    }

    // The topmost strip (each clipped to its output)
    for (auto it = m_windows.rbegin(); it != m_windows.rend(); it++)
    {
        if (!it->toplevel || !it->node.visible() || !it->output)
            continue;

        SkRect strip { shownFrame(*it) };

        if (strip.intersect(SkRect::Make(it->output->rect())) && strip.contains(pos.x(), pos.y()))
            return it->toplevel.get();
    }

    return nullptr;
}

bool ShowDesktop::PointerMove() noexcept
{
    const ShowDesktop *s { Get(false) };
    return s && s->windowAt(cursor()->pos());
}

bool ShowDesktop::PointerButton(const CZPointerButtonEvent &e) noexcept
{
    ShowDesktop *s { Get(false) };

    if (!s)
        return false;

    // The release of a strip's press
    if (!e.pressed)
    {
        if (!s->m_pressed)
            return false;

        s->m_pressed = false;
        return true;
    }

    ToplevelRole *toplevel { s->windowAt(cursor()->pos()) };

    if (!toplevel)
        return false;

    s->m_pressed = true;

    // That window: activated, on top, and every window back
    Leave();
    toplevel->surface()->raise();
    toplevel->configureState(toplevel->pendingConfiguration().windowState | CZWinActivated);

    if (GetCompositor()->deskActive())
    {
        GetCompositor()->deactivateDesk();
        GetCompositor()->updateActiveHeavenClient();
    }

    return true;
}

void ShowDesktop::DragMoved() noexcept
{
    const ShowDesktop *s { Get(false) };

    // Over a strip: they come back, to take the drop
    if (s && s->windowAt(cursor()->pos()))
        Leave();
}
