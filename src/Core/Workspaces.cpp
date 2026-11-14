#include <Core/Workspaces.h>
#include <Core/Crystals.h>
#include <Roles/ToplevelRole.h>
#include <Roles/Surface.h>
#include <Scene/Scene.h>
#include <Seat/Output.h>
#include <Effects/TopbarAutoHide.h>
#include <Louvre/LCompositor.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Seat/LKeyboard.h>
#include <Louvre/Cursor/LCursor.h>
#include <Louvre/Roles/LLayerRole.h>
#include <linux/input-event-codes.h>
#include <algorithm>
#include <cmath>

// Windows of workspaces not shown are moved this far right: no input reaches them.
static constexpr Int32 ParkX { 1 << 21 };

static LOutput *s_renderingOutput { nullptr };
static Workspaces *s_swiping { nullptr };

static Workspaces *Existing(const LOutput *output) noexcept
{
    return output ? static_cast<const Output*>(output)->workspaces.get() : nullptr;
}

Workspaces::Workspaces(Output &output) noexcept :
    m_output(output),
    m_spring(0.0, 0.0, 0.0, SpringStiffness, CZSpringAnimation::DampingRatioNoBouncy,
        [this](CZAnimation *a)
        {
            m_pos = a->value();
            onPosChanged();
        },
        [this](CZAnimation*)
        {
            m_pos = m_spring.to();
            removeClosed();
            onPosChanged();
        })
{
    insert(0, nullptr); // The desktop
}

Workspaces::~Workspaces()
{
    m_spring.pause();

    if (s_swiping == this)
        s_swiping = nullptr;
}

Workspaces *Workspaces::Of(LOutput *output) noexcept
{
    if (!output)
        return nullptr;

    auto &ws { static_cast<Output*>(output)->workspaces };

    if (!ws)
        ws = std::make_unique<Workspaces>(*static_cast<Output*>(output));

    return ws.get();
}

void Workspaces::Clear() noexcept
{
    s_swiping = nullptr;
    s_renderingOutput = nullptr;

    for (LOutput *output : seat()->outputs())
        static_cast<Output*>(output)->workspaces.reset();
}

/* ---------------------------------------- Membership ---------------------------------------- */

ToplevelRole *Workspaces::toplevel(size_t index) const noexcept
{
    return index < m_spaces.size() ? m_spaces[index]->toplevel.get() : nullptr;
}

Workspaces *Workspaces::Find(const LSurface *surface, size_t *index) noexcept
{
    if (!surface || surface->cursorRole() || surface->dndIcon())
        return nullptr;

    const LSurface *root { surface->parent() ? surface->topmostParent() : surface };

    if (auto *toplevel { static_cast<ToplevelRole*>(root->toplevel()) })
    {
        // Its own workspace (fullscreen)
        for (LOutput *output : compositor()->outputs())
            if (Workspaces *ws { Existing(output) })
                for (size_t i = 1; i < ws->m_spaces.size(); i++)
                    if (ws->m_spaces[i]->toplevel.get() == toplevel)
                    {
                        if (index) *index = i;
                        return ws;
                    }

        // The desktop of the output it's on (its center; not its role position, which may be parked)
        const SkIPoint center { root->pos() + SkIPoint::Make(root->size().width() / 2, root->size().height() / 2) };

        // Or the one it overlaps the most (mostly off-screen), or the first one
        const SkIRect rect { SkIRect::MakePtSize(root->pos(), root->size()) };
        LOutput *best { compositor()->outputs().empty() ? nullptr : compositor()->outputs().front() };
        Int64 bestArea { -1 };

        for (LOutput *output : compositor()->outputs())
        {
            if (output->rect().contains(center.x(), center.y()))
            {
                best = output;
                break;
            }

            SkIRect overlap;
            const Int64 area { overlap.intersect(rect, output->rect()) ? Int64(overlap.width()) * overlap.height() : 0 };

            if (area > bestArea)
            {
                best = output;
                bestArea = area;
            }
        }

        if (index) *index = 0;
        return Existing(best);
    }

    // Top bars: in the workspace the user is in (their menus too)
    if (root->layerRole() && TopbarAutoHide::IsTopbar(*root->layerRole()))
    {
        Workspaces *ws { Existing(root->layerRole()->exclusiveOutput()) };

        if (ws && index)
            *index = ws->m_current;

        return ws;
    }

    if (Crystals::IsDesk(const_cast<LSurface*>(root)) && root->layerRole())
    {
        if (index) *index = 0;
        return Existing(root->layerRole()->exclusiveOutput());
    }

    return nullptr;
}

