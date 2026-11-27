#include <Core/SessionLock.h>
#include <cmath>
#include <Louvre/Cursor/LShapeCursorSource.h>
#include <Louvre/Cursor/LCursor.h>
#include <Core/Events/CZPointerSwipeUpdateEvent.h>
#include <Core/Events/CZPointerMoveEvent.h>
#include <Core/CZCore.h>
#include <AK/AKApp.h>
#include <Seat/Pointer.h>
#include <Core/Crystals.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Roles/LLayerRole.h>
#include <Louvre/Seat/LKeyboard.h>
#include <Louvre/Roles/LToplevelRole.h>
#include <Louvre/Roles/LSurface.h>
#include <Scene/Scene.h>
#include <Decorations/SSD.h>
#include <Effects/TopbarAutoHide.h>
#include <Core/Workspaces.h>
#include <Effects/Expose.h>
#include <Effects/ShowDesktop.h>
#include <Core/TrackpadSettings.h>
#include <Core/Events/CZPointerSwipeBeginEvent.h>
#include <Core/Events/CZPointerSwipeEndEvent.h>
#include <Louvre/Seat/LDND.h>
#include <Louvre/Roles/LToplevelMoveSession.h>
#include <Louvre/Roles/LToplevelResizeSession.h>
#include <linux/input-event-codes.h>

static void SetShapeCursor(CZCursorShape shape) noexcept
{
    auto source { cursor()->source() };
    auto shapeSource { source ? source->asShape() : nullptr };

    if (!shapeSource || shapeSource->shape() != shape)
        cursor()->setSource(LShapeCursorSource::MakeShape(shape));

    cursor()->setVisible(true);
}

static bool PointerSessionsActive() noexcept
{
    for (auto *s : seat()->toplevelMoveSessions())
        if (!s->triggeringEvent().isTouchEvent()) return true;

    for (auto *s : seat()->toplevelResizeSessions())
        if (!s->triggeringEvent().isTouchEvent()) return true;

    return false;
}

void Pointer::pointerMoveEvent(const CZPointerMoveEvent &e)
{
    // Locked: only the lock surfaces get it (Louvre's default, see Seat::surfaceAt())
    if (SessionLockManager::IsLocked())
    {
        LPointer::pointerMoveEvent(e);

        if (!focus())
            SetShapeCursor(CZCursorShape::Default);

        return;
    }

    // Top bars hidden by fullscreen windows: revealed at the top edge, hidden when left. Dragging
    // something over the windows moved aside by Show Desktop brings them back (to take the drop).
    struct AtExit
    {
        ~AtExit()
        {
            TopbarAutoHide::PointerMoved();

            if (seat()->dnd()->dragging())
                ShowDesktop::DragMoved();
        }
    } atExit;

    // Exposé takes the pointer (but over the dock): no client gets it
    if (Expose::Active() && !draggingSurface() && !seat()->dnd()->dragging())
    {
        const SkPoint prev { cursor()->pos() };
        cursor()->move(e.delta.x(), e.delta.y());

        if (Expose::PointerMove())
        {
            cursor()->repaintOutputs(true);
            setFocus(nullptr);
            SetShapeCursor(CZCursorShape::Default);
            return;
        }

        cursor()->setPos(prev);
    }

    // Show Desktop: the strips of the windows moved aside take no input but clicks (see ShowDesktop)
    if (ShowDesktop::Active() && !draggingSurface() && !PointerSessionsActive() && !seat()->dnd()->dragging())
    {
        const SkPoint prev { cursor()->pos() };
        cursor()->move(e.delta.x(), e.delta.y());

        if (ShowDesktop::PointerMove())
        {
            cursor()->repaintOutputs(true);
            setFocus(nullptr);
            SSD::PointerMotion({});
            SetShapeCursor(CZCursorShape::Default);
            return;
        }

        cursor()->setPos(prev);
    }

    auto kay { AKApp::Get() };
    auto scene { GetScene()->scene };

    // Louvre only knows surfaces: over decorations it'd focus the surface behind them. Check where the
    // cursor lands first (unless something holds the pointer: a drag, a move/resize, a constraint).
    SSD::Hit decoration;

    // A window button held down holds the pointer too (no surface gets it until released).
    const bool buttonPressed { SSD::ButtonPressed() };

    if (!draggingSurface() && !PointerSessionsActive() && !seat()->dnd()->dragging() &&
        !(focus() && focus()->pointerConstraintEnabled()))
    {
        const SkPoint prev { cursor()->pos() };
        cursor()->move(e.delta.x(), e.delta.y());
        decoration = SSD::HitAt(cursor()->pos());

        if (decoration.ssd || buttonPressed)
        {
            cursor()->repaintOutputs(true);
            setFocus(nullptr);
        }
        else
            cursor()->setPos(prev);
    }

    if (!decoration.ssd && !(buttonPressed && !focus()))
        LPointer::pointerMoveEvent(e);

    // Decorations: hover looks and cursor, handled here (never through Kay events).
    if (const auto shape { SSD::PointerMotion(decoration) })
    {
        SetShapeCursor(*shape);
        return;
    }

    if (buttonPressed)
    {
        SetShapeCursor(CZCursorShape::Default);
        return;
    }

    e.pos = cursor()->pos();
    kay->core()->sendEvent(e, *kay);

    // Keep the client surface cursor
    if (focus())
        return;

    // Otherwise use the focused node cursor
    if (auto *node = scene->pointerFocus())
    {
        // Decorations (e.g. while moving/resizing from them): the closest cursor up the tree.
        if (SSD::IsDecoration(node))
            while (!node->cursor() && node->parent())
                node = node->parent();

        if (node->cursor())
            SetShapeCursor(*node->cursor());
        else
            cursor()->setVisible(false);
    }
}

