#include <Core/Types.h>
#include <Core/Compositor.h>
#include <Seat/Seat.h>

Compositor*                   GetCompositor() noexcept { return static_cast<Compositor*>(CZ::compositor()); }
Scene *                       GetScene()      noexcept { return GetCompositor()->scene.get(); }
const std::list<Surface *> &  GetSurfaces()   noexcept { return (const std::list<Surface*>&)GetCompositor()->surfaces(); }
Seat *                        GetSeat()       noexcept { return static_cast<Seat*>(seat());}
