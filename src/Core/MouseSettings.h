#ifndef MOUSESETTINGS_H
#define MOUSESETTINGS_H

#include <Core/Types.h>

struct libinput_device;

/**
 * @brief The mouse settings chosen in System Settings (Mouse), applied as soon as they change.
 *
 * Read from `$XDG_CONFIG_HOME/cz-crystals/mouse.conf` (`~/.config/cz-crystals/mouse.conf` by
 * default), written by System Settings (see its MouseSettings, keep both in sync), and watched:
 *
 * - TrackingSpeed (-1: slowest … 1: fastest, 0 by default) and NaturalScrolling (true by default):
 *   libinput settings, applied to the pointer devices that aren't touchpads (DRM backend only).
 * - ScrollSpeed (0.25 … 4, 1 by default): what the wheel scrolls is multiplied by it (see Pointer).
 *
 * DoubleClickMs is there too, but it's the apps that honor it (see CZMouseSettings).
 */
class MouseSettings
{
public:
    /// Reads them, and watches the file (see Crystals' initialization).
    static void Init() noexcept;
    static void Clear() noexcept;

    /// Applies them to @p dev if it's a pointer device (see Seat::configureLibinputDevice()).
    static void Configure(libinput_device *dev) noexcept;

    /// What the wheel scrolls is multiplied by it.
    static Float32 ScrollSpeed() noexcept;
};

#endif // MOUSESETTINGS_H
