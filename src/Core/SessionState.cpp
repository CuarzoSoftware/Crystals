#include <Core/SessionState.h>
#include <Core/Workspaces.h>
#include <Core/Crystals.h>
#include <Core/Log.h>
#include <Roles/ToplevelRole.h>
#include <Roles/Surface.h>
#include <Seat/Output.h>
#include <Seat/Seat.h>
#include <Effects/Expose.h>
#include <Effects/ShowDesktop.h>
#include <Effects/TopbarAutoHide.h>
#include <Effects/ResizeBlackout.h>
#include <Effects/MinimizeAnimation.h>
#include <Effects/FullscreenAnimation.h>
#include <Effects/DNDReturnAnimation.h>
#include <Louvre/LCompositor.h>
#include <Louvre/Cursor/LCursor.h>
#include <Louvre/Seat/LOutputMode.h>
#include <Core/CZWeak.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace
{

struct OutputState
{
    std::string id;   // Make, model and serial number
    std::string name; // Connector
    SkIRect rect;
    SkISize modeSize;
    UInt32 refreshRate { 0 };
    CZTransform transform { CZTransform::Normal };
    Float32 scale { 1.f };
    bool hasWorkspaces { false };
    Workspaces::Saved workspaces;
    LOutput *now { nullptr }; // Once back (see Match())
};

struct WindowState
{
    CZWeak<ToplevelRole> toplevel;
    size_t output;          // Index in State::outputs
    SkIPoint offset;        // Its position minus the output's
    SkPoint rel;            // Where it was in the output (see Workspaces::Evacuation)
    SkIPoint pos;
    UInt32 desktopId;
    bool exclusive;         // The output was its exclusive one (fullscreen, maximized)
    bool root;              // Not a child window (dialogs follow their parent)
};

struct State
{
    std::vector<OutputState> outputs;
    std::vector<WindowState> windows;
    SkPoint cursor;
};

std::unique_ptr<State> Saved;
bool IsSuspended { false };
bool WaitingForOutput { false };

std::string Id(LOutput *output) noexcept
{
    return std::string(output->manufacturer()) + "|" + output->model() + "|" + output->serialNumber();
}

// Each saved output's current one: the same display on the same connector, else the same display
void Match(State &state) noexcept
{
    std::vector<LOutput*> free { compositor()->outputs().begin(), compositor()->outputs().end() };

    for (const bool sameConnector : { true, false })
        for (OutputState &o : state.outputs)
        {
            if (o.now)
                continue;

            const auto it { std::find_if(free.begin(), free.end(), [&](LOutput *output){
                return Id(output) == o.id && (!sameConnector || output->name() == o.name); }) };

            if (it == free.end())
                continue;

            o.now = *it;
            free.erase(it);
        }
}

// As they were, if they're the same ones
void ArrangeOutputs(State &state) noexcept
{
    if (state.outputs.size() != compositor()->outputs().size() ||
        std::any_of(state.outputs.begin(), state.outputs.end(), [](const OutputState &o){ return !o.now; }))
        return;

    std::vector<LSeat::OutputConfiguration> configurations;
    bool differs { false };

    for (const OutputState &o : state.outputs)
    {
        auto mode { o.now->currentMode() };

        for (const auto &m : o.now->modes())
            if (m->size() == o.modeSize && m->refreshRate() == o.refreshRate)
                mode = m;

        differs |= mode != o.now->currentMode() || o.now->pos() != o.rect.topLeft() ||
                   o.now->transform() != o.transform || o.now->fractionalScale() != o.scale;
        configurations.push_back({ *o.now, true, mode, o.rect.topLeft(), o.transform, o.scale });
    }

    if (!differs)
        return;

    Log(CZInfo, CZLN, "Session restored: arranging the displays as they were");
    GetSeat()->configureOutputsRequest(nullptr, configurations);
}

LOutput *Closest(const SkIRect &gone) noexcept
{
    LOutput *closest { nullptr };
    Int64 best { INT64_MAX };

    for (LOutput *o : compositor()->outputs())
    {
        const SkIRect r { o->rect() };
        const Int64 dx { std::max<Int64>({ 0, Int64(r.left()) - gone.right(), Int64(gone.left()) - r.right() }) };
        const Int64 dy { std::max<Int64>({ 0, Int64(r.top()) - gone.bottom(), Int64(gone.top()) - r.bottom() }) };

        if (dx * dx + dy * dy < best)
        {
            best = dx * dx + dy * dy;
            closest = o;
        }
    }

    return closest;
}

}

