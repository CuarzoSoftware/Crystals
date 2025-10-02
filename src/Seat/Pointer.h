#ifndef POINTER_H
#define POINTER_H

#include <LPointer.h>
#include <Core/Types.h>

class Pointer final : public LPointer
{
public:
    using LPointer::LPointer;

    void pointerMoveEvent(const CZPointerMoveEvent &e) override;
    void pointerButtonEvent(const CZPointerButtonEvent &e) override;
    void pointerSwipeUpdateEvent(const CZPointerSwipeUpdateEvent &e) override;
};

#endif // POINTER_H
