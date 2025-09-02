#include <Events/CZPointerSwipeUpdateEvent.h>
#include <Seat/Pointer.h>
#include <Scene/Scene.h>

void Pointer::pointerSwipeUpdateEvent(const CZPointerSwipeUpdateEvent &e)
{
    LPointer::pointerSwipeUpdateEvent(e);

    auto &root { GetScene()->root };
    root.layout().setPosition(YGEdgeLeft, root.layout().calculatedLeft() + e.delta.x());
}
