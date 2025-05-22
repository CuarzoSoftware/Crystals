#ifndef GLOBAL_H
#define GLOBAL_H

#include <LNamespaces.h>
#include <AK/AK.h>
#include <list>

using namespace Louvre;
using namespace AK;

class Compositor;
class Application;
class Scene;
class Surface;

/**
 * For quick access to global stuff.
 */
namespace G
{
    inline Compositor *compositor() noexcept { return (Compositor*)Louvre::compositor(); }
    inline Application *app() noexcept { return (Application*)akApp(); }
    Scene *scene() noexcept;
    const std::list<Surface*> &surfaces() noexcept;
};

#endif // GLOBAL_H
