#include <Cursor/LShapeCursorSource.h>
#include <Events/CZPointerSwipeUpdateEvent.h>
#include <Events/CZPointerMoveEvent.h>
#include <Seat/Pointer.h>
#include <Scene/Scene.h>
#include <Core/CZCore.h>
#include <AKApp.h>
#include <LCursor.h>

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
    CZCore::Get()->sendEvent(e, *AKApp::Get());
}

void Pointer::pointerSwipeUpdateEvent(const CZPointerSwipeUpdateEvent &e)
{
    LPointer::pointerSwipeUpdateEvent(e);

    auto &root { GetScene()->root };
    root.layout().setPosition(YGEdgeLeft, root.layout().calculatedLeft() + e.delta.x());
}