bool Workspaces::IsParked(const LSurface *surface) noexcept
{
    size_t index;
    const Workspaces *ws { Find(surface, &index) };
    return ws && index != ws->m_current;
}

SkIPoint Workspaces::ParkOffset(const LSurface *surface) noexcept
{
    return IsParked(surface) ? SkIPoint::Make(ParkX, 0) : SkIPoint::Make(0, 0);
}

Int32 Workspaces::slideX(size_t index) const noexcept
{
    return Int32(std::lround((Float64(index) - m_pos) * (m_output.rect().width() + Gap)));
}

SkIPoint Workspaces::ViewOffset(const LSurface *surface) noexcept
{
    size_t index;
    const Workspaces *ws { Find(surface, &index) };

    if (!ws)
        return SkIPoint::Make(0, 0);

    return SkIPoint::Make(ws->slideX(index) - (index != ws->m_current ? ParkX : 0), 0);
}

bool Workspaces::HidesSurface(const LSurface *surface) noexcept
{
    size_t index;
    const Workspaces *ws { Find(surface, &index) };

    if (!ws)
        return false;

    // Out of view, or drawn on another output while sliding
    return std::abs(Float64(index) - ws->m_pos) >= 0.999 ||
           (s_renderingOutput && s_renderingOutput != &ws->m_output && ws->moving());
}

bool Workspaces::moving() const noexcept
{
    return m_swiping || m_spring.isRunning();
}

/* ------------------------------------------ Spaces ------------------------------------------ */

Workspaces::Space &Workspaces::insert(size_t index, ToplevelRole *toplevel) noexcept
{
    auto space { std::make_unique<Space>() };
    space->toplevel.reset(toplevel);

    if (toplevel)
    {
        // Black behind the fullscreen window
        space->backdrop = std::make_unique<AKSolidColor>(CZAdaptiveColor(SK_ColorBLACK), &GetScene()->backdrops);
        space->backdrop->layout().setPositionType(YGPositionTypeAbsolute);
        const SkRegion empty;
        space->backdrop->setInputRegion(&empty);
    }

    index = std::min(index, m_spaces.size());
    Space &ref { *space };
    m_spaces.insert(m_spaces.begin() + index, std::move(space));

    // The shown one keeps showing
    if (index <= m_current && m_spaces.size() > 1)
    {
        m_current++;
        m_pos += 1.0;

        if (m_spring.isRunning())
        {
            m_spring.setTo(m_spring.to() + 1.0);
            m_spring.setValue(m_spring.value() + 1.0);
        }
    }

    onPosChanged();
    return ref;
}

void Workspaces::close(size_t index, Int64 target, Float64 stiffness) noexcept
{
    if (index == 0 || index >= m_spaces.size())
        return;

    m_spaces[index]->toplevel.reset();
    m_spaces[index]->closing = true;

    // Leaving the shown one for the desktop: from the workspace right next to it, not across the
    // ones between them (it looks the same, it's out of the way)
    if (target == 0 && index == m_current && index > 1 && !moving())
    {
        auto space { std::move(m_spaces[index]) };
        m_spaces.erase(m_spaces.begin() + index);
        m_spaces.insert(m_spaces.begin() + 1, std::move(space));
        m_current = 1;
        m_pos = 1.0;
    }

    if (target >= 0)
        switchTo(target, 0.0, stiffness);
    else
        removeClosed();
}

