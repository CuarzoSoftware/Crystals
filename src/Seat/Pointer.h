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
};

#endif // POINTER_H
