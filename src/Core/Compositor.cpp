/*#include <protocols/BackgroundBlur/GBackgroundBlurManager.h>
#include <protocols/InvisibleRegion/GInvisibleRegionManager.h>
#include <protocols/SvgPath/GSvgPathManager.h>*/

#include <Core/Compositor.h>
#include <Core/Log.h>
#include <Scene/Scene.h>

#include <Roles/Surface.h>
#include <Roles/SubsurfaceRole.h>

#include <Seat/Seat.h>
#include <Seat/Output.h>
#include <Seat/Pointer.h>

#include <Effects/SurfaceBlurManager.h>

Compositor::Compositor() noexcept
{
    // LBackgroundBlur::maskingCapabilities.set(LBackgroundBlur::RoundRectMaskCap | LBackgroundBlur::SVGPathMaskCap);
}

void Compositor::initialized() noexcept
{
    app = AKApp::Make();
    scene = Scene::Make();
    GetSeat()->configureInputDevices();
    LCompositor::initialized();
    Log(CZInfo, CZLN, "Compositor initialized");
}

void Compositor::uninitialized() noexcept {}

bool Compositor::createGlobalsRequest()
{
    /*
    using namespace Louvre::Protocols;
    createGlobal<BackgroundBlur::GBackgroundBlurManager>();
    createGlobal<InvisibleRegion::GInvisibleRegionManager>();
    createGlobal<SvgPath::GSvgPathManager>();*/
    return LCompositor::createGlobalsRequest();
}

LFactoryObject *Compositor::createObjectRequest(LFactoryObject::Type objectType, const void *params)
{
    switch (objectType)
    {
    case LFactoryObject::Type::LSurface:
        return new Surface(params);
    case LFactoryObject::Type::LSubsurfaceRole:
        return new SubsurfaceRole(params);
    case LFactoryObject::Type::LOutput:
        return new Output(params);
    case LFactoryObject::Type::LSeat:
        return new Seat(params);
    case LFactoryObject::Type::LPointer:
        return new Pointer(params);
    //case LFactoryObject::Type::LBackgroundBlur:
    //    return new SurfaceBlurManager(params);
    default:
        return nullptr;
    }
}
