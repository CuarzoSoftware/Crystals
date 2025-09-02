#ifndef TYPES_H
#define TYPES_H

#include <LNamespaces.h>
#include <list>

using namespace CZ;

/* Core */

class Compositor;

/* Seat */

class Seat;
class Pointer;
class Output;

/* Roles */

class Surface;
class SubsurfaceRole;

/* Scene */

class Scene;
class SurfaceView;

/* Global handles */

Compositor *GetCompositor() noexcept;
Scene *GetScene() noexcept;
Seat *GetSeat() noexcept;
const std::list<Surface*> &GetSurfaces() noexcept;

#endif // TYPES_H