void Workspaces::removeClosed() noexcept
{
    if (moving())
        return;

    bool removed { false };

    for (size_t i = m_spaces.size(); i-- > 1;)
    {
        if (!m_spaces[i]->closing || i == m_current)
            continue;

        m_spaces.erase(m_spaces.begin() + i);
        removed = true;

        if (i < m_current)
            m_current--;
    }

    if (!removed)
        return;

    m_pos = Float64(m_current);
    onPosChanged();
}

void Workspaces::switchTo(Int64 index, Float64 velocity, Float64 stiffness) noexcept
{
    index = std::clamp<Int64>(index, 0, Int64(m_spaces.size()) - 1);
    const bool changed { size_t(index) != m_current };
    m_current = size_t(index);

    if (changed)
        seat()->dismissPopups();

    startSpring(Float64(index), velocity, stiffness);

    if (changed)
    {
        activateCurrent();
        TopbarAutoHide::UpdateAll();
    }
}

void Workspaces::startSpring(Float64 target, Float64 velocity, Float64 stiffness) noexcept
{
    if (std::abs(m_pos - target) < 0.0005 && std::abs(velocity) < 0.01)
    {
        m_spring.pause();
        m_pos = target;
        removeClosed();
        onPosChanged();
        return;
    }

    m_spring.setStiffness(stiffness);
    m_spring.setFrom(m_pos);
    m_spring.setTo(target);
    m_spring.setVelocity(velocity);
    m_spring.reset(); // (Re)starts it from there
}

void Workspaces::activateCurrent() noexcept
{
    ToplevelRole *target { currentFullscreen() };

    // The desktop: its topmost window on this output
    if (!target && !m_spaces[m_current]->closing)
    {
        const auto &middle { compositor()->layers()[LLayerMiddle] };

        for (auto it = middle.rbegin(); it != middle.rend() && !target; it++)
        {
            LSurface *s { *it };
            auto *toplevel { static_cast<ToplevelRole*>(s->toplevel()) };
            size_t index;

            if (toplevel && !s->parent() && s->mapped() && !toplevel->isMinimized() && Find(s, &index) == this && index == 0)
                target = toplevel;
        }
    }

    // No window there: the desktop is the active "app" (its menus)
    if (!target)
    {
        if (!m_spaces[m_current]->toplevel && seat()->activeToplevel())
            GetCompositor()->activateDesk();
        return;
    }

    target->surface()->raise();

    if (!target->isActivated() || !target->pendingConfiguration().windowState.has(CZWinActivated))
        target->configureState(target->pendingConfiguration().windowState | CZWinActivated);

    // Its menus, not the desktop's
    if (GetCompositor()->deskActive())
    {
        GetCompositor()->deactivateDesk();
        GetCompositor()->updateActiveHeavenClient();
    }
}

void Workspaces::onPosChanged() noexcept
{
    const SkIRect &r { m_output.rect() };

    for (size_t i = 0; i < m_spaces.size(); i++)
    {
        if (!m_spaces[i]->backdrop)
            continue;

        auto &backdrop { *m_spaces[i]->backdrop };
        backdrop.layout().setPosition(YGEdgeLeft, r.x() + slideX(i));
        backdrop.layout().setPosition(YGEdgeTop, r.y());
        backdrop.layout().setWidth(r.width());
        backdrop.layout().setHeight(r.height());
        backdrop.setVisible(std::abs(Float64(i) - m_pos) < 0.999 &&
                            (!s_renderingOutput || s_renderingOutput == &m_output || !moving()));
    }

    for (Surface *surface : GetSurfaces())
        surface->view.syncVisibility();

    m_output.repaint();
}

/* ------------------------------------------ Events ------------------------------------------ */

