#include <Louvre/Protocols/BackgroundBlur/GBackgroundBlurManager.h>
#include <Louvre/Protocols/InvisibleRegion/GInvisibleRegionManager.h>
#include <Louvre/Protocols/SvgPath/GSvgPathManager.h>
#include <Louvre/Cursor/LCursor.h>
#include <Louvre/Cursor/LCursorSource.h>
#include <Louvre/LGlobal.h>
#include <Core/CZCursorShape.h>
#include <Core/Client.h>
#include <Core/Crystals.h>
#include <Core/Log.h>
#include <Scene/Scene.h>

#include <Roles/Surface.h>
#include <Roles/SubsurfaceRole.h>

#include <Seat/Seat.h>
#include <Seat/Output.h>
#include <Seat/Pointer.h>
#include <Seat/Keyboard.h>

#include <Effects/SurfaceBlurManager.h>

#include <RResourceTracker.h>

Crystals::Crystals() noexcept
{
    LBackgroundBlur::MaskCaps.set(LBackgroundBlur::RoundRectMaskCap | LBackgroundBlur::SVGPathMaskCap);
}

void Crystals::initialized() noexcept
{
    auto core { CZCore::Get() };
    core->setAnimationInterval(64);

    app = AKApp::GetOrMake();
    scene = Scene::Make();
    GetSeat()->configureInputDevices();
    LCompositor::initialized();

    if ((heaven = HNCompositor::GetOrMake()))
    {
        heaven->onClientRegistered.subscribe(this, [this](const char *privateHandle, const char *dbusId){
            for (auto *cli : clients())
            {
                if (strcmp(cli->privateHandle().c_str(), privateHandle) == 0)
                {
                    Log(CZInfo, CZLN, "Found LClient for D-BUS ID {}", dbusId);
                    cli->dbusId = dbusId;

                    if (seat()->activeToplevel() && seat()->activeToplevel()->client() == cli)
                        cli->setActiveHeavenClient();
                    return;
                }
            }
        });

        heaven->onCloseActiveClient.subscribe(this, [this](){
            if (seat()->activeToplevel())
                seat()->activeToplevel()->close();
        });

        heaven->onToggleActiveClientFullscreen.subscribe(this, [this](){
            if (auto *tl = seat()->activeToplevel())
            {
                auto state { tl->pendingConfiguration().windowState };
                if (state.has(CZWinFullscreen))
                    tl->unsetFullscreenRequest();
                else
                    tl->setFullscreenRequest(nullptr);
            }
        });

        heaven->onToggleActiveClientMaximized.subscribe(this, [this](){
            if (auto *tl = seat()->activeToplevel())
            {
                auto state { tl->pendingConfiguration().windowState };
                if (state.has(CZWinMaximized))
                    tl->unsetMaximizedRequest();
                else
                    tl->setMaximizedRequest();
            }
        });

        heaven->onToggleActiveClientMinimized.subscribe(this, [this](){
            if (auto *tl = seat()->activeToplevel())
            {
                tl->setMinimized(!tl->isMinimized());
            }
        });
    }
}

void Crystals::uninitialized() noexcept {}

LFactoryObject *Crystals::createObjectRequest(LFactoryObject::Type objectType, const void *params)
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
    case LFactoryObject::Type::LClient:
        return new Client(params);
    default:
        return nullptr;
    }
}
