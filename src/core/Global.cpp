#include <core/Application.h>
#include <core/Compositor.h>

Scene *G::scene() noexcept
{
    return &app()->scene;
}

const std::list<Surface*> &G::surfaces() noexcept
{
    return (const std::list<Surface*> &)compositor()->surfaces();
}