bool SessionState::Suspended() noexcept
{
    return IsSuspended;
}

void SessionState::Save() noexcept
{
    /* Everything animated ends now, where it was going: the outputs they're drawn on (and refer
     * to) are about to be destroyed, and nothing is drawn (so no animation advances) until the
     * session is back, on other outputs. */

    // Where the windows really are, nothing shown that belongs to an output
    ShowDesktop::Reset();
    Expose::Reset();

    // Each output on the workspace it was sliding (or being swiped) to
    Workspaces::FinishAll();

    // Windows being (un)minimized, entering or leaving fullscreen, drag icons flying back: shown
    // as they end up. Snapshots baked for the outputs' contexts go with them
    MinimizeAnimation::Clear();
    FullscreenAnimation::Clear();
    DNDReturnAnimation::Clear();

    for (Surface *surface : GetSurfaces())
        surface->view.syncVisibility();

    Saved = std::make_unique<State>();
    State &state { *Saved };
    state.cursor = cursor()->pos();

    for (LOutput *output : compositor()->outputs())
    {
        OutputState &o { state.outputs.emplace_back() };
        o.id = Id(output);
        o.name = output->name();
        o.rect = output->rect();
        o.transform = output->transform();
        o.scale = output->fractionalScale();

        if (const auto mode { output->currentMode() })
        {
            o.modeSize = mode->size();
            o.refreshRate = mode->refreshRate();
        }

        if (const auto &ws { static_cast<Output*>(output)->workspaces })
        {
            o.hasWorkspaces = true;
            o.workspaces = ws->save();
        }
    }

    const auto indexOf = [](LOutput *output) -> size_t {
        const auto &outputs { compositor()->outputs() };
        const auto it { std::find(outputs.begin(), outputs.end(), output) };
        return it == outputs.end() ? SIZE_MAX : size_t(it - outputs.begin());
    };

    // Each window's output (its workspace's) and where it is on it
    for (const auto &p : Workspaces::Placements())
    {
        ToplevelRole *toplevel { p.toplevel.get() };
        const size_t index { indexOf(p.output) };

        if (!toplevel || !toplevel->surface() || index == SIZE_MAX)
            continue;

        LSurface *surface { toplevel->surface() };
        const SkIRect gone { p.output->rect() };

        // Its floating size and place (fullscreen: the ones it goes back to), see Workspaces::Evacuate()
        const bool fullscreen { toplevel->pendingConfiguration().windowState.has(CZWinFullscreen) };
        const SkISize size { fullscreen ? toplevel->prevRect.size() : toplevel->windowGeometry().size() };
        const SkPoint room { SkScalar(std::max(1, gone.width() - size.width())), SkScalar(std::max(1, gone.height() - size.height())) };
        const SkIPoint pos { fullscreen ? toplevel->prevRect.topLeft() : surface->pos() };

        state.windows.push_back({
            CZWeak<ToplevelRole>(toplevel), index, p.offset,
            SkPoint::Make(std::clamp((pos.x() - gone.x()) / room.x(), 0.f, 1.f), std::clamp((pos.y() - gone.y()) / room.y(), 0.f, 1.f)),
            surface->pos(), toplevel->desktopId, toplevel->exclusiveOutput() == p.output, surface->parent() == nullptr });
    }

    IsSuspended = true;
    WaitingForOutput = false;
    Log(CZInfo, CZLN, "Session left: remembered {} display(s) and {} window(s)", state.outputs.size(), state.windows.size());
}