void Workspaces::Sync(ToplevelRole &toplevel) noexcept
{
    LSurface *s { toplevel.surface() };

    // Fullscreen as soon as it's asked to (the slide doesn't wait for the client)
    const bool wanted { s && s->mapped() && !s->parent() && !toplevel.isMinimized() &&
                        toplevel.pendingConfiguration().windowState.has(CZWinFullscreen) &&
                        toplevel.exclusiveOutput() };

    Workspaces *ws { nullptr };
    size_t index { 0 };

    for (LOutput *output : compositor()->outputs())
        if (Workspaces *w { Existing(output) })
            for (size_t i = 1; i < w->m_spaces.size(); i++)
                if (w->m_spaces[i]->toplevel.get() == &toplevel)
                {
                    ws = w;
                    index = i;
                }

    // Moved to another output: a new workspace there
    if (wanted && ws && &ws->m_output != toplevel.exclusiveOutput())
    {
        ws->close(index, index == ws->m_current ? Int64(index) - 1 : -1);
        ws = nullptr;
    }

    if (wanted && !ws)
    {
        Workspaces *target { Of(toplevel.exclusiveOutput()) };
        const size_t at { target->m_current + 1 };
        target->insert(at, &toplevel);
        target->switchTo(Int64(at), 0.0, FullscreenStiffness);
    }
    else if (!wanted && ws)
    {
        // Back to a window: to the desktop, where it lands. Gone (or minimized): the previous one.
        const bool unfullscreened { s && s->mapped() && !toplevel.isMinimized() };
        ws->close(index, index != ws->m_current ? -1 : unfullscreened ? 0 : Int64(index) - 1,
                  unfullscreened ? FullscreenStiffness : SpringStiffness);
    }
}

void Workspaces::Removed(ToplevelRole &toplevel) noexcept
{
    for (LOutput *output : compositor()->outputs())
        if (Workspaces *ws { Existing(output) })
            for (size_t i = 1; i < ws->m_spaces.size(); i++)
                if (ws->m_spaces[i]->toplevel.get() == &toplevel)
                {
                    ws->close(i, i == ws->m_current ? Int64(i) - 1 : -1);
                    return;
                }
}

void Workspaces::Reveal(ToplevelRole &toplevel) noexcept
{
    size_t index;
    Workspaces *ws { Find(toplevel.surface(), &index) };

    if (ws && index != ws->m_current)
        ws->switchTo(Int64(index));
}

bool Workspaces::KeyEvent(const CZKeyboardKeyEvent &e) noexcept
{
    if (e.code != KEY_LEFT && e.code != KEY_RIGHT)
        return false;

    const auto *keyboard { seat()->keyboard() };
    const bool ctrl { keyboard->isKeyCodePressed(KEY_LEFTCTRL) || keyboard->isKeyCodePressed(KEY_RIGHTCTRL) };
    const bool super { keyboard->isKeyCodePressed(KEY_LEFTMETA) || keyboard->isKeyCodePressed(KEY_RIGHTMETA) };

    if (!ctrl || !super)
        return false;

    // Releases (and repeats) are taken too, the client never saw the press
    if (e.isPressed)
        if (Workspaces *ws { Of(cursor()->output()) })
            ws->switchTo(Int64(ws->m_current) + (e.code == KEY_LEFT ? -1 : 1));

    return true;
}

bool Workspaces::SwipeBegin(UInt32 fingers) noexcept
{
    if (fingers != 3)
        return false;

    Workspaces *ws { Of(cursor()->output()) };

    if (!ws)
        return false;

    s_swiping = ws;
    ws->m_swiping = true;
    ws->m_swipeVelocity = 0.0;
    ws->m_swipeRaw = ws->m_spring.isRunning() ? ws->m_spring.to() : ws->m_pos; // Grabbing it mid-slide
    ws->m_swipeLast = std::chrono::steady_clock::now();
    return true;
}

