#include "Core/CZCore.h"
#include "Scene/Scene.h"
#include <Seat/Seat.h>
#include <Core/CZInputDevice.h>
#include <Core/Compositor.h>
#include <Core/Log.h>

#include <Events/CZInputDevicePluggedEvent.h>
#include <Louvre/Backends/LBackend.h>
#include <libinput.h>

Seat::Seat(const void *params) noexcept : LSeat(params)
{
    log = Log.newWithContext("Seat");
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
    // libinput_device_config_tap_set_enabled(libinputDev, LIBINPUT_CONFIG_TAP_ENABLED);
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

void Seat::inputDevicePluggedEvent(const CZInputDevicePluggedEvent &e)
{
    configureLibinputDevice(e.device->nativeHandle.libinput);
}

void Seat::inputEvent(const CZInputEvent &e) noexcept
{
    LSeat::inputEvent(e);
}
