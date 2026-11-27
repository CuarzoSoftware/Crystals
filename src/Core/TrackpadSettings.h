#ifndef TRACKPADSETTINGS_H
#define TRACKPADSETTINGS_H

#include <Core/Types.h>

struct libinput_device;

/**
 * @brief The trackpad settings chosen in System Settings (Trackpad), applied as soon as they change.
 *
 * Read from `$XDG_CONFIG_HOME/cz-crystals/trackpad.conf` (`~/.config/cz-crystals/trackpad.conf` by
 * default), written by System Settings (see its TrackpadSettings, keep both in sync), and watched:
 *
 * - TrackingSpeed (-1: slowest … 1: fastest, 0 by default): the pointer acceleration.
 * - SecondaryClick: `TwoFingers` (click or tap with two fingers, the default) or `BottomRight`
 *   (click in the bottom right corner).
 * - TapToClick (false by default) and NaturalScrolling (true by default).
 * - WorkspaceSwipe, Expose and ShowDesktop (true by default): the three finger swipes (sideways,
 *   up and down, see Pointer). Those turned off go to the clients instead.
 *
 * The first four are libinput settings, applied to the touchpads (DRM backend only).
 */
class TrackpadSettings
{
public:
    /// Reads them, and watches the file (see Crystals' initialization).
    static void Init() noexcept;
    static void Clear() noexcept;

    /// Applies them to @p dev if it's a touchpad (see Seat::configureLibinputDevice()), and returns
    /// whether it is.
    static bool Configure(libinput_device *dev) noexcept;

    // The three finger swipes
    static bool WorkspaceSwipe() noexcept;
    static bool Expose() noexcept;
    static bool ShowDesktop() noexcept;
};

#endif // TRACKPADSETTINGS_H