bool Workspaces::SwipeUpdate(UInt32 fingers, const SkPoint &delta) noexcept
{
    CZ_UNUSED(fingers)
    Workspaces *ws { s_swiping };

    if (!ws)
        return false;

    // Fingers left: the next workspace (to the right) comes in
    ws->m_swipeRaw += -delta.x() * SwipeSpeed / SwipeDistance;

    // Only to the workspaces next to the current one. Past them it resists, increasingly (rubber
    // band): r(x) = M·x / (x + M/s) starts following at slope s and never exceeds M.
    const Float64 last { Float64(ws->m_spaces.size() - 1) };
    const Float64 lo { std::max(0.0, Float64(ws->m_current) - 1.0) };
    const Float64 hi { std::min(last, Float64(ws->m_current) + 1.0) };
    const auto rubber = [](Float64 x){ return SwipeMaxOvershoot * x / (x + SwipeMaxOvershoot / SwipeOvershootSlope); };

    // The raw position doesn't wander off (coming back responds right away)
    const Float64 rawLimit { 4.0 * SwipeMaxOvershoot / SwipeOvershootSlope };
    ws->m_swipeRaw = std::clamp(ws->m_swipeRaw, lo - rawLimit, hi + rawLimit);

    const Float64 raw { ws->m_swipeRaw };
    const Float64 target { raw < lo ? lo - rubber(lo - raw) : raw > hi ? hi + rubber(raw - hi) : raw };
    const Float64 prev { ws->m_spring.isRunning() ? ws->m_spring.to() : ws->m_pos };

    // The fingers' velocity (to pick where it goes when released)
    const auto now { std::chrono::steady_clock::now() };
    const Float64 dt { std::chrono::duration<Float64>(now - ws->m_swipeLast).count() };
    ws->m_swipeLast = now;

    if (dt > 0.0)
        ws->m_swipeVelocity = 0.6 * ((target - prev) / std::max(dt, 0.004)) + 0.4 * ws->m_swipeVelocity;

    // Followed through a stiff spring: fast movements are smoothed, it keeps its momentum
    if (ws->m_spring.isRunning())
    {
        ws->m_spring.setStiffness(SwipeFollowStiffness); // Grabbed mid-slide
        ws->m_spring.setTo(target);
    }
    else
    {
        ws->m_spring.setStiffness(SwipeFollowStiffness);
        ws->m_spring.setFrom(ws->m_pos);
        ws->m_spring.setTo(target);
        ws->m_spring.setVelocity(0.0);
        ws->m_spring.reset();
    }

    return true;
}

bool Workspaces::SwipeEnd(bool cancelled) noexcept
{
    Workspaces *ws { s_swiping };

    if (!ws)
        return false;

    s_swiping = nullptr;
    ws->m_swiping = false;

    // A stale velocity (fingers held still) doesn't count
    const Float64 idle { std::chrono::duration<Float64>(std::chrono::steady_clock::now() - ws->m_swipeLast).count() };
    const Float64 fingers { cancelled || idle > 0.1 ? 0.0 : std::clamp(ws->m_swipeVelocity, -SwipeMaxVelocity, SwipeMaxVelocity) };

    // Where the fingers were going, at most one workspace away
    Int64 target { Int64(ws->m_current) };

    if (!cancelled)
    {
        const Float64 at { ws->m_spring.isRunning() ? ws->m_spring.to() : ws->m_pos };
        target = std::clamp<Int64>(std::lround(at + fingers * 0.2), Int64(ws->m_current) - 1, Int64(ws->m_current) + 1);
    }

    // Continues with the momentum it has (limited, so a flick doesn't snap it there)
    const Float64 velocity { ws->m_spring.isRunning() ? std::clamp(ws->m_spring.velocity(), -SwipeMaxVelocity, SwipeMaxVelocity) : 0.0 };
    ws->switchTo(target, velocity);
    return true;
}

void Workspaces::BeginRender(LOutput *output) noexcept
{
    s_renderingOutput = output;

    // Backdrops of outputs switching workspaces only on them
    for (LOutput *o : compositor()->outputs())
        if (Workspaces *ws { Existing(o) }; ws && ws->moving())
            for (size_t i = 0; i < ws->m_spaces.size(); i++)
                if (ws->m_spaces[i]->backdrop)
                    ws->m_spaces[i]->backdrop->setVisible(o == output && std::abs(Float64(i) - ws->m_pos) < 0.999);
}

void Workspaces::EndRender() noexcept
{
    s_renderingOutput = nullptr;
}