void Pointer::pointerButtonEvent(const CZPointerButtonEvent &e)
{
    if (SessionLockManager::IsLocked())
    {
        LPointer::pointerButtonEvent(e);
        return;
    }

    if (!seat()->dnd()->dragging() && !draggingSurface() && Expose::PointerButton(e))
        return;

    // A strip of a window moved aside by Show Desktop: that window comes back, activated
    if (!seat()->dnd()->dragging() && !draggingSurface() && ShowDesktop::PointerButton(e))
        return;

    if (!focus() && !seat()->dnd()->dragging())
    {
        // Louvre doesn't stop move/resize sessions without a focused surface (e.g. started from the
        // decorations).
        if (!e.pressed && e.button == BTN_LEFT)
        {
            for (auto it = seat()->toplevelResizeSessions().begin(); it != seat()->toplevelResizeSessions().end();)
                it = (*it)->triggeringEvent().isTouchEvent() ? std::next(it) : (*it)->stop();

            for (auto it = seat()->toplevelMoveSessions().begin(); it != seat()->toplevelMoveSessions().end();)
                it = (*it)->triggeringEvent().isTouchEvent() ? std::next(it) : (*it)->stop();
        }

    }

    // Decorations take their events (activating, moving, resizing the window, its buttons), the
    // surface behind them (or the client) never gets them. Not while a client holds the pointer.
    if (!seat()->dnd()->dragging() && !draggingSurface() && SSD::PointerButton(SSD::HitAt(cursor()->pos()), e))
        return;

    LPointer::pointerButtonEvent(e);

    // Clicking the desktop deactivates the active toplevel, so the desktop's menus show (like
    // clicking the desktop on macOS makes Finder the active app).
    // Clicking a panel that takes no keyboard input (the top bar, the dock): the active window keeps
    // the keyboard focus (Louvre gives it to whatever is clicked), so it stays active.
    if (e.pressed && focus())
    {
        LSurface *clicked { focus()->parent() ? focus()->topmostParent() : focus() };

        // What Louvre gave the keyboard to (subsurfaces never get it, their closest ancestor does)
        LSurface *focused { focus() };
        while (focused->subsurface() && focused->parent())
            focused = focused->parent();

        if (clicked->layerRole() && clicked->layerRole()->keyboardInteractivity() == LLayerRole::NoInteractivity &&
            seat()->keyboard()->focus() == focused && !seat()->keyboard()->grab())
            seat()->keyboard()->setFocus(seat()->activeToplevel() ? seat()->activeToplevel()->surface() : nullptr);
    }

    if (e.pressed && Crystals::IsDesk(focus()))
        GetCompositor()->activateDesk();
    else if (e.pressed && focus() && GetCompositor()->deskActive())
    {
        // Back to a window that may still count as activated (its deactivation not acked yet):
        // activate it again explicitly.
        LSurface *surface { focus()->parent() ? focus()->topmostParent() : focus() };

        if (auto *toplevel { surface->toplevel() })
        {
            toplevel->configureState(toplevel->pendingConfiguration().windowState | CZWinActivated);
            GetCompositor()->deactivateDesk();
            GetCompositor()->updateActiveHeavenClient();
        }
    }
    CZCore::Get()->sendEvent(e, *AKApp::Get());
}

