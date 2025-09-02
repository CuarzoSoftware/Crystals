#ifndef POINTER_H
#define POINTER_H

#include <LPointer.h>
#include <Core/Types.h>

class Pointer final : public LPointer
{
public:
    using LPointer::LPointer;

    void pointerSwipeUpdateEvent(const CZPointerSwipeUpdateEvent &e) override;
};

#endif // POINTER_H
