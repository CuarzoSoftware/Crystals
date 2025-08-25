#ifndef TYPES_H
#define TYPES_H

#include <LNamespaces.h>
#include <list>

using namespace CZ;

class Compositor;
class Surface;
class Scene;
class Output;
class SurfaceView;

/* Global handles */

Compositor *GetCompositor() noexcept;
Scene *GetScene() noexcept;
const std::list<Surface*> &GetSurfaces() noexcept;

#endif // TYPES_H
