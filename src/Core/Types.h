#ifndef TYPES_H
#define TYPES_H

#include <Louvre/Louvre.h>
#include <list>

using namespace CZ;

/* Core */

class Client;
class Crystals;

/* Seat */

class Seat;
class Pointer;
class Output;

/* Roles */

class Surface;
class SubsurfaceRole;
class ToplevelRole;

/* Scene */

class Scene;
class SurfaceView;

/* Global handles */

Crystals *GetCompositor() noexcept;
Scene *GetScene() noexcept;
Seat *GetSeat() noexcept;
const std::list<Surface*> &GetSurfaces() noexcept;

#endif // TYPES_H
