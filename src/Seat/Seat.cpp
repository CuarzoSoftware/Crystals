#include <algorithm>
#include <Louvre/LCompositor.h>
#include <Louvre/Cursor/LCursor.h>
#include <Seat/Output.h>
#include <Effects/Expose.h>
#include <Core/Workspaces.h>
#include <Core/SessionLock.h>
#include <Louvre/Roles/LSubsurfaceRole.h>
#include <Louvre/Roles/LPopupRole.h>
#include <Louvre/Roles/LBackgroundBlur.h>
#include <Louvre/Backends/LBackend.h>

#include <Core/CZCore.h>
#include <Core/CZInputDevice.h>
#include <Core/Client.h>
#include <Core/Crystals.h>
#include <Core/Log.h>

#include <Scene/Scene.h>
#include <Seat/Seat.h>

#include <Roles/Surface.h>

#include <libinput.h>

Seat::Seat(const void *params) noexcept : LSeat(params)
{
    log = Log.newWithContext("Seat");
}

void Seat::activeToplevelChanged(LToplevelRole *prev) noexcept
{
    if (prev)
        prev->surface()->backgroundBlur()->configureState(LBackgroundBlur::Disabled);

    if (activeToplevel())
    {
        activeToplevel()->surface()->backgroundBlur()->configureState(LBackgroundBlur::Enabled);
        GetCompositor()->deactivateDesk(); // A window took over from the desktop
    }

    GetCompositor()->updateActiveHeavenClient();
}

void Seat::configureLibinputDevice(libinput_device *dev) noexcept
{
    if (compositor()->backend()->id() != LBackendId::DRM)
        return;

    if (!dev)
    {
        log(CZWarning, CZLN, "nullptr libinput device handle");
        return;
    }

    // Disable while typing
    libinput_device_config_dwt_set_enabled(dev, LIBINPUT_CONFIG_DWT_DISABLED);

    // Disable touchpad click areas
    libinput_device_config_click_set_method(dev, LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);

    // Enable natural scrolling
    libinput_device_config_scroll_set_natural_scroll_enabled(dev, 1);

    // Tap to click
    /* libinput_device_config_tap_set_enabled(libinputDev, LIBINPUT_CONFIG_TAP_ENABLED); */
}

void Seat::configureInputDevices() noexcept
{
    for (auto &dev : inputDevices())
        configureLibinputDevice(dev->nativeHandle.libinput);
}

void Seat::enabledChanged()
{
    LSeat::enabledChanged();

    if (enabled())
        configureInputDevices();
}

void Seat::inputDevicePlugged(std::shared_ptr<CZInputDevice> dev)
{
    configureLibinputDevice(dev->nativeHandle.libinput);
}

void Seat::outputUnplugged(LOutput *output)
{
    // Exposé shown there: gone with it
    if (Expose::ShownOn() && static_cast<LOutput*>(Expose::ShownOn()) == output)
        Expose::Clear();

    // Found while it's there, placed once it's removed and the others rearranged (their windows
    // move with them)
    const auto placements { Workspaces::Placements() };
    const auto evacuation { Workspaces::Evacuate(output) };
    Output::Configuring = true;
    LSeat::outputUnplugged(output);
    Output::Configuring = false;
    Output::SyncLastRects();
    cursor()->setPos(cursor()->pos());
    Workspaces::FollowOutputs(placements);
    Workspaces::Place(evacuation);
    Workspaces::ScheduleReachable();
}

bool Seat::configureOutputsRequest(LClient *client, const std::vector<OutputConfiguration> &configurations)
{
    // Where every window is (before anything moves), and the windows of outputs being disabled
    const auto placements { Workspaces::Placements() };
    std::vector<Workspaces::Evacuation> evacuations;
    const auto &outputs { compositor()->outputs() };

    for (const auto &conf : configurations)
    {
        if (conf.initialized || std::find(outputs.begin(), outputs.end(), &conf.output) == outputs.end())
            continue;

        if (Expose::ShownOn() && static_cast<LOutput*>(Expose::ShownOn()) == &conf.output)
            Expose::Clear();

        evacuations.push_back(Workspaces::Evacuate(&conf.output));
    }

    Output::Configuring = true;
    const bool ok { LSeat::configureOutputsRequest(client, configurations) };
    Output::Configuring = false;

    // Where the client put them (not rearranged afterwards, see Output::resizeGL())
    Output::SyncLastRects();
    cursor()->setPos(cursor()->pos());

    Workspaces::FollowOutputs(placements);

    for (const auto &evacuation : evacuations)
        Workspaces::Place(evacuation);

    Workspaces::ScheduleReachable();
    return ok;
}

void Seat::inputEvent(const CZInputEvent &e) noexcept
{
    LSeat::inputEvent(e);
}

// A popup's tree like LSeat::surfaceAt(): its popups, subsurfaces above, itself, subsurfaces below.
static LSurface *SubsurfacesAt(const std::vector<LSubsurfaceRole*> &list, SkIPoint p) noexcept
{
    for (auto it = list.rbegin(); it != list.rend(); it++)
    {
        LSurface *s { (*it)->surface() };

        if (!s->mapped())
            continue;

        if (LSurface *hit { SubsurfacesAt(s->subsurfacesAbove(), p) })
            return hit;

        if (s->inputRegionContainsGlobalPoint(p))
            return s;

        if (LSurface *hit { SubsurfacesAt(s->subsurfacesBelow(), p) })
            return hit;
    }

    return nullptr;
}

static LSurface *PopupAt(LSurface *popup, SkIPoint p) noexcept
{
    if (!popup->mapped() || !static_cast<Surface*>(popup)->view.visible())
        return nullptr;

    const auto &children { popup->popup()->childPopups() };

    for (auto it = children.rbegin(); it != children.rend(); it++)
        if (LSurface *hit { PopupAt((*it)->surface(), p) })
            return hit;

    if (LSurface *hit { SubsurfacesAt(popup->subsurfacesAbove(), p) })
        return hit;

    if (popup->inputRegionContainsGlobalPoint(p))
        return popup;

    return SubsurfacesAt(popup->subsurfacesBelow(), p);
}

LSurface *Seat::surfaceAt(SkIPoint point) const noexcept
{
    // Locked: nothing but the lock surfaces
    if (SessionLockManager::IsLocked())
        return SessionLockManager::SurfaceAt(point);

    // Top-level popups as stacked in the scene (the latest on top)
    if (Scene *scene { GetScene() })
    {
        const auto &views { scene->popups.children(true) };

        for (auto it = views.rbegin(); it != views.rend(); it++)
            if (auto *view { dynamic_cast<SurfaceView*>(*it) }; view && view->surface.popup())
                if (LSurface *hit { PopupAt(&view->surface, point) })
                    return hit;
    }

    return LSeat::surfaceAt(point);
}
