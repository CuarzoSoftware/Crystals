#include <Core/Types.h>
#include <Core/Crystals.h>
#include <Seat/Seat.h>

Crystals*                     GetCompositor() noexcept { return static_cast<Crystals*>(CZ::compositor()); }
Scene *                       GetScene()      noexcept { return GetCompositor()->scene.get(); }
const std::list<Surface *> &  GetSurfaces()   noexcept { return (const std::list<Surface*>&)GetCompositor()->surfaces(); }
Seat *                        GetSeat()       noexcept { return static_cast<Seat*>(seat());}