void SessionState::OutputPlugged() noexcept
{
    if (WaitingForOutput)
        Restore();
}

void SessionState::Restore() noexcept
{
    if (!Saved)
    {
        IsSuspended = false;
        return;
    }

    // None connected: once one is (see OutputPlugged()). Real unplugs from now on
    IsSuspended = false;
    WaitingForOutput = compositor()->outputs().empty();

    if (WaitingForOutput)
        return;

    const std::unique_ptr<State> saved { std::move(Saved) };
    State &state { *saved };
    Match(state);
    ArrangeOutputs(state);

    // Black until the desktop, the docks and the top bars are back on the new outputs (they
    // recreate their surfaces for them), or BlackoutTimeoutMs passed: then they fade in together,
    // instead of the windows showing over nothing first. Nothing was drawn on them yet
    ResizeBlackout::BeginAll(BlackoutTimeoutMs);

    for (LOutput *o : compositor()->outputs())
        if (auto *output { static_cast<Output*>(o) }; output->blackout)
            output->blackout->joinGroup();

    // The windows of the outputs still there: where they were in them, on their desktops
    std::vector<Workspaces::Evacuation> evacuations(state.outputs.size());
    size_t restored { 0 }, moved { 0 };

    for (const WindowState &w : state.windows)
    {
        ToplevelRole *toplevel { w.toplevel.get() };

        // Closed meanwhile
        if (!toplevel || !toplevel->surface())
            continue;

        const OutputState &o { state.outputs[w.output] };

        if (!o.now)
        {
            // Its output is gone: to the closest one (its child windows with it), below
            if (w.root)
            {
                evacuations[w.output].windows.push_back({ w.toplevel, w.rel, w.pos });
                moved++;
            }

            continue;
        }

        toplevel->desktopId = w.desktopId;
        toplevel->surface()->setPos(o.now->pos() + w.offset);

        // Where a fullscreen one goes back to follows its output too
        if (toplevel->pendingConfiguration().windowState.has(CZWinFullscreen))
            toplevel->prevRect.offset(o.now->pos() - o.rect.topLeft());

        if (w.exclusive)
            toplevel->setExclusiveOutput(o.now);

        restored++;
    }

    // Their workspaces (after their windows: fullscreen ones get theirs back). The new outputs may
    // have got theirs already (just a first desktop): dropped first, all of them, so no window is
    // looked up in workspaces that don't have its desktop yet (see below)
    for (LOutput *o : compositor()->outputs())
        static_cast<Output*>(o)->workspaces.reset();

    for (const OutputState &o : state.outputs)
        if (o.now && o.hasWorkspaces)
            Workspaces::Of(o.now)->restore(o.workspaces);

    // Their desktops, again: while the workspaces were being recreated, a window whose desktop
    // wasn't there yet (any but the first one) was given the first one (see Workspaces::Find())
    for (const WindowState &w : state.windows)
        if (ToplevelRole *toplevel { w.toplevel.get() }; toplevel && state.outputs[w.output].now)
            toplevel->desktopId = w.desktopId;

    // The windows of the outputs disconnected meanwhile
    for (size_t i = 0; i < evacuations.size(); i++)
    {
        if (evacuations[i].windows.empty())
            continue;

        evacuations[i].target = Closest(state.outputs[i].rect);
        Workspaces::Place(evacuations[i]);
    }

    Output::SyncLastRects();
    cursor()->setPos(state.cursor); // Inside an output (the closest one if its own is gone)
    Expose::WindowsChanged();
    TopbarAutoHide::UpdateAll();
    Workspaces::ScheduleReachable(); // Once the panels are back (another mode, a smaller display...)
    compositor()->repaintAllOutputs();

    Log(CZInfo, CZLN, "Session restored: {} window(s) back in place, {} moved from displays no longer connected", restored, moved);
}
