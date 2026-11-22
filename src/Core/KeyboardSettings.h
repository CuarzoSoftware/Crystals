#ifndef KEYBOARDSETTINGS_H
#define KEYBOARDSETTINGS_H

#include <Core/Types.h>

/**
 * @brief The keyboard settings chosen in System Settings (Keyboard), applied as soon as they change.
 *
 * Read from `$XDG_CONFIG_HOME/cz-crystals/keyboard.conf` (`~/.config/cz-crystals/keyboard.conf` by
 * default), written by System Settings (see its KeyboardSettings, keep both in sync), and watched:
 *
 * - RepeatRate (repeats per second, 0: none) and RepeatDelay (ms): LKeyboard::setRepeatInfo().
 * - Layouts and Variants (comma separated, XKB names): the keymap. With several, Super+Space
 *   switches between them (XKB's grp:win_space_toggle).
 * - BacklightTimeout (seconds, 0: never): the keyboard backlight turns off after that long without
 *   input (through UPower), and back on with the next input.
 *
 * Without the file, the keymap is the system's (localectl), unless XKB_DEFAULT_LAYOUT is set.
 */
class KeyboardSettings
{
public:
    /// Applies them, and watches the file (see Crystals' initialization).
    static void Init() noexcept;
    static void Clear() noexcept;

    /// Any input (see Seat::inputEvent()): the backlight comes back on if it was turned off.
    static void Activity() noexcept;
};

#endif // KEYBOARDSETTINGS_H
