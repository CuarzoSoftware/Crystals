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

        if (clicked->layerRole() && clicked->layerRole()->keyboardInteractivity() == LLayerRole::NoInteractivity &&
            seat()->keyboard()->focus() == focus() && !seat()->keyboard()->grab())
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
