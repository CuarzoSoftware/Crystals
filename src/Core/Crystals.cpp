#include <cstring>
#include <CZ/Core/Events/CZColorSchemeEvent.h>
#include <Core/Volume.h>
#include <Louvre/Protocols/BackgroundBlur/GBackgroundBlurManager.h>
#include <Louvre/Protocols/InvisibleRegion/GInvisibleRegionManager.h>
#include <Louvre/Protocols/SvgPath/GSvgPathManager.h>
#include <Louvre/Cursor/LCursor.h>
#include <Louvre/Cursor/LCursorSource.h>
#include <Louvre/LGlobal.h>
#include <Core/CZCursorShape.h>
#include <Core/Client.h>
#include <Louvre/Roles/LLayerRole.h>
#include <Louvre/Roles/LSurface.h>
#include <Core/Crystals.h>
#include <Core/CaptureManager.h>
#include <Core/Log.h>
#include <Scene/Scene.h>

#include <Roles/Surface.h>
#include <Roles/SubsurfaceRole.h>
#include <Roles/ToplevelRole.h>
#include <Roles/LayerRole.h>
#include <Decorations/SSDTheme.h>
#include <Decorations/SSD.h>

#include <Seat/Seat.h>
#include <Seat/Output.h>
#include <Seat/Pointer.h>
#include <Seat/DND.h>
#include <Effects/DNDReturnAnimation.h>
#include <Seat/Keyboard.h>

#include <Effects/SurfaceBlurManager.h>
#include <Effects/MinimizeAnimation.h>
#include <Effects/FullscreenAnimation.h>
#include <Core/Workspaces.h>

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
    initColorScheme();
    SSD::WarmUp(); // Not when the first decorated window appears (the font lookup stalls)
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

                    // Its menus may be the ones to show now (its toplevel or the desktop's).
                    if ((seat()->activeToplevel() && !m_deskActive && seat()->activeToplevel()->client() == cli) ||
                        ((!seat()->activeToplevel() || m_deskActive) && cli == deskClient()))
                        updateActiveHeavenClient();
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
                // Through the request: rejected while fullscreen (see ToplevelRole)
                if (tl->isMinimized())
                    tl->setMinimized(false);
                else
                    tl->setMinimizedRequest();
            }
        });
    }
}

bool Crystals::IsDesk(LSurface *surface) noexcept
{
    if (!surface)
        return false;

    if (surface->parent())
        surface = surface->topmostParent();

    return surface->layerRole() && surface->layerRole()->scope() == DeskScope;
}

bool Crystals::IsShell(LSurface *surface) noexcept
{
    if (!surface)
        return false;

    if (surface->parent())
        surface = surface->topmostParent();

    if (!surface->layerRole())
        return false;

    for (const char *scope : ShellScopes)
        if (surface->layerRole()->scope() == scope)
            return true;

    return false;
}

void Crystals::restack() noexcept
{
    // raise() calls Surface::raised(), which calls this again
    if (m_restacking)
        return;

    m_restacking = true;

    // Raises (in their current order) the parentless surfaces of a layer for which @p pred holds,
    // unless they're already above the rest.
    const auto raiseAll = [this](LSurfaceLayer layer, auto pred)
    {
        const std::list<LSurface*> list { layers()[layer] };
        bool seen { false }, ordered { true };

        for (LSurface *s : list)
        {
            if (s->parent())
                continue;

            if (pred(s))
                seen = true;
            else if (seen)
            {
                ordered = false;
                break;
            }
        }

        if (ordered)
            return;

        for (LSurface *s : list)
            if (!s->parent() && pred(s))
                s->raise();
    };

    // The shell above the rest of the overlay layer, the rest of the background layer above the desktop
    raiseAll(LLayerOverlay, [](LSurface *s){ return IsShell(s); });
    raiseAll(LLayerBackground, [](LSurface *s){ return !IsDesk(s); });

    m_restacking = false;
}

Client *Crystals::deskClient() const noexcept
{
    for (LSurface *surface : surfaces())
        if (surface->mapped() && IsDesk(surface))
            return static_cast<Client*>(surface->client());

    return nullptr;
}

void Crystals::updateActiveHeavenClient() noexcept
{
    if (!heaven)
        return;

    // A toplevel's menus, unless the desktop was clicked since it was activated (its deactivation
    // may still be pending).
    if (auto *toplevel { seat()->activeToplevel() }; toplevel && !m_deskActive)
    {
        static_cast<Client*>(toplevel->client())->setActiveHeavenClient(toplevel);
        return;
    }

    // Nothing active: the desktop's menus (once Desk registered with Heaven).
    if (auto *desk { deskClient() }; desk && !desk->dbusId.empty())
        desk->setActiveHeavenClient(nullptr);
    else
        heaven->setActiveClient("");
}

void Crystals::activateDesk() noexcept
{
    if (auto *toplevel { seat()->activeToplevel() })
        toplevel->configureState(toplevel->pendingConfiguration().windowState & ~CZWinActivated);

    if (m_deskActive)
        return;

    m_deskActive = true;
    updateActiveHeavenClient();
}

void Crystals::uninitialized() noexcept
{
    Volume::Clear();
    m_colorSchemeMonitor.reset();
    MinimizeAnimation::Clear();
    FullscreenAnimation::Clear();
    Workspaces::Clear();
    DNDReturnAnimation::Clear();
    SSDTheme::Clear();
}

LFactoryObject *Crystals::createObjectRequest(LFactoryObject::Type objectType, const void *params)
{
    switch (objectType)
    {
    case LFactoryObject::Type::LDND:
        return new DND(params);
    case LFactoryObject::Type::LSurface:
        return new Surface(params);
    case LFactoryObject::Type::LSubsurfaceRole:
        return new SubsurfaceRole(params);
    case LFactoryObject::Type::LToplevelRole:
        return new ToplevelRole(params);
    case LFactoryObject::Type::LLayerRole:
        return new LayerRole(params);
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
    case LFactoryObject::Type::LImageCaptureManager:
        return new CaptureManager(params);
    default:
        return nullptr;
    }
}

/******************************************* LIGHT / DARK *******************************************/

void Crystals::initColorScheme() noexcept
{
    m_colorSchemeMonitor = CZColorSchemeMonitor::GetOrMake();

    if (!m_colorSchemeMonitor)
        return;

    m_colorSchemeMonitor->onChanged.subscribe(this, [this](CZColorScheme scheme){ setColorScheme(scheme); });

    if (m_colorSchemeMonitor->known())
        setColorScheme(m_colorSchemeMonitor->colorScheme());
}

void Crystals::setColorScheme(CZColorScheme scheme) noexcept
{
    if (scheme == CZColorScheme::Unknown)
        scheme = CZColorScheme::Light;

    if (m_colorScheme == scheme)
        return;

    m_colorScheme = scheme;
    Log(CZInfo, CZLN, "Color scheme: {}", scheme == CZColorScheme::Dark ? "dark" : "light");

    // Kay nodes (e.g. the window buttons) through the scene...
    if (this->scene && this->scene->scene)
    {
        const CZColorSchemeEvent event { scheme };
        CZCore::Get()->sendEvent(event, *this->scene->scene);
    }

    // ...and the decorations' own colors
    for (Surface *surface : GetSurfaces())
        if (surface->toplevel())
            static_cast<ToplevelRole*>(surface->toplevel())->updateDecorations();

    for (LOutput *output : outputs())
        output->repaint();
}
