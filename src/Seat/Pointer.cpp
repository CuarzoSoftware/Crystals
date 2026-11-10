#include <Louvre/Cursor/LShapeCursorSource.h>
#include <Louvre/Cursor/LCursor.h>
#include <Core/Events/CZPointerSwipeUpdateEvent.h>
#include <Core/Events/CZPointerMoveEvent.h>
#include <Core/CZCore.h>
#include <AK/AKApp.h>
#include <Seat/Pointer.h>
#include <Core/Crystals.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Roles/LToplevelRole.h>
#include <Louvre/Roles/LSurface.h>
#include <Scene/Scene.h>

void Pointer::pointerMoveEvent(const CZPointerMoveEvent &e)
{
    auto kay { AKApp::Get() };
    auto scene { GetScene()->scene };

    LPointer::pointerMoveEvent(e);
    e.pos = cursor()->pos();
    kay->core()->sendEvent(e, *kay);

    // Keep the client surface cursor
    if (focus())
        return;

    // Otherwise use the focused node cursor
    if (auto *node = scene->pointerFocus())
    {
        if (node->cursor())
        {
            auto shapeSource { cursor()->source()->asShape() };

            if (!shapeSource || shapeSource->shape() != node->cursor())
                cursor()->setSource(LShapeCursorSource::MakeShape(*node->cursor()));
        }
        else
            cursor()->setVisible(false);
    }
}

void Pointer::pointerButtonEvent(const CZPointerButtonEvent &e)
{
    LPointer::pointerButtonEvent(e);

    // Clicking the desktop deactivates the active toplevel, so the desktop's menus show (like
    // clicking the desktop on macOS makes Finder the active app).
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
