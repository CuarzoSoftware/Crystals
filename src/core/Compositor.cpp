#include <protocols/BackgroundBlur/GBackgroundBlurManager.h>
#include <protocols/InvisibleRegion/GInvisibleRegionManager.h>
#include <protocols/SvgPath/GSvgPathManager.h>

#include <core/Application.h>
#include <core/Compositor.h>
#include <roles/Surface.h>
#include <seat/Output.h>
#include <effects/SurfaceBlurManager.h>
#include <sys/poll.h>

Compositor::Compositor() noexcept
{
    LBackgroundBlur::maskingCapabilities.set(LBackgroundBlur::RoundRectMaskCap | LBackgroundBlur::SVGPathMaskCap);
}

Compositor::~Compositor() {}

void Compositor::initialized()
{
    // Louvre's OpenGL context is available
    app = std::make_unique<Application>();

    // TODO: Handle custom output arrangement
    LCompositor::initialized();
}

void Compositor::uninitialized()
{
    // Prevent Kay GL resource leaks
    while (!outputs().empty()) removeOutput(outputs().back());

    // Destroy Kay app before Louvre's main OpenGL context
    app.reset();
}

bool Compositor::createGlobalsRequest()
{
    using namespace Louvre::Protocols;
    createGlobal<BackgroundBlur::GBackgroundBlurManager>();
    createGlobal<InvisibleRegion::GInvisibleRegionManager>();
    createGlobal<SvgPath::GSvgPathManager>();
    return LCompositor::createGlobalsRequest();
}

LFactoryObject *Compositor::createObjectRequest(LFactoryObject::Type objectType, const void *params)
{
    switch (objectType)
    {
    case LFactoryObject::Type::LOutput:
        return new Output(params);
    case LFactoryObject::Type::LSurface:
        return new Surface(params);
    case LFactoryObject::Type::LBackgroundBlur:
        return new SurfaceBlurManager(params);
    default:
        return nullptr;
    }
}