// Three finger swipes: the first few units decide. Horizontal ones switch workspaces (see
// Workspaces), vertical ones open/close Exposé (see Expose), or, down while it's closed, show the
// desktop (and up leaves it, see ShowDesktop). Others, and those turned off in System Settings
// (see TrackpadSettings), go to clients.
enum class SwipeAxis { None, Undecided, Horizontal, Vertical, Desktop };
static SwipeAxis s_swipeAxis { SwipeAxis::None };
static SkPoint s_swipeStart {};
static CZPointerSwipeBeginEvent s_swipeBegin; // Sent to the client if the swipe turns out to be its
static constexpr SkScalar SwipeDecideDistance { 12.f };

void Pointer::pointerSwipeBeginEvent(const CZPointerSwipeBeginEvent &e)
{
    const bool anyEnabled { TrackpadSettings::WorkspaceSwipe() || TrackpadSettings::Expose() || TrackpadSettings::ShowDesktop() };

    if (e.fingers != 3 || !anyEnabled || SessionLockManager::IsLocked())
    {
        LPointer::pointerSwipeBeginEvent(e);
        return;
    }

    s_swipeAxis = SwipeAxis::Undecided;
    s_swipeStart = SkPoint::Make(0, 0);
    s_swipeBegin = e;
}

void Pointer::pointerSwipeUpdateEvent(const CZPointerSwipeUpdateEvent &e)
{
    switch (s_swipeAxis)
    {
    case SwipeAxis::None:
        LPointer::pointerSwipeUpdateEvent(e);
        return;
    case SwipeAxis::Undecided:
    {
        s_swipeStart += e.delta;

        if (s_swipeStart.length() < SwipeDecideDistance)
            return;

        const bool sideways { std::abs(s_swipeStart.x()) > std::abs(s_swipeStart.y()) };

        // Sideways switches workspaces (Exposé too, see Expose), up/down opens/closes Exposé
        if (sideways && TrackpadSettings::WorkspaceSwipe())
        {
            s_swipeAxis = SwipeAxis::Horizontal;
            ShowDesktop::Leave(); // The windows moved aside come back
            Workspaces::SwipeBegin(3);
            Workspaces::SwipeUpdate(3, s_swipeStart);
        }
        else if (!sideways && TrackpadSettings::ShowDesktop() && (ShowDesktop::Active() || (!Expose::Active() && s_swipeStart.y() > 0.f)))
        {
            s_swipeAxis = SwipeAxis::Desktop;
            ShowDesktop::SwipeBegin();
            ShowDesktop::SwipeUpdate(s_swipeStart);
        }
        else if (!sideways && TrackpadSettings::Expose() && !ShowDesktop::Active() && (Expose::Active() || s_swipeStart.y() < 0.f))
        {
            s_swipeAxis = SwipeAxis::Vertical;
            Expose::SwipeBegin();
            Expose::SwipeUpdate(s_swipeStart);
        }
        else
        {
            // Turned off: the client's, from its start
            s_swipeAxis = SwipeAxis::None;
            LPointer::pointerSwipeBeginEvent(s_swipeBegin);
            CZPointerSwipeUpdateEvent update { e };
            update.delta = s_swipeStart;
            LPointer::pointerSwipeUpdateEvent(update);
        }
        return;
    }
    case SwipeAxis::Horizontal:
        Workspaces::SwipeUpdate(e.fingers, e.delta);
        return;
    case SwipeAxis::Vertical:
        Expose::SwipeUpdate(e.delta);
        return;
    case SwipeAxis::Desktop:
        ShowDesktop::SwipeUpdate(e.delta);
        return;
    }
}

void Pointer::pointerSwipeEndEvent(const CZPointerSwipeEndEvent &e)
{
    const SwipeAxis axis { s_swipeAxis };
    s_swipeAxis = SwipeAxis::None;

    switch (axis)
    {
    case SwipeAxis::None:
        LPointer::pointerSwipeEndEvent(e);
        break;
    case SwipeAxis::Horizontal:
        Workspaces::SwipeEnd(e.cancelled);
        break;
    case SwipeAxis::Vertical:
        Expose::SwipeEnd(e.cancelled);
        break;
    case SwipeAxis::Desktop:
        ShowDesktop::SwipeEnd(e.cancelled);
        break;
    default:
        break;
    }
}
