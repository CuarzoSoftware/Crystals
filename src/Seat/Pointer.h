#ifndef POINTER_H
#define POINTER_H

#include <Louvre/Seat/LPointer.h>
#include <Core/Types.h>

class Pointer final : public LPointer
{
public:
    using LPointer::LPointer;

    void pointerMoveEvent(const CZPointerMoveEvent &e) override;
    void pointerButtonEvent(const CZPointerButtonEvent &e) override;

    // Three finger swipes switch workspaces (see Workspaces), others go to clients
    void pointerSwipeBeginEvent(const CZPointerSwipeBeginEvent &e) override;
    void pointerSwipeUpdateEvent(const CZPointerSwipeUpdateEvent &e) override;
    void pointerSwipeEndEvent(const CZPointerSwipeEndEvent &e) override;
};

#endif // POINTER_H
