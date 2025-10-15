#include <Louvre/Protocols/BackgroundBlur/GBackgroundBlurManager.h>
#include <Louvre/Protocols/InvisibleRegion/GInvisibleRegionManager.h>
#include <Louvre/Protocols/SvgPath/GSvgPathManager.h>
#include <Louvre/Cursor/LCursor.h>
#include <Louvre/Cursor/LCursorSource.h>
#include <Louvre/LGlobal.h>
#include <Core/CZCursorShape.h>
#include <Core/Compositor.h>
#include <Core/Log.h>
#include <Scene/Scene.h>

#include <Roles/Surface.h>
#include <Roles/SubsurfaceRole.h>

#include <Seat/Seat.h>
#include <Seat/Output.h>
#include <Seat/Pointer.h>
#include <Seat/Keyboard.h>

#include <Effects/SurfaceBlurManager.h>

Compositor::Compositor() noexcept
{
    LBackgroundBlur::MaskCaps.set(LBackgroundBlur::RoundRectMaskCap | LBackgroundBlur::SVGPathMaskCap);
}

void Compositor::initialized() noexcept
{
    auto core { CZCore::Get() };
    core->setAnimationInterval(64);

    app = AKApp::GetOrMake();
    scene = Scene::Make();
    GetSeat()->configureInputDevices();
    LCompositor::initialized();
}

void Compositor::uninitialized() noexcept {}

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
    case LFactoryObject::Type::LKeyboard:
        return new Keyboard(params);
    case LFactoryObject::Type::LBackgroundBlur:
        return new SurfaceBlurManager(params);
    default:
        return nullptr;
    }
}
